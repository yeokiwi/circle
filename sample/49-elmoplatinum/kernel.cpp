//
// kernel.cpp
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
#include "kernel.h"
#include <circle/netdevice.h>
#include <circle/string.h>
#include <assert.h>

#define STATUS_INTERVAL_MS	10000		// interval of the status messages

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	// The interrupt driven serial driver is used, so that writing a debug
	// message does not stall the EtherCAT cycle, until it has been sent.
	m_Serial (&m_Interrupt, FALSE, SERIAL_DEVICE_NUMBER),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_pElmoMaster (0)
{
	m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	// the interrupt system is initialized first, because the serial device
	// uses interrupts
	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (SERIAL_BAUD_RATE);
	}

	if (bOK)
	{
		// The debug messages are written to the serial interface by default.
		// The application option "logtarget=" in cmdline.txt can redirect
		// them to another registered device instead.
		const char *pLogTarget = m_Options.GetAppOptionString ("logtarget");

		CDevice *pTarget = 0;
		if (pLogTarget != 0)
		{
			pTarget = m_DeviceNameService.GetDevice (pLogTarget, FALSE);
		}

		boolean bTargetNotFound = pLogTarget != 0 && pTarget == 0;

		if (pTarget == 0)
		{
			pTarget = &m_Serial;
		}

		bOK = m_Logger.Initialize (pTarget);

		if (   bOK
		    && bTargetNotFound)
		{
			m_Logger.Write (FromKernel, LogWarning,
					"Log device \"%s\" not found, using the serial interface",
					pLogTarget);
		}
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		bOK = CNetDevice::InitializeOnBoardDevice ();
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

#if RASPPI >= 5
	m_Logger.Write (FromKernel, LogNotice,
			"Debug messages go to the UART connector (uart10) at %u baud",
			SERIAL_BAUD_RATE);
#endif

	if (CNetDevice::GetNetDevice (NetDeviceTypeEthernet, ETHERCAT_NET_DEVICE) == 0)
	{
		m_Logger.Write (FromKernel, LogError,
				"Net device %u for EtherCAT not found", ETHERCAT_NET_DEVICE);
	}

	m_pElmoMaster = new CElmoPlatinumMaster (ETHERCAT_NET_DEVICE, ETHERCAT_CYCLE_TIME_US);
	assert (m_pElmoMaster != 0);

	unsigned nLastTicks = m_Timer.GetTicks ();

	while (1)
	{
		// this is required to hold the SoC temperature down
		m_CPUThrottle.Update ();

		unsigned nTicks = m_Timer.GetTicks ();
		if (nTicks - nLastTicks >= MSEC2HZ (STATUS_INTERVAL_MS))
		{
			nLastTicks = nTicks;

			ShowStatus ();
		}

		m_Scheduler.MsSleep (2000);
	}

	return ShutdownHalt;
}

void CKernel::ShowStatus (void)
{
	assert (m_pElmoMaster != 0);

	TElmoPlatinumStatus *pStatus = new TElmoPlatinumStatus;
	if (pStatus == 0)
	{
		return;
	}

	m_pElmoMaster->GetStatus (pStatus);

	m_Logger.Write (FromKernel, LogNotice,
			"EtherCAT: %s, drive: %s, WKC %d of %d, %llu cycle(s), "
			"%llu WKC error(s)",
			CElmoPlatinumMaster::GetMasterStateString (pStatus->MasterState),
			CElmoPlatinumMaster::GetDriveStateString (pStatus->DriveState),
			pStatus->nWorkCounter, pStatus->nExpectedWorkCounter,
			(unsigned long long) pStatus->nCycles,
			(unsigned long long) pStatus->nWKCErrors);

	delete pStatus;
}
