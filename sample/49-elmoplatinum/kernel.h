//
// kernel.h
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
#ifndef _kernel_h
#define _kernel_h

#include <circle/actled.h>
#include <circle/koptions.h>
#include <circle/devicenameservice.h>
#include <circle/cputhrottle.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/sched/scheduler.h>
#include <circle/types.h>
#include "elmoplatinum.h"

// The on-board Ethernet controller is used for EtherCAT. This application is
// console-only: no web server, no second net device, no screen is required.
// All debug/status messages go to the serial interface (see below).
#define ETHERCAT_NET_DEVICE	0

// Process data cycle time in microseconds
#define ETHERCAT_CYCLE_TIME_US	4000

// The debug messages are written to this serial device. The Raspberry Pi 5 has
// a dedicated UART connector (the 3-pin JST connector), which is served by
// uart10 and is the serial device 10 in Circle. Other models use the UART on
// GPIO14/15. This can be overwritten with "logtarget=" in cmdline.txt.
#if RASPPI >= 5
#define SERIAL_DEVICE_NUMBER	10
#else
#define SERIAL_DEVICE_NUMBER	0
#endif

#define SERIAL_BAUD_RATE	115200

enum TShutdownMode
{
	ShutdownNone,
	ShutdownHalt,
	ShutdownReboot
};

class CKernel
{
public:
	CKernel (void);
	~CKernel (void);

	boolean Initialize (void);

	TShutdownMode Run (void);

private:
	void ShowStatus (void);

private:
	// do not change this order
	CActLED			m_ActLED;
	CKernelOptions		m_Options;
	CDeviceNameService	m_DeviceNameService;
	CCPUThrottle		m_CPUThrottle;
	CExceptionHandler	m_ExceptionHandler;
	CInterruptSystem	m_Interrupt;
	CSerialDevice		m_Serial;		// debug messages go here
	CTimer			m_Timer;
	CLogger			m_Logger;
	CScheduler		m_Scheduler;

	CElmoPlatinumMaster    *m_pElmoMaster;
};

#endif
