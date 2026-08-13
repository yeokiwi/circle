//
// oshw.cpp
//
// Hardware abstraction layer of SOEM (Simple Open EtherCAT Master) for Circle
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2026  R. Stange <rsta2@gmx.net>
//
// This file is part of the Circle port of SOEM (Simple Open EtherCAT Master),
// which is dual-licensed under GPLv3 and a commercial license. See the file
// LICENSE.md in the SOEM sources for full license information.
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
#include "oshw.h"
#include <circle/netdevice.h>
#include <circle/macaddress.h>
#include <circle/string.h>
#include <circle/util.h>
#include <assert.h>

uint16 oshw_htons (uint16 host)
{
	// Circle runs on ARM processors in little endian mode only
	return (uint16) le2be16 (host);
}

uint16 oshw_ntohs (uint16 network)
{
	return (uint16) be2le16 (network);
}

ec_adaptert *oshw_find_adapters (void)
{
	ec_adaptert *pFirst = 0;
	ec_adaptert *pLast = 0;

	for (unsigned nIndex = 0; nIndex < MAX_NET_DEVICES; nIndex++)
	{
		CNetDevice *pNetDevice = CNetDevice::GetNetDevice (NetDeviceTypeEthernet, nIndex);
		if (pNetDevice == 0)
		{
			break;
		}

		ec_adaptert *pAdapter = (ec_adaptert *) osal_malloc (sizeof (ec_adaptert));
		if (pAdapter == 0)
		{
			break;
		}

		CString Name;
		Name.Format ("eth%u", nIndex);
		strncpy (pAdapter->name, (const char *) Name, sizeof pAdapter->name - 1);
		pAdapter->name[sizeof pAdapter->name - 1] = '\0';

		CString MACString ("unknown");
		const CMACAddress *pMACAddress = pNetDevice->GetMACAddress ();
		if (pMACAddress != 0)
		{
			pMACAddress->Format (&MACString);
		}

		CString Description;
		Description.Format ("Ethernet device %u (MAC %s, link %s)", nIndex,
				    (const char *) MACString,
				    pNetDevice->IsLinkUp () ? "up" : "down");
		strncpy (pAdapter->desc, (const char *) Description, sizeof pAdapter->desc - 1);
		pAdapter->desc[sizeof pAdapter->desc - 1] = '\0';

		pAdapter->next = 0;

		if (pLast == 0)
		{
			pFirst = pAdapter;
		}
		else
		{
			pLast->next = pAdapter;
		}

		pLast = pAdapter;
	}

	return pFirst;
}

void oshw_free_adapters (ec_adaptert *adapter)
{
	while (adapter != 0)
	{
		ec_adaptert *pNext = adapter->next;

		osal_free (adapter);

		adapter = pNext;
	}
}
