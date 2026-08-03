//
// gpiocontroller.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2025  R. Stange <rsta2@o2online.de>
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
#ifndef _gpiocontroller_h
#define _gpiocontroller_h

#include <circle/interrupt.h>
#include <circle/gpiopin.h>
#if RASPPI <= 4
	#include <circle/usertimer.h>
	#include <circle/pwmoutput.h>
#endif
#include <circle/types.h>

// Number of GPIO pins on the header which can be controlled by this sample.
// See gpiocontroller.cpp for the actual Broadcom (BCM) pin numbers used.
#define GPIO_CONTROL_PINS	24

// Software PWM frequency limits. The software PWM engine is driven by the
// CUserTimer, which has a 1 us time base (USER_CLOCKHZ). To keep a usable duty
// cycle resolution the output frequency is capped well below that base rate.
#define SOFTPWM_MIN_FREQ	1		// Hz
#define SOFTPWM_MAX_FREQ	100000		// Hz (100 kHz)

// Hardware PWM frequency limits. These are only available on the two hardware
// PWM channels (see s_HWPins), but reach much higher frequencies than the
// software engine.
#define HWPWM_MIN_FREQ		1000		// Hz (1 kHz)
#define HWPWM_MAX_FREQ		20000000	// Hz (20 MHz)

// Number of hardware PWM channels (fixed by the SoC).
#define HWPWM_CHANNELS		2

// Operating mode of a single header pin.
enum TPinMode
{
	PinModeManual,		// static HIGH/LOW output
	PinModeSoftPWM,		// software generated PWM (CUserTimer)
	PinModeHardwarePWM	// driven by the hardware PWM peripheral
};

class CGPIOController	/// Manages the header GPIO pins, software PWM and hardware PWM
{
public:
	/// \param pInterruptSystem Pointer to the interrupt system object (for CUserTimer)
	CGPIOController (CInterruptSystem *pInterruptSystem);

	~CGPIOController (void);

	/// \brief Configure all controllable pins as outputs and start the timer
	/// \return Operation successful?
	boolean Initialize (void);

	//
	// Header pins (generic)
	//

	/// \return Number of controllable pins
	unsigned GetPinCount (void) const;

	/// \param nIndex Pin index (0 .. GetPinCount()-1)
	/// \return Broadcom (BCM) GPIO number of this pin
	unsigned GetPinNumber (unsigned nIndex) const;

	/// \param nPinNumber Broadcom (BCM) GPIO number
	/// \return Matching pin index or -1 if this pin is not controllable
	int GetPinIndex (unsigned nPinNumber) const;

	/// \param nIndex Pin index (0 .. GetPinCount()-1)
	/// \return Current operating mode of the pin
	TPinMode GetPinMode (unsigned nIndex) const;

	/// \param nIndex Pin index (0 .. GetPinCount()-1)
	/// \return TRUE if this pin can be driven by a hardware PWM channel
	boolean IsHardwarePWMPin (unsigned nIndex) const;

	//
	// Manual (static) output control
	//

	/// \param nIndex Pin index (0 .. GetPinCount()-1)
	/// \return Current output level (LOW or HIGH)
	unsigned GetPinLevel (unsigned nIndex) const;

	/// \param nIndex Pin index (0 .. GetPinCount()-1)
	/// \param nLevel New output level (LOW or HIGH)
	/// \note Puts the pin back into manual mode if it was a software PWM pin
	void SetPinLevel (unsigned nIndex, unsigned nLevel);

	//
	// Software PWM (any header pin)
	//

	/// \brief Enable a software generated PWM on the given pin
	/// \param nIndex Pin index (0 .. GetPinCount()-1)
	/// \param nFrequencyHz Requested frequency (clamped to [SOFTPWM_MIN/MAX_FREQ])
	/// \param nDutyPercent Duty cycle 0 .. 100
	/// \param pActualFreqHz Out: frequency actually used (may be clamped), or 0
	/// \return Operation successful?
	boolean StartSoftPWM (unsigned nIndex, unsigned nFrequencyHz, unsigned nDutyPercent,
			      unsigned *pActualFreqHz = 0);

	/// \brief Disable software PWM on the given pin (returns it to manual LOW)
	void StopSoftPWM (unsigned nIndex);

	/// \param nIndex Pin index (0 .. GetPinCount()-1)
	/// \return Requested frequency in Hz of the software PWM, or 0 if not active
	unsigned GetSoftPWMFrequency (unsigned nIndex) const;

	/// \param nIndex Pin index (0 .. GetPinCount()-1)
	/// \return Duty cycle 0..100 of the software PWM (only valid if active)
	unsigned GetSoftPWMDuty (unsigned nIndex) const;

	//
	// Hardware PWM (two channels on fixed pins)
	//

	/// \param nChannel Channel index (0 .. HWPWM_CHANNELS-1)
	/// \return Number of pins which can be assigned to this channel
	unsigned GetHWPinCount (unsigned nChannel) const;

	/// \param nChannel Channel index (0 .. HWPWM_CHANNELS-1)
	/// \param nPinSel Selection index (0 .. GetHWPinCount()-1)
	/// \return Broadcom (BCM) GPIO number usable for this channel
	unsigned GetHWPinNumber (unsigned nChannel, unsigned nPinSel) const;

	/// \brief (Re)configure the hardware PWM. Both channels share the frequency.
	/// \param nFrequencyHz Requested frequency (clamped to [HWPWM_MIN/MAX_FREQ])
	/// \param bEnable      Per-channel enable flag
	/// \param nPinNumber   Per-channel BCM pin (must be valid for the channel)
	/// \param nDutyPercent Per-channel duty cycle 0 .. 100
	/// \param pActualFreqHz Out: frequency actually generated, or 0
	/// \return Operation successful?
	boolean SetHardwarePWM (unsigned nFrequencyHz,
				const boolean *bEnable,
				const unsigned *nPinNumber,
				const unsigned *nDutyPercent,
				unsigned *pActualFreqHz = 0);

	/// \brief Disable the hardware PWM completely
	void StopHardwarePWM (void);

	/// \return TRUE if at least one hardware PWM channel is currently enabled
	boolean IsHardwarePWMActive (void) const;

	/// \return Configured hardware PWM frequency in Hz (0 if disabled)
	unsigned GetHWPWMFrequency (void) const;

	/// \return Actually generated hardware PWM frequency in Hz (0 if disabled)
	unsigned GetHWPWMActualFrequency (void) const;

	/// \param nChannel Channel index (0 .. HWPWM_CHANNELS-1)
	boolean  GetHWChannelEnabled (unsigned nChannel) const;
	unsigned GetHWChannelPin (unsigned nChannel) const;
	unsigned GetHWChannelDuty (unsigned nChannel) const;

private:
#if RASPPI <= 4
	static void TimerHandler (CUserTimer *pUserTimer, void *pParam);

	// wake the software PWM timer so a configuration change takes effect soon
	void KickSoftTimer (void);
#endif

#if RASPPI <= 4
	// find a clock source and integer divider for a hardware PWM frequency
	boolean ComputeHWClock (unsigned nFrequencyHz, unsigned *pRange,
				TGPIOClockSource *pSource, unsigned *pDivider,
				unsigned *pActualFreqHz);
#endif

private:
#if RASPPI <= 4
	CUserTimer m_UserTimer;
#endif

	CGPIOPin m_Pin[GPIO_CONTROL_PINS];
	TPinMode m_Mode[GPIO_CONTROL_PINS];		// current mode per pin
	unsigned m_nLevel[GPIO_CONTROL_PINS];		// manual output level per pin

	// Software PWM state (the fields below are read by the timer handler)
	struct TSoftPWM
	{
		volatile boolean  bActive;		// software PWM enabled?
		volatile boolean  bStatic;		// duty 0% or 100% (no toggling)
		volatile unsigned nFrequencyHz;		// requested frequency
		volatile unsigned nDutyPercent;		// duty cycle 0..100
		volatile unsigned nHighUs;		// HIGH time in microseconds
		volatile unsigned nLowUs;		// LOW time in microseconds
		volatile unsigned nLevel;		// current output level
		volatile u64	  nNextEdgeUs;		// virtual time of next toggle
	};
	TSoftPWM m_Soft[GPIO_CONTROL_PINS];

	volatile u64	   m_nNowUs;			// virtual time (advanced by handler)
	volatile unsigned  m_nLastDelayUs;		// last programmed timer delay
	volatile unsigned  m_nSoftActiveCount;		// number of toggling soft pins

	// Hardware PWM state
#if RASPPI <= 4
	CPWMOutput *m_pPWM;
#endif
	unsigned m_nHWFrequency;			// requested (0 if disabled)
	unsigned m_nHWActualFrequency;			// generated (0 if disabled)
	unsigned m_nHWRange;				// PWM range for duty conversion
	boolean  m_bHWEnable[HWPWM_CHANNELS];
	unsigned m_nHWPin[HWPWM_CHANNELS];
	unsigned m_nHWDuty[HWPWM_CHANNELS];
};

#endif
