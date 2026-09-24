//
// zmtpconnection.cpp
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
#include <zeromq/zmtpconnection.h>
#include <circle/net/in.h>
#include <circle/timer.h>
#include <circle/util.h>
#include <assert.h>

#define ZMTP_FLAG_MORE		0x01
#define ZMTP_FLAG_LONG		0x02
#define ZMTP_FLAG_COMMAND	0x04
#define ZMTP_FLAG_RESERVED	0xF8

#define ZMTP_GREETING_SIZE	64

#define ZMTP_MAX_COMMAND_SIZE	1024	// larger command frames are a protocol error
#define ZMTP_MAX_PING_CONTEXT	16

CZMTPConnection::CZMTPConnection (CSocket *pSocket, const char *pSocketType)
:	m_pSocket (pSocket),
	m_State (StateInit),
	m_nStartTicks (0),
	m_nGreetingGot (0),
	m_pRxBody (0),
	m_bRxDiscard (FALSE),
	m_nStageHead (0),
	m_nStageTail (0)
{
	assert (m_pSocket != 0);

	assert (pSocketType != 0);
	strncpy (m_SocketType, pSocketType, sizeof m_SocketType - 1);
	m_SocketType[sizeof m_SocketType - 1] = '\0';

	m_PeerSocketType[0] = '\0';

	ResetFrame ();
}

CZMTPConnection::~CZMTPConnection (void)
{
	delete [] m_pRxBody;
	m_pRxBody = 0;

	delete m_pSocket;
	m_pSocket = 0;
}

boolean CZMTPConnection::Start (void)
{
	assert (m_State == StateInit);
	m_nStartTicks = CTimer::Get ()->GetTicks ();

	// ZMTP 3.0 greeting (see RFC 23)
	u8 Greeting[ZMTP_GREETING_SIZE];
	memset (Greeting, 0, sizeof Greeting);

	Greeting[0] = 0xFF;			// signature
	Greeting[9] = 0x7F;
	Greeting[10] = 3;			// version-major
	Greeting[11] = 0;			// version-minor (3.0, see zmtpconnection.h)
	memcpy (&Greeting[12], "NULL", 4);	// mechanism, padded with zeros
	Greeting[32] = 0;			// as-server (not used by NULL)

	m_State = StateGreeting;		// before SendRaw(), which may set StateFailed

	return SendRaw (Greeting, sizeof Greeting);
}

TZMTPEvent CZMTPConnection::Poll (CZMQMessage *pMessage)
{
	assert (pMessage != 0);
	assert (m_State != StateInit);

	while (m_State != StateFailed)
	{
		if (m_State == StateGreeting)
		{
			int nResult = Read (&m_PeerGreeting[m_nGreetingGot],
					    ZMTP_GREETING_SIZE - m_nGreetingGot);
			if (nResult <= 0)
			{
				break;
			}

			m_nGreetingGot += nResult;
			if (m_nGreetingGot < ZMTP_GREETING_SIZE)
			{
				continue;
			}

			static const u8 NullMechanism[20] = {'N', 'U', 'L', 'L'};

			if (   m_PeerGreeting[0] != 0xFF
			    || !(m_PeerGreeting[9] & 0x01)
			    || m_PeerGreeting[10] < 3
			    || memcmp (&m_PeerGreeting[12], NullMechanism, sizeof NullMechanism) != 0)
			{
				m_State = StateFailed;	// no ZMTP 3.x peer or not NULL mechanism

				break;
			}

			// READY command with our socket type
			static const char PropertyName[] = "Socket-Type";
			size_t nTypeLength = strlen (m_SocketType);

			u8 Property[1 + sizeof PropertyName - 1 + 4 + ZMTP_SOCKET_TYPE_SIZE];
			unsigned i = 0;
			Property[i++] = sizeof PropertyName - 1;
			memcpy (&Property[i], PropertyName, sizeof PropertyName - 1);
			i += sizeof PropertyName - 1;
			Property[i++] = 0;
			Property[i++] = 0;
			Property[i++] = 0;
			Property[i++] = (u8) nTypeLength;
			memcpy (&Property[i], m_SocketType, nTypeLength);
			i += nTypeLength;

			m_State = StateReady;

			SendCommand ("READY", Property, i);

			continue;
		}

		switch (m_RxPhase)
		{
		case RxPhaseFlags: {
			int nResult = Read (&m_uchRxFlags, 1);
			if (nResult <= 0)
			{
				goto NoMoreData;
			}

			if (   (m_uchRxFlags & ZMTP_FLAG_RESERVED)
			    || (   (m_uchRxFlags & ZMTP_FLAG_COMMAND)
				&& (m_uchRxFlags & ZMTP_FLAG_MORE)))
			{
				m_State = StateFailed;

				goto NoMoreData;
			}

			m_nRxLengthSize = m_uchRxFlags & ZMTP_FLAG_LONG ? 8 : 1;
			m_nRxLengthGot = 0;
			m_RxPhase = RxPhaseLength;
			} break;

		case RxPhaseLength: {
			int nResult = Read (&m_RxLength[m_nRxLengthGot],
					    m_nRxLengthSize - m_nRxLengthGot);
			if (nResult <= 0)
			{
				goto NoMoreData;
			}

			m_nRxLengthGot += nResult;
			if (m_nRxLengthGot < m_nRxLengthSize)
			{
				break;
			}

			u64 nLength = 0;
			for (unsigned i = 0; i < m_nRxLengthSize; i++)
			{
				nLength = (nLength << 8) | m_RxLength[i];
			}

			if (   nLength > ZMQ_MAX_FRAME_SIZE
			    || (   (m_uchRxFlags & ZMTP_FLAG_COMMAND)
				&& nLength > ZMTP_MAX_COMMAND_SIZE))
			{
				m_State = StateFailed;

				goto NoMoreData;
			}

			m_nRxBodyLength = (size_t) nLength;
			m_nRxBodyGot = 0;

			assert (m_pRxBody == 0);
			if (m_nRxBodyLength > 0)
			{
				m_pRxBody = new u8[m_nRxBodyLength];
				if (m_pRxBody == 0)
				{
					m_State = StateFailed;

					goto NoMoreData;
				}
			}

			m_RxPhase = RxPhaseBody;
			} break;

		case RxPhaseBody: {
			if (m_nRxBodyGot < m_nRxBodyLength)
			{
				int nResult = Read (m_pRxBody + m_nRxBodyGot,
						    m_nRxBodyLength - m_nRxBodyGot);
				if (nResult <= 0)
				{
					goto NoMoreData;
				}

				m_nRxBodyGot += nResult;
				if (m_nRxBodyGot < m_nRxBodyLength)
				{
					break;
				}
			}

			TZMTPEvent Event = FrameReceived (pMessage);

			ResetFrame ();

			if (Event != ZMTPEventNone)
			{
				return Event;
			}
			} break;
		}
	}

NoMoreData:
	return m_State == StateFailed ? ZMTPEventError : ZMTPEventNone;
}

boolean CZMTPConnection::IsReady (void) const
{
	return m_State == StateActive;
}

boolean CZMTPConnection::IsFailed (void) const
{
	return m_State == StateFailed;
}

const char *CZMTPConnection::GetPeerSocketType (void) const
{
	return m_PeerSocketType;
}

unsigned CZMTPConnection::GetStartTicks (void) const
{
	return m_nStartTicks;
}

boolean CZMTPConnection::Send (const CZMQMessage &rMessage)
{
	if (m_State != StateActive)
	{
		return FALSE;
	}

	unsigned nFrames = rMessage.GetFrameCount ();
	if (nFrames == 0)
	{
		return TRUE;
	}

	size_t nTotal = 0;
	for (unsigned i = 0; i < nFrames; i++)
	{
		size_t nLength = rMessage.GetFrameSize (i);
		nTotal += (nLength > 255 ? 9 : 2) + nLength;
	}

	u8 *pBuffer = new u8[nTotal];
	if (pBuffer == 0)
	{
		return FALSE;
	}

	u8 *p = pBuffer;
	for (unsigned i = 0; i < nFrames; i++)
	{
		size_t nLength = rMessage.GetFrameSize (i);

		u8 uchFlags = i+1 < nFrames ? ZMTP_FLAG_MORE : 0;
		if (nLength > 255)
		{
			*p++ = uchFlags | ZMTP_FLAG_LONG;

			u64 nLength64 = nLength;
			for (int j = 7; j >= 0; j--)
			{
				p[j] = (u8) nLength64;
				nLength64 >>= 8;
			}
			p += 8;
		}
		else
		{
			*p++ = uchFlags;
			*p++ = (u8) nLength;
		}

		if (nLength > 0)
		{
			memcpy (p, rMessage.GetFrame (i), nLength);
			p += nLength;
		}
	}

	assert (p == pBuffer + nTotal);

	boolean bOK = SendRaw (pBuffer, nTotal);

	delete [] pBuffer;

	return bOK;
}

void CZMTPConnection::SetSendTimeout (unsigned nMicroSeconds)
{
	assert (m_pSocket != 0);
	m_pSocket->SetOptionSendTimeout (nMicroSeconds);
}

boolean CZMTPConnection::SendCommand (const char *pName, const void *pData, size_t nLength)
{
	assert (pName != 0);
	size_t nNameLength = strlen (pName);
	assert (0 < nNameLength && nNameLength <= 255);

	size_t nBodyLength = 1 + nNameLength + nLength;
	assert (nBodyLength <= ZMTP_MAX_COMMAND_SIZE);

	u8 Buffer[9 + ZMTP_MAX_COMMAND_SIZE];
	unsigned i = 0;

	if (nBodyLength > 255)
	{
		Buffer[i++] = ZMTP_FLAG_COMMAND | ZMTP_FLAG_LONG;
		for (int j = 7; j >= 0; j--)
		{
			Buffer[i+j] = (u8) (nBodyLength >> (8 * (7-j)));
		}
		i += 8;
	}
	else
	{
		Buffer[i++] = ZMTP_FLAG_COMMAND;
		Buffer[i++] = (u8) nBodyLength;
	}

	Buffer[i++] = (u8) nNameLength;
	memcpy (&Buffer[i], pName, nNameLength);
	i += nNameLength;

	if (nLength > 0)
	{
		assert (pData != 0);
		memcpy (&Buffer[i], pData, nLength);
		i += nLength;
	}

	return SendRaw (Buffer, i);
}

boolean CZMTPConnection::SendRaw (const void *pBuffer, size_t nLength)
{
	assert (m_pSocket != 0);

	if (m_State == StateFailed)
	{
		return FALSE;
	}

	// CSocket::Send() sends the whole buffer or fails
	if (m_pSocket->Send (pBuffer, nLength, 0) != (int) nLength)
	{
		m_State = StateFailed;

		return FALSE;
	}

	return TRUE;
}

int CZMTPConnection::Read (void *pBuffer, size_t nLength)
{
	assert (pBuffer != 0);
	assert (nLength > 0);

	if (m_nStageHead == m_nStageTail)
	{
		assert (m_pSocket != 0);
		int nResult = m_pSocket->Receive (m_RxStage, sizeof m_RxStage, MSG_DONTWAIT);
		if (nResult <= 0)
		{
			if (nResult < 0)
			{
				m_State = StateFailed;
			}

			return nResult;
		}

		m_nStageHead = 0;
		m_nStageTail = nResult;
	}

	size_t nAvailable = m_nStageTail - m_nStageHead;
	if (nLength > nAvailable)
	{
		nLength = nAvailable;
	}

	memcpy (pBuffer, &m_RxStage[m_nStageHead], nLength);
	m_nStageHead += nLength;

	return (int) nLength;
}

TZMTPEvent CZMTPConnection::FrameReceived (CZMQMessage *pMessage)
{
	if (m_uchRxFlags & ZMTP_FLAG_COMMAND)
	{
		return CommandReceived (pMessage);
	}

	if (m_State != StateActive)
	{
		m_State = StateFailed;		// data frame before READY

		return ZMTPEventError;
	}

	if (!m_bRxDiscard)
	{
		if (!m_RxMessage.AddFrameOwned (m_pRxBody, m_nRxBodyLength))
		{
			m_bRxDiscard = TRUE;	// too many frames, drop the message
		}
	}
	else
	{
		delete [] m_pRxBody;
	}
	m_pRxBody = 0;

	if (m_uchRxFlags & ZMTP_FLAG_MORE)
	{
		return ZMTPEventNone;
	}

	if (m_bRxDiscard)
	{
		m_RxMessage.Clear ();
		m_bRxDiscard = FALSE;

		return ZMTPEventNone;
	}

	pMessage->Clear ();
	pMessage->Swap (m_RxMessage);

	return ZMTPEventMessage;
}

TZMTPEvent CZMTPConnection::CommandReceived (CZMQMessage *pMessage)
{
	const u8 *pBody = m_pRxBody;
	size_t nLength = m_nRxBodyLength;

	if (   nLength < 1
	    || nLength < 1 + (size_t) pBody[0])
	{
		m_State = StateFailed;

		return ZMTPEventError;
	}

	char Name[256];
	size_t nNameLength = pBody[0];
	memcpy (Name, &pBody[1], nNameLength);
	Name[nNameLength] = '\0';

	const u8 *pData = &pBody[1 + nNameLength];
	size_t nDataLength = nLength - 1 - nNameLength;

	if (strcmp (Name, "ERROR") == 0)
	{
		m_State = StateFailed;

		return ZMTPEventError;
	}

	if (m_State == StateReady)
	{
		if (   strcmp (Name, "READY") != 0
		    || !ParseReady (pData, nDataLength))
		{
			m_State = StateFailed;

			return ZMTPEventError;
		}

		m_State = StateActive;

		return ZMTPEventReady;
	}

	assert (m_State == StateActive);

	if (strcmp (Name, "PING") == 0)
	{
		// PING: ttl (2 bytes), context (0..16 bytes); PONG returns the context
		if (nDataLength >= 2)
		{
			size_t nContextLength = nDataLength - 2;
			if (nContextLength > ZMTP_MAX_PING_CONTEXT)
			{
				nContextLength = ZMTP_MAX_PING_CONTEXT;
			}

			SendCommand ("PONG", pData + 2, nContextLength);
		}

		return m_State == StateFailed ? ZMTPEventError : ZMTPEventNone;
	}

	boolean bSubscribe = strcmp (Name, "SUBSCRIBE") == 0;
	if (   bSubscribe
	    || strcmp (Name, "CANCEL") == 0)
	{
		// return a ZMTP 3.0 style subscription message
		u8 *pBuffer = new u8[1 + nDataLength];
		if (pBuffer == 0)
		{
			return ZMTPEventNone;
		}

		pBuffer[0] = bSubscribe ? 1 : 0;
		if (nDataLength > 0)
		{
			memcpy (&pBuffer[1], pData, nDataLength);
		}

		pMessage->Clear ();
		pMessage->AddFrameOwned (pBuffer, 1 + nDataLength);

		return ZMTPEventMessage;
	}

	return ZMTPEventNone;		// ignore unknown commands (e.g. PONG)
}

boolean CZMTPConnection::ParseReady (const u8 *pData, size_t nLength)
{
	static const char PropertyName[] = "Socket-Type";

	while (nLength > 0)
	{
		size_t nNameLength = pData[0];
		if (nLength < 1 + nNameLength + 4)
		{
			return FALSE;
		}

		const char *pName = (const char *) &pData[1];
		pData += 1 + nNameLength;
		nLength -= 1 + nNameLength;

		u32 nValueLength =   (u32) pData[0] << 24 | (u32) pData[1] << 16
				   | (u32) pData[2] << 8 | pData[3];
		pData += 4;
		nLength -= 4;

		if (nValueLength > nLength)
		{
			return FALSE;
		}

		if (   nNameLength == sizeof PropertyName - 1
		    && strncasecmp (pName, PropertyName, nNameLength) == 0)
		{
			if (nValueLength >= sizeof m_PeerSocketType)
			{
				return FALSE;
			}

			memcpy (m_PeerSocketType, pData, nValueLength);
			m_PeerSocketType[nValueLength] = '\0';
		}

		pData += nValueLength;
		nLength -= nValueLength;
	}

	return m_PeerSocketType[0] != '\0';
}

void CZMTPConnection::ResetFrame (void)
{
	delete [] m_pRxBody;
	m_pRxBody = 0;

	m_RxPhase = RxPhaseFlags;
	m_uchRxFlags = 0;
	m_nRxLengthSize = 0;
	m_nRxLengthGot = 0;
	m_nRxBodyLength = 0;
	m_nRxBodyGot = 0;
}
