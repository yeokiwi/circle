//
// zmqclient.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "zmqclient.h"
#include <circle/net/dnsclient.h>
#include <circle/net/ipaddress.h>
#include <circle/net/in.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

#define RECONNECT_DELAY_MS	5000	// delay between two connection attempts

static const char FromZeroMQ[] = "zmqclient";

CZeroMQClient::CZeroMQClient (CNetSubSystem *pNetSubSystem, CElmoPlatinumCore1Master *pElmo,
			      unsigned nStatusIntervalMs)
:	m_pNetSubSystem (pNetSubSystem),
	m_pElmo (pElmo),
	m_nStatusIntervalMs (nStatusIntervalMs),
	m_usPort (0),
	m_bConnectRequested (FALSE),
	m_pSocket (0),
	m_pZMTP (0),
	m_bConnected (FALSE),
	m_nLastStatusTicks (0),
	m_nLastConnectAttemptTicks (0)
{
	SetName (FromZeroMQ);

	assert (m_pNetSubSystem != 0);
	assert (m_pElmo != 0);
}

CZeroMQClient::~CZeroMQClient (void)
{
	Disconnect ();

	m_pElmo = 0;
	m_pNetSubSystem = 0;
}

void CZeroMQClient::Connect (const char *pHost, u16 usPort)
{
	assert (pHost != 0);

	m_Host = pHost;
	m_usPort = usPort;
	m_bConnectRequested = TRUE;

	// let Run() attempt to connect on its next iteration
	m_nLastConnectAttemptTicks = 0;
}

void CZeroMQClient::Run (void)
{
	m_nLastStatusTicks = CTimer::Get ()->GetTicks ();

	while (1)
	{
		if (m_bConnected)
		{
			ReceiveCommands ();

			if (m_bConnected)
			{
				unsigned nTicks = CTimer::Get ()->GetTicks ();
				if (nTicks - m_nLastStatusTicks >= MSEC2HZ (m_nStatusIntervalMs))
				{
					m_nLastStatusTicks = nTicks;

					PublishStatus ();
				}
			}
		}
		else if (m_bConnectRequested)
		{
			unsigned nTicks = CTimer::Get ()->GetTicks ();
			if (   m_nLastConnectAttemptTicks == 0
			    || nTicks - m_nLastConnectAttemptTicks >= MSEC2HZ (RECONNECT_DELAY_MS))
			{
				m_nLastConnectAttemptTicks = nTicks;

				TryConnect ();
			}
		}

		CScheduler::Get ()->MsSleep (50);
	}
}

boolean CZeroMQClient::TryConnect (void)
{
	assert (m_pNetSubSystem != 0);

	CDNSClient DNSClient (m_pNetSubSystem);

	CIPAddress IPBroker;
	if (!DNSClient.Resolve ((const char *) m_Host, &IPBroker))
	{
		CLogger::Get ()->Write (FromZeroMQ, LogWarning, "Cannot resolve: %s",
					(const char *) m_Host);

		return FALSE;
	}

	IPBroker.Format (&m_IPServerString);

	assert (m_pSocket == 0);
	m_pSocket = new CSocket (m_pNetSubSystem, IPPROTO_TCP);
	assert (m_pSocket != 0);

	if (m_pSocket->Connect (IPBroker, m_usPort) != 0)
	{
		CLogger::Get ()->Write (FromZeroMQ, LogWarning, "Cannot connect to %s:%u",
					(const char *) m_IPServerString, m_usPort);

		delete m_pSocket;
		m_pSocket = 0;

		return FALSE;
	}

	assert (m_pZMTP == 0);
	m_pZMTP = new CZMTPConnection (m_pSocket);
	assert (m_pZMTP != 0);

	if (!m_pZMTP->Handshake ())
	{
		CLogger::Get ()->Write (FromZeroMQ, LogWarning,
					"ZMTP handshake with %s:%u failed",
					(const char *) m_IPServerString, m_usPort);

		delete m_pZMTP;
		m_pZMTP = 0;

		delete m_pSocket;
		m_pSocket = 0;

		return FALSE;
	}

	m_bConnected = TRUE;

	CLogger::Get ()->Write (FromZeroMQ, LogNotice, "Connected to broker %s:%u",
				(const char *) m_IPServerString, m_usPort);

	return TRUE;
}

void CZeroMQClient::Disconnect (void)
{
	delete m_pZMTP;
	m_pZMTP = 0;

	delete m_pSocket;
	m_pSocket = 0;

	m_bConnected = FALSE;
}

void CZeroMQClient::ReceiveCommands (void)
{
	assert (m_pZMTP != 0);

	u8 *ppFrame[ZMTP_MAX_FRAMES];
	size_t pLength[ZMTP_MAX_FRAMES];

	while (1)
	{
		int nFrames = m_pZMTP->ReceiveMessage (ppFrame, pLength, ZMTP_MAX_FRAMES);
		if (nFrames == 0)
		{
			return;
		}

		if (nFrames < 0)
		{
			CLogger::Get ()->Write (FromZeroMQ, LogWarning,
						"Connection to %s:%u lost",
						(const char *) m_IPServerString, m_usPort);

			Disconnect ();

			return;
		}

		if (   nFrames >= 2
		    && pLength[0] == 7
		    && memcmp (ppFrame[0], "command", 7) == 0)
		{
			HandleCommandMessage (ppFrame[1], pLength[1]);
		}

		for (int i = 0; i < nFrames; i++)
		{
			delete [] ppFrame[i];
		}
	}
}

void CZeroMQClient::HandleCommandMessage (const u8 *pPayload, size_t nLength)
{
	assert (m_pElmo != 0);
	assert (pPayload != 0);

	// Minimal hand-rolled scan for a {"cmd":"start"} / {"cmd":"stop"} body.
	// This avoids pulling in a JSON parser for this one simple, fixed shape;
	// no JSON parsing exists elsewhere in Circle either, only serialization.
	char *pBuffer = new char[nLength + 1];
	memcpy (pBuffer, pPayload, nLength);
	pBuffer[nLength] = '\0';

	boolean bStart = FALSE;
	boolean bStop = FALSE;

	const char *pCmd = strstr (pBuffer, "\"cmd\"");
	if (pCmd != 0)
	{
		if (strstr (pCmd, "\"start\"") != 0)
		{
			bStart = TRUE;
		}
		else if (strstr (pCmd, "\"stop\"") != 0)
		{
			bStop = TRUE;
		}
	}

	delete [] pBuffer;

	if (bStart)
	{
		CLogger::Get ()->Write (FromZeroMQ, LogNotice, "Received Start command (arm)");

		m_pElmo->SubmitCommand (ElmoCommandStart);
	}
	else if (bStop)
	{
		CLogger::Get ()->Write (FromZeroMQ, LogNotice, "Received Stop command (disarm)");

		m_pElmo->SubmitCommand (ElmoCommandStop);
	}
	else
	{
		CLogger::Get ()->Write (FromZeroMQ, LogWarning, "Ignoring unrecognized command");
	}
}

void CZeroMQClient::PublishStatus (void)
{
	assert (m_pElmo != 0);
	assert (m_pZMTP != 0);

	// the status object is too big for the stack of a task
	TElmoPlatinumStatus *pStatus = new TElmoPlatinumStatus;
	if (pStatus == 0)
	{
		return;
	}

	m_pElmo->GetStatus (pStatus);

	CString JSON;
	BuildStatusJSON (JSON, pStatus);

	delete pStatus;

	static const char TypeStatus[] = "status";

	const void *ppFrame[2] = { TypeStatus, (const char *) JSON };
	size_t pLength[2] = { sizeof TypeStatus - 1, JSON.GetLength () };

	if (!m_pZMTP->SendMessage (ppFrame, pLength, 2))
	{
		CLogger::Get ()->Write (FromZeroMQ, LogWarning,
					"Connection to %s:%u lost",
					(const char *) m_IPServerString, m_usPort);

		Disconnect ();
	}
}

void CZeroMQClient::BuildStatusJSON (CString &rJSON, const TElmoPlatinumStatus *pStatus)
{
	assert (pStatus != 0);

	rJSON = "{";

	AppendString (rJSON, "state", CElmoPlatinumCore1Master::GetMasterStateString (pStatus->MasterState));
	AppendString (rJSON, "interface", pStatus->InterfaceName);
	AppendString (rJSON, "mac", pStatus->MACAddress);
	AppendString (rJSON, "linkspeed", pStatus->LinkSpeed);
	AppendBoolean (rJSON, "linkup", pStatus->LinkUp);

	AppendBoolean (rJSON, "slavefound", pStatus->bSlaveFound);
	AppendString (rJSON, "slavename", pStatus->SlaveName);
	AppendNumber (rJSON, "vendor", pStatus->VendorID);
	AppendNumber (rJSON, "product", pStatus->ProductCode);
	AppendBoolean (rJSON, "iselmoplatinum", pStatus->bIsElmoPlatinum);

	AppendString (rJSON, "drivestate", CElmoPlatinumCore1Master::GetDriveStateString (pStatus->DriveState));
	AppendNumber (rJSON, "statusword", pStatus->StatusWord);
	AppendNumber (rJSON, "controlword", pStatus->ControlWord);
	AppendSigned (rJSON, "actualvelocity", pStatus->ActualVelocity);
	AppendSigned (rJSON, "targetvelocity", pStatus->TargetVelocity);
	AppendBoolean (rJSON, "armed", pStatus->bArmed);

	AppendSigned (rJSON, "wkc", pStatus->nWorkCounter);
	AppendSigned (rJSON, "expectedwkc", pStatus->nExpectedWorkCounter);
	AppendNumber (rJSON, "cycletime", pStatus->nCycleTimeUs);
	AppendNumber (rJSON, "cycles", pStatus->nCycles);
	AppendNumber (rJSON, "wkcerrors", pStatus->nWKCErrors);
	AppendNumber (rJSON, "opseconds", pStatus->nOperationalSeconds);
	AppendString (rJSON, "error", pStatus->LastError);

	rJSON.TrimRight (",");
	rJSON.Append ("}");
}

void CZeroMQClient::AppendString (CString &rJSON, const char *pName, const char *pValue)
{
	assert (pName != 0);
	assert (pValue != 0);

	CString Value;
	EscapeString (Value, pValue);

	CString Field;
	Field.Format ("\"%s\":\"%s\",", pName, (const char *) Value);

	rJSON.Append ((const char *) Field);
}

void CZeroMQClient::AppendNumber (CString &rJSON, const char *pName, u64 nValue)
{
	assert (pName != 0);

	CString Field;
	Field.Format ("\"%s\":%llu,", pName, (unsigned long long) nValue);

	rJSON.Append ((const char *) Field);
}

void CZeroMQClient::AppendSigned (CString &rJSON, const char *pName, s64 nValue)
{
	assert (pName != 0);

	CString Field;
	Field.Format ("\"%s\":%lld,", pName, (long long) nValue);

	rJSON.Append ((const char *) Field);
}

void CZeroMQClient::AppendBoolean (CString &rJSON, const char *pName, boolean bValue)
{
	assert (pName != 0);

	CString Field;
	Field.Format ("\"%s\":%s,", pName, bValue ? "true" : "false");

	rJSON.Append ((const char *) Field);
}

void CZeroMQClient::EscapeString (CString &rResult, const char *pString)
{
	assert (pString != 0);

	rResult = "";

	for (const char *p = pString; *p != '\0'; p++)
	{
		switch (*p)
		{
		case '"':
			rResult.Append ("\\\"");
			break;

		case '\\':
			rResult.Append ("\\\\");
			break;

		default:
			if (' ' <= *p && *p <= '~')
			{
				rResult.Append (*p);
			}
			break;
		}
	}
}
