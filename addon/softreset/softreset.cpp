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

#define MAX_COMMAND_SIZE	SOFTRESET_MAX_COMMAND
#define MAX_PACKETS_PER_UPDATE	4

#define REBOOT_DELAY_MS		200		// let the log and network reply go out

LOGMODULE ("softreset");

const char CSoftReset::DefaultSerialMagic[] = "circle-reboot";

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
	m_usPort (DefaultPort),
	m_pSerialMagic (0),
	m_pSerialConsole (0),
	m_bSerialEcho (TRUE),
	m_nSerialLineLength (0),
	m_bSerialOverflow (FALSE)
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

	// the serial magic handler cannot be unregistered, it checks s_pThis
	m_pSerialMagic = 0;
	m_pSerialConsole = 0;

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

boolean CSoftReset::EnableSerialMagic (CSerialDevice *pSerial, const char *pMagic)
{
	if (m_pSerialMagic != 0)
	{
		LOGERR ("Serial magic reset already enabled");

		return FALSE;
	}

	if (   pSerial == 0
	    || pMagic == 0
	    || *pMagic == '\0')
	{
		return FALSE;
	}

	pSerial->RegisterMagicReceivedHandler (pMagic, SerialMagicHandler);

	m_pSerialMagic = pSerial;

	LOGNOTE ("Serial magic reset enabled (\"%s\")", pMagic);

	return TRUE;
}

boolean CSoftReset::EnableSerialConsole (CDevice *pDevice, const char *pPassword,
					 boolean bEcho)
{
	if (m_pSerialConsole != 0)
	{
		LOGERR ("Serial console reset already enabled");

		return FALSE;
	}

	if (pDevice == 0)
	{
		return FALSE;
	}

	m_SerialPassword = pPassword != 0 ? pPassword : "";
	m_bSerialEcho = bEcho;
	m_nSerialLineLength = 0;
	m_bSerialOverflow = FALSE;

	m_pSerialConsole = pDevice;

	LOGNOTE ("Serial console reset enabled (type \"REBOOT%s\")",
		 m_SerialPassword.GetLength () > 0 ? " <password>" : "");

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

	if (m_pSerialConsole != 0)
	{
		UpdateSerialConsole ();
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
	case SourceSerial:	return "serial";
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

		CString SenderString;
		Sender.Format (&SenderString);

		const char *pReply = HandleCommand (Buffer, m_Password, SourceNetwork,
						    SenderString);
		SendReply (pReply, Sender, usSenderPort);
	}
}

const char *CSoftReset::HandleCommand (char *pCommand, const char *pPassword,
					TSource Source, const char *pFrom)
{
	assert (pCommand != 0);
	assert (pPassword != 0);
	assert (pFrom != 0);

	// strip leading and trailing white space (e.g. newline)
	while (*pCommand == ' ' || *pCommand == '\t')
	{
		pCommand++;
	}

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

	if (strcasecmp (pCommand, "PING") == 0)
	{
		return "PONG\n";
	}

	if (strcasecmp (pCommand, "REBOOT") == 0)
	{
		if (strcmp (pArgument, pPassword) != 0)
		{
			LOGWARN ("Reset command from %s rejected (wrong password)", pFrom);

			return "ERROR Access denied\n";
		}

		LOGNOTE ("Reset command from %s", pFrom);

		RequestReset (Source);

		return "OK\n";
	}

	return "ERROR Unknown command\n";
}

void CSoftReset::SendReply (const char *pReply, const CIPAddress &rSender, u16 usSenderPort)
{
	assert (m_pSocket != 0);
	assert (pReply != 0);

	m_pSocket->SendTo (pReply, strlen (pReply), MSG_DONTWAIT, rSender, usSenderPort);
}

void CSoftReset::UpdateSerialConsole (void)
{
	assert (m_pSerialConsole != 0);

	char Buffer[64];
	int nResult;
	while ((nResult = m_pSerialConsole->Read (Buffer, sizeof Buffer)) > 0)
	{
		for (int i = 0; i < nResult; i++)
		{
			char chChar = Buffer[i];

			switch (chChar)
			{
			case '\r':
			case '\n':
				if (m_bSerialEcho)
				{
					SerialWrite ("\r\n");
				}

				if (m_bSerialOverflow)
				{
					SerialWrite ("ERROR Command too long\r\n");
				}
				else if (m_nSerialLineLength > 0)
				{
					m_SerialLine[m_nSerialLineLength] = '\0';

					const char *pReply = HandleCommand (m_SerialLine,
									    m_SerialPassword,
									    SourceSerial,
									    "serial console");
					// terminal needs CR LF
					CString Reply (pReply);
					Reply.Replace ("\n", "\r\n");
					SerialWrite (Reply);
				}

				m_nSerialLineLength = 0;
				m_bSerialOverflow = FALSE;
				break;

			case '\b':
			case '\x7F':
				if (m_nSerialLineLength > 0)
				{
					m_nSerialLineLength--;

					if (m_bSerialEcho)
					{
						SerialWrite ("\b \b");
					}
				}
				break;

			default:
				if (   chChar < ' '
				    || chChar > '~')
				{
					break;		// ignore control characters
				}

				if (m_nSerialLineLength >= MAX_COMMAND_SIZE)
				{
					m_bSerialOverflow = TRUE;

					break;
				}

				m_SerialLine[m_nSerialLineLength++] = chChar;

				if (m_bSerialEcho)
				{
					m_pSerialConsole->Write (&chChar, 1);
				}
				break;
			}
		}
	}
}

void CSoftReset::SerialWrite (const char *pString)
{
	assert (m_pSerialConsole != 0);
	assert (pString != 0);

	m_pSerialConsole->Write (pString, strlen (pString));
}

void CSoftReset::SerialMagicHandler (void)
{
	if (s_pThis != 0)
	{
		s_pThis->RequestReset (SourceSerial);
	}
}
