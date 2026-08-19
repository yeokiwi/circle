//
// kernel.cpp
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
#include "kernel.h"
#include <circle/netdevice.h>
#include <circle/memory.h>
#include <circle/string.h>
#include <assert.h>

#define STATUS_INTERVAL_MS	10000		// interval of the local status log messages

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	// The interrupt driven serial driver is used, so that writing a debug
	// message does not stall the EtherCAT cycle, until it has been sent.
	m_Serial (&m_Interrupt, FALSE, SERIAL_DEVICE_NUMBER),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_USBHCI (&m_Interrupt, &m_Timer),
	m_Net (0, 0, 0, 0, "zmqclient", NetDeviceTypeEthernet, ZMQ_NET_DEVICE),
	m_bNetAvailable (FALSE),
	m_pEtherCAT (0),
	m_pCoreRunner (0),
	m_pZeroMQ (0)
{
	m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	// the interrupt system is initialized first, because the serial device
	// uses interrupts
	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		// A screen is not required for this application, which is normally
		// used headless. A failure is not fatal here, because the debug
		// messages go to the serial interface.
		m_Screen.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (SERIAL_BAUD_RATE);
	}

	if (bOK)
	{
		// The debug messages are written to the serial interface. The option
		// "logtarget=" in cmdline.txt selects another device instead
		// (e.g. "logtarget=tty1" for the screen).
		const char *pLogTarget = m_Options.GetAppOptionString ("logtarget");

		CDevice *pTarget = 0;
		if (pLogTarget != 0)
		{
			pTarget = m_DeviceNameService.GetDevice (pLogTarget, FALSE);
		}

		boolean bTargetNotFound = pLogTarget != 0 && pTarget == 0;

		if (pTarget == 0)
		{
			pTarget = &m_Serial;
		}

		bOK = m_Logger.Initialize (pTarget);

		if (   bOK
		    && bTargetNotFound)
		{
			m_Logger.Write (FromKernel, LogWarning,
					"Log device \"%s\" not found, using the serial interface",
					pLogTarget);
		}
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		// Initialize the on-board Ethernet device before the USB host
		// controller, so that it becomes net device 0 and the USB Ethernet
		// device becomes net device 1.
		bOK = CNetDevice::InitializeOnBoardDevice ();
	}

	if (bOK)
	{
		bOK = m_USBHCI.Initialize ();
	}

	if (bOK)
	{
		// The TCP/IP stack is only initialized, if its net device is available.
		// The EtherCAT master (on core 1) works without it.
		if (CNetDevice::GetNetDevice (NetDeviceTypeEthernet, ZMQ_NET_DEVICE) != 0)
		{
			// FALSE: do not wait for the network to come up, so that the
			// EtherCAT master can be started, while the link is still down
			m_bNetAvailable = m_Net.Initialize (FALSE);

			bOK = m_bNetAvailable;
		}
		else
		{
			m_Logger.Write (FromKernel, LogWarning,
					"Net device %u not found, the ZeroMQ client is not available",
					ZMQ_NET_DEVICE);
		}
	}

	if (bOK)
	{
		if (CNetDevice::GetNetDevice (NetDeviceTypeEthernet, ETHERCAT_NET_DEVICE) == 0)
		{
			m_Logger.Write (FromKernel, LogError,
					"Net device %u for EtherCAT not found", ETHERCAT_NET_DEVICE);
		}

		m_pEtherCAT = new CEtherCATCore1Master (ETHERCAT_NET_DEVICE, ETHERCAT_CYCLE_TIME_US);
		assert (m_pEtherCAT != 0);

		if (m_bNetAvailable)
		{
			const char *pBrokerHost =
				m_Options.GetAppOptionString ("zmqbroker", ZMQ_BROKER_HOST_DEFAULT);
			unsigned nBrokerPort =
				m_Options.GetAppOptionDecimal ("zmqport", ZMQ_BROKER_PORT_DEFAULT);

			m_pZeroMQ = new CZeroMQClient (&m_Net, m_pEtherCAT, ZMQ_STATUS_INTERVAL_MS);
			assert (m_pZeroMQ != 0);

			m_pZeroMQ->Connect (pBrokerHost, (u16) nBrokerPort);
		}
	}

	if (bOK)
	{
		// Start core 1 last, after all core-0 initialization (USB, net,
		// scheduler) is complete. From this point on, the EtherCAT master
		// runs concurrently on core 1.
		m_pCoreRunner = new CEtherCATCoreRunner (CMemorySystem::Get (), m_pEtherCAT);
		assert (m_pCoreRunner != 0);

		bOK = m_pCoreRunner->Initialize ();

		if (!bOK)
		{
			m_Logger.Write (FromKernel, LogError,
					"Cannot start core 1 for the EtherCAT master "
					"(rebuild with \"./configure --multicore\"?)");
		}
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

#if RASPPI >= 5
	m_Logger.Write (FromKernel, LogNotice,
			"Debug messages go to the UART connector (uart10) at %u baud",
			SERIAL_BAUD_RATE);
#endif

	m_Logger.Write (FromKernel, LogNotice, "%u net device(s) available",
			CNetDevice::GetNumNetDevices ());

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
	assert (m_pEtherCAT != 0);

	TEtherCATStatus *pStatus = new TEtherCATStatus;
	if (pStatus == 0)
	{
		return;
	}

	m_pEtherCAT->GetStatus (pStatus);

	m_Logger.Write (FromKernel, LogNotice,
			"EtherCAT: %s, %u slave(s), WKC %d of %d, %llu cycle(s)",
			CEtherCATCore1Master::GetMasterStateString (pStatus->State),
			pStatus->nSlaveCount, pStatus->nWorkCounter,
			pStatus->nExpectedWorkCounter,
			(unsigned long long) pStatus->nCycles);

	delete pStatus;
}
