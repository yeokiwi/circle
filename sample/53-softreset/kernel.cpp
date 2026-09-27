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
#include <circle/string.h>

// Reset by GPIO: push button between this GPIO and GND (header pin 11)
#define RESET_GPIO_PIN		17
#define RESET_HOLD_MS		2000		// hold the button for 2 seconds

// Reset by network: send "REBOOT <password>" to this UDP port (see softreset.py)
#define RESET_UDP_PORT		CSoftReset::DefaultPort
#define RESET_PASSWORD		"circle"	// set to 0 to disable the password

// Network configuration
#define USE_DHCP

#ifndef USE_DHCP
static const u8 IPAddress[]      = {192, 168, 0, 250};
static const u8 NetMask[]        = {255, 255, 255, 0};
static const u8 DefaultGateway[] = {192, 168, 0, 1};
static const u8 DNSServer[]      = {192, 168, 0, 1};
#endif

LOGMODULE ("kernel");

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_USBHCI (&m_Interrupt, &m_Timer, TRUE)		// TRUE: enable plug-and-play
#ifndef USE_DHCP
	, m_Net (IPAddress, NetMask, DefaultGateway, DNSServer)
#endif
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
		bOK = m_USBHCI.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Net.Initialize (FALSE);		// FALSE: do not wait for DHCP
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	LOGNOTE ("Compile time: " __DATE__ " " __TIME__);

	// Enable all reset sources
	m_SoftReset.RegisterResetHandler (ResetHandler, this);

	m_SoftReset.EnableKeyboard ();
	m_SoftReset.EnableGPIO (RESET_GPIO_PIN, TRUE, RESET_HOLD_MS);
	m_SoftReset.EnableNetwork (&m_Net, RESET_UDP_PORT, RESET_PASSWORD);

	LOGNOTE ("The system can be rebooted by:");
	LOGNOTE ("- pressing Ctrl+Alt+Del on an USB keyboard");
	LOGNOTE ("- holding a button on GPIO%u (to GND) for %u ms",
		 RESET_GPIO_PIN, RESET_HOLD_MS);
	LOGNOTE ("- sending \"REBOOT%s%s\" to UDP port %u (e.g. with softreset.py)",
		 RESET_PASSWORD != 0 ? " " : "", RESET_PASSWORD != 0 ? RESET_PASSWORD : "",
		 (unsigned) RESET_UDP_PORT);

	unsigned nStartTicks = m_Timer.GetClockTicks ();
	unsigned nLastSeconds = 0;

	for (unsigned nCount = 0; TRUE; nCount++)
	{
		// This must be called from TASK_LEVEL to update the tree of connected USB devices.
		m_USBHCI.UpdatePlugAndPlay ();

		// Process the reset sources, does not return if a reset has been requested
		m_SoftReset.Update ();

		// Simulated application work: show that we are alive
		unsigned nSeconds = (m_Timer.GetClockTicks () - nStartTicks) / CLOCKHZ;
		if (nSeconds != nLastSeconds)
		{
			nLastSeconds = nSeconds;

			if (nSeconds % 10 == 0)
			{
				LOGNOTE ("Running for %u seconds", nSeconds);
			}
		}

		m_Screen.Rotor (0, nCount);

		m_Scheduler.Yield ();
	}

	return ShutdownReboot;
}

void CKernel::ResetHandler (CSoftReset::TSource Source, void *pParam)
{
	CKernel *pThis = (CKernel *) pParam;

	// Bring the application into a safe state here (e.g. switch off outputs,
	// close files). This sample just switches off the ACT LED.
	pThis->m_ActLED.Off ();

	LOGNOTE ("Application prepared for reset (source: %s)",
		 CSoftReset::GetSourceName (Source));
}
