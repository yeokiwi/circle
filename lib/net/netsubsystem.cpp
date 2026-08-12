//
// netsubsystem.cpp
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
#include <circle/net/netsubsystem.h>
#include <circle/net/nettask.h>
#include <circle/net/dhcpclient.h>
#include <circle/sched/scheduler.h>
#include <assert.h>

CNetSubSystem *CNetSubSystem::s_pThis[MAX_NET_SUBSYSTEMS] = {0};
unsigned CNetSubSystem::s_nInstances = 0;

CNetSubSystem::CNetSubSystem (const u8 *pIPAddress, const u8 *pNetMask, const u8 *pDefaultGateway,
			      const u8 *pDNSServer, const char *pHostname, TNetDeviceType DeviceType,
			      unsigned nDeviceIndex)
:	m_Hostname (pHostname != 0 ? pHostname : ""),
	m_nInstance (MAX_NET_SUBSYSTEMS),
	m_NetDevLayer (&m_Config, DeviceType, nDeviceIndex),
	m_LinkLayer (&m_Config, &m_NetDevLayer),
	m_NetworkLayer (&m_Config, &m_LinkLayer),
	m_TransportLayer (&m_Config, &m_NetworkLayer),
	m_bUseDHCP (pIPAddress == 0 ? TRUE : FALSE),
	m_pDHCPClient (0)
{
	for (unsigned nInstance = 0; nInstance < MAX_NET_SUBSYSTEMS; nInstance++)
	{
		if (s_pThis[nInstance] == 0)
		{
			m_nInstance = nInstance;
			s_pThis[nInstance] = this;

			if (s_nInstances <= nInstance)
			{
				s_nInstances = nInstance+1;
			}

			break;
		}
	}
	assert (m_nInstance < MAX_NET_SUBSYSTEMS);

	m_Config.SetDHCP (m_bUseDHCP);

	if (!m_bUseDHCP)
	{
		m_Config.SetIPAddress (pIPAddress);
		m_Config.SetNetMask (pNetMask);

		if (pDefaultGateway != 0)
		{
			m_Config.SetDefaultGateway (pDefaultGateway);
		}

		if (pDNSServer != 0)
		{
			m_Config.SetDNSServer (pDNSServer);
		}
	}
}

CNetSubSystem::~CNetSubSystem (void)
{
	assert (m_nInstance < MAX_NET_SUBSYSTEMS);
	s_pThis[m_nInstance] = 0;

	while (   s_nInstances > 0
	       && s_pThis[s_nInstances-1] == 0)
	{
		s_nInstances--;
	}
}

boolean CNetSubSystem::Initialize (boolean bWaitForActivate)
{
	m_bUseDHCP = m_Config.GetIPAddress ()->IsNull ();
	m_Config.SetDHCP (m_bUseDHCP);

	if (!m_NetDevLayer.Initialize (bWaitForActivate))
	{
		return FALSE;
	}

	if (!m_LinkLayer.Initialize ())
	{
		return FALSE;
	}

	if (!m_NetworkLayer.Initialize ())
	{
		return FALSE;
	}

	m_LinkLayer.AttachLayer (&m_NetworkLayer);

	if (!m_TransportLayer.Initialize ())
	{
		return FALSE;
	}

	new CNetTask (this);

	if (!bWaitForActivate)
	{
		return TRUE;
	}

	while (!IsRunning ())
	{
		CScheduler::Get ()->Yield ();
	}

	return TRUE;
}

void CNetSubSystem::Process (void)
{
	if (   m_nInstance >= MAX_NET_SUBSYSTEMS
	    || s_pThis[m_nInstance] != this)	// destructor has been called
	{
		return;
	}

	if (   m_bUseDHCP
	    && m_pDHCPClient == 0
	    && m_NetDevLayer.IsRunning ())
	{
		m_pDHCPClient = new CDHCPClient (this, m_Hostname);
		assert (m_pDHCPClient != 0);
	}

	m_NetDevLayer.Process ();

	m_LinkLayer.Process ();

	m_NetworkLayer.Process ();

	m_TransportLayer.Process ();
}

CNetConfig *CNetSubSystem::GetConfig (void)
{
	return &m_Config;
}

CNetDeviceLayer *CNetSubSystem::GetNetDeviceLayer (void)
{
	return &m_NetDevLayer;
}

CLinkLayer *CNetSubSystem::GetLinkLayer (void)
{
	return &m_LinkLayer;
}

CNetworkLayer *CNetSubSystem::GetNetworkLayer (void)
{
	return &m_NetworkLayer;
}

CTransportLayer *CNetSubSystem::GetTransportLayer (void)
{
	return &m_TransportLayer;
}

boolean CNetSubSystem::IsRunning (void) const
{
	if (!m_NetDevLayer.IsRunning ())
	{
		return FALSE;
	}

	if (!m_bUseDHCP)
	{
		return TRUE;
	}

	if (m_pDHCPClient == 0)
	{
		return FALSE;
	}

	return m_pDHCPClient->IsBound ();
}

const char *CNetSubSystem::GetHostname (void) const
{
	return m_Hostname;
}

unsigned CNetSubSystem::GetInstance (void) const
{
	return m_nInstance;
}

CNetSubSystem *CNetSubSystem::Get (void)
{
	assert (s_pThis[0] != 0);
	return s_pThis[0];
}

CNetSubSystem *CNetSubSystem::Get (unsigned nInstance)
{
	if (nInstance >= MAX_NET_SUBSYSTEMS)
	{
		return 0;
	}

	return s_pThis[nInstance];
}

unsigned CNetSubSystem::GetInstanceCount (void)
{
	return s_nInstances;
}
