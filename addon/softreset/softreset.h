//
// softreset.h
//
// Software reset (reboot) of the Raspberry Pi, triggered by an USB keyboard,
// a GPIO input (e.g. a push button) or a command over the Ethernet (UDP)
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
#include <circle/device.h>
#include <circle/timer.h>
#include <circle/string.h>
#include <circle/macros.h>
#include <circle/types.h>

/// \brief Software reset of the Raspberry Pi from different trigger sources
/// \details A reset can be triggered by:
/// - Keyboard: pressing Ctrl+Alt+Del on an USB keyboard ("ukbd1", hot-plug supported)
/// - GPIO: holding an input pin (e.g. a push button to GND) active for some time
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
		SourceApplication,
		SourceUnknown
	};

	/// \brief Called at TASK_LEVEL before the system reboots
	/// \param Source Source, which triggered the reset
	/// \param pParam User parameter handed over to RegisterResetHandler()
	typedef void TResetHandler (TSource Source, void *pParam);

	static const u16 DefaultPort = 5050;		///< Default UDP port for network reset
	static const unsigned DefaultHoldMs = 1000;	///< Default hold time of the GPIO input

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

	void HandleCommand (char *pCommand, const CIPAddress &rSender, u16 usSenderPort);
	void SendReply (const char *pReply, const CIPAddress &rSender, u16 usSenderPort);

	void PerformReset (void) NORETURN;

	static void KeyboardShutdownHandler (void);
	static void KeyboardRemovedHandler (CDevice *pDevice, void *pContext);

	void PollGPIO (void);
	static void GPIOTimerHandler (TKernelTimerHandle hTimer, void *pParam, void *pContext);

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

	static CSoftReset *s_pThis;
};

#endif
