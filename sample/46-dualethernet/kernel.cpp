//
// kernel.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014-2026  R. Stange <rsta2@gmx.net>
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
#include <circle/macaddress.h>
#include <circle/string.h>
#include <assert.h>

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_USBHCI (&m_Interrupt, &m_Timer),
	m_Net0 (0, 0, 0, 0, "eth0", NetDeviceTypeEthernet, 0),
	m_Net1 (0, 0, 0, 0, "eth1", NetDeviceTypeEthernet, 1)
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
		// Initialize the on-board Ethernet device before the USB host controller, so
		// that it becomes net device 0 and the USB Ethernet device becomes net device 1.
		bOK = CNetDevice::InitializeOnBoardDevice ();
	}

	if (bOK)
	{
		bOK = m_USBHCI.Initialize ();
	}

	// FALSE: do not wait for the network to come up, so that one stack does not
	// block the other one, if its link is down
	if (bOK)
	{
		bOK = m_Net0.Initialize (FALSE);
	}

	if (bOK)
	{
		bOK = m_Net1.Initialize (FALSE);
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

	m_Logger.Write (FromKernel, LogNotice, "%u net device(s) available",
			CNetDevice::GetNumNetDevices ());

	if (CNetDevice::GetNetDevice (NetDeviceTypeEthernet, 1) == 0)
	{
		m_Logger.Write (FromKernel, LogWarning,
				"Second Ethernet device not found. Is the USB Ethernet device attached?");
	}

	while (1)
	{
		ShowNetSubSystem (&m_Net0, "eth0");
		ShowNetSubSystem (&m_Net1, "eth1");

		m_Scheduler.MsSleep (5000);
	}

	return ShutdownHalt;
}

void CKernel::ShowNetSubSystem (CNetSubSystem *pNet, const char *pLabel)
{
	assert (pNet != 0);
	assert (pLabel != 0);

	CString MACString ("unknown");
	const CMACAddress *pMACAddress = pNet->GetNetDeviceLayer ()->GetMACAddress ();
	if (pMACAddress != 0)
	{
		pMACAddress->Format (&MACString);
	}

	CString IPString ("not assigned");
	if (pNet->IsRunning ())
	{
		pNet->GetConfig ()->GetIPAddress ()->Format (&IPString);
	}

	m_Logger.Write (FromKernel, LogNotice, "%s: MAC %s, IP %s", pLabel,
			(const char *) MACString, (const char *) IPString);
}
