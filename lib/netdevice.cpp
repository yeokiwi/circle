//
// netdevice.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014-2020  R. Stange <rsta2@o2online.de>
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
#include <circle/netdevice.h>
#include <circle/bcm54213.h>
#include <circle/macb.h>

const char *CNetDevice::s_SpeedString[NetDeviceSpeedUnknown] =
{
	"10BASE-T Half-duplex",
	"10BASE-T Full-duplex",
	"100BASE-TX Half-duplex",
	"100BASE-TX Full-duplex",
	"1000BASE-T Half-duplex",
	"1000BASE-T Full-duplex"
};

unsigned CNetDevice::s_nDeviceNumber = 0;

CNetDevice *CNetDevice::s_pDevice[MAX_NET_DEVICES];

void CNetDevice::AddNetDevice (void)
{
	if (s_nDeviceNumber < MAX_NET_DEVICES)
	{
		s_pDevice[s_nDeviceNumber++] = this;
	}
}

const char *CNetDevice::GetSpeedString (TNetDeviceSpeed Speed)
{
	if (Speed >= NetDeviceSpeedUnknown)
	{
		return "Unknown";
	}

	return s_SpeedString[Speed];
}

CNetDevice *CNetDevice::GetNetDevice (unsigned nDeviceNumber)
{
	if (nDeviceNumber < s_nDeviceNumber)
	{
		return s_pDevice[nDeviceNumber];
	}

	return 0;
}

CNetDevice *CNetDevice::GetNetDevice (TNetDeviceType Type)
{
	return GetNetDevice (Type, 0);
}

CNetDevice *CNetDevice::GetNetDevice (TNetDeviceType Type, unsigned nIndex)
{
	for (unsigned nDeviceNumber = 0; nDeviceNumber < s_nDeviceNumber; nDeviceNumber++)
	{
		CNetDevice *pDevice = s_pDevice[nDeviceNumber];
		if (pDevice == 0)
		{
			break;
		}

		if (   Type == NetDeviceTypeAny
		    || pDevice->GetType () == Type)
		{
			if (nIndex-- == 0)
			{
				return pDevice;
			}
		}
	}

	return 0;
}

unsigned CNetDevice::GetNumNetDevices (void)
{
	return s_nDeviceNumber;
}

boolean CNetDevice::InitializeOnBoardDevice (void)
{
	// The on-board net device is a system-wide resource, which must be initialized only
	// once, even if multiple CNetDeviceLayer instances are used. On the models, which do
	// not have a dedicated on-board driver, the device is a USB device and is registered,
	// while the USB host controller is initialized.
#if RASPPI == 4 || RASPPI >= 5
	static boolean s_bInitialized = FALSE;
	if (s_bInitialized)
	{
		return TRUE;
	}

#if RASPPI == 4
	static CBcm54213Device s_Device;
#else
	static CMACBDevice s_Device;
#endif

	if (!s_Device.Initialize ())
	{
		return FALSE;
	}

	s_bInitialized = TRUE;
#endif

	return TRUE;
}
