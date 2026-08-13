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
#include "webserver.h"
#include <circle/netdevice.h>
#include <circle/string.h>
#include <assert.h>

#define STATUS_INTERVAL_MS	10000		// interval of the status messages

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_USBHCI (&m_Interrupt, &m_Timer),
	m_Net (0, 0, 0, 0, "ethercat", NetDeviceTypeEthernet, WEBSERVER_NET_DEVICE),
	m_bNetAvailable (FALSE),
	m_pEtherCAT (0)
{
	m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}

	if (bOK)
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
		if (pTarget == 0)
		{
			pTarget = &m_Screen;
		}

		bOK = m_Logger.Initialize (pTarget);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		// Initialize the on-board Ethernet device before the USB host controller,
		// so that it becomes net device 0 and the USB Ethernet device becomes
		// net device 1.
		bOK = CNetDevice::InitializeOnBoardDevice ();
	}

	if (bOK)
	{
		bOK = m_USBHCI.Initialize ();
	}

	if (bOK)
	{
		// The TCP/IP stack is only initialized, if its net device is available.
		// The EtherCAT master works without it.
		if (CNetDevice::GetNetDevice (NetDeviceTypeEthernet, WEBSERVER_NET_DEVICE) != 0)
		{
			// FALSE: do not wait for the network to come up, so that the
			// EtherCAT master can be started, while the link is still down
			m_bNetAvailable = m_Net.Initialize (FALSE);

			bOK = m_bNetAvailable;
		}
		else
		{
			m_Logger.Write (FromKernel, LogWarning,
					"Net device %u not found, the web server is not available",
					WEBSERVER_NET_DEVICE);
		}
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

	m_Logger.Write (FromKernel, LogNotice, "%u net device(s) available",
			CNetDevice::GetNumNetDevices ());

	if (CNetDevice::GetNetDevice (NetDeviceTypeEthernet, ETHERCAT_NET_DEVICE) == 0)
	{
		m_Logger.Write (FromKernel, LogError,
				"Net device %u for EtherCAT not found", ETHERCAT_NET_DEVICE);
	}

	m_pEtherCAT = new CEtherCATMaster (ETHERCAT_NET_DEVICE, ETHERCAT_CYCLE_TIME_US);
	assert (m_pEtherCAT != 0);

	if (m_bNetAvailable)
	{
		new CWebServer (&m_Net, m_pEtherCAT);
	}

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
	if (m_bNetAvailable)
	{
		CString IPString ("not assigned");
		if (m_Net.IsRunning ())
		{
			m_Net.GetConfig ()->GetIPAddress ()->Format (&IPString);
		}

		m_Logger.Write (FromKernel, LogNotice, "Open \"http://%s/\" in your web browser",
				(const char *) IPString);
	}

	assert (m_pEtherCAT != 0);

	TEtherCATStatus *pStatus = new TEtherCATStatus;
	if (pStatus == 0)
	{
		return;
	}

	m_pEtherCAT->GetStatus (pStatus);

	m_Logger.Write (FromKernel, LogNotice,
			"EtherCAT: %s, %u slave(s), WKC %d of %d, %llu cycle(s)",
			CEtherCATMaster::GetMasterStateString (pStatus->State),
			pStatus->nSlaveCount, pStatus->nWorkCounter,
			pStatus->nExpectedWorkCounter,
			(unsigned long long) pStatus->nCycles);

	delete pStatus;
}
