//
// elmodrive.cpp
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
#include "elmodrive.h"
#include <jsd/jsd.h>
#include <jsd/jsd_egd.h>
#include <jsd/jsd_epd_nominal.h>
#include <jsd/jsd_elmo_common.h>
#include <circle/netdevice.h>
#include <circle/macaddress.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

#define ELMO_TASK_STACK_SIZE	(128 * 1024)	// SOEM and JSD use big stack frames

#define RETRY_DELAY_MS		5000		// delay before the bus is scanned again
#define STATUS_UPDATE_MS	100		// interval of the status updates

// acceleration used for the master generated profiles (counts/s^2)
#define ELMO_CYCLIC_ACCEL	20000.0

static const char FromElmo[] = "elmo";

CElmoMaster::CElmoMaster (unsigned nDeviceIndex, unsigned nCycleTimeUs)
:	CTask (ELMO_TASK_STACK_SIZE),
	m_nDeviceIndex (nDeviceIndex),
	m_nCycleTimeUs (nCycleTimeUs),
	m_pJSD (0),
	m_bRunning (FALSE),
	m_nDriveCount (0),
	m_nSlaveInfoCount (0),
	m_nCommandIn (0),
	m_nCommandOut (0),
	m_nCycles (0),
	m_nWKCErrors (0),
	m_nMissedDeadlines (0),
	m_nMaxCyclePeriodUs (0),
	m_nOperationalTicks (0),
	m_MasterState ("Initializing"),
	m_pStatus (0)
{
	SetName ("elmo");

	m_Interface.Format ("eth%u", nDeviceIndex);

	memset (m_Drive, 0, sizeof m_Drive);
	memset (m_SlaveInfo, 0, sizeof m_SlaveInfo);

	m_pStatus = new TElmoStatus;
	assert (m_pStatus != 0);
	memset (m_pStatus, 0, sizeof *m_pStatus);

	strncpy (m_pStatus->InterfaceName, (const char *) m_Interface,
		 sizeof m_pStatus->InterfaceName - 1);
	m_pStatus->nCycleTimeUs = nCycleTimeUs;
}

CElmoMaster::~CElmoMaster (void)
{
	Stop ();

	delete m_pStatus;
	m_pStatus = 0;
}

void CElmoMaster::Run (void)
{
	CLogger::Get ()->Write (FromElmo, LogNotice,
				"Elmo master starting on %s (cycle time %u us)",
				(const char *) m_Interface, m_nCycleTimeUs);

	while (1)
	{
		if (!Discover ())
		{
			UpdateStatus ();

			CScheduler::Get ()->MsSleep (RETRY_DELAY_MS);

			continue;
		}

		if (!Start ())
		{
			Stop ();

			UpdateStatus ();

			CScheduler::Get ()->MsSleep (RETRY_DELAY_MS);

			continue;
		}

		CyclicProcess ();

		Stop ();

		CScheduler::Get ()->MsSleep (RETRY_DELAY_MS);
	}
}

//
// Bus
//

boolean CElmoMaster::Discover (void)
{
	m_nDriveCount = 0;
	memset (m_Drive, 0, sizeof m_Drive);

	m_nSlaveInfoCount = 0;
	memset (m_SlaveInfo, 0, sizeof m_SlaveInfo);

	m_MasterState = "Scanning";

	CNetDevice *pNetDevice = CNetDevice::GetNetDevice (NetDeviceTypeEthernet,
							   m_nDeviceIndex);
	if (pNetDevice == 0)
	{
		SetError ("Net device is not available");

		return FALSE;
	}

	if (!pNetDevice->IsLinkUp ())
	{
		SetError ("The Ethernet link is down");

		return FALSE;
	}

	// JSD needs the slave configuration before the bus is initialized, so the
	// bus is scanned once here to find out, which slaves are Elmo drives.
	ecx_contextt *pScan = new ecx_contextt;
	if (pScan == 0)
	{
		SetError ("Out of memory");

		return FALSE;
	}

	memset (pScan, 0, sizeof *pScan);

	boolean bOK = FALSE;

	if (ecx_init (pScan, (const char *) m_Interface) > 0)
	{
		if (ecx_config_init (pScan) > 0)
		{
			// Collect the identification of every slave first, so that a
			// device, which is not driven by this application, can be
			// identified from the log and from the web page.
			ReadSlaveInfo (pScan);

			for (unsigned i = 0; i < m_nSlaveInfoCount; i++)
			{
				TElmoSlaveInfo *pInfo = &m_SlaveInfo[i];

				if (!pInfo->bElmo)
				{
					strncpy (pInfo->Note, "not an Elmo device",
						 sizeof pInfo->Note - 1);

					continue;
				}

				TElmoFamily Family = ElmoFamilyUnknown;

				if (jsd_egd_product_code_is_compatible (pInfo->ProductCode))
				{
					Family = ElmoFamilyGold;
				}
				else if (jsd_epd_nominal_product_code_is_compatible (
						pInfo->ProductCode))
				{
					Family = ElmoFamilyPlatinum;
				}
				else
				{
					strncpy (pInfo->Note,
						 "Elmo product code is not known to JSD",
						 sizeof pInfo->Note - 1);

					continue;
				}

				if (m_nDriveCount >= ELMO_MAX_DRIVES)
				{
					strncpy (pInfo->Note,
						 "too many drives (see ELMO_MAX_DRIVES)",
						 sizeof pInfo->Note - 1);

					continue;
				}

				TElmoDrive *pDrive = &m_Drive[m_nDriveCount];

				pDrive->bPresent = TRUE;
				pDrive->nSlave = pInfo->nSlave;
				pDrive->Family = Family;
				pDrive->ProductCode = pInfo->ProductCode;
				pDrive->SerialNumber = pInfo->Serial;

				// An Elmo Gold drive accepts either the profiled or the
				// cyclic synchronous commands, not both. The profiled mode
				// is used by default, because the drive generates the
				// motion profile itself then, which is much less sensitive
				// to a jittery cycle time.
				pDrive->Mode = ElmoCommandModeProfiled;

				pDrive->Motion = ElmoCommandNone;

				pInfo->bDriven = TRUE;
				strncpy (pInfo->Note,
					 Family == ElmoFamilyGold
						? "driven as an Elmo Gold drive"
						: "driven as an Elmo Platinum drive",
					 sizeof pInfo->Note - 1);

				m_nDriveCount++;
			}

			LogSlaveInfo ();

			m_pStatus->nSlaveCount = pScan->slavecount;

			bOK = m_nDriveCount > 0;
			if (!bOK)
			{
				SetError ("No Elmo drives found on the bus");
			}
		}
		else
		{
			SetError ("No slaves found on the bus");
		}

		ecx_close (pScan);
	}
	else
	{
		SetError ("Cannot open the net device");
	}

	delete pScan;

	if (bOK)
	{
		CLogger::Get ()->Write (FromElmo, LogNotice, "%u Elmo drive(s) found",
					m_nDriveCount);
	}

	return bOK;
}

void CElmoMaster::ReadSlaveInfo (ecx_contextt *pContext)
{
	assert (pContext != 0);

	m_nSlaveInfoCount = 0;
	memset (m_SlaveInfo, 0, sizeof m_SlaveInfo);

	for (int nSlave = 1;
	         nSlave <= pContext->slavecount
	         && m_nSlaveInfoCount < ELMO_MAX_SLAVES;
	         nSlave++)
	{
		ec_slavet *pSlave = &pContext->slavelist[nSlave];
		TElmoSlaveInfo *pInfo = &m_SlaveInfo[m_nSlaveInfoCount++];

		pInfo->nSlave = nSlave;
		strncpy (pInfo->Name, pSlave->name, sizeof pInfo->Name - 1);

		pInfo->VendorID = pSlave->eep_man;
		pInfo->ProductCode = pSlave->eep_id;
		pInfo->Revision = pSlave->eep_rev;
		pInfo->Serial = pSlave->eep_ser;

		pInfo->bElmo = pSlave->eep_man == JSD_ELMO_VENDOR_ID;

		// The slaves are in the PRE-OP state here, so their mailbox is
		// active and the standard identification objects can be read.
		if (pSlave->mbx_proto & ECT_MBXPROT_COE)
		{
			pInfo->bHasCoE = TRUE;

			ReadSlaveSDOString (pContext, nSlave, 0x1008, pInfo->DeviceName,
					    sizeof pInfo->DeviceName);
			ReadSlaveSDOString (pContext, nSlave, 0x1009,
					    pInfo->HardwareVersion,
					    sizeof pInfo->HardwareVersion);
			ReadSlaveSDOString (pContext, nSlave, 0x100A,
					    pInfo->SoftwareVersion,
					    sizeof pInfo->SoftwareVersion);
		}
	}

	if (pContext->slavecount > ELMO_MAX_SLAVES)
	{
		CLogger::Get ()->Write (FromElmo, LogWarning,
			"Only the first %u of %d slaves are reported "
			"(see ELMO_MAX_SLAVES)", ELMO_MAX_SLAVES, pContext->slavecount);
	}
}

void CElmoMaster::ReadSlaveSDOString (ecx_contextt *pContext, unsigned nSlave,
				      u16 usIndex, char *pString, size_t nSize)
{
	assert (pContext != 0);
	assert (pString != 0);
	assert (nSize > 0);

	*pString = '\0';

	char Buffer[ELMO_MAX_NAME];
	memset (Buffer, 0, sizeof Buffer);

	int nBufferSize = sizeof Buffer - 1;

	if (ecx_SDOread (pContext, (uint16) nSlave, usIndex, 0x00, FALSE,
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

void CElmoMaster::LogSlaveInfo (void)
{
	for (unsigned i = 0; i < m_nSlaveInfoCount; i++)
	{
		const TElmoSlaveInfo *pInfo = &m_SlaveInfo[i];

		CLogger::Get ()->Write (FromElmo, LogNotice,
			"slave[%u] \"%s\" vendor 0x%X product 0x%X rev 0x%X "
			"serial 0x%X: %s",
			pInfo->nSlave, pInfo->Name, pInfo->VendorID,
			pInfo->ProductCode, pInfo->Revision, pInfo->Serial,
			pInfo->Note);

		if (pInfo->bHasCoE)
		{
			CLogger::Get ()->Write (FromElmo, LogNotice,
				"slave[%u] device \"%s\" hardware \"%s\" software \"%s\"",
				pInfo->nSlave, pInfo->DeviceName,
				pInfo->HardwareVersion, pInfo->SoftwareVersion);
		}
	}
}

boolean CElmoMaster::Start (void)
{
	assert (m_pJSD == 0);

	m_MasterState = "Configuring";

	m_pJSD = jsd_alloc ();
	if (m_pJSD == 0)
	{
		SetError ("Cannot allocate the JSD context");

		return FALSE;
	}

	uint8_t loop_period_ms = (uint8_t) (m_nCycleTimeUs / 1000);
	if (loop_period_ms < 1)
	{
		loop_period_ms = 1;
	}

	for (unsigned i = 0; i < m_nDriveCount; i++)
	{
		TElmoDrive *pDrive = &m_Drive[i];

		jsd_slave_config_t Config;
		memset (&Config, 0, sizeof Config);

		Config.configuration_active = true;

		CString Name;
		Name.Format ("drive%u", i);
		strncpy (Config.name, (const char *) Name, JSD_NAME_LEN-1);

		if (pDrive->Family == ElmoFamilyGold)
		{
			Config.driver_type = JSD_DRIVER_TYPE_EGD;

			Config.egd.drive_cmd_mode =
				  pDrive->Mode == ElmoCommandModeCyclic
				? JSD_EGD_DRIVE_CMD_MODE_CS
				: JSD_EGD_DRIVE_CMD_MODE_PROFILED;

			Config.egd.loop_period_ms = (int8_t) loop_period_ms;
			Config.egd.max_motor_speed = ELMO_MAX_MOTOR_SPEED;
			Config.egd.max_profile_accel = ELMO_MAX_PROFILE_ACCEL;
			Config.egd.max_profile_decel = ELMO_MAX_PROFILE_DECEL;
			Config.egd.velocity_tracking_error = ELMO_VELOCITY_TRACKING_ERROR;
			Config.egd.position_tracking_error = ELMO_POSITION_TRACKING_ERROR;
			Config.egd.peak_current_limit = ELMO_PEAK_CURRENT_A;
			Config.egd.peak_current_time = ELMO_PEAK_CURRENT_TIME_S;
			Config.egd.continuous_current_limit = ELMO_CONTINUOUS_CURRENT_A;
			Config.egd.motor_stuck_current_level_pct = 0.0f;	// disabled
			Config.egd.over_speed_threshold = ELMO_OVER_SPEED_THRESHOLD;
			Config.egd.low_position_limit = ELMO_LOW_POSITION_LIMIT;
			Config.egd.high_position_limit = ELMO_HIGH_POSITION_LIMIT;
			Config.egd.brake_engage_msec = ELMO_BRAKE_ENGAGE_MS;
			Config.egd.brake_disengage_msec = ELMO_BRAKE_DISENGAGE_MS;
			Config.egd.smooth_factor = ELMO_SMOOTH_FACTOR;
			Config.egd.ctrl_gain_scheduling_mode =
				JSD_ELMO_GAIN_SCHEDULING_MODE_PRELOADED;

			// The parameter CRC of the drive is not known here, so the
			// check is overridden. The check of the peak current against
			// the maximum current of the drive (MC[1]) stays active.
			Config.egd.crc = INT32_MIN;
			Config.egd.drive_max_current_limit = 0.0f;
		}
		else
		{
			Config.driver_type = JSD_DRIVER_TYPE_EPD_NOMINAL;

			Config.epd_nominal.loop_period_ms = loop_period_ms;
			Config.epd_nominal.max_motor_speed = ELMO_MAX_MOTOR_SPEED;
			Config.epd_nominal.max_profile_accel = ELMO_MAX_PROFILE_ACCEL;
			Config.epd_nominal.max_profile_decel = ELMO_MAX_PROFILE_DECEL;
			Config.epd_nominal.velocity_tracking_error =
				ELMO_VELOCITY_TRACKING_ERROR;
			Config.epd_nominal.position_tracking_error =
				ELMO_POSITION_TRACKING_ERROR;
			Config.epd_nominal.peak_current_limit = ELMO_PEAK_CURRENT_A;
			Config.epd_nominal.peak_current_time = ELMO_PEAK_CURRENT_TIME_S;
			Config.epd_nominal.continuous_current_limit =
				ELMO_CONTINUOUS_CURRENT_A;
			Config.epd_nominal.motor_stuck_current_level_pct = 0.0f;
			Config.epd_nominal.over_speed_threshold = ELMO_OVER_SPEED_THRESHOLD;
			Config.epd_nominal.low_position_limit = ELMO_LOW_POSITION_LIMIT;
			Config.epd_nominal.high_position_limit = ELMO_HIGH_POSITION_LIMIT;
			Config.epd_nominal.brake_engage_msec = ELMO_BRAKE_ENGAGE_MS;
			Config.epd_nominal.brake_disengage_msec = ELMO_BRAKE_DISENGAGE_MS;
			Config.epd_nominal.smooth_factor = ELMO_SMOOTH_FACTOR;
			Config.epd_nominal.ctrl_gain_scheduling_mode =
				JSD_ELMO_GAIN_SCHEDULING_MODE_PRELOADED;

			// see the comment for the Gold drive above
			Config.epd_nominal.crc = 0;

			// A Platinum drive supports the profiled and the cyclic
			// synchronous commands at the same time.
			pDrive->Mode = ElmoCommandModeProfiled;
		}

		jsd_set_slave_config (m_pJSD, (uint16_t) pDrive->nSlave, Config);
	}

	if (!jsd_init (m_pJSD, (const char *) m_Interface, 1, EC_TIMEOUTRET))
	{
		SetError ("Cannot bring the bus to the OPERATIONAL state");

		return FALSE;
	}

	// the process data sizes are known now, publish them with the bus scan
	for (unsigned i = 0; i < m_nSlaveInfoCount; i++)
	{
		TElmoSlaveInfo *pInfo = &m_SlaveInfo[i];

		if (pInfo->nSlave <= (unsigned) m_pJSD->ecx_context.slavecount)
		{
			pInfo->nInputBytes =
				m_pJSD->ecx_context.slavelist[pInfo->nSlave].Ibytes;
			pInfo->nOutputBytes =
				m_pJSD->ecx_context.slavelist[pInfo->nSlave].Obytes;
		}
	}

	m_bRunning = TRUE;
	m_MasterState = "Operational";
	m_LastError = "";
	m_nOperationalTicks = CTimer::Get ()->GetTicks ();

	CLogger::Get ()->Write (FromElmo, LogNotice, "The bus is operational");

	return TRUE;
}

void CElmoMaster::Stop (void)
{
	m_bRunning = FALSE;

	for (unsigned i = 0; i < m_nDriveCount; i++)
	{
		m_Drive[i].bArmed = FALSE;
		m_Drive[i].Motion = ElmoCommandNone;
	}

	if (m_pJSD != 0)
	{
		jsd_free (m_pJSD);

		m_pJSD = 0;
	}
}

void CElmoMaster::CyclicProcess (void)
{
	assert (m_pJSD != 0);

	u64 nNextWakeTicks = CTimer::GetClockTicks64 () + m_nCycleTimeUs;
	u64 nLastCycleTicks = CTimer::GetClockTicks64 ();
	unsigned nLastStatusTicks = CTimer::Get ()->GetTicks ();

	while (m_bRunning)
	{
		// measure the period of the previous cycle
		u64 nNowTicks = CTimer::GetClockTicks64 ();
		unsigned nPeriodUs = (unsigned) (nNowTicks - nLastCycleTicks);
		nLastCycleTicks = nNowTicks;

		if (   m_nCycles > 1
		    && nPeriodUs > m_nMaxCyclePeriodUs)
		{
			m_nMaxCyclePeriodUs = nPeriodUs;
		}

		ApplyCommands ();

		jsd_read (m_pJSD, EC_TIMEOUTRET);

		if (m_pJSD->wkc != m_pJSD->expected_wkc)
		{
			m_nWKCErrors++;
		}

		for (unsigned i = 0; i < m_nDriveCount; i++)
		{
			ServiceDrive (i);
		}

		jsd_write (m_pJSD);

		m_nCycles++;

		CheckWatchdog ();

		unsigned nTicks = CTimer::Get ()->GetTicks ();
		if (nTicks - nLastStatusTicks >= MSEC2HZ (STATUS_UPDATE_MS))
		{
			nLastStatusTicks = nTicks;

			UpdateStatus ();
		}

		// Wait for the next cycle. An absolute deadline is used, so that the
		// period does not drift with the processing time.
		nNextWakeTicks += m_nCycleTimeUs;

		u64 nCurrentTicks = CTimer::GetClockTicks64 ();
		if (nNextWakeTicks > nCurrentTicks)
		{
			CScheduler::Get ()->usSleep (
				(unsigned) (nNextWakeTicks - nCurrentTicks));
		}
		else
		{
			// the cycle took longer than the cycle time
			m_nMissedDeadlines++;

			nNextWakeTicks = nCurrentTicks;

			CScheduler::Get ()->Yield ();
		}
	}

	UpdateStatus ();
}

//
// Drives
//

// The JSD API has one set of functions per drive family. These helpers
// dispatch to the driver, which matches the family of the drive.

void CElmoMaster::ServiceDrive (unsigned nDrive)
{
	TElmoDrive *pDrive = &m_Drive[nDrive];
	uint16_t nSlave = (uint16_t) pDrive->nSlave;

	boolean bGold = pDrive->Family == ElmoFamilyGold;

	// read the process data of this drive from the IO map
	if (bGold)
	{
		jsd_egd_read (m_pJSD, nSlave);
	}
	else
	{
		jsd_epd_nominal_read (m_pJSD, nSlave);
	}

	// update a master generated profile, if one is active
	if (   pDrive->bArmed
	    && pDrive->Mode == ElmoCommandModeCyclic
	    && pDrive->Motion != ElmoCommandNone)
	{
		UpdateCyclicMotion (nDrive);
	}

	// encode the process data of this drive into the IO map
	if (bGold)
	{
		jsd_egd_process (m_pJSD, nSlave);
	}
	else
	{
		jsd_epd_nominal_process (m_pJSD, nSlave);
	}
}

void CElmoMaster::HaltDrive (unsigned nDrive, const char *pReason)
{
	TElmoDrive *pDrive = &m_Drive[nDrive];

	if (pDrive->Family == ElmoFamilyGold)
	{
		jsd_egd_halt (m_pJSD, (uint16_t) pDrive->nSlave);
	}
	else
	{
		jsd_epd_nominal_halt (m_pJSD, (uint16_t) pDrive->nSlave);
	}

	pDrive->bArmed = FALSE;
	pDrive->Motion = ElmoCommandNone;

	if (pReason != 0)
	{
		SetDriveError (pDrive, pReason);

		CLogger::Get ()->Write (FromElmo, LogWarning, "drive %u halted: %s",
					nDrive, pReason);
	}
}

void CElmoMaster::CheckWatchdog (void)
{
	unsigned nTicks = CTimer::Get ()->GetTicks ();

	for (unsigned i = 0; i < m_nDriveCount; i++)
	{
		TElmoDrive *pDrive = &m_Drive[i];

		if (!pDrive->bArmed)
		{
			continue;
		}

		if (nTicks - pDrive->nHeartbeatTicks >= MSEC2HZ (ELMO_WATCHDOG_MS))
		{
			HaltDrive (i, "Watchdog expired, no heartbeat from the web interface");
		}
	}
}

void CElmoMaster::Heartbeat (void)
{
	unsigned nTicks = CTimer::Get ()->GetTicks ();

	for (unsigned i = 0; i < ELMO_MAX_DRIVES; i++)
	{
		m_Drive[i].nHeartbeatTicks = nTicks;
	}
}

//
// Commands
//

boolean CElmoMaster::QueueCommand (const TElmoCommand &rCommand, CString &rError)
{
	if (rCommand.nDrive >= m_nDriveCount)
	{
		rError = "No such drive";

		return FALSE;
	}

	if (!m_bRunning)
	{
		rError = "The bus is not operational";

		return FALSE;
	}

	boolean bOK = FALSE;

	m_CommandMutex.Acquire ();

	unsigned nNext = (m_nCommandIn + 1) % (sizeof m_CommandQueue / sizeof m_CommandQueue[0]);
	if (nNext != m_nCommandOut)
	{
		m_CommandQueue[m_nCommandIn] = rCommand;
		m_nCommandIn = nNext;

		bOK = TRUE;
	}

	m_CommandMutex.Release ();

	if (!bOK)
	{
		rError = "The command queue is full";
	}

	// any command from the web interface is a sign of life
	Heartbeat ();

	return bOK;
}

void CElmoMaster::ApplyCommands (void)
{
	while (1)
	{
		TElmoCommand Command;

		m_CommandMutex.Acquire ();

		boolean bAvailable = m_nCommandOut != m_nCommandIn;
		if (bAvailable)
		{
			Command = m_CommandQueue[m_nCommandOut];
			m_nCommandOut = (m_nCommandOut + 1)
				      % (sizeof m_CommandQueue / sizeof m_CommandQueue[0]);
		}

		m_CommandMutex.Release ();

		if (!bAvailable)
		{
			break;
		}

		ApplyCommand (Command);
	}

	// keep the drives, which want to be enabled, calling reset
	for (unsigned i = 0; i < m_nDriveCount; i++)
	{
		TElmoDrive *pDrive = &m_Drive[i];

		if (   !pDrive->bEnableRequested
		    || IsServoEnabled (i))
		{
			continue;
		}

		if (pDrive->Family == ElmoFamilyGold)
		{
			jsd_egd_reset (m_pJSD, (uint16_t) pDrive->nSlave);
		}
		else
		{
			jsd_epd_nominal_reset (m_pJSD, (uint16_t) pDrive->nSlave);
		}
	}
}

void CElmoMaster::ApplyCommand (const TElmoCommand &rCommand)
{
	unsigned nDrive = rCommand.nDrive;
	if (nDrive >= m_nDriveCount)
	{
		return;
	}

	TElmoDrive *pDrive = &m_Drive[nDrive];
	uint16_t nSlave = (uint16_t) pDrive->nSlave;
	boolean bGold = pDrive->Family == ElmoFamilyGold;

	pDrive->LastError[0] = '\0';

	switch (rCommand.Code)
	{
	case ElmoCommandEnable:
		pDrive->bEnableRequested = TRUE;

		if (bGold)
		{
			jsd_egd_reset (m_pJSD, nSlave);
		}
		else
		{
			jsd_epd_nominal_reset (m_pJSD, nSlave);
		}
		break;

	case ElmoCommandDisable:
		pDrive->bEnableRequested = FALSE;
		pDrive->bArmed = FALSE;
		pDrive->Motion = ElmoCommandNone;

		if (bGold)
		{
			jsd_egd_disable_drive (m_pJSD, nSlave);
		}
		else
		{
			// the Platinum driver has no disable function
			jsd_epd_nominal_halt (m_pJSD, nSlave);
		}
		break;

	case ElmoCommandResetFaults:
		if (bGold)
		{
			jsd_egd_clear_errors (m_pJSD, nSlave);
			jsd_egd_reset (m_pJSD, nSlave);
		}
		else
		{
			jsd_epd_nominal_clear_errors (m_pJSD, nSlave);
			jsd_epd_nominal_reset (m_pJSD, nSlave);
		}
		break;

	case ElmoCommandArm:
		if (!IsServoEnabled (nDrive))
		{
			SetDriveError (pDrive, "The drive must be enabled before it is armed");

			break;
		}

		pDrive->bArmed = TRUE;
		pDrive->nHeartbeatTicks = CTimer::Get ()->GetTicks ();
		break;

	case ElmoCommandDisarm:
		pDrive->bArmed = FALSE;
		pDrive->Motion = ElmoCommandNone;
		break;

	case ElmoCommandStop:
		HaltDrive (nDrive, 0);
		break;

	case ElmoCommandJog:
	case ElmoCommandMoveTo:
	case ElmoCommandMoveBy:
	case ElmoCommandTorque:
		StartMotion (nDrive, rCommand);
		break;

	default:
		SetDriveError (pDrive, "Unknown command");
		break;
	}
}

void CElmoMaster::SetDriveError (TElmoDrive *pDrive, const char *pMessage)
{
	assert (pDrive != 0);
	assert (pMessage != 0);

	strncpy (pDrive->LastError, pMessage, sizeof pDrive->LastError - 1);
	pDrive->LastError[sizeof pDrive->LastError - 1] = '\0';
}

boolean CElmoMaster::IsServoEnabled (unsigned nDrive)
{
	TElmoDrive *pDrive = &m_Drive[nDrive];

	if (   m_pJSD == 0
	    || !m_bRunning)
	{
		return FALSE;
	}

	if (pDrive->Family == ElmoFamilyGold)
	{
		return !!jsd_egd_get_state (m_pJSD, (uint16_t) pDrive->nSlave)->servo_enabled;
	}

	return !!jsd_epd_nominal_get_state (m_pJSD,
					    (uint16_t) pDrive->nSlave)->servo_enabled;
}

/// \brief Clamp a value to a symmetric range
static double Clamp (double fValue, double fLimit)
{
	if (fValue > fLimit)
	{
		return fLimit;
	}

	if (fValue < -fLimit)
	{
		return -fLimit;
	}

	return fValue;
}

void CElmoMaster::StartMotion (unsigned nDrive, const TElmoCommand &rCommand)
{
	TElmoDrive *pDrive = &m_Drive[nDrive];
	uint16_t nSlave = (uint16_t) pDrive->nSlave;
	boolean bGold = pDrive->Family == ElmoFamilyGold;

	if (!pDrive->bArmed)
	{
		SetDriveError (pDrive, "The drive must be armed before it can move");

		return;
	}

	if (!IsServoEnabled (nDrive))
	{
		SetDriveError (pDrive, "The drive is not enabled");

		return;
	}

	// All targets are clamped here. The limits, which are shown in the web
	// interface, are advisory only, this is where they are enforced.
	double fVelocity = Clamp (rCommand.fValue, ELMO_MAX_JOG_VELOCITY);
	double fTorque = Clamp (rCommand.fValue, ELMO_MAX_TORQUE_A);

	s32 nTarget = rCommand.nTarget;
	if (   ELMO_LOW_POSITION_LIMIT != ELMO_HIGH_POSITION_LIMIT
	    && rCommand.Code == ElmoCommandMoveTo)
	{
		if (nTarget < ELMO_LOW_POSITION_LIMIT)
		{
			nTarget = ELMO_LOW_POSITION_LIMIT;
		}
		else if (nTarget > ELMO_HIGH_POSITION_LIMIT)
		{
			nTarget = ELMO_HIGH_POSITION_LIMIT;
		}
	}

	// the actual position is the starting point of a master generated profile
	s32 nActualPosition =   bGold
			      ? jsd_egd_get_state (m_pJSD, nSlave)->actual_position
			      : jsd_epd_nominal_get_state (m_pJSD, nSlave)->actual_position;

	if (rCommand.Code == ElmoCommandMoveBy)
	{
		nTarget += nActualPosition;
	}

	pDrive->Motion = rCommand.Code;
	pDrive->nMotionTarget = nTarget;
	pDrive->fMotionValue =   rCommand.Code == ElmoCommandTorque
			       ? fTorque : fVelocity;

	pDrive->fCmdPosition = nActualPosition;
	pDrive->fCmdVelocity = 0.0;

	if (pDrive->Mode == ElmoCommandModeProfiled)
	{
		// The drive generates the motion profile. The command has to be sent
		// once only, JSD repeats it in each call of the process function.
		switch (rCommand.Code)
		{
		case ElmoCommandJog: {
			jsd_elmo_motion_command_prof_vel_t Command;
			memset (&Command, 0, sizeof Command);

			Command.target_velocity = (int32_t) fVelocity;
			Command.profile_accel = ELMO_MAX_PROFILE_ACCEL;
			Command.profile_decel = ELMO_MAX_PROFILE_DECEL;

			if (bGold)
			{
				jsd_egd_set_motion_command_prof_vel (m_pJSD, nSlave, Command);
			}
			else
			{
				jsd_epd_nominal_set_motion_command_prof_vel (m_pJSD, nSlave,
									     Command);
			}
			} break;

		case ElmoCommandMoveTo:
		case ElmoCommandMoveBy: {
			jsd_elmo_motion_command_prof_pos_t Command;
			memset (&Command, 0, sizeof Command);

			Command.target_position = nTarget;
			Command.profile_velocity =
				(uint32_t) Clamp (fVelocity < 0.0 ? -fVelocity : fVelocity,
						  ELMO_MAX_MOVE_VELOCITY);
			Command.end_velocity = 0;
			Command.profile_accel = ELMO_MAX_PROFILE_ACCEL;
			Command.profile_decel = ELMO_MAX_PROFILE_DECEL;
			Command.relative = 0;		// nTarget is absolute here

			if (Command.profile_velocity == 0)
			{
				SetDriveError (pDrive, "The move velocity must not be zero");

				pDrive->Motion = ElmoCommandNone;

				return;
			}

			if (bGold)
			{
				jsd_egd_set_motion_command_prof_pos (m_pJSD, nSlave, Command);
			}
			else
			{
				jsd_epd_nominal_set_motion_command_prof_pos (m_pJSD, nSlave,
									     Command);
			}
			} break;

		case ElmoCommandTorque: {
			jsd_elmo_motion_command_prof_torque_t Command;
			memset (&Command, 0, sizeof Command);

			Command.target_torque_amps = fTorque;

			if (bGold)
			{
				jsd_egd_set_motion_command_prof_torque (m_pJSD, nSlave,
									Command);
			}
			else
			{
				jsd_epd_nominal_set_motion_command_prof_torque (m_pJSD, nSlave,
									        Command);
			}
			} break;

		default:
			break;
		}
	}

	CLogger::Get ()->Write (FromElmo, LogNotice, "drive %u: %s (target %d, value %d)",
				nDrive, GetCommandName (rCommand.Code), (int) nTarget,
				(int) pDrive->fMotionValue);
}

void CElmoMaster::UpdateCyclicMotion (unsigned nDrive)
{
	// In the cyclic synchronous modes the master has to send a new setpoint in
	// each cycle. A simple trapezoidal profile is generated here, so that the
	// drive does not get a velocity step.
	TElmoDrive *pDrive = &m_Drive[nDrive];
	uint16_t nSlave = (uint16_t) pDrive->nSlave;
	boolean bGold = pDrive->Family == ElmoFamilyGold;

	double fDeltaTime = (double) m_nCycleTimeUs / 1.0e6;
	double fAccelStep = ELMO_CYCLIC_ACCEL * fDeltaTime;

	switch (pDrive->Motion)
	{
	case ElmoCommandJog: {
		// ramp the velocity to the requested value
		double fTarget = pDrive->fMotionValue;

		if (pDrive->fCmdVelocity < fTarget)
		{
			pDrive->fCmdVelocity += fAccelStep;
			if (pDrive->fCmdVelocity > fTarget)
			{
				pDrive->fCmdVelocity = fTarget;
			}
		}
		else if (pDrive->fCmdVelocity > fTarget)
		{
			pDrive->fCmdVelocity -= fAccelStep;
			if (pDrive->fCmdVelocity < fTarget)
			{
				pDrive->fCmdVelocity = fTarget;
			}
		}

		jsd_elmo_motion_command_csv_t Command;
		memset (&Command, 0, sizeof Command);

		Command.target_velocity = (int32_t) pDrive->fCmdVelocity;

		if (bGold)
		{
			jsd_egd_set_motion_command_csv (m_pJSD, nSlave, Command);
		}
		else
		{
			jsd_epd_nominal_set_motion_command_csv (m_pJSD, nSlave, Command);
		}
		} break;

	case ElmoCommandMoveTo:
	case ElmoCommandMoveBy: {
		double fRemaining = (double) pDrive->nMotionTarget - pDrive->fCmdPosition;
		double fDirection = fRemaining >= 0.0 ? 1.0 : -1.0;
		double fDistance = fRemaining * fDirection;

		double fMaxVelocity = pDrive->fMotionValue;
		if (fMaxVelocity < 0.0)
		{
			fMaxVelocity = -fMaxVelocity;
		}

		double fSpeed = pDrive->fCmdVelocity * fDirection;

		// distance, which is needed to stop from the current speed
		double fStopDistance = fSpeed * fSpeed / (2.0 * ELMO_CYCLIC_ACCEL);

		if (fDistance <= fStopDistance)
		{
			fSpeed -= fAccelStep;			// decelerate
		}
		else
		{
			fSpeed += fAccelStep;			// accelerate
		}

		if (fSpeed > fMaxVelocity)
		{
			fSpeed = fMaxVelocity;
		}
		else if (fSpeed < 0.0)
		{
			fSpeed = 0.0;
		}

		pDrive->fCmdVelocity = fSpeed * fDirection;
		pDrive->fCmdPosition += pDrive->fCmdVelocity * fDeltaTime;

		// the target has been reached, when the remaining distance is smaller
		// than one step
		if (fDistance <= fSpeed * fDeltaTime + 1.0)
		{
			pDrive->fCmdPosition = pDrive->nMotionTarget;
			pDrive->fCmdVelocity = 0.0;
			pDrive->Motion = ElmoCommandNone;
		}

		jsd_elmo_motion_command_csp_t Command;
		memset (&Command, 0, sizeof Command);

		Command.target_position = (int32_t) pDrive->fCmdPosition;

		if (bGold)
		{
			jsd_egd_set_motion_command_csp (m_pJSD, nSlave, Command);
		}
		else
		{
			jsd_epd_nominal_set_motion_command_csp (m_pJSD, nSlave, Command);
		}
		} break;

	case ElmoCommandTorque: {
		jsd_elmo_motion_command_cst_t Command;
		memset (&Command, 0, sizeof Command);

		Command.target_torque_amps = pDrive->fMotionValue;

		if (bGold)
		{
			jsd_egd_set_motion_command_cst (m_pJSD, nSlave, Command);
		}
		else
		{
			jsd_epd_nominal_set_motion_command_cst (m_pJSD, nSlave, Command);
		}
		} break;

	default:
		break;
	}
}

//
// Status
//

/// \return Name of a DS-402 mode of operation
static const char *GetModeOfOperationString (int nMode)
{
	switch (nMode)
	{
	case 0:		return "Disabled";
	case 1:		return "Profiled position";
	case 3:		return "Profiled velocity";
	case 4:		return "Profiled torque";
	case 8:		return "Cyclic sync. position";
	case 9:		return "Cyclic sync. velocity";
	case 10:	return "Cyclic sync. torque";
	default:	break;
	}

	return "Unknown";
}

void CElmoMaster::UpdateStatus (void)
{
	assert (m_pStatus != 0);

	// the net device information is collected first, this may block
	CString MACString ("unknown");
	CString SpeedString ("down");
	boolean bLinkUp = FALSE;

	CNetDevice *pNetDevice = CNetDevice::GetNetDevice (NetDeviceTypeEthernet,
							   m_nDeviceIndex);
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
	}

	m_StatusMutex.Acquire ();

	m_pStatus->LinkUp = bLinkUp;
	strncpy (m_pStatus->MACAddress, (const char *) MACString,
		 sizeof m_pStatus->MACAddress - 1);
	m_pStatus->MACAddress[sizeof m_pStatus->MACAddress - 1] = '\0';
	strncpy (m_pStatus->LinkSpeed, (const char *) SpeedString,
		 sizeof m_pStatus->LinkSpeed - 1);
	m_pStatus->LinkSpeed[sizeof m_pStatus->LinkSpeed - 1] = '\0';

	strncpy (m_pStatus->MasterState, (const char *) m_MasterState,
		 sizeof m_pStatus->MasterState - 1);
	m_pStatus->MasterState[sizeof m_pStatus->MasterState - 1] = '\0';
	strncpy (m_pStatus->LastError, (const char *) m_LastError,
		 sizeof m_pStatus->LastError - 1);
	m_pStatus->LastError[sizeof m_pStatus->LastError - 1] = '\0';

	m_pStatus->nDriveCount = m_nDriveCount;
	m_pStatus->nSlaveInfoCount = m_nSlaveInfoCount;
	memcpy (m_pStatus->SlaveInfo, m_SlaveInfo, sizeof m_SlaveInfo);
	m_pStatus->nCycles = m_nCycles;
	m_pStatus->nWKCErrors = m_nWKCErrors;
	m_pStatus->nMissedDeadlines = m_nMissedDeadlines;
	m_pStatus->nMaxCyclePeriodUs = m_nMaxCyclePeriodUs;

	m_pStatus->nWorkCounter = m_pJSD != 0 ? m_pJSD->wkc : 0;
	m_pStatus->nExpectedWorkCounter = m_pJSD != 0 ? m_pJSD->expected_wkc : 0;

	m_pStatus->nOperationalSeconds =
		  m_bRunning && m_nOperationalTicks != 0
		? (CTimer::Get ()->GetTicks () - m_nOperationalTicks) / HZ : 0;

	for (unsigned i = 0; i < ELMO_MAX_DRIVES; i++)
	{
		TElmoDriveStatus *pStatus = &m_pStatus->Drives[i];
		TElmoDrive *pDrive = &m_Drive[i];

		memset (pStatus, 0, sizeof *pStatus);

		if (!pDrive->bPresent)
		{
			continue;
		}

		pStatus->bPresent = TRUE;
		pStatus->nSlave = pDrive->nSlave;
		pStatus->ProductCode = pDrive->ProductCode;
		pStatus->SerialNumber = pDrive->SerialNumber;
		pStatus->bArmed = pDrive->bArmed;

		boolean bGold = pDrive->Family == ElmoFamilyGold;

		strncpy (pStatus->Family, bGold ? "Gold" : "Platinum",
			 sizeof pStatus->Family - 1);
		strncpy (pStatus->CommandMode,
			 pDrive->Mode == ElmoCommandModeCyclic ? "Cyclic" : "Profiled",
			 sizeof pStatus->CommandMode - 1);
		strncpy (pStatus->Motion, GetCommandName (pDrive->Motion),
			 sizeof pStatus->Motion - 1);
		strncpy (pStatus->LastError, pDrive->LastError,
			 sizeof pStatus->LastError - 1);

		// A Gold drive supports one command mode only, which is fixed, when
		// the bus is configured. A Platinum drive supports both.
		pStatus->bCanProfiled =    !bGold
					|| pDrive->Mode == ElmoCommandModeProfiled;
		pStatus->bCanCyclic =    !bGold
				      || pDrive->Mode == ElmoCommandModeCyclic;

		pStatus->fPeakCurrentLimit = ELMO_PEAK_CURRENT_A;
		pStatus->fContinuousCurrentLimit = ELMO_CONTINUOUS_CURRENT_A;
		pStatus->fMaxJogVelocity = ELMO_MAX_JOG_VELOCITY;
		pStatus->fMaxTorque = ELMO_MAX_TORQUE_A;

		if (   m_pJSD == 0
		    || !m_bRunning)
		{
			continue;
		}

		uint16_t nSlave = (uint16_t) pDrive->nSlave;

		if (bGold)
		{
			const jsd_egd_state_t *pState = jsd_egd_get_state (m_pJSD, nSlave);

			pStatus->nActualPosition = pState->actual_position;
			pStatus->nActualVelocity = pState->actual_velocity;
			pStatus->nCommandPosition = pState->cmd_position;
			pStatus->fActualCurrent = pState->actual_current;
			pStatus->fBusVoltage = pState->bus_voltage;
			pStatus->fDriveTemperature = pState->drive_temperature;

			pStatus->bServoEnabled = !!pState->servo_enabled;
			pStatus->bMotorOn = !!pState->motor_on;
			pStatus->bInMotion = !!pState->in_motion;
			pStatus->bSTOEngaged = !!pState->sto_engaged;
			pStatus->bWarning = !!pState->warning;
			pStatus->bTargetReached = !!pState->target_reached;

			pStatus->nFaultCode = pState->fault_code;
			pStatus->nEMCYCode = pState->emcy_error_code;

			strncpy (pStatus->FaultString,
				 jsd_egd_fault_code_to_string (pState->fault_code),
				 sizeof pStatus->FaultString - 1);
			strncpy (pStatus->StateMachine,
				 jsd_elmo_state_machine_state_to_string (
					pState->actual_state_machine_state),
				 sizeof pStatus->StateMachine - 1);
			strncpy (pStatus->ModeOfOperation,
				 GetModeOfOperationString (pState->actual_mode_of_operation),
				 sizeof pStatus->ModeOfOperation - 1);

			for (unsigned j = 0; j < JSD_EGD_NUM_DIGITAL_INPUTS; j++)
			{
				if (pState->digital_inputs[j])
				{
					pStatus->usDigitalInputs |= 1 << j;
				}
			}
		}
		else
		{
			const jsd_epd_nominal_state_t *pState =
				jsd_epd_nominal_get_state (m_pJSD, nSlave);

			pStatus->nActualPosition = pState->actual_position;
			pStatus->nActualVelocity = pState->actual_velocity;
			pStatus->nCommandPosition = pState->cmd_position;
			pStatus->fActualCurrent = pState->actual_current;
			pStatus->fBusVoltage = pState->bus_voltage;
			pStatus->fDriveTemperature = pState->drive_temperature;

			pStatus->bServoEnabled = !!pState->servo_enabled;
			pStatus->bMotorOn = !!pState->motor_on;
			pStatus->bInMotion = !!pState->in_motion;
			pStatus->bSTOEngaged = !!pState->sto_engaged;
			pStatus->bWarning = !!pState->warning;
			pStatus->bTargetReached = !!pState->target_reached;

			pStatus->nFaultCode = pState->fault_code;
			pStatus->nEMCYCode = pState->emcy_error_code;

			strncpy (pStatus->FaultString,
				 jsd_epd_nominal_fault_code_to_string (pState->fault_code),
				 sizeof pStatus->FaultString - 1);
			strncpy (pStatus->StateMachine,
				 jsd_elmo_state_machine_state_to_string (
					pState->actual_state_machine_state),
				 sizeof pStatus->StateMachine - 1);
			strncpy (pStatus->ModeOfOperation,
				 GetModeOfOperationString (pState->actual_mode_of_operation),
				 sizeof pStatus->ModeOfOperation - 1);

			for (unsigned j = 0; j < JSD_EPD_NOMINAL_NUM_DIGITAL_INPUTS
					     && j < 16; j++)
			{
				if (pState->digital_inputs[j])
				{
					pStatus->usDigitalInputs |= 1 << j;
				}
			}
		}

		// The drive is faulted, but the emergency message never arrived. JSD
		// sets this combination after a timeout of one second.
		pStatus->bFaultUnknown = pStatus->nEMCYCode == 0xFFFF;
	}

	m_StatusMutex.Release ();
}

void CElmoMaster::GetStatus (TElmoStatus *pStatus)
{
	assert (pStatus != 0);
	assert (m_pStatus != 0);

	m_StatusMutex.Acquire ();

	memcpy (pStatus, m_pStatus, sizeof *pStatus);

	m_StatusMutex.Release ();
}

void CElmoMaster::SetError (const char *pMessage)
{
	assert (pMessage != 0);

	if (m_LastError.Compare (pMessage) != 0)
	{
		CLogger::Get ()->Write (FromElmo, LogWarning, "%s", pMessage);
	}

	m_LastError = pMessage;
	m_MasterState = "Error";
}

//
// Command names, used by the web interface
//

static const char *s_CommandName[] =
{
	"none", "enable", "disable", "resetfaults", "arm", "disarm", "stop",
	"jog", "moveto", "moveby", "torque"
};

const char *CElmoMaster::GetCommandName (TElmoCommandCode Code)
{
	if (Code >= ElmoCommandUnknown)
	{
		return "unknown";
	}

	return s_CommandName[Code];
}

TElmoCommandCode CElmoMaster::GetCommandCode (const char *pName)
{
	assert (pName != 0);

	for (unsigned i = 0; i < sizeof s_CommandName / sizeof s_CommandName[0]; i++)
	{
		if (strcmp (pName, s_CommandName[i]) == 0)
		{
			return (TElmoCommandCode) i;
		}
	}

	return ElmoCommandUnknown;
}
