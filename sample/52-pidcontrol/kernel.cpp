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
#include <assert.h>

// control loop
#define SAMPLE_TIME_US		10000		// 10 ms (100 Hz)
#define SAMPLE_TIME		(SAMPLE_TIME_US / 1000000.0f)
#define LOG_INTERVAL		50		// samples (0.5 s)

// simulated heater process
#define PLANT_AMBIENT		25.0f		// degrees C
#define PLANT_GAIN		0.8f		// K per % heater power
#define PLANT_TIME_CONSTANT	5.0f		// seconds
#define PLANT_DEAD_TIME		0.5f		// seconds
#define PLANT_NOISE		0.05f		// K

// heater power
#define OUTPUT_MIN		0.0f		// %
#define OUTPUT_MAX		100.0f		// %
#define OUTPUT_MAX_RATE		100.0f		// % per second

// relay auto-tuning
#define TUNE_SETPOINT		50.0f		// degrees C
#define TUNE_OUTPUT_BASE	40.0f		// %
#define TUNE_OUTPUT_STEP	30.0f		// %, relay toggles between 10% and 70%
#define TUNE_HYSTERESIS		0.2f		// K, must be larger than the noise
#define TUNE_CYCLES		3
#define TUNE_TIMEOUT		120.0f		// seconds
#define TUNING_RULE		CPIDAutoTuner::RuleTyreusLuybenPID

// fallback gains, if the auto-tuning fails
#define DEFAULT_KP		5.0f
#define DEFAULT_KI		1.0f
#define DEFAULT_KD		2.0f

#define DERIVATIVE_FILTER	0.1f		// seconds

static const char FromKernel[] = "kernel";

const CKernel::TPhase CKernel::s_Phases[] =
{
	// name			duration  setpoint  disturbance  manual  output
	{"Hold",		15.0f,	  50.0f,    0.0f,	 FALSE,  0.0f},
	{"Setpoint step",	25.0f,	  70.0f,    0.0f,	 FALSE,  0.0f},
	{"Disturbance",		25.0f,	  70.0f,    15.0f,	 FALSE,  0.0f},
	{"Manual mode",		10.0f,	  70.0f,    0.0f,	 TRUE,   30.0f},
	{"Bumpless auto",	25.0f,	  70.0f,    0.0f,	 FALSE,  0.0f},
	{"Cool down",		30.0f,	  40.0f,    0.0f,	 FALSE,  0.0f}
};

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_Plant (PLANT_AMBIENT, PLANT_GAIN, PLANT_TIME_CONSTANT, PLANT_DEAD_TIME,
		 SAMPLE_TIME, PLANT_NOISE),
	m_PID (DEFAULT_KP, DEFAULT_KI, DEFAULT_KD, SAMPLE_TIME),
	m_nNextSampleTicks (0),
	m_fTime (0.0f),
	m_nOverruns (0)
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

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

	m_Logger.Write (FromKernel, LogNotice,
			"Simulated heater: K=%.2f K/%%, tau=%.1f s, dead time=%.2f s, ambient=%.1f C",
			PLANT_GAIN, PLANT_TIME_CONSTANT, PLANT_DEAD_TIME, PLANT_AMBIENT);
	m_Logger.Write (FromKernel, LogNotice, "Control loop period: %u ms", SAMPLE_TIME_US / 1000);

	m_nNextSampleTicks = CTimer::GetClockTicks64 ();

	// step 1: determine the controller gains
	AutoTune ();

	// step 2: configure the controller
	m_PID.SetOutputLimits (OUTPUT_MIN, OUTPUT_MAX);
	m_PID.SetOutputRateLimit (OUTPUT_MAX_RATE);
	m_PID.SetAntiWindup (CPIDController::AntiWindupBackCalculation);
	m_PID.SetDerivativeFilter (DERIVATIVE_FILTER);

	// continue bumpless from the current process state
	m_PID.Initialize (m_Plant.GetMeasurement (), m_AutoTuner.GetOutput ());

	m_Logger.Write (FromKernel, LogNotice, "PID gains: Kp=%.3f Ki=%.3f Kd=%.3f",
			m_PID.GetKp (), m_PID.GetKi (), m_PID.GetKd ());

	// step 3: closed-loop control in different situations
	for (unsigned i = 0; i < sizeof s_Phases / sizeof s_Phases[0]; i++)
	{
		RunPhase (&s_Phases[i]);
	}

	m_Logger.Write (FromKernel, LogNotice, "Demo finished (%u loop overruns)", m_nOverruns);

	return ShutdownHalt;
}

void CKernel::AutoTune (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Relay auto-tuning at %.1f C (output %.0f +/- %.0f %%)",
			TUNE_SETPOINT, TUNE_OUTPUT_BASE, TUNE_OUTPUT_STEP);

	m_AutoTuner.Start (TUNE_SETPOINT, TUNE_OUTPUT_BASE, TUNE_OUTPUT_STEP,
			   TUNE_HYSTERESIS, TUNE_CYCLES, TUNE_TIMEOUT);

	for (unsigned nSample = 0; m_AutoTuner.IsRunning (); nSample++)
	{
		WaitForNextSample ();

		float fMeasurement = m_Plant.GetMeasurement ();
		float fOutput = m_AutoTuner.Update (fMeasurement, SAMPLE_TIME);
		m_Plant.Update (fOutput);

		if (nSample % LOG_INTERVAL == 0)
		{
			m_Logger.Write (FromKernel, LogNotice,
					"%-13s t=%5.1f SP=%5.1f PV=%6.2f OUT=%6.2f cycles=%u",
					"Auto-tuning", m_fTime, TUNE_SETPOINT, fMeasurement, fOutput,
					m_AutoTuner.GetCycles ());
		}
	}

	if (m_AutoTuner.GetState () != CPIDAutoTuner::StateFinished)
	{
		m_Logger.Write (FromKernel, LogWarning, "Auto-tuning failed, using default gains");

		return;
	}

	m_Logger.Write (FromKernel, LogNotice,
			"Auto-tuning finished: Ku=%.3f Tu=%.3f s amplitude=%.3f K",
			m_AutoTuner.GetUltimateGain (), m_AutoTuner.GetUltimatePeriod (),
			m_AutoTuner.GetAmplitude ());

	for (unsigned i = 0; i < CPIDAutoTuner::RuleUnknown; i++)
	{
		CPIDAutoTuner::TTuningRule Rule = (CPIDAutoTuner::TTuningRule) i;

		float fKp, fKi, fKd;
		if (m_AutoTuner.GetTunings (Rule, &fKp, &fKi, &fKd))
		{
			m_Logger.Write (FromKernel, LogNotice, "%c %-20s Kp=%7.3f Ki=%7.3f Kd=%7.3f",
					Rule == TUNING_RULE ? '*' : ' ',
					CPIDAutoTuner::GetRuleName (Rule), fKp, fKi, fKd);
		}
	}

	m_AutoTuner.ApplyTunings (TUNING_RULE, &m_PID);
}

void CKernel::RunPhase (const TPhase *pPhase)
{
	assert (pPhase != 0);

	m_Logger.Write (FromKernel, LogNotice, "--- %s: setpoint %.1f C, disturbance %.1f K%s",
			pPhase->pName, pPhase->fSetpoint, pPhase->fDisturbance,
			pPhase->bManual ? ", manual mode" : "");

	m_Plant.SetDisturbance (pPhase->fDisturbance);

	if (pPhase->bManual)
	{
		m_PID.SetManualOutput (pPhase->fManualOutput);
	}
	m_PID.SetAutomatic (!pPhase->bManual);

	float fAbsErrorIntegral = 0.0f;
	float fMaxOvershoot = 0.0f;

	unsigned nSamples = (unsigned) (pPhase->fDuration / SAMPLE_TIME + 0.5f);
	for (unsigned nSample = 0; nSample < nSamples; nSample++)
	{
		WaitForNextSample ();

		// read sensor, calculate, write actuator
		float fMeasurement = m_Plant.GetMeasurement ();
		float fOutput = m_PID.Update (pPhase->fSetpoint, fMeasurement);
		m_Plant.Update (fOutput);

		float fError = pPhase->fSetpoint - m_Plant.GetTemperature ();
		fAbsErrorIntegral += (fError < 0.0f ? -fError : fError) * SAMPLE_TIME;
		if (-fError > fMaxOvershoot)
		{
			fMaxOvershoot = -fError;
		}

		if (nSample % LOG_INTERVAL == 0)
		{
			m_Logger.Write (FromKernel, LogNotice,
					"%-13s t=%5.1f SP=%5.1f PV=%6.2f OUT=%6.2f "
					"P=%7.2f I=%7.2f D=%6.2f%s",
					pPhase->pName, m_fTime, pPhase->fSetpoint, fMeasurement, fOutput,
					m_PID.GetPTerm (), m_PID.GetITerm (), m_PID.GetDTerm (),
					!m_PID.IsAutomatic () ? " MAN" : (m_PID.IsSaturated () ? " SAT" : ""));
		}
	}

	m_Logger.Write (FromKernel, LogNotice, "--- %s done: IAE=%.2f K*s, max. above setpoint=%.2f K",
			pPhase->pName, fAbsErrorIntegral, fMaxOvershoot);
}

void CKernel::WaitForNextSample (void)
{
	m_nNextSampleTicks += SAMPLE_TIME_US;

	u64 nNow = CTimer::GetClockTicks64 ();
	if (nNow >= m_nNextSampleTicks)
	{
		// we are late (e.g. because of screen output), resynchronize
		m_nOverruns++;
		m_nNextSampleTicks = nNow;
	}
	else
	{
		while (CTimer::GetClockTicks64 () < m_nNextSampleTicks)
		{
			// busy wait, the loop timing is more exact than with MsDelay()
		}
	}

	m_fTime += SAMPLE_TIME;
}
