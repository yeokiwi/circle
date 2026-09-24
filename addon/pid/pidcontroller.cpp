//
// pidcontroller.cpp
//
// Discrete PID controller for closed-loop control applications
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
#include <pid/pidcontroller.h>
#include <assert.h>

#define NO_LIMIT	3.0e38f

CPIDController::CPIDController (float fKp, float fKi, float fKd, float fSampleTime)
:	m_fKp (0.0f),
	m_fKi (0.0f),
	m_fKd (0.0f),
	m_fSampleTime (fSampleTime),
	m_fOutputMin (-NO_LIMIT),
	m_fOutputMax (NO_LIMIT),
	m_AntiWindup (AntiWindupClamp),
	m_fTrackingTime (0.0f),
	m_fDerivativeFilter (0.0f),
	m_fSetpointWeightP (1.0f),
	m_fSetpointWeightD (0.0f),
	m_fMaxRate (0.0f),
	m_fDeadband (0.0f),
	m_Direction (DirectionDirect),
	m_fFeedForward (0.0f),
	m_bAutomatic (TRUE),
	m_fManualOutput (0.0f)
{
	assert (fSampleTime > 0.0f);

	SetTunings (fKp, fKi, fKd);
	Reset ();
}

CPIDController::~CPIDController (void)
{
}

void CPIDController::SetTunings (float fKp, float fKi, float fKd)
{
	assert (fKp >= 0.0f);
	assert (fKi >= 0.0f);
	assert (fKd >= 0.0f);

	m_fKp = fKp;
	m_fKi = fKi;
	m_fKd = fKd;
}

void CPIDController::SetSampleTime (float fSampleTime)
{
	assert (fSampleTime > 0.0f);
	m_fSampleTime = fSampleTime;
}

void CPIDController::SetOutputLimits (float fMin, float fMax)
{
	assert (fMin < fMax);

	m_fOutputMin = fMin;
	m_fOutputMax = fMax;

	m_fITerm = Clamp (m_fITerm, m_fOutputMin - m_fFeedForward, m_fOutputMax - m_fFeedForward);
	m_fOutput = Clamp (m_fOutput, m_fOutputMin, m_fOutputMax);
}

void CPIDController::SetAntiWindup (TAntiWindup Mode, float fTrackingTime)
{
	assert (fTrackingTime >= 0.0f);

	m_AntiWindup = Mode;
	m_fTrackingTime = fTrackingTime;
}

void CPIDController::SetDerivativeFilter (float fTimeConstant)
{
	assert (fTimeConstant >= 0.0f);
	m_fDerivativeFilter = fTimeConstant;
}

void CPIDController::SetSetpointWeights (float fB, float fC)
{
	m_fSetpointWeightP = fB;
	m_fSetpointWeightD = fC;
}

void CPIDController::SetOutputRateLimit (float fMaxRate)
{
	assert (fMaxRate >= 0.0f);
	m_fMaxRate = fMaxRate;
}

void CPIDController::SetDeadband (float fDeadband)
{
	assert (fDeadband >= 0.0f);
	m_fDeadband = fDeadband;
}

void CPIDController::SetDirection (TDirection Direction)
{
	if (Direction != m_Direction)
	{
		m_Direction = Direction;

		// the controller has to restart from the current output
		m_bTrackPending = TRUE;
		m_fTrackOutput = m_fOutput;
	}
}

void CPIDController::SetFeedForward (float fFeedForward)
{
	m_fFeedForward = fFeedForward;
}

void CPIDController::SetAutomatic (boolean bAutomatic)
{
	if (bAutomatic && !m_bAutomatic)
	{
		// bumpless transfer: continue from the last manual output
		m_bTrackPending = TRUE;
		m_fTrackOutput = m_fOutput;
	}

	m_bAutomatic = bAutomatic;
}

void CPIDController::SetManualOutput (float fOutput)
{
	m_fManualOutput = fOutput;

	if (!m_bAutomatic)
	{
		m_fOutput = Clamp (fOutput, m_fOutputMin, m_fOutputMax);
	}
}

void CPIDController::Reset (void)
{
	m_bFirstRun = TRUE;
	m_bPrevSetpointValid = FALSE;
	m_fPrevSetpoint = 0.0f;
	m_fPrevMeasurement = 0.0f;
	m_bTrackPending = FALSE;
	m_fTrackOutput = 0.0f;

	m_fError = 0.0f;
	m_fPTerm = 0.0f;
	m_fITerm = 0.0f;
	m_fDTerm = 0.0f;
	m_fOutput = Clamp (0.0f, m_fOutputMin, m_fOutputMax);
	m_bSaturated = FALSE;
}

void CPIDController::Initialize (float fMeasurement, float fOutput)
{
	Reset ();

	m_bFirstRun = FALSE;
	m_fPrevMeasurement = fMeasurement;

	m_bTrackPending = TRUE;
	m_fTrackOutput = fOutput;
	m_fOutput = Clamp (fOutput, m_fOutputMin, m_fOutputMax);
}

float CPIDController::Update (float fSetpoint, float fMeasurement)
{
	return Update (fSetpoint, fMeasurement, m_fSampleTime);
}

float CPIDController::Update (float fSetpoint, float fMeasurement, float fDeltaTime)
{
	if (fDeltaTime <= 0.0f)
	{
		return m_fOutput;
	}

	const float fSign = m_Direction == DirectionDirect ? 1.0f : -1.0f;

	if (m_bFirstRun)
	{
		m_fPrevMeasurement = fMeasurement;
	}

	if (!m_bPrevSetpointValid)
	{
		m_fPrevSetpoint = fSetpoint;
		m_bPrevSetpointValid = TRUE;
	}

	m_fError = fSetpoint - fMeasurement;

	// proportional term with setpoint weighting
	m_fPTerm = m_fKp * fSign * (m_fSetpointWeightP * fSetpoint - fMeasurement);

	// derivative term with setpoint weighting and first-order low-pass filter
	float fDerivError     = fSign * (m_fSetpointWeightD * fSetpoint - fMeasurement);
	float fPrevDerivError = fSign * (m_fSetpointWeightD * m_fPrevSetpoint - m_fPrevMeasurement);
	if (m_bFirstRun)
	{
		m_fDTerm = 0.0f;
	}
	else
	{
		m_fDTerm =   (m_fDerivativeFilter * m_fDTerm + m_fKd * (fDerivError - fPrevDerivError))
			   / (m_fDerivativeFilter + fDeltaTime);
	}

	m_fPrevSetpoint = fSetpoint;
	m_fPrevMeasurement = fMeasurement;

	float fPrevOutput = m_fOutput;
	boolean bFirstRun = m_bFirstRun;
	m_bFirstRun = FALSE;

	// manual mode or bumpless start: the integrator tracks the requested output
	if (!m_bAutomatic || m_bTrackPending)
	{
		float fOutput = m_bAutomatic ? m_fTrackOutput : m_fManualOutput;
		fOutput = Clamp (fOutput, m_fOutputMin, m_fOutputMax);

		m_fITerm = fOutput - m_fPTerm - m_fDTerm - m_fFeedForward;
		m_fOutput = fOutput;
		m_bSaturated = FALSE;
		m_bTrackPending = FALSE;

		return m_fOutput;
	}

	// integral term (the integrator stores the integral of Ki*e, so that gain changes are bumpless)
	float fDeltaI = 0.0f;
	if (Abs (m_fError) > m_fDeadband)
	{
		fDeltaI = m_fKi * fSign * m_fError * fDeltaTime;
	}

	if (m_AntiWindup == AntiWindupClamp)
	{
		// conditional integration: do not integrate further into saturation
		float fValue = m_fPTerm + m_fITerm + fDeltaI + m_fDTerm + m_fFeedForward;
		if (   (fValue > m_fOutputMax && fDeltaI > 0.0f)
		    || (fValue < m_fOutputMin && fDeltaI < 0.0f))
		{
			fDeltaI = 0.0f;
		}

		m_fITerm = Clamp (m_fITerm + fDeltaI, m_fOutputMin - m_fFeedForward,
						      m_fOutputMax - m_fFeedForward);
	}
	else
	{
		m_fITerm += fDeltaI;
	}

	// unlimited output
	float fValue = m_fPTerm + m_fITerm + m_fDTerm + m_fFeedForward;

	// rate limit
	float fOutput = fValue;
	if (   m_fMaxRate > 0.0f
	    && !bFirstRun)
	{
		float fMaxDelta = m_fMaxRate * fDeltaTime;
		fOutput = Clamp (fOutput, fPrevOutput - fMaxDelta, fPrevOutput + fMaxDelta);
	}

	// output limits
	fOutput = Clamp (fOutput, m_fOutputMin, m_fOutputMax);

	m_bSaturated = fOutput != fValue;

	if (   m_AntiWindup == AntiWindupBackCalculation
	    && m_bSaturated)
	{
		// feed back the difference between limited and unlimited output
		float fGain = fDeltaTime / TrackingTime ();
		if (fGain > 1.0f)
		{
			fGain = 1.0f;
		}

		m_fITerm += fGain * (fOutput - fValue);
	}

	m_fOutput = fOutput;

	return m_fOutput;
}

float CPIDController::TrackingTime (void) const
{
	// returns the tracking time constant Tt
	if (m_fTrackingTime > 0.0f)
	{
		return m_fTrackingTime;
	}

	if (   m_fKp > 0.0f
	    && m_fKi > 0.0f)
	{
		return m_fKp / m_fKi;		// Ti
	}

	return m_fSampleTime;
}

float CPIDController::Clamp (float fValue, float fMin, float fMax)
{
	if (fValue < fMin)
	{
		return fMin;
	}

	if (fValue > fMax)
	{
		return fMax;
	}

	return fValue;
}
