//
// netdevlayer.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2015-2025  R. Stange <rsta2@gmx.net>
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
#ifndef _circle_net_netdevlayer_h
#define _circle_net_netdevlayer_h

#include <circle/net/netconfig.h>
#include <circle/netdevice.h>
#include <circle/net/netbuffer.h>
#include <circle/net/netbufferqueue.h>
#include <circle/types.h>

class CNetDeviceLayer
{
public:
	/// \param pNetConfig    Pointer to the network configuration
	/// \param DeviceType    Type of the net device to be used
	/// \param nDeviceIndex  Zero-based index of the device within the devices of this type
	CNetDeviceLayer (CNetConfig *pNetConfig, TNetDeviceType DeviceType,
			 unsigned nDeviceIndex = 0);
	~CNetDeviceLayer (void);

	boolean Initialize (boolean bWaitForActivate);

	void Process (void);

	// returns 0, if net device is not available yet
	const CMACAddress *GetMACAddress (void) const;

	void Send (CNetBuffer *pNetBuffer);
	CNetBuffer *Receive (void);

	boolean IsRunning (void) const;		// is net device available and link up?

	// terminated with 00:00:00:00:00:00
	boolean SetMulticastFilter (const u8 Groups[][MAC_ADDRESS_SIZE]);

private:
	TNetDeviceType m_DeviceType;
	unsigned m_nDeviceIndex;
	CNetConfig *m_pNetConfig;
	CNetDevice *m_pDevice;

	CNetBufferQueue m_TxQueue;
	CNetBufferQueue m_RxQueue;

	CNetBuffer *m_pRxBuffer;
};

#endif
