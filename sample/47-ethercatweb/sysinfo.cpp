//
// sysinfo.cpp
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
#include "sysinfo.h"
#include <circle/machineinfo.h>
#include <circle/cputhrottle.h>
#include <circle/bcmpropertytags.h>
#include <circle/memory.h>
#include <circle/netdevice.h>
#include <circle/macaddress.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/version.h>
#include <circle/string.h>
#include <circle/util.h>
#include <assert.h>

static void CopyString (char *pDest, size_t nSize, const char *pSource)
{
	assert (pDest != 0);
	assert (nSize > 0);
	assert (pSource != 0);

	strncpy (pDest, pSource, nSize-1);
	pDest[nSize-1] = '\0';
}

static void CopyIPAddress (char *pDest, size_t nSize, const CIPAddress *pAddress)
{
	if (pAddress == 0)
	{
		CopyString (pDest, nSize, "");

		return;
	}

	CString String;
	pAddress->Format (&String);

	CopyString (pDest, nSize, (const char *) String);
}

static boolean TaskCallback (CTask *pTask, const char *pName, TTaskState State,
			     TTaskFlags Flags, void *pParam)
{
	TSystemStatus *pStatus = (TSystemStatus *) pParam;
	assert (pStatus != 0);

	pStatus->nTaskCount++;

	if (pStatus->nTasksInStatus >= SYSINFO_MAX_TASKS)
	{
		return TRUE;
	}

	TTaskStatus *pTaskStatus = &pStatus->Tasks[pStatus->nTasksInStatus++];

	CopyString (pTaskStatus->Name, sizeof pTaskStatus->Name, pName != 0 ? pName : "");

	const char *pState = "unknown";
	switch (State)
	{
	case TaskStateNew:		pState = "new";		break;
	case TaskStateReady:		pState = "ready";	break;
	case TaskStateBlocked:		pState = "blocked";	break;
	case TaskStateBlockedWithTimeout: pState = "blocked";	break;
	case TaskStateSleeping:		pState = "sleeping";	break;
	case TaskStateTerminated:	pState = "terminated";	break;
	default:			break;
	}

	CopyString (pTaskStatus->State, sizeof pTaskStatus->State, pState);

	pTaskStatus->Suspended = !!(Flags & TaskFlagSuspended);

	return TRUE;
}

void GetSystemStatus (TSystemStatus *pStatus, CNetSubSystem *pNet)
{
	assert (pStatus != 0);

	memset (pStatus, 0, sizeof *pStatus);

	// hardware
	CMachineInfo *pMachineInfo = CMachineInfo::Get ();
	if (pMachineInfo != 0)
	{
		CopyString (pStatus->MachineName, sizeof pStatus->MachineName,
			    pMachineInfo->GetMachineName ());
		CopyString (pStatus->SoCName, sizeof pStatus->SoCName,
			    pMachineInfo->GetSoCName ());

		pStatus->RevisionRaw = pMachineInfo->GetRevisionRaw ();
		pStatus->nRAMSizeMBytes = pMachineInfo->GetRAMSize ();
	}

	// CPU
	CCPUThrottle *pCPUThrottle = CCPUThrottle::Get ();
	if (pCPUThrottle != 0)
	{
		pStatus->nCPUClockRate = pCPUThrottle->GetClockRate ();
		pStatus->nMinCPUClockRate = pCPUThrottle->GetMinClockRate ();
		pStatus->nMaxCPUClockRate = pCPUThrottle->GetMaxClockRate ();
		pStatus->nTemperature = pCPUThrottle->GetTemperature ();
		pStatus->nMaxTemperature = pCPUThrottle->GetMaxTemperature ();
	}

	CBcmPropertyTags Tags;
	TPropertyTagSimple TagGetThrottled;
	TagGetThrottled.nValue = 0;
	if (Tags.GetTag (PROPTAG_GET_THROTTLED, &TagGetThrottled, sizeof TagGetThrottled, 4))
	{
		pStatus->ThrottledState = TagGetThrottled.nValue;
	}

	// memory
	CMemorySystem *pMemorySystem = CMemorySystem::Get ();
	if (pMemorySystem != 0)
	{
		pStatus->nHeapFreeSpace = pMemorySystem->GetHeapFreeSpace (HEAP_ANY);
	}

	// software
	CopyString (pStatus->CircleVersion, sizeof pStatus->CircleVersion,
		    CIRCLE_VERSION_STRING);
	CopyString (pStatus->CompileTime, sizeof pStatus->CompileTime,
		    __DATE__ " " __TIME__);

	CTimer *pTimer = CTimer::Get ();
	if (pTimer != 0)
	{
		pStatus->nUptimeSeconds = pTimer->GetUptime ();

		CString *pTimeString = pTimer->GetTimeString ();
		if (pTimeString != 0)
		{
			CopyString (pStatus->TimeString, sizeof pStatus->TimeString,
				    (const char *) *pTimeString);

			delete pTimeString;
		}
	}

	// network
	if (pNet != 0)
	{
		CopyString (pStatus->Hostname, sizeof pStatus->Hostname,
			    pNet->GetHostname () != 0 ? pNet->GetHostname () : "");

		pStatus->NetRunning = pNet->IsRunning ();

		CNetConfig *pConfig = pNet->GetConfig ();
		if (pConfig != 0)
		{
			CopyIPAddress (pStatus->IPAddress, sizeof pStatus->IPAddress,
				       pConfig->GetIPAddress ());
			CopyIPAddress (pStatus->DefaultGateway, sizeof pStatus->DefaultGateway,
				       pConfig->GetDefaultGateway ());
			CopyIPAddress (pStatus->DNSServer, sizeof pStatus->DNSServer,
				       pConfig->GetDNSServer ());

			const u8 *pNetMask = pConfig->GetNetMask ();
			if (pNetMask != 0)
			{
				CIPAddress NetMask (pNetMask);

				CopyIPAddress (pStatus->NetMask, sizeof pStatus->NetMask, &NetMask);
			}
		}
	}

	pStatus->nNetDeviceCount = CNetDevice::GetNumNetDevices ();

	for (unsigned i = 0; i < SYSINFO_MAX_NETDEVICES; i++)
	{
		CNetDevice *pNetDevice = CNetDevice::GetNetDevice (NetDeviceTypeEthernet, i);
		if (pNetDevice == 0)
		{
			break;
		}

		TNetDeviceStatus *pDeviceStatus = &pStatus->NetDevices[i];

		CString Name;
		Name.Format ("eth%u", i);
		CopyString (pDeviceStatus->Name, sizeof pDeviceStatus->Name,
			    (const char *) Name);

		CString MACString ("unknown");
		const CMACAddress *pMACAddress = pNetDevice->GetMACAddress ();
		if (pMACAddress != 0)
		{
			pMACAddress->Format (&MACString);
		}
		CopyString (pDeviceStatus->MACAddress, sizeof pDeviceStatus->MACAddress,
			    (const char *) MACString);

		pDeviceStatus->LinkUp = pNetDevice->IsLinkUp ();
		CopyString (pDeviceStatus->LinkSpeed, sizeof pDeviceStatus->LinkSpeed,
			    pDeviceStatus->LinkUp
			    ? CNetDevice::GetSpeedString (pNetDevice->GetLinkSpeed ())
			    : "down");

		pStatus->nNetDevicesInStatus++;
	}

	// tasks
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->EnumerateTasks (TaskCallback, pStatus);
	}
}
