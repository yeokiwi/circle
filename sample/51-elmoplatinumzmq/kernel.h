//
// kernel.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
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
#include <circle/screen.h>
#include <circle/serial.h>
#include <circle/exceptionhandler.h>
#include <circle/interrupt.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/cputhrottle.h>
#include <circle/usb/usbhcidevice.h>
#include <circle/sched/scheduler.h>
#include <circle/net/netsubsystem.h>
#include <circle/multicore.h>
#include <circle/types.h>
#include "elmo_core1.h"
#include "zmqclient.h"

#ifndef ARM_ALLOW_MULTI_CORE
#error This sample requires multi-core support: run "./configure --multicore" (see README) and rebuild.
#endif

// The on-board Ethernet controller is used for the Elmo Platinum drive,
// exclusively on core 1 (raw SOEM cyclic loop, no CScheduler). The second
// Ethernet port (normally a USB Ethernet device, "eth1") runs the TCP/IP
// stack with the ZeroMQ client, on core 0 (CScheduler-based, like all
// networking in Circle). Swap these values, if you want it the other way
// around.
#define ELMO_NET_DEVICE		0
#define ZMQ_NET_DEVICE		1

// Process data cycle time in microseconds
#define ELMO_CYCLE_TIME_US	4000

// Default ZeroMQ broker address, overridable with "zmqbroker="/"zmqport=" in
// cmdline.txt
#define ZMQ_BROKER_HOST_DEFAULT	"192.168.1.10"
#define ZMQ_BROKER_PORT_DEFAULT	5555

// interval of the status messages sent to the ZeroMQ broker
#define ZMQ_STATUS_INTERVAL_MS	500

// The debug messages are written to this serial device. The Raspberry Pi 5 has
// a dedicated UART connector (the 3-pin JST connector), which is served by
// uart10 and is the serial device 10 in Circle. Other models use the UART on
// GPIO14/15. This can be overwritten with "logdev=" in cmdline.txt.
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

/// \brief Runs the Elmo Platinum drive master on core 1. Cores 2/3 are unused.
class CElmoCoreRunner : public CMultiCoreSupport
{
public:
	CElmoCoreRunner (CMemorySystem *pMemorySystem, CElmoPlatinumCore1Master *pMaster)
	:	CMultiCoreSupport (pMemorySystem),
		m_pMaster (pMaster)
	{
	}

	void Run (unsigned nCore) override
	{
		if (nCore == 1)
		{
			m_pMaster->Run ();	// never returns
		}
	}

private:
	CElmoPlatinumCore1Master *m_pMaster;
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
	CScreenDevice		m_Screen;
	CSerialDevice		m_Serial;		// debug messages go here
	CTimer			m_Timer;
	CLogger			m_Logger;
	CUSBHCIDevice		m_USBHCI;
	CScheduler		m_Scheduler;

	// the TCP/IP stack for the ZeroMQ client, bound to ZMQ_NET_DEVICE
	CNetSubSystem		m_Net;

	boolean			m_bNetAvailable;

	CElmoPlatinumCore1Master *m_pElmo;
	CElmoCoreRunner		 *m_pCoreRunner;	// starts core 1, see above
	CZeroMQClient		 *m_pZeroMQ;
};

#endif
