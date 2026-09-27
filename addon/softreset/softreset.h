//
// softreset.h
//
// Software reset (reboot) of the Raspberry Pi, triggered by an USB keyboard,
// a GPIO input (e.g. a push button), the serial console or a command over the
// Ethernet (UDP)
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
#ifndef _softreset_softreset_h
#define _softreset_softreset_h

#include <circle/usb/usbkeyboard.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/gpiopin.h>
#include <circle/serial.h>
#include <circle/device.h>
#include <circle/timer.h>
#include <circle/string.h>
#include <circle/macros.h>
#include <circle/types.h>

#define SOFTRESET_MAX_COMMAND	128		///< Maximum length of a command

/// \brief Software reset of the Raspberry Pi from different trigger sources
/// \details A reset can be triggered by:
/// - Keyboard: pressing Ctrl+Alt+Del on an USB keyboard ("ukbd1", hot-plug supported)
/// - GPIO: holding an input pin (e.g. a push button to GND) active for some time
/// - Serial: receiving a magic string (compatible with REBOOTMAGIC of "make flash")\n
///   or the command "REBOOT [password]" on the serial console
/// - Network: sending the UDP command "REBOOT [password]" to a port
/// - Application: calling RequestReset()
///
/// A trigger only requests the reset (this is safe from any context). The reset
/// itself is performed from Update(), which has to be called continuously from
/// TASK_LEVEL (e.g. from the main loop of the application). Before the system
/// reboots, an optional reset handler gets called, which can be used to bring
/// the application into a safe state (e.g. switch off actuators, flush files).
///
/// \note Only one instance of this class is allowed.
class CSoftReset
{
public:
	enum TSource			///< Source, which triggered the reset
	{
		SourceNone,
		SourceKeyboard,
		SourceGPIO,
		SourceNetwork,
		SourceSerial,
		SourceApplication,
		SourceUnknown
	};

	/// \brief Called at TASK_LEVEL before the system reboots
	/// \param Source Source, which triggered the reset
	/// \param pParam User parameter handed over to RegisterResetHandler()
	typedef void TResetHandler (TSource Source, void *pParam);

	static const u16 DefaultPort = 5050;		///< Default UDP port for network reset
	static const unsigned DefaultHoldMs = 1000;	///< Default hold time of the GPIO input
	static const char DefaultSerialMagic[];		///< Default magic string for serial reset

public:
	CSoftReset (void);
	~CSoftReset (void);

	/// \brief Enable reset by pressing Ctrl+Alt+Del on an USB keyboard
	/// \return Operation successful?
	/// \note The USB keyboard ("ukbd1") is detected in Update(), whenever it is attached.\n
	///	  The application must call CUSBHCIDevice::UpdatePlugAndPlay() in its main loop.
	/// \note This overwrites a shutdown handler, which has been registered by the application.
	boolean EnableKeyboard (void);

	/// \brief Enable reset by a GPIO input (e.g. a push button)
	/// \param nPin GPIO pin number (SoC number, not header position)
	/// \param bActiveLow TRUE, if the input is active on low level (button to GND)
	/// \param nHoldMs The input must be active for this time to trigger a reset (debouncing)
	/// \param bPull Enable the internal pull-up (bActiveLow) or pull-down resistor
	/// \param bImmediate Reboot immediately from the timer interrupt (reset handler is\n
	///	   not called), so that this works even if Update() is not called any more
	/// \return Operation successful?
	/// \note The input must have been inactive once, before it can trigger a reset. This\n
	///	  prevents a reset loop, if the input is stuck in the active state.
	boolean EnableGPIO (unsigned nPin, boolean bActiveLow = TRUE,
			    unsigned nHoldMs = DefaultHoldMs, boolean bPull = TRUE,
			    boolean bImmediate = FALSE);

	/// \brief Enable reset by an UDP command over the network (Ethernet or WLAN)
	/// \param pNetSubSystem Pointer to the network subsystem
	/// \param usPort UDP port number to listen on
	/// \param pPassword Password, which must follow the command (0 for none)
	/// \return Operation successful?
	/// \note Supported commands (case insensitive, answered with an UDP reply):\n
	///	  "REBOOT [password]" - reboot the system (answer "OK")\n
	///	  "PING"              - check, if the system is alive (answer "PONG")
	boolean EnableNetwork (CNetSubSystem *pNetSubSystem, u16 usPort = DefaultPort,
			       const char *pPassword = 0);

	/// \brief Enable reset by receiving a magic string on the serial interface
	/// \param pSerial Pointer to the serial device (must use the interrupt driver,\n
	///	   i.e. CSerialDevice has been constructed with a CInterruptSystem pointer)
	/// \param pMagic Magic string (must remain valid), use the same string for the\n
	///	   variable REBOOTMAGIC in Config.mk to reboot automatically on "make flash"
	/// \return Operation successful?
	/// \note The received data is only scanned, it is still available to the\n
	///	   application with CSerialDevice::Read(). The magic string is detected in\n
	///	   interrupt context, even if Update() is not called.
	boolean EnableSerialMagic (CSerialDevice *pSerial, const char *pMagic = DefaultSerialMagic);

	/// \brief Enable reset by a command, which is typed on the serial console
	/// \param pDevice Pointer to the serial device (or any other character device)
	/// \param pPassword Password, which must follow the command (0 for none)
	/// \param bEcho Echo the received characters (for use with a terminal program)
	/// \param pEchoDevice Additional device (e.g. the screen), on which the typed\n
	///	   characters and the replies are displayed (0 for none)
	/// \return Operation successful?
	/// \note Commands are terminated with CR or LF, backspace is supported:\n
	///	  "REBOOT [password]" - reboot the system (answer "OK")\n
	///	  "PING"              - check, if the system is alive (answer "PONG")
	/// \note The received data is consumed by Update(). Do not read from this\n
	///	  device in the application.
	boolean EnableSerialConsole (CDevice *pDevice, const char *pPassword = 0,
				     boolean bEcho = TRUE, CDevice *pEchoDevice = 0);

	/// \param pHandler Handler to be called before the system reboots
	/// \param pParam User parameter handed over to the handler
	void RegisterResetHandler (TResetHandler *pHandler, void *pParam = 0);

	/// \param bAutoReboot Reboot automatically in Update() (default TRUE)?\n
	///	   If FALSE, Update() returns TRUE on a reset request and the application\n
	///	   is responsible for the reboot (e.g. by calling Reboot()).
	void SetAutoReboot (boolean bAutoReboot);

	/// \brief Request a reset (can be called from any context)
	/// \param Source Source to be reported
	void RequestReset (TSource Source = SourceApplication);

	/// \return Has a reset been requested?
	boolean IsResetRequested (void) const;
	/// \return Source, which requested the reset (SourceNone if not requested)
	TSource GetResetSource (void) const;

	/// \brief Process the trigger sources and perform a requested reset
	/// \return TRUE if a reset has been requested (only with auto reboot disabled)
	/// \note Must be called continuously from TASK_LEVEL.
	boolean Update (void);

	/// \brief Reboot the system now
	static void Reboot (void) NORETURN;

	/// \return Name of the source
	static const char *GetSourceName (TSource Source);

private:
	void UpdateKeyboard (void);
	void UpdateNetwork (void);
	void UpdateSerialConsole (void);

	/// \return Reply to be sent back
	const char *HandleCommand (char *pCommand, const char *pPassword,
				   TSource Source, const char *pFrom);
	void SendReply (const char *pReply, const CIPAddress &rSender, u16 usSenderPort);
	void ConsoleEcho (const char *pString, boolean bToSerial);

	void PerformReset (void) NORETURN;

	static void KeyboardShutdownHandler (void);
	static void KeyboardRemovedHandler (CDevice *pDevice, void *pContext);

	void PollGPIO (void);
	static void GPIOTimerHandler (TKernelTimerHandle hTimer, void *pParam, void *pContext);

	static void SerialMagicHandler (void);

private:
	volatile TSource m_Source;
	boolean m_bAutoReboot;

	TResetHandler *m_pResetHandler;
	void *m_pResetParam;

	// Keyboard
	boolean m_bKeyboardEnabled;
	CUSBKeyboardDevice * volatile m_pKeyboard;
	CDevice::TRegistrationHandle m_hKeyboardRemoved;

	// GPIO
	boolean m_bGPIOEnabled;
	CGPIOPin m_GPIOPin;
	boolean m_bActiveLow;
	boolean m_bImmediate;
	unsigned m_nHoldTicks;
	unsigned m_nActiveTicks;
	boolean m_bGPIOArmed;
	TKernelTimerHandle m_hGPIOTimer;

	// Network
	CNetSubSystem *m_pNetSubSystem;
	CSocket *m_pSocket;
	u16 m_usPort;
	CString m_Password;

	// Serial
	CSerialDevice *m_pSerialMagic;
	CDevice *m_pSerialConsole;
	CString m_SerialPassword;
	boolean m_bSerialEcho;
	CDevice *m_pEchoDevice;
	char m_chSerialLast;
	char m_SerialLine[SOFTRESET_MAX_COMMAND+1];
	unsigned m_nSerialLineLength;
	boolean m_bSerialOverflow;

	static CSoftReset *s_pThis;
};

#endif
