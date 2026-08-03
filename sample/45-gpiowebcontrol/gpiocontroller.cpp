//
// gpiocontroller.cpp
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
#include "gpiocontroller.h"
#if RASPPI <= 4
	#include <circle/machineinfo.h>
#endif
#include <assert.h>

// The GPIO pins (Broadcom / BCM numbering) exposed on the 40-pin header.
// GPIO14/15 are deliberately left out because they are used by the serial
// port for the system log. GPIO0/1 are reserved (ID EEPROM) and not listed.
static const unsigned s_PinNumbers[GPIO_CONTROL_PINS] =
{
	 2,  3,  4, 17, 27, 22, 10,  9,
	11,  5,  6, 13, 19, 26, 18, 23,
	24, 25, 12, 16, 20, 21,  7,  8
};

// Pins usable for the two hardware PWM channels (BCM numbering).
//   Channel 0 (PWM0): GPIO12 (Alt0) or GPIO18 (Alt5)
//   Channel 1 (PWM1): GPIO13 (Alt0) or GPIO19 (Alt5)
static const unsigned s_HWPins[HWPWM_CHANNELS][2] =
{
	{ 12, 18 },
	{ 13, 19 }
};

// Use the FIQ for the software PWM timer to minimize jitter.
#define WAVE_USE_FIQ		TRUE

#if RASPPI <= 4

// Upper bound for the hardware PWM clock. Reaching 20 MHz output with any duty
// cycle resolution requires a fast PWM clock; 100 MHz is aggressive but keeps a
// couple of duty steps available at the very top of the frequency range.
#define HWPWM_MAX_CLOCK		100000000U

// Maximum PWM range (duty cycle resolution) used at low frequencies.
#define HWPWM_MAX_RANGE		4096U

// Alternate function used to route a hardware PWM channel to a header pin.
static TGPIOMode HWPinAltMode (unsigned nPinNumber)
{
	switch (nPinNumber)
	{
	case 12:	return GPIOModeAlternateFunction0;
	case 13:	return GPIOModeAlternateFunction0;
	case 18:	return GPIOModeAlternateFunction5;
	case 19:	return GPIOModeAlternateFunction5;
	default:	return GPIOModeOutput;
	}
}

// Map a channel index to the Circle PWM channel constant.
static unsigned HWChannelConstant (unsigned nChannel)
{
	return nChannel == 0 ? PWM_CHANNEL1 : PWM_CHANNEL2;
}

#endif

CGPIOController::CGPIOController (CInterruptSystem *pInterruptSystem)
:
#if RASPPI <= 4
	m_UserTimer (pInterruptSystem, TimerHandler, this, WAVE_USE_FIQ),
#endif
	m_nNowUs (0),
	m_nLastDelayUs (0),
	m_nSoftActiveCount (0),
#if RASPPI <= 4
	m_pPWM (0),
#endif
	m_nHWFrequency (0),
	m_nHWActualFrequency (0),
	m_nHWRange (0)
{
#if RASPPI > 4
	// pInterruptSystem is only used to drive the software PWM CUserTimer,
	// which is not available on the Raspberry Pi 5 (RP1).
	(void) pInterruptSystem;
#endif

	for (unsigned i = 0; i < GPIO_CONTROL_PINS; i++)
	{
		m_Mode[i]   = PinModeManual;
		m_nLevel[i] = LOW;

		m_Soft[i].bActive      = FALSE;
		m_Soft[i].bStatic      = FALSE;
		m_Soft[i].nFrequencyHz = 0;
		m_Soft[i].nDutyPercent = 0;
		m_Soft[i].nHighUs      = 0;
		m_Soft[i].nLowUs       = 0;
		m_Soft[i].nLevel       = LOW;
		m_Soft[i].nNextEdgeUs  = 0;
	}

	for (unsigned c = 0; c < HWPWM_CHANNELS; c++)
	{
		m_bHWEnable[c] = FALSE;
		m_nHWPin[c]    = 0;
		m_nHWDuty[c]   = 0;
	}
}

CGPIOController::~CGPIOController (void)
{
	StopHardwarePWM ();

#if RASPPI <= 4
	m_UserTimer.Stop ();
#endif
}

boolean CGPIOController::Initialize (void)
{
	for (unsigned i = 0; i < GPIO_CONTROL_PINS; i++)
	{
		m_Pin[i].AssignPin (s_PinNumbers[i]);
		m_Pin[i].SetMode (GPIOModeOutput);
		m_Pin[i].Write (LOW);

		m_Mode[i]   = PinModeManual;
		m_nLevel[i] = LOW;
	}

#if RASPPI <= 4
	// The user timer is now armed with a long (idle) delay. As soon as a
	// software PWM is requested, the handler shortens the interval.
	return m_UserTimer.Initialize ();
#else
	// No software PWM timer on the Raspberry Pi 5 (RP1); nothing to arm.
	return TRUE;
#endif
}

//
// Header pins (generic)
//

unsigned CGPIOController::GetPinCount (void) const
{
	return GPIO_CONTROL_PINS;
}

unsigned CGPIOController::GetPinNumber (unsigned nIndex) const
{
	assert (nIndex < GPIO_CONTROL_PINS);
	return s_PinNumbers[nIndex];
}

int CGPIOController::GetPinIndex (unsigned nPinNumber) const
{
	for (unsigned i = 0; i < GPIO_CONTROL_PINS; i++)
	{
		if (s_PinNumbers[i] == nPinNumber)
		{
			return (int) i;
		}
	}

	return -1;
}

TPinMode CGPIOController::GetPinMode (unsigned nIndex) const
{
	assert (nIndex < GPIO_CONTROL_PINS);
	return m_Mode[nIndex];
}

boolean CGPIOController::IsHardwarePWMPin (unsigned nIndex) const
{
	assert (nIndex < GPIO_CONTROL_PINS);

	unsigned nPin = s_PinNumbers[nIndex];
	for (unsigned c = 0; c < HWPWM_CHANNELS; c++)
	{
		for (unsigned s = 0; s < 2; s++)
		{
			if (s_HWPins[c][s] == nPin)
			{
				return TRUE;
			}
		}
	}

	return FALSE;
}

//
// Manual (static) output control
//

unsigned CGPIOController::GetPinLevel (unsigned nIndex) const
{
	assert (nIndex < GPIO_CONTROL_PINS);
	return m_nLevel[nIndex];
}

void CGPIOController::SetPinLevel (unsigned nIndex, unsigned nLevel)
{
	assert (nIndex < GPIO_CONTROL_PINS);

	// A pin driven by the hardware PWM is not touched manually.
	if (m_Mode[nIndex] == PinModeHardwarePWM)
	{
		return;
	}

	// Leaving software PWM (if any) returns the pin to manual mode.
	if (m_Mode[nIndex] == PinModeSoftPWM)
	{
		StopSoftPWM (nIndex);
	}

	m_nLevel[nIndex] = nLevel ? HIGH : LOW;
	m_Pin[nIndex].Write (m_nLevel[nIndex]);
}

//
// Software PWM
//

boolean CGPIOController::StartSoftPWM (unsigned nIndex, unsigned nFrequencyHz,
				       unsigned nDutyPercent, unsigned *pActualFreqHz)
{
	assert (nIndex < GPIO_CONTROL_PINS);

	if (pActualFreqHz != 0)
	{
		*pActualFreqHz = 0;
	}

#if RASPPI <= 4
	// A pin currently used by the hardware PWM cannot be used for software PWM.
	if (m_Mode[nIndex] == PinModeHardwarePWM)
	{
		return FALSE;
	}

	// Clamp the requested frequency to the supported software PWM range.
	if (nFrequencyHz < SOFTPWM_MIN_FREQ)
	{
		nFrequencyHz = SOFTPWM_MIN_FREQ;
	}
	else if (nFrequencyHz > SOFTPWM_MAX_FREQ)
	{
		nFrequencyHz = SOFTPWM_MAX_FREQ;
	}

	if (nDutyPercent > 100)
	{
		nDutyPercent = 100;
	}

	// One period in microseconds (USER_CLOCKHZ is the 1 MHz timer base).
	unsigned nPeriodUs = USER_CLOCKHZ / nFrequencyHz;
	if (nPeriodUs < 2)
	{
		nPeriodUs = 2;
	}

	unsigned nHighUs = (unsigned) (((u64) nPeriodUs * nDutyPercent) / 100);
	unsigned nLowUs  = nPeriodUs - nHighUs;

	// Disable the pin in the handler while its parameters are updated.
	m_Soft[nIndex].bActive = FALSE;

	m_Soft[nIndex].nFrequencyHz = nFrequencyHz;
	m_Soft[nIndex].nDutyPercent = nDutyPercent;
	m_Soft[nIndex].nHighUs      = nHighUs;
	m_Soft[nIndex].nLowUs       = nLowUs;

	if (nDutyPercent == 0 || nHighUs == 0)
	{
		// Constant LOW output.
		m_Soft[nIndex].bStatic = TRUE;
		m_Soft[nIndex].nLevel  = LOW;
		m_Pin[nIndex].Write (LOW);
	}
	else if (nDutyPercent >= 100 || nLowUs == 0)
	{
		// Constant HIGH output.
		m_Soft[nIndex].bStatic = TRUE;
		m_Soft[nIndex].nLevel  = HIGH;
		m_Pin[nIndex].Write (HIGH);
	}
	else
	{
		// Start in the HIGH phase; the handler drives the toggling.
		m_Soft[nIndex].bStatic     = FALSE;
		m_Soft[nIndex].nLevel      = HIGH;
		m_Soft[nIndex].nNextEdgeUs = m_nNowUs + nHighUs;
		m_Pin[nIndex].Write (HIGH);
	}

	m_Mode[nIndex]         = PinModeSoftPWM;
	m_Soft[nIndex].bActive = TRUE;

	// Recount the toggling pins. If the timer was idle before, wake it up so
	// the new pin starts toggling without waiting for the idle delay.
	unsigned nOldCount = m_nSoftActiveCount;

	unsigned nCount = 0;
	for (unsigned i = 0; i < GPIO_CONTROL_PINS; i++)
	{
		if (m_Soft[i].bActive && !m_Soft[i].bStatic)
		{
			nCount++;
		}
	}
	m_nSoftActiveCount = nCount;

	if (nOldCount == 0 && nCount > 0)
	{
		KickSoftTimer ();
	}

	if (pActualFreqHz != 0)
	{
		*pActualFreqHz = nFrequencyHz;
	}

	return TRUE;
#else
	// Software PWM relies on CUserTimer, which is not available on the
	// Raspberry Pi 5 (RP1). Report failure so the web UI shows an error.
	(void) nFrequencyHz;
	(void) nDutyPercent;

	return FALSE;
#endif
}

void CGPIOController::StopSoftPWM (unsigned nIndex)
{
	assert (nIndex < GPIO_CONTROL_PINS);

	if (m_Mode[nIndex] != PinModeSoftPWM)
	{
		return;
	}

	m_Soft[nIndex].bActive = FALSE;
	m_Soft[nIndex].bStatic = FALSE;

	m_Mode[nIndex]   = PinModeManual;
	m_nLevel[nIndex] = LOW;
	m_Pin[nIndex].Write (LOW);

	unsigned nCount = 0;
	for (unsigned i = 0; i < GPIO_CONTROL_PINS; i++)
	{
		if (m_Soft[i].bActive && !m_Soft[i].bStatic)
		{
			nCount++;
		}
	}
	m_nSoftActiveCount = nCount;
}

unsigned CGPIOController::GetSoftPWMFrequency (unsigned nIndex) const
{
	assert (nIndex < GPIO_CONTROL_PINS);
	return m_Mode[nIndex] == PinModeSoftPWM ? m_Soft[nIndex].nFrequencyHz : 0;
}

unsigned CGPIOController::GetSoftPWMDuty (unsigned nIndex) const
{
	assert (nIndex < GPIO_CONTROL_PINS);
	return m_Soft[nIndex].nDutyPercent;
}

#if RASPPI <= 4

void CGPIOController::KickSoftTimer (void)
{
	// Wake the timer so a newly configured pin toggles without waiting for the
	// current (idle) delay to expire. Setting m_nLastDelayUs keeps the virtual
	// time consistent: the handler adds this value on its next invocation.
	m_nLastDelayUs = 2;
	m_UserTimer.Start (2);
}

#endif

//
// Hardware PWM
//

unsigned CGPIOController::GetHWPinCount (unsigned nChannel) const
{
	assert (nChannel < HWPWM_CHANNELS);
	return 2;
}

unsigned CGPIOController::GetHWPinNumber (unsigned nChannel, unsigned nPinSel) const
{
	assert (nChannel < HWPWM_CHANNELS);
	assert (nPinSel < 2);
	return s_HWPins[nChannel][nPinSel];
}

boolean CGPIOController::SetHardwarePWM (unsigned nFrequencyHz,
					 const boolean *bEnable,
					 const unsigned *nPinNumber,
					 const unsigned *nDutyPercent,
					 unsigned *pActualFreqHz)
{
	assert (bEnable != 0);
	assert (nPinNumber != 0);
	assert (nDutyPercent != 0);

	if (pActualFreqHz != 0)
	{
		*pActualFreqHz = 0;
	}

#if RASPPI <= 4
	// Clamp the requested frequency to the supported hardware PWM range.
	if (nFrequencyHz < HWPWM_MIN_FREQ)
	{
		nFrequencyHz = HWPWM_MIN_FREQ;
	}
	else if (nFrequencyHz > HWPWM_MAX_FREQ)
	{
		nFrequencyHz = HWPWM_MAX_FREQ;
	}

	// Validate the per-channel pin selection and collect the enabled channels.
	boolean  bEn[HWPWM_CHANNELS];
	unsigned nPin[HWPWM_CHANNELS];
	unsigned nDuty[HWPWM_CHANNELS];
	unsigned nEnabled = 0;

	for (unsigned c = 0; c < HWPWM_CHANNELS; c++)
	{
		bEn[c]   = FALSE;
		nPin[c]  = 0;
		nDuty[c] = nDutyPercent[c] > 100 ? 100 : nDutyPercent[c];

		if (bEnable[c])
		{
			if (   nPinNumber[c] != s_HWPins[c][0]
			    && nPinNumber[c] != s_HWPins[c][1])
			{
				return FALSE;		// invalid pin for this channel
			}

			bEn[c]  = TRUE;
			nPin[c] = nPinNumber[c];
			nEnabled++;
		}
	}

	// Tear down whatever hardware PWM was running before.
	StopHardwarePWM ();

	if (nEnabled == 0)
	{
		return TRUE;			// nothing to enable, all off
	}

	// Work out a clock source, divider and range for the requested frequency.
	unsigned nRange;
	TGPIOClockSource Source;
	unsigned nDivider;
	unsigned nActualFreq;
	if (!ComputeHWClock (nFrequencyHz, &nRange, &Source, &nDivider, &nActualFreq))
	{
		return FALSE;
	}

	m_pPWM = new CPWMOutput (Source, nDivider, nRange, TRUE /* M/S mode */);
	if (m_pPWM == 0)
	{
		return FALSE;
	}

	// Route the enabled channels to their header pins (must be done before
	// starting the peripheral so the first edges appear on the pin).
	for (unsigned c = 0; c < HWPWM_CHANNELS; c++)
	{
		if (!bEn[c])
		{
			continue;
		}

		int nIdx = GetPinIndex (nPin[c]);
		assert (nIdx >= 0);

		// Release a possible software PWM on that pin first.
		if (m_Mode[nIdx] == PinModeSoftPWM)
		{
			StopSoftPWM ((unsigned) nIdx);
		}

		m_Pin[nIdx].SetMode (HWPinAltMode (nPin[c]));
		m_Mode[nIdx] = PinModeHardwarePWM;
	}

	if (!m_pPWM->Start ())
	{
		delete m_pPWM;
		m_pPWM = 0;

		// Revert the pins we just switched to alternate function.
		for (unsigned c = 0; c < HWPWM_CHANNELS; c++)
		{
			if (bEn[c])
			{
				int nIdx = GetPinIndex (nPin[c]);
				if (nIdx >= 0)
				{
					m_Pin[nIdx].SetMode (GPIOModeOutput);
					m_Pin[nIdx].Write (LOW);
					m_Mode[nIdx]   = PinModeManual;
					m_nLevel[nIdx] = LOW;
				}
			}
		}

		return FALSE;
	}

	// Program the duty cycle of each channel (disabled channels stay LOW).
	for (unsigned c = 0; c < HWPWM_CHANNELS; c++)
	{
		unsigned nValue = 0;
		if (bEn[c])
		{
			nValue = (unsigned) (((u64) nRange * nDuty[c] + 50) / 100);
			if (nValue > nRange)
			{
				nValue = nRange;
			}
		}

		m_pPWM->Write (HWChannelConstant (c), nValue);
	}

	// Store the resulting state.
	m_nHWFrequency       = nFrequencyHz;
	m_nHWActualFrequency = nActualFreq;
	m_nHWRange           = nRange;
	for (unsigned c = 0; c < HWPWM_CHANNELS; c++)
	{
		m_bHWEnable[c] = bEn[c];
		m_nHWPin[c]    = nPin[c];
		m_nHWDuty[c]   = nDuty[c];
	}

	if (pActualFreqHz != 0)
	{
		*pActualFreqHz = nActualFreq;
	}

	return TRUE;
#else
	// The hardware PWM peripheral is not supported by this sample on the
	// Raspberry Pi 5 (RP1). Only software PWM is available there.
	(void) nFrequencyHz;
	(void) bEnable;
	(void) nPinNumber;
	(void) nDutyPercent;

	return FALSE;
#endif
}

void CGPIOController::StopHardwarePWM (void)
{
#if RASPPI <= 4
	if (m_pPWM != 0)
	{
		m_pPWM->Stop ();

		delete m_pPWM;
		m_pPWM = 0;
	}
#endif

	// Return the previously used pins to a safe manual LOW output.
	for (unsigned c = 0; c < HWPWM_CHANNELS; c++)
	{
		if (m_bHWEnable[c])
		{
			int nIdx = GetPinIndex (m_nHWPin[c]);
			if (nIdx >= 0)
			{
				m_Pin[nIdx].SetMode (GPIOModeOutput);
				m_Pin[nIdx].Write (LOW);
				m_Mode[nIdx]   = PinModeManual;
				m_nLevel[nIdx] = LOW;
			}
		}

		m_bHWEnable[c] = FALSE;
		m_nHWPin[c]    = 0;
		m_nHWDuty[c]   = 0;
	}

	m_nHWFrequency       = 0;
	m_nHWActualFrequency = 0;
	m_nHWRange           = 0;
}

boolean CGPIOController::IsHardwarePWMActive (void) const
{
	for (unsigned c = 0; c < HWPWM_CHANNELS; c++)
	{
		if (m_bHWEnable[c])
		{
			return TRUE;
		}
	}

	return FALSE;
}

unsigned CGPIOController::GetHWPWMFrequency (void) const
{
	return m_nHWFrequency;
}

unsigned CGPIOController::GetHWPWMActualFrequency (void) const
{
	return m_nHWActualFrequency;
}

boolean CGPIOController::GetHWChannelEnabled (unsigned nChannel) const
{
	assert (nChannel < HWPWM_CHANNELS);
	return m_bHWEnable[nChannel];
}

unsigned CGPIOController::GetHWChannelPin (unsigned nChannel) const
{
	assert (nChannel < HWPWM_CHANNELS);
	return m_nHWPin[nChannel];
}

unsigned CGPIOController::GetHWChannelDuty (unsigned nChannel) const
{
	assert (nChannel < HWPWM_CHANNELS);
	return m_nHWDuty[nChannel];
}

#if RASPPI <= 4

boolean CGPIOController::ComputeHWClock (unsigned nFrequencyHz, unsigned *pRange,
					 TGPIOClockSource *pSource, unsigned *pDivider,
					 unsigned *pActualFreqHz)
{
	assert (nFrequencyHz > 0);

	// Choose a PWM range (= duty cycle resolution). Use a large range at low
	// frequencies for fine resolution and reduce it towards the top of the
	// range where the PWM clock would otherwise become unrealistically fast.
	unsigned nRange = HWPWM_MAX_CLOCK / nFrequencyHz;
	if (nRange > HWPWM_MAX_RANGE)
	{
		nRange = HWPWM_MAX_RANGE;
	}
	if (nRange < 2)
	{
		nRange = 2;
	}

	double fTargetClock = (double) nFrequencyHz * nRange;

	// Find the clock source and integer divider which come closest.
	double		 fBestDiff   = fTargetClock;	// worst case: divide down to ~0
	TGPIOClockSource BestSource  = GPIOClockSourceUnknown;
	unsigned	 nBestDivider = 0;
	double		 fBestClock  = 0.0;

	for (unsigned nSourceId = 0; nSourceId <= GPIO_CLOCK_SOURCE_ID_MAX; nSourceId++)
	{
		unsigned nSourceRate =
			CMachineInfo::Get ()->GetGPIOClockSourceRate (nSourceId);
		if (nSourceRate == GPIO_CLOCK_SOURCE_UNUSED)
		{
			continue;
		}

		// Round to the nearest integer divider and clamp to the valid range.
		unsigned nDivider = (unsigned) (nSourceRate / fTargetClock + 0.5);
		if (nDivider < 2)
		{
			nDivider = 2;
		}
		if (nDivider > 4095)
		{
			nDivider = 4095;
		}

		double fClock = (double) nSourceRate / nDivider;
		double fDiff  = fClock >= fTargetClock ? fClock - fTargetClock
						       : fTargetClock - fClock;
		if (fDiff < fBestDiff)
		{
			fBestDiff    = fDiff;
			BestSource   = static_cast<TGPIOClockSource> (nSourceId);
			nBestDivider = nDivider;
			fBestClock   = fClock;
		}
	}

	if (nBestDivider == 0)
	{
		return FALSE;
	}

	*pRange        = nRange;
	*pSource       = BestSource;
	*pDivider      = nBestDivider;
	*pActualFreqHz = (unsigned) (fBestClock / nRange + 0.5);

	return TRUE;
}

#endif

#if RASPPI <= 4

void CGPIOController::TimerHandler (CUserTimer *pUserTimer, void *pParam)
{
	CGPIOController *pThis = (CGPIOController *) pParam;
	assert (pThis != 0);
	assert (pUserTimer != 0);

	// Advance the virtual time by the delay that has just elapsed.
	pThis->m_nNowUs += pThis->m_nLastDelayUs;
	u64 nNow = pThis->m_nNowUs;

	// Determine the next edge across all toggling software PWM pins and drive
	// any pin whose edge time has been reached.
	boolean  bAny     = FALSE;
	unsigned nMinDelay = 0;

	for (unsigned i = 0; i < GPIO_CONTROL_PINS; i++)
	{
		if (!pThis->m_Soft[i].bActive || pThis->m_Soft[i].bStatic)
		{
			continue;
		}

		// If the pin fell more than one period behind (e.g. the timer was
		// just woken from its idle delay, or an interrupt was very late),
		// resync it to the current time instead of replaying a long burst
		// of missed edges.
		unsigned nPeriodUs = pThis->m_Soft[i].nHighUs + pThis->m_Soft[i].nLowUs;
		if (nNow > pThis->m_Soft[i].nNextEdgeUs + nPeriodUs)
		{
			pThis->m_Soft[i].nNextEdgeUs = nNow;
		}

		// Catch up on all edges that are due (handles a late interrupt).
		while (pThis->m_Soft[i].nNextEdgeUs <= nNow)
		{
			if (pThis->m_Soft[i].nLevel == HIGH)
			{
				pThis->m_Soft[i].nLevel = LOW;
				pThis->m_Pin[i].Write (LOW);
				pThis->m_Soft[i].nNextEdgeUs += pThis->m_Soft[i].nLowUs;
			}
			else
			{
				pThis->m_Soft[i].nLevel = HIGH;
				pThis->m_Pin[i].Write (HIGH);
				pThis->m_Soft[i].nNextEdgeUs += pThis->m_Soft[i].nHighUs;
			}
		}

		unsigned nDelta = (unsigned) (pThis->m_Soft[i].nNextEdgeUs - nNow);
		if (!bAny || nDelta < nMinDelay)
		{
			bAny      = TRUE;
			nMinDelay = nDelta;
		}
	}

	if (!bAny)
	{
		// Idle: nothing to toggle, check back in one second.
		nMinDelay = USER_CLOCKHZ;
	}

	if (nMinDelay < 2)
	{
		nMinDelay = 2;			// CUserTimer requires a delay > 1
	}

	pThis->m_nLastDelayUs = nMinDelay;
	pUserTimer->Start (nMinDelay);
}

#endif
