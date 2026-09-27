//
// softreset.cpp
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
#include <softreset/softreset.h>
#include <circle/devicenameservice.h>
#include <circle/sched/scheduler.h>
#include <circle/net/in.h>
#include <circle/startup.h>
#include <circle/logger.h>
#include <circle/netdevice.h>
#include <circle/util.h>
#include <assert.h>

#define GPIO_POLL_TICKS		1		// poll the GPIO input every 10 ms (HZ = 100)

#define MAX_COMMAND_SIZE	128
#define MAX_PACKETS_PER_UPDATE	4

#define REBOOT_DELAY_MS		200		// let the log and network reply go out

LOGMODULE ("softreset");

CSoftReset *CSoftReset::s_pThis = 0;

CSoftReset::CSoftReset (void)
:	m_Source (SourceNone),
	m_bAutoReboot (TRUE),
	m_pResetHandler (0),
	m_pResetParam (0),
	m_bKeyboardEnabled (FALSE),
	m_pKeyboard (0),
	m_hKeyboardRemoved (0),
	m_bGPIOEnabled (FALSE),
	m_bActiveLow (TRUE),
	m_bImmediate (FALSE),
	m_nHoldTicks (1),
	m_nActiveTicks (0),
	m_bGPIOArmed (FALSE),
	m_hGPIOTimer (0),
	m_pNetSubSystem (0),
	m_pSocket (0),
	m_usPort (DefaultPort)
{
	assert (s_pThis == 0);
	s_pThis = this;
}

CSoftReset::~CSoftReset (void)
{
	if (m_bGPIOEnabled)
	{
		m_bGPIOEnabled = FALSE;

		if (m_hGPIOTimer != 0)
		{
			CTimer::Get ()->CancelKernelTimer (m_hGPIOTimer);
			m_hGPIOTimer = 0;
		}
	}

	if (m_pKeyboard != 0)
	{
		m_pKeyboard->UnregisterRemovedHandler (m_hKeyboardRemoved);
		m_pKeyboard = 0;
	}

	delete m_pSocket;
	m_pSocket = 0;

	m_pResetHandler = 0;

	s_pThis = 0;
}

boolean CSoftReset::EnableKeyboard (void)
{
	m_bKeyboardEnabled = TRUE;

	LOGNOTE ("Keyboard reset enabled (Ctrl+Alt+Del)");

	UpdateKeyboard ();

	return TRUE;
}

boolean CSoftReset::EnableGPIO (unsigned nPin, boolean bActiveLow, unsigned nHoldMs,
				boolean bPull, boolean bImmediate)
{
	if (m_bGPIOEnabled)
	{
		LOGERR ("GPIO reset already enabled");

		return FALSE;
	}

	if (nPin >= GPIO_PINS)
	{
		LOGERR ("Invalid GPIO pin (%u)", nPin);

		return FALSE;
	}

	m_bActiveLow = bActiveLow;
	m_bImmediate = bImmediate;

	m_nHoldTicks = MSEC2HZ (nHoldMs) / GPIO_POLL_TICKS;
	if (m_nHoldTicks == 0)
	{
		m_nHoldTicks = 1;
	}

	m_nActiveTicks = 0;
	m_bGPIOArmed = FALSE;

	m_GPIOPin.AssignPin (nPin);
	m_GPIOPin.SetMode (  !bPull     ? GPIOModeInput
			   : bActiveLow ? GPIOModeInputPullUp
					: GPIOModeInputPullDown);

	m_bGPIOEnabled = TRUE;

	m_hGPIOTimer = CTimer::Get ()->StartKernelTimer (GPIO_POLL_TICKS, GPIOTimerHandler, this);

	LOGNOTE ("GPIO reset enabled (GPIO%u active %s for %u ms%s)", nPin,
		 bActiveLow ? "low" : "high", nHoldMs, bImmediate ? ", immediate" : "");

	return TRUE;
}

boolean CSoftReset::EnableNetwork (CNetSubSystem *pNetSubSystem, u16 usPort,
				   const char *pPassword)
{
	if (m_pNetSubSystem != 0)
	{
		LOGERR ("Network reset already enabled");

		return FALSE;
	}

	if (   pNetSubSystem == 0
	    || usPort == 0)
	{
		return FALSE;
	}

	m_usPort = usPort;
	m_Password = pPassword != 0 ? pPassword : "";

	// The socket is created in Update(), when the network is up (e.g. DHCP bound)
	m_pNetSubSystem = pNetSubSystem;

	LOGNOTE ("Network reset enabled (UDP port %u%s)", (unsigned) usPort,
		 m_Password.GetLength () > 0 ? ", password protected" : "");

	UpdateNetwork ();

	return TRUE;
}

void CSoftReset::RegisterResetHandler (TResetHandler *pHandler, void *pParam)
{
	m_pResetParam = pParam;
	m_pResetHandler = pHandler;
}

void CSoftReset::SetAutoReboot (boolean bAutoReboot)
{
	m_bAutoReboot = bAutoReboot;
}

void CSoftReset::RequestReset (TSource Source)
{
	if (Source == SourceNone)
	{
		Source = SourceUnknown;
	}

	// the first request wins
	if (m_Source == SourceNone)
	{
		m_Source = Source;
	}
}

boolean CSoftReset::IsResetRequested (void) const
{
	return m_Source != SourceNone;
}

CSoftReset::TSource CSoftReset::GetResetSource (void) const
{
	return m_Source;
}

boolean CSoftReset::Update (void)
{
	if (m_bKeyboardEnabled)
	{
		UpdateKeyboard ();
	}

	if (m_pNetSubSystem != 0)
	{
		UpdateNetwork ();
	}

	if (m_Source == SourceNone)
	{
		return FALSE;
	}

	if (!m_bAutoReboot)
	{
		return TRUE;
	}

	PerformReset ();
}

void CSoftReset::Reboot (void)
{
	reboot ();
}

const char *CSoftReset::GetSourceName (TSource Source)
{
	switch (Source)
	{
	case SourceNone:	return "none";
	case SourceKeyboard:	return "keyboard";
	case SourceGPIO:	return "GPIO";
	case SourceNetwork:	return "network";
	case SourceApplication:	return "application";
	default:		return "unknown";
	}
}

void CSoftReset::PerformReset (void)
{
	TSource Source = m_Source;

	LOGNOTE ("Reset requested by %s, rebooting", GetSourceName (Source));

	if (m_pResetHandler != 0)
	{
		(*m_pResetHandler) (Source, m_pResetParam);
	}

	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->MsSleep (REBOOT_DELAY_MS);
	}
	else
	{
		CTimer::SimpleMsDelay (REBOOT_DELAY_MS);
	}

	Reboot ();
}

void CSoftReset::UpdateKeyboard (void)
{
	if (m_pKeyboard != 0)
	{
		return;
	}

	CUSBKeyboardDevice *pKeyboard =
		(CUSBKeyboardDevice *) CDeviceNameService::Get ()->GetDevice ("ukbd1", FALSE);
	if (pKeyboard == 0)
	{
		return;
	}

	pKeyboard->RegisterShutdownHandler (KeyboardShutdownHandler);
	m_hKeyboardRemoved = pKeyboard->RegisterRemovedHandler (KeyboardRemovedHandler, this);

	m_pKeyboard = pKeyboard;

	LOGNOTE ("USB keyboard attached, press Ctrl+Alt+Del to reboot");
}

void CSoftReset::KeyboardShutdownHandler (void)
{
	if (s_pThis != 0)
	{
		s_pThis->RequestReset (SourceKeyboard);
	}
}

void CSoftReset::KeyboardRemovedHandler (CDevice *pDevice, void *pContext)
{
	CSoftReset *pThis = (CSoftReset *) pContext;
	assert (pThis != 0);

	pThis->m_pKeyboard = 0;
	pThis->m_hKeyboardRemoved = 0;
}

void CSoftReset::PollGPIO (void)
{
	boolean bActive = m_GPIOPin.Read () == (m_bActiveLow ? LOW : HIGH);

	if (!m_bGPIOArmed)
	{
		// the input must have been inactive once, to prevent a reset loop
		if (!bActive)
		{
			m_bGPIOArmed = TRUE;
		}

		return;
	}

	if (!bActive)
	{
		m_nActiveTicks = 0;

		return;
	}

	if (++m_nActiveTicks < m_nHoldTicks)
	{
		return;
	}

	m_nActiveTicks = 0;

	if (m_bImmediate)
	{
		Reboot ();
	}

	RequestReset (SourceGPIO);
}

void CSoftReset::GPIOTimerHandler (TKernelTimerHandle hTimer, void *pParam, void *pContext)
{
	CSoftReset *pThis = (CSoftReset *) pParam;
	assert (pThis != 0);

	pThis->m_hGPIOTimer = 0;

	if (!pThis->m_bGPIOEnabled)
	{
		return;
	}

	pThis->PollGPIO ();

	pThis->m_hGPIOTimer = CTimer::Get ()->StartKernelTimer (GPIO_POLL_TICKS,
								GPIOTimerHandler, pThis);
}

void CSoftReset::UpdateNetwork (void)
{
	assert (m_pNetSubSystem != 0);

	if (m_pSocket == 0)
	{
		if (!m_pNetSubSystem->IsRunning ())
		{
			return;
		}

		m_pSocket = new CSocket (m_pNetSubSystem, IPPROTO_UDP);
		assert (m_pSocket != 0);

		if (m_pSocket->Bind (m_usPort) < 0)
		{
			LOGERR ("Cannot bind UDP port %u", (unsigned) m_usPort);

			delete m_pSocket;
			m_pSocket = 0;

			m_pNetSubSystem = 0;	// give up

			return;
		}

		CString IPString;
		m_pNetSubSystem->GetConfig ()->GetIPAddress ()->Format (&IPString);
		LOGNOTE ("Listening for reset command on %s:%u",
			 (const char *) IPString, (unsigned) m_usPort);
	}

	for (unsigned i = 0; i < MAX_PACKETS_PER_UPDATE; i++)
	{
		char Buffer[FRAME_BUFFER_SIZE];
		CIPAddress Sender;
		u16 usSenderPort;
		int nResult = m_pSocket->ReceiveFrom (Buffer, sizeof Buffer, MSG_DONTWAIT,
						      &Sender, &usSenderPort);
		if (nResult <= 0)
		{
			break;
		}

		if (nResult > MAX_COMMAND_SIZE)
		{
			SendReply ("ERROR Command too long\n", Sender, usSenderPort);

			continue;
		}

		Buffer[nResult] = '\0';

		HandleCommand (Buffer, Sender, usSenderPort);
	}
}

void CSoftReset::HandleCommand (char *pCommand, const CIPAddress &rSender, u16 usSenderPort)
{
	// strip trailing white space (e.g. newline)
	size_t nLength = strlen (pCommand);
	while (   nLength > 0
	       && (   pCommand[nLength-1] == ' '  || pCommand[nLength-1] == '\t'
		   || pCommand[nLength-1] == '\r' || pCommand[nLength-1] == '\n'))
	{
		pCommand[--nLength] = '\0';
	}

	// split command and argument
	char *pArgument = strchr (pCommand, ' ');
	if (pArgument != 0)
	{
		*pArgument++ = '\0';
		while (*pArgument == ' ')
		{
			pArgument++;
		}
	}
	else
	{
		pArgument = pCommand + nLength;		// empty string
	}

	CString SenderString;
	rSender.Format (&SenderString);

	if (strcasecmp (pCommand, "PING") == 0)
	{
		SendReply ("PONG\n", rSender, usSenderPort);
	}
	else if (strcasecmp (pCommand, "REBOOT") == 0)
	{
		if (strcmp (pArgument, m_Password) != 0)
		{
			LOGWARN ("Reset command from %s rejected (wrong password)",
				 (const char *) SenderString);

			SendReply ("ERROR Access denied\n", rSender, usSenderPort);

			return;
		}

		LOGNOTE ("Reset command from %s", (const char *) SenderString);

		SendReply ("OK\n", rSender, usSenderPort);

		RequestReset (SourceNetwork);
	}
	else
	{
		SendReply ("ERROR Unknown command\n", rSender, usSenderPort);
	}
}

void CSoftReset::SendReply (const char *pReply, const CIPAddress &rSender, u16 usSenderPort)
{
	assert (m_pSocket != 0);
	assert (pReply != 0);

	m_pSocket->SendTo (pReply, strlen (pReply), MSG_DONTWAIT, rSender, usSenderPort);
}
