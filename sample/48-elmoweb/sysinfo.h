//
// sysinfo.h
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
#ifndef _sysinfo_h
#define _sysinfo_h

#include <circle/net/netsubsystem.h>
#include <circle/types.h>

#define SYSINFO_MAX_TASKS	24
#define SYSINFO_MAX_NETDEVICES	4

/// \brief Status of one task of the Circle scheduler
struct TTaskStatus
{
	char	 Name[32];
	char	 State[16];
	boolean	 Suspended;
};

/// \brief Status of one net device
struct TNetDeviceStatus
{
	char	 Name[8];
	char	 MACAddress[20];
	char	 LinkSpeed[32];
	boolean	 LinkUp;
};

/// \brief Status of the Raspberry Pi (Compute Module 5) and of Circle
struct TSystemStatus
{
	// hardware
	char	 MachineName[64];
	char	 SoCName[32];
	u32	 RevisionRaw;
	unsigned nRAMSizeMBytes;

	// CPU
	unsigned nCPUClockRate;			// Hz (0 if unknown)
	unsigned nMinCPUClockRate;
	unsigned nMaxCPUClockRate;
	unsigned nTemperature;			// degrees Celsius (0 if unknown)
	unsigned nMaxTemperature;
	u32	 ThrottledState;		// see TSystemThrottledState in cputhrottle.h

	// memory
	u64	 nHeapFreeSpace;		// bytes

	// software
	char	 CircleVersion[32];
	char	 CompileTime[32];
	unsigned nUptimeSeconds;
	char	 TimeString[32];		// "" if the time is not known

	// network
	char	 Hostname[64];
	char	 IPAddress[20];
	char	 NetMask[20];
	char	 DefaultGateway[20];
	char	 DNSServer[20];
	boolean	 NetRunning;
	unsigned nNetDeviceCount;
	unsigned nNetDevicesInStatus;
	TNetDeviceStatus NetDevices[SYSINFO_MAX_NETDEVICES];

	// tasks
	unsigned nTaskCount;
	unsigned nTasksInStatus;
	TTaskStatus Tasks[SYSINFO_MAX_TASKS];
};

/// \brief Collect the current system status
/// \param pStatus The status will be written here
/// \param pNet Network subsystem, which serves the web server
void GetSystemStatus (TSystemStatus *pStatus, CNetSubSystem *pNet);

#endif
