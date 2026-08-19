//
// ethercat_core1.cpp
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
#include "ethercat_core1.h"
#include <soemcpp.h>
#include <circle/netdevice.h>
#include <circle/macaddress.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

#define RECONFIGURE_DELAY_MS		5000	// delay before the bus is scanned again
#define STATUS_UPDATE_MS		500	// interval of the status updates
#define PHY_UPDATE_MS			2000	// interval of the net device PHY updates

#define MAX_FAILED_CYCLES		500	// cycles without answer before reconfiguring

#define OP_CHECK_RETRIES		200

static const char FromEtherCAT[] = "ethercat";

CEtherCATCore1Master::CEtherCATCore1Master (unsigned nDeviceIndex, unsigned nCycleTimeUs)
:	m_nDeviceIndex (nDeviceIndex),
	m_nCycleTimeUs (nCycleTimeUs),
	m_pContext (0),
	m_pIOMap (0),
	m_bInitialized (FALSE),
	m_bConfigured (FALSE),
	m_bRunning (TRUE),		// the bus is brought to OPERATIONAL by default
	m_nExpectedWKC (0),
	m_nWorkCounter (0),
	m_nCycles (0),
	m_nWKCErrors (0),
	m_nOperationalTicks (0),
	m_State (EtherCATMasterInit),
	m_pStatus (0),
	m_Command (EtherCATCommandNone)
{
	// SOEM addresses the net devices by name ("eth0", "eth1", ...)
	m_Interface.Format ("eth%u", nDeviceIndex);

	// The SOEM context, the process data image and the status are allocated
	// on the heap, because they are too big for the stack.
	m_pContext = new ecx_contextt;
	assert (m_pContext != 0);
	memset (m_pContext, 0, sizeof *m_pContext);

	m_pIOMap = new u8[ETHERCAT_IOMAP_SIZE];
	assert (m_pIOMap != 0);
	memset (m_pIOMap, 0, ETHERCAT_IOMAP_SIZE);

	m_pStatus = new TEtherCATStatus;
	assert (m_pStatus != 0);
	memset (m_pStatus, 0, sizeof *m_pStatus);

	m_pSlaveInfo = new TEtherCATSlaveInfo[ETHERCAT_MAX_SLAVES];
	assert (m_pSlaveInfo != 0);
	memset (m_pSlaveInfo, 0, sizeof (TEtherCATSlaveInfo) * ETHERCAT_MAX_SLAVES);

	strncpy (m_pStatus->InterfaceName, (const char *) m_Interface,
		 sizeof m_pStatus->InterfaceName - 1);
	m_pStatus->nCycleTimeUs = nCycleTimeUs;
}

CEtherCATCore1Master::~CEtherCATCore1Master (void)
{
	if (m_bInitialized)
	{
		ecx_close (m_pContext);

		m_bInitialized = FALSE;
	}

	delete [] m_pSlaveInfo;
	m_pSlaveInfo = 0;

	delete m_pStatus;
	m_pStatus = 0;

	delete [] m_pIOMap;
	m_pIOMap = 0;

	delete m_pContext;
	m_pContext = 0;
}

void CEtherCATCore1Master::Run (void)
{
	CLogger::Get ()->Write (FromEtherCAT, LogNotice,
				"EtherCAT master starting on %s (cycle time %u us)",
				(const char *) m_Interface, m_nCycleTimeUs);

	while (1)
	{
		HandleCommand ();

		if (!m_bConfigured)
		{
			if (!Configure ())
			{
				UpdateStatus ();

				CTimer::Get ()->MsDelay (RECONFIGURE_DELAY_MS);

				continue;
			}

			m_bConfigured = TRUE;
		}

		CyclicProcess ();
	}
}

boolean CEtherCATCore1Master::Configure (void)
{
	// start from a defined state, also when the bus is scanned again
	if (m_bInitialized)
	{
		ecx_close (m_pContext);

		m_bInitialized = FALSE;
	}

	memset (m_pContext, 0, sizeof *m_pContext);

	m_nExpectedWKC = 0;
	m_nWorkCounter = 0;
	m_nOperationalTicks = 0;

	m_LastError = "";

	if (!ecx_init (m_pContext, (const char *) m_Interface))
	{
		SetError ("Net device is not available");
		SetState (EtherCATMasterError);

		return FALSE;
	}

	m_bInitialized = TRUE;

	// The net device is not managed by a TCP/IP stack here, so its settings
	// have to be updated according to the PHY status by this loop.
	ecx_updatephy (&m_pContext->port);

	if (!ecx_linkup (&m_pContext->port))
	{
		SetState (EtherCATMasterNoLink);

		return FALSE;
	}

	SetState (EtherCATMasterConfiguring);

	if (   ecx_config_init (m_pContext) <= 0
	    || m_pContext->slavecount <= 0)
	{
		SetState (EtherCATMasterNoSlaves);

		return FALSE;
	}

	CLogger::Get ()->Write (FromEtherCAT, LogNotice, "%d slave(s) found",
				m_pContext->slavecount);

	// read the slave information, while the slaves are in PRE_OP and the
	// mailboxes can be accessed directly
	ReadSlaveInfo ();

	int nIOMapSize = ecx_config_map_group (m_pContext, m_pIOMap, 0);
	if (nIOMapSize <= 0)
	{
		SetError ("Cannot map the process data");
		SetState (EtherCATMasterError);

		return FALSE;
	}

	if (nIOMapSize > ETHERCAT_IOMAP_SIZE)
	{
		// the process data image did not fit into the buffer
		SetError ("Process data image is too big, increase ETHERCAT_IOMAP_SIZE");
		SetState (EtherCATMasterError);

		return FALSE;
	}

	ecx_configdc (m_pContext);

	ecx_statecheck (m_pContext, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);

	ec_groupt *pGroup = &m_pContext->grouplist[0];
	m_nExpectedWKC = pGroup->outputsWKC * 2 + pGroup->inputsWKC;

	CLogger::Get ()->Write (FromEtherCAT, LogNotice,
				"Process data mapped: %u byte(s) out, %u byte(s) in, expected WKC %d",
				(unsigned) pGroup->Obytes, (unsigned) pGroup->Ibytes,
				m_nExpectedWKC);

	SetState (EtherCATMasterSafeOp);

	if (!m_bRunning)
	{
		// a Stop command is pending (or was already active before this
		// (re-)configuration), so the bus stays parked in SAFE_OP
		CLogger::Get ()->Write (FromEtherCAT, LogNotice,
					"Staying in SAFE_OP (Stop is active)");

		SetState (EtherCATMasterStopped);

		UpdateStatus ();

		return TRUE;
	}

	// request the OPERATIONAL state for all slaves, while process data is
	// exchanged, which is required for this transition
	if (RequestState (EC_STATE_OPERATIONAL, OP_CHECK_RETRIES))
	{
		SetState (EtherCATMasterOperational);

		m_nOperationalTicks = CTimer::Get ()->GetTicks ();

		ClearError ();

		CLogger::Get ()->Write (FromEtherCAT, LogNotice, "All slaves are operational");
	}
	else
	{
		// the bus stays in SAFE_OP, the inputs are valid in this state too
		ecx_readstate (m_pContext);

		for (int nSlave = 1; nSlave <= m_pContext->slavecount; nSlave++)
		{
			ec_slavet *pSlave = &m_pContext->slavelist[nSlave];

			if (pSlave->state != EC_STATE_OPERATIONAL)
			{
				CLogger::Get ()->Write (FromEtherCAT, LogWarning,
					"Slave %d not operational (state 0x%02X, AL status 0x%04X: %s)",
					nSlave, (unsigned) pSlave->state,
					(unsigned) pSlave->ALstatuscode,
					ec_ALstatuscode2string (pSlave->ALstatuscode));
			}
		}

		SetError ("Not all slaves reached the OPERATIONAL state");
		SetState (EtherCATMasterSafeOp);
	}

	UpdateStatus ();

	return TRUE;
}

boolean CEtherCATCore1Master::RequestState (u16 usALState, unsigned nRetries)
{
	assert (m_pContext != 0);

	m_pContext->slavelist[0].state = usALState;

	ecx_send_processdata (m_pContext);
	ecx_receive_processdata (m_pContext, EC_TIMEOUTRET);

	ecx_writestate (m_pContext, 0);

	unsigned nTries = nRetries;
	do
	{
		ecx_send_processdata (m_pContext);
		ecx_receive_processdata (m_pContext, EC_TIMEOUTRET);

		ecx_statecheck (m_pContext, 0, usALState, 50000);
	}
	while (   --nTries > 0
	       && m_pContext->slavelist[0].state != usALState);

	return m_pContext->slavelist[0].state == usALState;
}

void CEtherCATCore1Master::CyclicProcess (void)
{
	unsigned nLastStatusTicks = CTimer::Get ()->GetTicks ();
	unsigned nLastPHYTicks = nLastStatusTicks;
	unsigned nFailedCycles = 0;

	while (m_bConfigured)
	{
		HandleCommand ();

		ecx_send_processdata (m_pContext);

		m_nWorkCounter = ecx_receive_processdata (m_pContext, EC_TIMEOUTRET);

		m_nCycles++;

		if (m_nWorkCounter < m_nExpectedWKC)
		{
			m_nWKCErrors++;

			if (m_nWorkCounter <= 0)
			{
				nFailedCycles++;
			}
		}
		else
		{
			nFailedCycles = 0;
		}

		unsigned nTicks = CTimer::Get ()->GetTicks ();

		if (nTicks - nLastPHYTicks >= MSEC2HZ (PHY_UPDATE_MS))
		{
			nLastPHYTicks = nTicks;

			ecx_updatephy (&m_pContext->port);
		}

		if (nTicks - nLastStatusTicks >= MSEC2HZ (STATUS_UPDATE_MS))
		{
			nLastStatusTicks = nTicks;

			if (!CheckSlaveStates ())
			{
				m_bConfigured = FALSE;
			}

			UpdateStatus ();
		}

		if (nFailedCycles >= MAX_FAILED_CYCLES)
		{
			CLogger::Get ()->Write (FromEtherCAT, LogWarning,
						"No response from the slaves, scanning the bus again");

			SetError ("Lost the connection to the slaves");

			m_bConfigured = FALSE;
		}

		CTimer::SimpleusDelay (m_nCycleTimeUs);
	}

	UpdateStatus ();
}

boolean CEtherCATCore1Master::CheckSlaveStates (void)
{
	assert (m_pContext != 0);

	if (!ecx_linkup (&m_pContext->port))
	{
		SetState (EtherCATMasterNoLink);

		return FALSE;
	}

	ec_groupt *pGroup = &m_pContext->grouplist[0];

	if (   m_bRunning
	    && m_State == EtherCATMasterOperational
	    && m_nWorkCounter >= m_nExpectedWKC
	    && !pGroup->docheckstate)
	{
		// all slaves are working as expected
		return TRUE;
	}

	if (   !m_bRunning
	    && m_State == EtherCATMasterStopped
	    && !pGroup->docheckstate)
	{
		// parked in SAFE_OP by a Stop command, nothing to do
		return TRUE;
	}

	// one or more slaves are not (yet) in the target state, check them one by one
	pGroup->docheckstate = FALSE;

	ecx_readstate (m_pContext);

	boolean bAllInTargetState = TRUE;
	u16 usTargetState = m_bRunning ? EC_STATE_OPERATIONAL : EC_STATE_SAFE_OP;

	for (int nSlave = 1; nSlave <= m_pContext->slavecount; nSlave++)
	{
		ec_slavet *pSlave = &m_pContext->slavelist[nSlave];

		if (pSlave->state == usTargetState)
		{
			if (pSlave->islost)
			{
				pSlave->islost = FALSE;

				CLogger::Get ()->Write (FromEtherCAT, LogNotice,
							"Slave %d found again", nSlave);
			}

			continue;
		}

		bAllInTargetState = FALSE;
		pGroup->docheckstate = TRUE;

		if (pSlave->state == EC_STATE_SAFE_OP + EC_STATE_ERROR)
		{
			CLogger::Get ()->Write (FromEtherCAT, LogWarning,
						"Slave %d is in SAFE_OP + ERROR, acknowledging",
						nSlave);

			pSlave->state = EC_STATE_SAFE_OP + EC_STATE_ACK;
			ecx_writestate (m_pContext, nSlave);
		}
		else if (   m_bRunning
			 && pSlave->state == EC_STATE_SAFE_OP)
		{
			CLogger::Get ()->Write (FromEtherCAT, LogWarning,
						"Slave %d is in SAFE_OP, changing to OPERATIONAL",
						nSlave);

			pSlave->state = EC_STATE_OPERATIONAL;
			ecx_writestate (m_pContext, nSlave);
		}
		else if (pSlave->state > EC_STATE_NONE)
		{
			if (ecx_reconfig_slave (m_pContext, nSlave, EC_TIMEOUTRET3) >= EC_STATE_PRE_OP)
			{
				pSlave->islost = FALSE;

				CLogger::Get ()->Write (FromEtherCAT, LogNotice,
							"Slave %d reconfigured", nSlave);
			}
		}
		else if (!pSlave->islost)
		{
			ecx_statecheck (m_pContext, nSlave, usTargetState, EC_TIMEOUTRET);

			if (pSlave->state == EC_STATE_NONE)
			{
				pSlave->islost = TRUE;

				CLogger::Get ()->Write (FromEtherCAT, LogError,
							"Slave %d lost", nSlave);
			}
		}

		if (   pSlave->islost
		    && pSlave->state <= EC_STATE_INIT)
		{
			if (ecx_recover_slave (m_pContext, nSlave, EC_TIMEOUTRET3))
			{
				pSlave->islost = FALSE;

				CLogger::Get ()->Write (FromEtherCAT, LogNotice,
							"Slave %d recovered", nSlave);
			}
		}
	}

	if (bAllInTargetState)
	{
		if (m_bRunning)
		{
			if (m_State != EtherCATMasterOperational)
			{
				SetState (EtherCATMasterOperational);

				m_nOperationalTicks = CTimer::Get ()->GetTicks ();

				ClearError ();

				CLogger::Get ()->Write (FromEtherCAT, LogNotice,
							"All slaves are operational");
			}
		}
		else
		{
			SetState (EtherCATMasterStopped);
		}
	}
	else
	{
		SetState (m_bRunning ? EtherCATMasterSafeOp : EtherCATMasterStopped);
	}

	return TRUE;
}

void CEtherCATCore1Master::ReadSlaveInfo (void)
{
	assert (m_pContext != 0);
	assert (m_pSlaveInfo != 0);

	memset (m_pSlaveInfo, 0, sizeof (TEtherCATSlaveInfo) * ETHERCAT_MAX_SLAVES);

	for (int nSlave = 1;
	         nSlave <= m_pContext->slavecount && nSlave <= ETHERCAT_MAX_SLAVES;
	         nSlave++)
	{
		ec_slavet *pSlave = &m_pContext->slavelist[nSlave];
		TEtherCATSlaveInfo *pInfo = &m_pSlaveInfo[nSlave-1];

		strncpy (pInfo->Name, pSlave->name, sizeof pInfo->Name - 1);

		pInfo->ConfigAddress = pSlave->configadr;
		pInfo->AliasAddress = pSlave->aliasadr;
		pInfo->VendorID = pSlave->eep_man;
		pInfo->ProductCode = pSlave->eep_id;
		pInfo->RevisionNumber = pSlave->eep_rev;
		pInfo->SerialNumber = pSlave->eep_ser;
		pInfo->MailboxProtocols = pSlave->mbx_proto;
		pInfo->HasDC = pSlave->hasdc;

		// read some standard CoE objects from the slaves, which support it
		if (pSlave->mbx_proto & ECT_MBXPROT_COE)
		{
			ReadSlaveSDOString (nSlave, 0x1008, pInfo->DeviceName,
					    sizeof pInfo->DeviceName);
			ReadSlaveSDOString (nSlave, 0x1009, pInfo->HardwareVersion,
					    sizeof pInfo->HardwareVersion);
			ReadSlaveSDOString (nSlave, 0x100A, pInfo->SoftwareVersion,
					    sizeof pInfo->SoftwareVersion);
		}
	}
}

void CEtherCATCore1Master::ReadSlaveSDOString (unsigned nSlave, u16 usIndex,
					  char *pString, size_t nSize)
{
	assert (pString != 0);
	assert (nSize > 0);

	*pString = '\0';

	char Buffer[ETHERCAT_MAX_NAME];
	memset (Buffer, 0, sizeof Buffer);

	int nBufferSize = sizeof Buffer - 1;

	if (ecx_SDOread (m_pContext, (uint16) nSlave, usIndex, 0x00, FALSE,
			 &nBufferSize, Buffer, EC_TIMEOUTRXM) <= 0)
	{
		return;
	}

	if (   nBufferSize <= 0
	    || (unsigned) nBufferSize >= sizeof Buffer)
	{
		return;
	}

	Buffer[nBufferSize] = '\0';

	// remove non-printable characters
	for (char *p = Buffer; *p != '\0'; p++)
	{
		if (   *p < ' '
		    || *p > '~')
		{
			*p = ' ';
		}
	}

	strncpy (pString, Buffer, nSize-1);
	pString[nSize-1] = '\0';
}

void CEtherCATCore1Master::UpdateStatus (void)
{
	assert (m_pStatus != 0);
	assert (m_pContext != 0);

	// collect the information about the net device first, this may block
	CString MACString ("unknown");
	CString SpeedString ("unknown");
	boolean bLinkUp = FALSE;

	CNetDevice *pNetDevice = CNetDevice::GetNetDevice (NetDeviceTypeEthernet, m_nDeviceIndex);
	if (pNetDevice != 0)
	{
		const CMACAddress *pMACAddress = pNetDevice->GetMACAddress ();
		if (pMACAddress != 0)
		{
			pMACAddress->Format (&MACString);
		}

		bLinkUp = pNetDevice->IsLinkUp ();
		if (bLinkUp)
		{
			SpeedString = CNetDevice::GetSpeedString (pNetDevice->GetLinkSpeed ());
		}
		else
		{
			SpeedString = "down";
		}
	}

	m_StatusSpinLock.Acquire ();

	m_pStatus->State = m_State;
	m_pStatus->LinkUp = bLinkUp;
	strncpy (m_pStatus->MACAddress, (const char *) MACString,
		 sizeof m_pStatus->MACAddress - 1);
	m_pStatus->MACAddress[sizeof m_pStatus->MACAddress - 1] = '\0';
	strncpy (m_pStatus->LinkSpeed, (const char *) SpeedString,
		 sizeof m_pStatus->LinkSpeed - 1);
	m_pStatus->LinkSpeed[sizeof m_pStatus->LinkSpeed - 1] = '\0';

	m_pStatus->nSlaveCount = m_bInitialized ? (unsigned) m_pContext->slavecount : 0;
	m_pStatus->nSlavesInStatus =   m_pStatus->nSlaveCount < ETHERCAT_MAX_SLAVES
				     ? m_pStatus->nSlaveCount : ETHERCAT_MAX_SLAVES;

	for (unsigned i = 0; i < m_pStatus->nSlavesInStatus; i++)
	{
		TEtherCATSlaveInfo *pInfo = &m_pSlaveInfo[i];
		ec_slavet *pSlave = &m_pContext->slavelist[i+1];

		// the dynamic values are taken from the SOEM context
		pInfo->State = pSlave->state;
		pInfo->ALStatusCode = pSlave->ALstatuscode;
		pInfo->OutputBits = pSlave->Obits;
		pInfo->InputBits = pSlave->Ibits;
		pInfo->IsLost = pSlave->islost;

		memcpy (&m_pStatus->Slaves[i], pInfo, sizeof (TEtherCATSlaveInfo));
	}

	m_pStatus->nWorkCounter = m_nWorkCounter;
	m_pStatus->nExpectedWorkCounter = m_nExpectedWKC;
	m_pStatus->nCycles = m_nCycles;
	m_pStatus->nWKCErrors = m_nWKCErrors;
	m_pStatus->nTxFrames = m_pContext->port.txframes;
	m_pStatus->nTxErrors = m_pContext->port.txerrors;
	m_pStatus->nRxFrames = m_pContext->port.stack.rxcnt;
	m_pStatus->nRxDropped = m_pContext->port.rxdropped;
	m_pStatus->nDCTime = m_pContext->DCtime;

	ec_groupt *pGroup = &m_pContext->grouplist[0];

	m_pStatus->nOutputBytes = m_bConfigured ? (unsigned) pGroup->Obytes : 0;
	m_pStatus->nInputBytes = m_bConfigured ? (unsigned) pGroup->Ibytes : 0;

	m_pStatus->nOutputBytesInStatus = 0;
	if (   m_pStatus->nOutputBytes > 0
	    && pGroup->outputs != 0)
	{
		m_pStatus->nOutputBytesInStatus =   m_pStatus->nOutputBytes < ETHERCAT_MAX_IO_BYTES
						  ? m_pStatus->nOutputBytes : ETHERCAT_MAX_IO_BYTES;

		memcpy (m_pStatus->Outputs, pGroup->outputs, m_pStatus->nOutputBytesInStatus);
	}

	m_pStatus->nInputBytesInStatus = 0;
	if (   m_pStatus->nInputBytes > 0
	    && pGroup->inputs != 0)
	{
		m_pStatus->nInputBytesInStatus =   m_pStatus->nInputBytes < ETHERCAT_MAX_IO_BYTES
						 ? m_pStatus->nInputBytes : ETHERCAT_MAX_IO_BYTES;

		memcpy (m_pStatus->Inputs, pGroup->inputs, m_pStatus->nInputBytesInStatus);
	}

	m_pStatus->nOperationalSeconds = 0;
	if (   m_State == EtherCATMasterOperational
	    && m_nOperationalTicks != 0)
	{
		m_pStatus->nOperationalSeconds =
			(CTimer::Get ()->GetTicks () - m_nOperationalTicks) / HZ;
	}

	strncpy (m_pStatus->LastError, (const char *) m_LastError,
		 sizeof m_pStatus->LastError - 1);
	m_pStatus->LastError[sizeof m_pStatus->LastError - 1] = '\0';

	m_StatusSpinLock.Release ();
}

void CEtherCATCore1Master::GetStatus (TEtherCATStatus *pStatus)
{
	assert (pStatus != 0);
	assert (m_pStatus != 0);

	m_StatusSpinLock.Acquire ();

	memcpy (pStatus, m_pStatus, sizeof *pStatus);

	m_StatusSpinLock.Release ();
}

void CEtherCATCore1Master::SubmitCommand (TEtherCATCommand Command)
{
	m_CommandSpinLock.Acquire ();

	m_Command = Command;

	m_CommandSpinLock.Release ();
}

TEtherCATCommand CEtherCATCore1Master::FetchAndClearCommand (void)
{
	m_CommandSpinLock.Acquire ();

	TEtherCATCommand Command = m_Command;
	m_Command = EtherCATCommandNone;

	m_CommandSpinLock.Release ();

	return Command;
}

void CEtherCATCore1Master::HandleCommand (void)
{
	TEtherCATCommand Command = FetchAndClearCommand ();

	switch (Command)
	{
	case EtherCATCommandStart:
		if (!m_bRunning)
		{
			m_bRunning = TRUE;

			if (m_bConfigured)
			{
				CLogger::Get ()->Write (FromEtherCAT, LogNotice,
							"Start requested, requesting OPERATIONAL");

				RequestState (EC_STATE_OPERATIONAL, OP_CHECK_RETRIES);
			}
			// otherwise Configure() will bring the bus up and request
			// OPERATIONAL directly, because m_bRunning is TRUE now
		}
		break;

	case EtherCATCommandStop:
		if (m_bRunning)
		{
			m_bRunning = FALSE;

			if (m_bConfigured)
			{
				CLogger::Get ()->Write (FromEtherCAT, LogNotice,
							"Stop requested, transitioning to SAFE_OP");

				RequestState (EC_STATE_SAFE_OP, OP_CHECK_RETRIES);
			}
		}
		break;

	case EtherCATCommandNone:
	default:
		break;
	}
}

void CEtherCATCore1Master::SetError (const char *pMessage)
{
	assert (pMessage != 0);

	// write each message to the log only once, the bus is scanned repeatedly
	if (m_LoggedError.Compare (pMessage) != 0)
	{
		CLogger::Get ()->Write (FromEtherCAT, LogWarning, "%s", pMessage);

		m_LoggedError = pMessage;
	}

	m_LastError = pMessage;
}

void CEtherCATCore1Master::ClearError (void)
{
	m_LastError = "";
	m_LoggedError = "";
}

void CEtherCATCore1Master::SetState (TEtherCATMasterState State)
{
	m_State = State;
}

const char *CEtherCATCore1Master::GetMasterStateString (TEtherCATMasterState State)
{
	static const char *StateString[] =
	{
		"Initializing",
		"No link",
		"No slaves found",
		"Configuring",
		"SAFE_OP",
		"OPERATIONAL",
		"Stopped",
		"Error"
	};

	if (State >= EtherCATMasterUnknown)
	{
		return "Unknown";
	}

	return StateString[State];
}

const char *CEtherCATCore1Master::GetSlaveStateString (u16 nState)
{
	boolean bError = !!(nState & 0x10);

	switch (nState & 0x0F)
	{
	case 0x00:	return bError ? "NONE + ERROR"	     : "NONE";
	case 0x01:	return bError ? "INIT + ERROR"	     : "INIT";
	case 0x02:	return bError ? "PRE_OP + ERROR"     : "PRE_OP";
	case 0x03:	return bError ? "BOOT + ERROR"	     : "BOOT";
	case 0x04:	return bError ? "SAFE_OP + ERROR"    : "SAFE_OP";
	case 0x08:	return bError ? "OPERATIONAL + ERROR": "OPERATIONAL";
	default:	break;
	}

	return "Unknown";
}
