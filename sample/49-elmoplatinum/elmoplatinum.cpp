//
// elmoplatinum.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2026  R. Stange <rsta2@gmx.net>
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
#include "elmoplatinum.h"
#include <soemcpp.h>
#include <circle/netdevice.h>
#include <circle/macaddress.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

// SOEM uses relatively large buffers on the stack.
#define ELMO_TASK_STACK_SIZE		(64 * 1024)

#define RECONFIGURE_DELAY_MS		5000	// delay before the bus is scanned again
#define STATUS_UPDATE_MS		500	// interval of the status updates

#define MAX_FAILED_CYCLES		500	// cycles without answer before reconfiguring
#define OP_CHECK_RETRIES		200

// Explicit delay between reaching SAFE_OP and requesting OPERATIONAL, as
// required by this application (not a SOEM/EtherCAT requirement by itself).
#define SAFE_OP_TO_OPERATIONAL_DELAY_US	5000

// A short settle time in OPERATION ENABLED before the demo motion starts, so
// that the status log clearly shows the enable sequence first.
#define DEMO_MOTION_SETTLE_MS		500

static const char FromElmo[] = "elmo";

//
// CoE objects used by this application (standard CiA 402 / DS402 objects,
// not Elmo specific). The process data image maps only the objects needed
// for the Profile Velocity mode (0x6060 = 3):
//
//	RxPDO (master -> drive), object 0x1600:
//		0x6040:00  Controlword          UNSIGNED16
//		0x60FF:00  Target velocity      INTEGER32
//
//	TxPDO (drive -> master), object 0x1A00:
//		0x6041:00  Statusword           UNSIGNED16
//		0x606C:00  Velocity actual value INTEGER32
//
// Each entry is packed as (index << 16) | (subindex << 8) | bit length, as
// required by the mapping objects 0x1600/0x1A00.
//
static const u32 s_RxPDOEntries[] =
{
	0x60400010,	// Controlword, 16 bit
	0x60FF0020	// Target velocity, 32 bit
};

static const u32 s_TxPDOEntries[] =
{
	0x60410010,	// Statusword, 16 bit
	0x606C0020	// Velocity actual value, 32 bit
};

// Layout of the mapped process data for this single slave (little endian,
// matches both the Raspberry Pi CPU and the EtherCAT wire format).
struct TElmoOutputs
{
	u16 ControlWord;
	s32 TargetVelocity;
} PACKED;

struct TElmoInputs
{
	u16 StatusWord;
	s32 ActualVelocity;
} PACKED;

// CiA 402 statusword bit masks (DS402, table "Statusword"), used to decode
// the state machine state. These are a public standard, not Elmo specific.
#define SW_MASK_6BIT		0x006F
#define SW_MASK_4BIT		0x004F
#define SW_NOT_READY_TO_SWITCH_ON	0x0000
#define SW_SWITCH_ON_DISABLED		0x0040
#define SW_READY_TO_SWITCH_ON		0x0021
#define SW_SWITCHED_ON			0x0023
#define SW_OPERATION_ENABLED		0x0027
#define SW_FAULT			0x0008
#define SW_FAULT_REACTION_ACTIVE	0x000F
#define SW_QUICK_STOP_ACTIVE		0x0007
#define SW_FAULT_BIT			(1 << 3)

// CiA 402 controlword commands (DS402, table "Controlword"), again a public
// standard, not Elmo specific.
#define CW_SHUTDOWN			0x0006
#define CW_SWITCH_ON			0x0007
#define CW_ENABLE_OPERATION		0x000F
#define CW_FAULT_RESET			0x0080

CElmoPlatinumMaster::CElmoPlatinumMaster (unsigned nDeviceIndex, unsigned nCycleTimeUs)
:	CTask (ELMO_TASK_STACK_SIZE),
	m_nDeviceIndex (nDeviceIndex),
	m_nCycleTimeUs (nCycleTimeUs),
	m_pContext (0),
	m_pIOMap (0),
	m_bInitialized (FALSE),
	m_bConfigured (FALSE),
	m_nExpectedWKC (0),
	m_nWorkCounter (0),
	m_nCycles (0),
	m_nWKCErrors (0),
	m_nOperationalTicks (0),
	m_State (ElmoMasterInit),
	m_bSlaveFound (FALSE),
	m_VendorID (0),
	m_ProductCode (0),
	m_bIsElmoPlatinum (FALSE),
	m_DriveState (CiA402Unknown),
	m_usStatusWord (0),
	m_usControlWord (0),
	m_nActualVelocity (0),
	m_nTargetVelocity (0),
	m_bDemoMotionActive (FALSE),
	m_bDemoMotionDone (FALSE),
	m_nDemoMotionStartTicks (0),
	m_pStatus (0)
{
	SetName ("elmo");

	m_Interface.Format ("eth%u", nDeviceIndex);

	m_pContext = new ecx_contextt;
	assert (m_pContext != 0);
	memset (m_pContext, 0, sizeof *m_pContext);

	m_pIOMap = new u8[ELMO_IOMAP_SIZE];
	assert (m_pIOMap != 0);
	memset (m_pIOMap, 0, ELMO_IOMAP_SIZE);

	m_pStatus = new TElmoPlatinumStatus;
	assert (m_pStatus != 0);
	memset (m_pStatus, 0, sizeof *m_pStatus);

	strncpy (m_pStatus->InterfaceName, (const char *) m_Interface,
		 sizeof m_pStatus->InterfaceName - 1);
	m_pStatus->nCycleTimeUs = nCycleTimeUs;
}

CElmoPlatinumMaster::~CElmoPlatinumMaster (void)
{
	if (m_bInitialized)
	{
		ecx_close (m_pContext);

		m_bInitialized = FALSE;
	}

	delete m_pStatus;
	m_pStatus = 0;

	delete [] m_pIOMap;
	m_pIOMap = 0;

	delete m_pContext;
	m_pContext = 0;
}

void CElmoPlatinumMaster::Run (void)
{
	CLogger::Get ()->Write (FromElmo, LogNotice,
				"Elmo Platinum master starting on %s (cycle time %u us)",
				(const char *) m_Interface, m_nCycleTimeUs);

	while (1)
	{
		if (!m_bConfigured)
		{
			if (!Configure ())
			{
				UpdateStatus ();

				CScheduler::Get ()->MsSleep (RECONFIGURE_DELAY_MS);

				continue;
			}

			m_bConfigured = TRUE;
		}

		CyclicProcess ();
	}
}

//
// Bus bring-up: INIT -> PRE_OP -> (configure the drive) -> SAFE_OP -> wait
// 5 ms -> OPERATIONAL
//

boolean CElmoPlatinumMaster::Configure (void)
{
	if (m_bInitialized)
	{
		ecx_close (m_pContext);

		m_bInitialized = FALSE;
	}

	memset (m_pContext, 0, sizeof *m_pContext);

	m_nExpectedWKC = 0;
	m_nWorkCounter = 0;
	m_nOperationalTicks = 0;
	m_bSlaveFound = FALSE;
	m_bDemoMotionActive = FALSE;
	m_bDemoMotionDone = FALSE;

	m_LastError = "";

	if (!ecx_init (m_pContext, (const char *) m_Interface))
	{
		SetError ("Net device is not available");
		SetState (ElmoMasterError);

		return FALSE;
	}

	m_bInitialized = TRUE;

	// the net device is not managed by a TCP/IP stack, so its PHY settings
	// have to be updated by this task
	ecx_updatephy (&m_pContext->port);

	if (!ecx_linkup (&m_pContext->port))
	{
		SetState (ElmoMasterNoLink);

		return FALSE;
	}

	// Step 1: bring every slave from INIT to PRE_OP. This is done internally
	// by ecx_config_init(), which requests PRE_OP and waits for it.
	if (   ecx_config_init (m_pContext) <= 0
	    || m_pContext->slavecount <= 0)
	{
		SetState (ElmoMasterNoSlaves);

		return FALSE;
	}

	// explicitly verify and log the PRE_OP state, as required by this
	// application, before any configuration is done
	ecx_statecheck (m_pContext, 0, EC_STATE_PRE_OP, EC_TIMEOUTSTATE);

	if (m_pContext->slavelist[0].state != EC_STATE_PRE_OP)
	{
		SetError ("Slave did not reach PRE_OP");
		SetState (ElmoMasterError);

		return FALSE;
	}

	SetState (ElmoMasterPreOp);

	CLogger::Get ()->Write (FromElmo, LogNotice,
				"%d slave(s) found, slave 1 is in PRE_OP, configuring the drive",
				m_pContext->slavecount);

	// the slave is in PRE_OP here, so its mailbox is active and the identity
	// and CoE configuration objects can be read and written
	ReadSlaveInfo ();

	// Step 2: configure the drive (PDO mapping, mode of operation, limits),
	// while it is still in PRE_OP
	if (!ConfigureDrive ())
	{
		SetState (ElmoMasterError);

		return FALSE;
	}

	// Step 3: map the process data. This maps the PDOs just configured above
	// and, as part of SOEM's own state handling, requests and waits for the
	// SAFE_OP state for every slave.
	int nIOMapSize = ecx_config_map_group (m_pContext, m_pIOMap, 0);
	if (nIOMapSize <= 0)
	{
		SetError ("Cannot map the process data");
		SetState (ElmoMasterError);

		return FALSE;
	}

	if (nIOMapSize > ELMO_IOMAP_SIZE)
	{
		SetError ("Process data image is too big, increase ELMO_IOMAP_SIZE");
		SetState (ElmoMasterError);

		return FALSE;
	}

	ec_groupt *pGroup = &m_pContext->grouplist[0];
	if (   pGroup->Obytes != sizeof (TElmoOutputs)
	    || pGroup->Ibytes != sizeof (TElmoInputs))
	{
		SetError ("Unexpected process data size, is the connected slave "
			  "really an Elmo Platinum drive?");
		SetState (ElmoMasterError);

		return FALSE;
	}

	// Distributed Clocks are intentionally not configured here (no
	// ecx_configdc() call): ConfigureDrive() above already forced the drive's
	// sync managers into Free Run mode via 0x1C32/0x1C33, so there is nothing
	// for it to do, and leaving it in would be misleading (see the comment in
	// ConfigureDrive() for why Free Run is used instead of DC-Sync1).

	// explicitly verify and log the SAFE_OP state
	ecx_statecheck (m_pContext, 0, EC_STATE_SAFE_OP, EC_TIMEOUTSTATE);

	if (m_pContext->slavelist[0].state != EC_STATE_SAFE_OP)
	{
		SetError ("Slave did not reach SAFE_OP");
		SetState (ElmoMasterError);

		return FALSE;
	}

	m_nExpectedWKC = pGroup->outputsWKC * 2 + pGroup->inputsWKC;

	SetState (ElmoMasterSafeOp);

	CLogger::Get ()->Write (FromElmo, LogNotice,
				"Slave reached SAFE_OP (%u byte(s) out, %u byte(s) in, "
				"expected WKC %d)",
				(unsigned) pGroup->Obytes, (unsigned) pGroup->Ibytes,
				m_nExpectedWKC);

	// Step 4: wait 5 ms before requesting OPERATIONAL, as required by this
	// application. This is a cooperative sleep, other tasks can run meanwhile.
	CLogger::Get ()->Write (FromElmo, LogNotice,
				"Waiting %u us before requesting OPERATIONAL",
				SAFE_OP_TO_OPERATIONAL_DELAY_US);

	CScheduler::Get ()->usSleep (SAFE_OP_TO_OPERATIONAL_DELAY_US);

	// Step 5: request OPERATIONAL. Process data has to be exchanged
	// continuously while this transition is pending, which some drives
	// require.
	m_pContext->slavelist[0].state = EC_STATE_OPERATIONAL;

	ecx_send_processdata (m_pContext);
	ecx_receive_processdata (m_pContext, EC_TIMEOUTRET);

	ecx_writestate (m_pContext, 0);

	unsigned nTries = OP_CHECK_RETRIES;
	do
	{
		ecx_send_processdata (m_pContext);
		ecx_receive_processdata (m_pContext, EC_TIMEOUTRET);

		ecx_statecheck (m_pContext, 0, EC_STATE_OPERATIONAL, 50000);
	}
	while (   --nTries > 0
	       && m_pContext->slavelist[0].state != EC_STATE_OPERATIONAL);

	if (m_pContext->slavelist[0].state != EC_STATE_OPERATIONAL)
	{
		ecx_readstate (m_pContext);

		CLogger::Get ()->Write (FromElmo, LogWarning,
			"Slave not operational (state 0x%02X, AL status 0x%04X: %s)",
			(unsigned) m_pContext->slavelist[1].state,
			(unsigned) m_pContext->slavelist[1].ALstatuscode,
			ec_ALstatuscode2string (m_pContext->slavelist[1].ALstatuscode));

		SetError ("Slave did not reach OPERATIONAL");
		SetState (ElmoMasterSafeOp);	// SAFE_OP inputs are still valid

		return FALSE;
	}

	SetState (ElmoMasterOperational);

	m_nOperationalTicks = CTimer::Get ()->GetTicks ();

	ClearError ();

	CLogger::Get ()->Write (FromElmo, LogNotice, "Slave is operational");

	UpdateStatus ();

	return TRUE;
}

boolean CElmoPlatinumMaster::ConfigureDrive (void)
{
	assert (m_pContext != 0);

	const uint16 nSlave = 1;
	const int nTimeout = EC_TIMEOUTRXM;

	if (   !m_bSlaveFound
	    || !m_bIsElmoPlatinum)
	{
		CLogger::Get ()->Write (FromElmo, LogWarning,
			"Slave 1 does not look like a known Elmo Platinum drive "
			"(vendor 0x%X, product 0x%X), trying to configure it anyway",
			m_VendorID, m_ProductCode);
	}

	// clear, then rewrite the RxPDO mapping object 0x1600 (Controlword,
	// Target velocity)
	u8 nZero = 0;
	u8 nCount;
	int nSize;

	nSize = sizeof nZero;
	if (ecx_SDOwrite (m_pContext, nSlave, 0x1600, 0x00, FALSE, nSize, &nZero,
			  nTimeout) <= 0)
	{
		SetError ("Cannot clear RxPDO mapping 0x1600");

		return FALSE;
	}

	for (unsigned i = 0; i < sizeof s_RxPDOEntries / sizeof s_RxPDOEntries[0]; i++)
	{
		u32 nEntry = s_RxPDOEntries[i];

		nSize = sizeof nEntry;
		if (ecx_SDOwrite (m_pContext, nSlave, 0x1600, (u8) (i + 1), FALSE,
				  nSize, &nEntry, nTimeout) <= 0)
		{
			SetError ("Cannot write RxPDO mapping entry");

			return FALSE;
		}
	}

	nCount = (u8) (sizeof s_RxPDOEntries / sizeof s_RxPDOEntries[0]);
	nSize = sizeof nCount;
	if (ecx_SDOwrite (m_pContext, nSlave, 0x1600, 0x00, FALSE, nSize, &nCount,
			  nTimeout) <= 0)
	{
		SetError ("Cannot set RxPDO mapping entry count");

		return FALSE;
	}

	// clear, then rewrite the TxPDO mapping object 0x1A00 (Statusword,
	// Velocity actual value)
	nSize = sizeof nZero;
	if (ecx_SDOwrite (m_pContext, nSlave, 0x1A00, 0x00, FALSE, nSize, &nZero,
			  nTimeout) <= 0)
	{
		SetError ("Cannot clear TxPDO mapping 0x1A00");

		return FALSE;
	}

	for (unsigned i = 0; i < sizeof s_TxPDOEntries / sizeof s_TxPDOEntries[0]; i++)
	{
		u32 nEntry = s_TxPDOEntries[i];

		nSize = sizeof nEntry;
		if (ecx_SDOwrite (m_pContext, nSlave, 0x1A00, (u8) (i + 1), FALSE,
				  nSize, &nEntry, nTimeout) <= 0)
		{
			SetError ("Cannot write TxPDO mapping entry");

			return FALSE;
		}
	}

	nCount = (u8) (sizeof s_TxPDOEntries / sizeof s_TxPDOEntries[0]);
	nSize = sizeof nCount;
	if (ecx_SDOwrite (m_pContext, nSlave, 0x1A00, 0x00, FALSE, nSize, &nCount,
			  nTimeout) <= 0)
	{
		SetError ("Cannot set TxPDO mapping entry count");

		return FALSE;
	}

	// assign 0x1600/0x1A00 to the sync managers via 0x1C12/0x1C13
	// (clear the assignment first, write the PDO index, then set count = 1)
	u16 nPDOIndex;

	nSize = sizeof nZero;
	if (   ecx_SDOwrite (m_pContext, nSlave, 0x1C12, 0x00, FALSE, nSize, &nZero,
			     nTimeout) <= 0
	    || ecx_SDOwrite (m_pContext, nSlave, 0x1C13, 0x00, FALSE, nSize, &nZero,
			     nTimeout) <= 0)
	{
		SetError ("Cannot clear PDO assignment");

		return FALSE;
	}

	nPDOIndex = 0x1600;
	nSize = sizeof nPDOIndex;
	if (ecx_SDOwrite (m_pContext, nSlave, 0x1C12, 0x01, FALSE, nSize, &nPDOIndex,
			  nTimeout) <= 0)
	{
		SetError ("Cannot assign RxPDO 0x1600");

		return FALSE;
	}

	nPDOIndex = 0x1A00;
	nSize = sizeof nPDOIndex;
	if (ecx_SDOwrite (m_pContext, nSlave, 0x1C13, 0x01, FALSE, nSize, &nPDOIndex,
			  nTimeout) <= 0)
	{
		SetError ("Cannot assign TxPDO 0x1A00");

		return FALSE;
	}

	u8 nOne = 1;
	nSize = sizeof nOne;
	if (   ecx_SDOwrite (m_pContext, nSlave, 0x1C12, 0x00, FALSE, nSize, &nOne,
			     nTimeout) <= 0
	    || ecx_SDOwrite (m_pContext, nSlave, 0x1C13, 0x00, FALSE, nSize, &nOne,
			     nTimeout) <= 0)
	{
		SetError ("Cannot activate PDO assignment");

		return FALSE;
	}

	// Force Free Run mode (no Distributed Clocks) on sync managers 2 (outputs)
	// and 3 (inputs) via objects 0x1C32:01 / 0x1C33:01 ("Synchronization Type":
	// 0x0000 Free Run, 0x0001 SM-Synchron, 0x0002 DC-Sync0, 0x0003 DC-Sync1).
	// Elmo Platinum drives default SM3 to DC-Sync1, which then requires the
	// master to program a real Sync1 cycle time via ecx_dcsync01() before the
	// slave accepts OPERATIONAL (AL status 0x2D, "DC Sync1 Cycle Time",
	// otherwise). This Circle SOEM port has no real SYNC0/SYNC1 pulse hardware
	// and does not discipline the Pi's clock to the DC master clock (see
	// addon/soem/README), so genuine DC-synchronous operation is not really
	// available here; Free Run matches what CyclicProcess() already does (a
	// free-running software poll loop) and sidesteps the issue entirely.
	// Best-effort: some firmware may already default to Free Run or expose
	// these objects read-only, so a failed write is only logged, not fatal.
	u16 nSyncType = 0x0000;	// Free Run
	nSize = sizeof nSyncType;
	if (   ecx_SDOwrite (m_pContext, nSlave, 0x1C32, 0x01, FALSE, nSize, &nSyncType,
			     nTimeout) <= 0
	    || ecx_SDOwrite (m_pContext, nSlave, 0x1C33, 0x01, FALSE, nSize, &nSyncType,
			     nTimeout) <= 0)
	{
		CLogger::Get ()->Write (FromElmo, LogWarning,
			"Cannot force Free Run mode via 0x1C32/0x1C33, the drive may "
			"require Distributed Clocks to reach OPERATIONAL");
	}
	else
	{
		CLogger::Get ()->Write (FromElmo, LogNotice,
			"Forced Free Run mode (no Distributed Clocks) on sync managers "
			"2 and 3");
	}

	// select the Profile Velocity mode of operation (object 0x6060). This is
	// not part of the cyclic process data, so it is set once here.
	s8 nMode = 3;	// Profile Velocity, see CiA 402 object 0x6060
	nSize = sizeof nMode;
	if (ecx_SDOwrite (m_pContext, nSlave, 0x6060, 0x00, FALSE, nSize, &nMode,
			  nTimeout) <= 0)
	{
		SetError ("Cannot set the mode of operation");

		return FALSE;
	}

	// conservative motion limits, enforced by the drive itself
	u32 nMaxVelocity = ELMO_MAX_PROFILE_VELOCITY;
	u32 nAccel = ELMO_PROFILE_ACCEL;
	u32 nDecel = ELMO_PROFILE_DECEL;

	nSize = sizeof nMaxVelocity;
	ecx_SDOwrite (m_pContext, nSlave, 0x607F, 0x00, FALSE, nSize, &nMaxVelocity,
		      nTimeout);

	nSize = sizeof nAccel;
	ecx_SDOwrite (m_pContext, nSlave, 0x6083, 0x00, FALSE, nSize, &nAccel, nTimeout);

	nSize = sizeof nDecel;
	ecx_SDOwrite (m_pContext, nSlave, 0x6084, 0x00, FALSE, nSize, &nDecel, nTimeout);

	CLogger::Get ()->Write (FromElmo, LogNotice,
				"Drive configured: Profile Velocity mode, "
				"max velocity %u counts/s",
				(unsigned) ELMO_MAX_PROFILE_VELOCITY);

	return TRUE;
}

//
// Cyclic process data exchange and the CiA 402 enable sequence
//

void CElmoPlatinumMaster::CyclicProcess (void)
{
	assert (m_pContext != 0);

	ec_groupt *pGroup = &m_pContext->grouplist[0];
	TElmoOutputs *pOutputs = (TElmoOutputs *) pGroup->outputs;
	TElmoInputs *pInputs = (TElmoInputs *) pGroup->inputs;
	assert (pOutputs != 0);
	assert (pInputs != 0);

	memset (pOutputs, 0, sizeof *pOutputs);

	unsigned nLastStatusTicks = CTimer::Get ()->GetTicks ();
	unsigned nFailedCycles = 0;

	while (m_bConfigured)
	{
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

			// decode the statusword and drive the CiA 402 enable sequence
			TElmoInputs Inputs;
			memcpy (&Inputs, pInputs, sizeof Inputs);

			m_usStatusWord = Inputs.StatusWord;
			m_nActualVelocity = Inputs.ActualVelocity;
			m_DriveState = DecodeStatusWord (m_usStatusWord);

			TCiA402State PreviousState = m_DriveState;

			switch (m_DriveState)
			{
			case CiA402Fault:
			case CiA402FaultReactionActive:
				m_usControlWord = CW_FAULT_RESET;
				break;

			case CiA402NotReadyToSwitchOn:
			case CiA402SwitchOnDisabled:
				m_usControlWord = CW_SHUTDOWN;
				break;

			case CiA402ReadyToSwitchOn:
				m_usControlWord = CW_SWITCH_ON;
				break;

			case CiA402SwitchedOn:
				m_usControlWord = CW_ENABLE_OPERATION;
				break;

			case CiA402OperationEnabled:
				m_usControlWord = CW_ENABLE_OPERATION;

#if ELMO_ENABLE_DEMO_MOTION
				if (!m_bDemoMotionActive
				    && !m_bDemoMotionDone)
				{
					unsigned nTicks = CTimer::Get ()->GetTicks ();

					if (m_nDemoMotionStartTicks == 0)
					{
						m_nDemoMotionStartTicks = nTicks;
					}
					else if (nTicks - m_nDemoMotionStartTicks
						 >= MSEC2HZ (DEMO_MOTION_SETTLE_MS))
					{
						m_bDemoMotionActive = TRUE;
						m_nDemoMotionStartTicks = nTicks;
						m_nTargetVelocity = ELMO_DEMO_VELOCITY_CPS;

						CLogger::Get ()->Write (FromElmo, LogNotice,
							"Starting demo motion: target velocity "
							"%d counts/s for %u ms",
							(int) m_nTargetVelocity,
							ELMO_DEMO_MOVE_MS);
					}
				}
				else if (m_bDemoMotionActive)
				{
					unsigned nTicks = CTimer::Get ()->GetTicks ();

					if (nTicks - m_nDemoMotionStartTicks
					    >= MSEC2HZ (ELMO_DEMO_MOVE_MS))
					{
						m_bDemoMotionActive = FALSE;
						m_bDemoMotionDone = TRUE;
						m_nTargetVelocity = 0;

						CLogger::Get ()->Write (FromElmo, LogNotice,
							"Demo motion done, actual velocity "
							"%d counts/s",
							(int) m_nActualVelocity);
					}
				}
#endif
				break;

			case CiA402QuickStopActive:
				m_usControlWord = CW_SHUTDOWN;
				m_nTargetVelocity = 0;
				m_bDemoMotionActive = FALSE;
				break;

			default:
				m_usControlWord = CW_SHUTDOWN;
				break;
			}

			if (PreviousState != m_DriveState)
			{
				CLogger::Get ()->Write (FromElmo, LogNotice,
					"Drive state: %s (statusword 0x%04X)",
					GetDriveStateString (m_DriveState),
					(unsigned) m_usStatusWord);
			}

			TElmoOutputs Outputs;
			Outputs.ControlWord = m_usControlWord;
			Outputs.TargetVelocity =   m_bDemoMotionActive
						 ? m_nTargetVelocity : 0;
			memcpy (pOutputs, &Outputs, sizeof Outputs);
		}

		unsigned nTicks = CTimer::Get ()->GetTicks ();

		if (nTicks - nLastStatusTicks >= MSEC2HZ (STATUS_UPDATE_MS))
		{
			nLastStatusTicks = nTicks;

			UpdateStatus ();
		}

		if (nFailedCycles >= MAX_FAILED_CYCLES)
		{
			CLogger::Get ()->Write (FromElmo, LogWarning,
						"No response from the slave, scanning the bus again");

			SetError ("Lost the connection to the slave");

			m_bConfigured = FALSE;
		}

		CScheduler::Get ()->usSleep (m_nCycleTimeUs);
	}

	UpdateStatus ();
}

TCiA402State CElmoPlatinumMaster::DecodeStatusWord (u16 usStatusWord)
{
	if ((usStatusWord & SW_MASK_4BIT) == SW_FAULT_REACTION_ACTIVE)
	{
		return CiA402FaultReactionActive;
	}

	if (usStatusWord & SW_FAULT_BIT)
	{
		if ((usStatusWord & SW_MASK_4BIT) == SW_FAULT)
		{
			return CiA402Fault;
		}
	}

	switch (usStatusWord & SW_MASK_6BIT)
	{
	case SW_NOT_READY_TO_SWITCH_ON:	return CiA402NotReadyToSwitchOn;
	case SW_SWITCH_ON_DISABLED:		return CiA402SwitchOnDisabled;
	case SW_READY_TO_SWITCH_ON:		return CiA402ReadyToSwitchOn;
	case SW_SWITCHED_ON:			return CiA402SwitchedOn;
	case SW_OPERATION_ENABLED:		return CiA402OperationEnabled;
	case SW_QUICK_STOP_ACTIVE:		return CiA402QuickStopActive;
	default:				break;
	}

	if ((usStatusWord & SW_MASK_4BIT) == SW_SWITCH_ON_DISABLED)
	{
		return CiA402SwitchOnDisabled;
	}

	return CiA402Unknown;
}

//
// Slave identification
//

void CElmoPlatinumMaster::ReadSlaveInfo (void)
{
	assert (m_pContext != 0);

	m_bSlaveFound = FALSE;
	m_bIsElmoPlatinum = FALSE;

	if (m_pContext->slavecount < 1)
	{
		return;
	}

	ec_slavet *pSlave = &m_pContext->slavelist[1];

	m_bSlaveFound = TRUE;
	m_SlaveName = pSlave->name;
	m_VendorID = pSlave->eep_man;
	m_ProductCode = pSlave->eep_id;

	m_bIsElmoPlatinum =
		   m_VendorID == ELMO_VENDOR_ID
		&& (   m_ProductCode == ELMO_PLATINUM_PRODUCT_CODE
		    || m_ProductCode == ELMO_PLATINUM_SAFETY_PRODUCT_CODE);

	CLogger::Get ()->Write (FromElmo, LogNotice,
		"slave[1] \"%s\" vendor 0x%X product 0x%X: %s",
		pSlave->name, m_VendorID, m_ProductCode,
		m_bIsElmoPlatinum ? "Elmo Platinum drive" : "unknown/unlisted device");

	if (pSlave->mbx_proto & ECT_MBXPROT_COE)
	{
		char DeviceName[ELMO_MAX_NAME];
		char HardwareVersion[ELMO_MAX_NAME];
		char SoftwareVersion[ELMO_MAX_NAME];

		ReadSlaveSDOString (0x1008, DeviceName, sizeof DeviceName);
		ReadSlaveSDOString (0x1009, HardwareVersion, sizeof HardwareVersion);
		ReadSlaveSDOString (0x100A, SoftwareVersion, sizeof SoftwareVersion);

		CLogger::Get ()->Write (FromElmo, LogNotice,
			"slave[1] device \"%s\" hardware \"%s\" software \"%s\"",
			DeviceName, HardwareVersion, SoftwareVersion);
	}
}

void CElmoPlatinumMaster::ReadSlaveSDOString (u16 usIndex, char *pString, size_t nSize)
{
	assert (pString != 0);
	assert (nSize > 0);

	*pString = '\0';

	char Buffer[ELMO_MAX_NAME];
	memset (Buffer, 0, sizeof Buffer);

	int nBufferSize = sizeof Buffer - 1;

	if (ecx_SDOread (m_pContext, 1, usIndex, 0x00, FALSE, &nBufferSize, Buffer,
			 EC_TIMEOUTRXM) <= 0)
	{
		return;
	}

	if (   nBufferSize <= 0
	    || (unsigned) nBufferSize >= sizeof Buffer)
	{
		return;
	}

	Buffer[nBufferSize] = '\0';

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

//
// Status
//

void CElmoPlatinumMaster::UpdateStatus (void)
{
	assert (m_pStatus != 0);

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
		SpeedString =   bLinkUp
			      ? CNetDevice::GetSpeedString (pNetDevice->GetLinkSpeed ())
			      : "down";
	}

	m_StatusMutex.Acquire ();

	m_pStatus->MasterState = m_State;
	m_pStatus->LinkUp = bLinkUp;
	strncpy (m_pStatus->MACAddress, (const char *) MACString,
		 sizeof m_pStatus->MACAddress - 1);
	m_pStatus->MACAddress[sizeof m_pStatus->MACAddress - 1] = '\0';
	strncpy (m_pStatus->LinkSpeed, (const char *) SpeedString,
		 sizeof m_pStatus->LinkSpeed - 1);
	m_pStatus->LinkSpeed[sizeof m_pStatus->LinkSpeed - 1] = '\0';

	m_pStatus->bSlaveFound = m_bSlaveFound;
	strncpy (m_pStatus->SlaveName, (const char *) m_SlaveName,
		 sizeof m_pStatus->SlaveName - 1);
	m_pStatus->SlaveName[sizeof m_pStatus->SlaveName - 1] = '\0';
	m_pStatus->VendorID = m_VendorID;
	m_pStatus->ProductCode = m_ProductCode;
	m_pStatus->bIsElmoPlatinum = m_bIsElmoPlatinum;

	m_pStatus->DriveState = m_DriveState;
	m_pStatus->StatusWord = m_usStatusWord;
	m_pStatus->ControlWord = m_usControlWord;
	m_pStatus->ActualVelocity = m_nActualVelocity;
	m_pStatus->TargetVelocity = m_nTargetVelocity;
	m_pStatus->bDemoMotionActive = m_bDemoMotionActive;

	m_pStatus->nWorkCounter = m_nWorkCounter;
	m_pStatus->nExpectedWorkCounter = m_nExpectedWKC;
	m_pStatus->nCycles = m_nCycles;
	m_pStatus->nWKCErrors = m_nWKCErrors;

	m_pStatus->nOperationalSeconds = 0;
	if (   m_State == ElmoMasterOperational
	    && m_nOperationalTicks != 0)
	{
		m_pStatus->nOperationalSeconds =
			(CTimer::Get ()->GetTicks () - m_nOperationalTicks) / HZ;
	}

	strncpy (m_pStatus->LastError, (const char *) m_LastError,
		 sizeof m_pStatus->LastError - 1);
	m_pStatus->LastError[sizeof m_pStatus->LastError - 1] = '\0';

	m_StatusMutex.Release ();
}

void CElmoPlatinumMaster::GetStatus (TElmoPlatinumStatus *pStatus)
{
	assert (pStatus != 0);
	assert (m_pStatus != 0);

	m_StatusMutex.Acquire ();

	memcpy (pStatus, m_pStatus, sizeof *pStatus);

	m_StatusMutex.Release ();
}

void CElmoPlatinumMaster::SetError (const char *pMessage)
{
	assert (pMessage != 0);

	if (m_LastError.Compare (pMessage) != 0)
	{
		CLogger::Get ()->Write (FromElmo, LogWarning, "%s", pMessage);
	}

	m_LastError = pMessage;
}

void CElmoPlatinumMaster::ClearError (void)
{
	m_LastError = "";
}

void CElmoPlatinumMaster::SetState (TElmoMasterState State)
{
	m_State = State;
}

const char *CElmoPlatinumMaster::GetMasterStateString (TElmoMasterState State)
{
	static const char *StateString[] =
	{
		"Initializing",
		"No link",
		"No slaves found",
		"PRE_OP",
		"SAFE_OP",
		"OPERATIONAL",
		"Error"
	};

	if (State >= ElmoMasterUnknown)
	{
		return "Unknown";
	}

	return StateString[State];
}

const char *CElmoPlatinumMaster::GetDriveStateString (TCiA402State State)
{
	static const char *StateString[] =
	{
		"Not ready to switch on",
		"Switch on disabled",
		"Ready to switch on",
		"Switched on",
		"Operation enabled",
		"Quick stop active",
		"Fault reaction active",
		"Fault"
	};

	if (State >= CiA402Unknown)
	{
		return "Unknown";
	}

	return StateString[State];
}
