//
// pidautotuner.cpp
//
// Relay feedback auto-tuner (Astrom-Hagglund method) for CPIDController
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
#include <pid/pidautotuner.h>
#include <assert.h>

#define PI	3.14159265f

struct TTuningRuleParams
{
	const char *pName;
	float fKpFactor;		// Kp = fKpFactor * Ku
	float fTiFactor;		// Ti = fTiFactor * Tu
	float fTdFactor;		// Td = fTdFactor * Tu
};

static const TTuningRuleParams s_Rules[CPIDAutoTuner::RuleUnknown] =
{
	{"Ziegler-Nichols PI",	0.45f,		1.0f / 1.2f,	0.0f},
	{"Ziegler-Nichols PID",	0.6f,		0.5f,		0.125f},
	{"Tyreus-Luyben PI",	1.0f / 3.2f,	2.2f,		0.0f},
	{"Tyreus-Luyben PID",	1.0f / 2.2f,	2.2f,		1.0f / 6.3f},
	{"Some overshoot PID",	0.33f,		0.5f,		1.0f / 3.0f},
	{"No overshoot PID",	0.2f,		0.5f,		1.0f / 3.0f}
};

CPIDAutoTuner::CPIDAutoTuner (void)
:	m_State (StateIdle),
	m_fOutput (0.0f),
	m_nCycles (0),
	m_fUltimateGain (0.0f),
	m_fUltimatePeriod (0.0f),
	m_fAmplitude (0.0f)
{
}

CPIDAutoTuner::~CPIDAutoTuner (void)
{
}

void CPIDAutoTuner::Start (float fSetpoint, float fOutputBase, float fOutputStep,
			   float fHysteresis, unsigned nCycles, float fTimeout,
			   CPIDController::TDirection Direction)
{
	assert (fOutputStep > 0.0f);
	assert (fHysteresis >= 0.0f);
	assert (nCycles >= 1);
	assert (fTimeout >= 0.0f);

	m_fSetpoint = fSetpoint;
	m_fOutputBase = fOutputBase;
	m_fOutputStep = fOutputStep;
	m_fHysteresis = fHysteresis;
	m_nCyclesRequired = nCycles;
	m_fTimeout = fTimeout;
	m_fSign = Direction == CPIDController::DirectionDirect ? 1.0f : -1.0f;

	m_fTime = 0.0f;
	m_fOutput = fOutputBase;
	m_bFirstUpdate = TRUE;
	m_bRelayHigh = FALSE;
	m_bCycleStarted = FALSE;
	m_fCycleStartTime = 0.0f;
	m_fCycleMax = 0.0f;
	m_fCycleMin = 0.0f;
	m_nCycles = 0;
	m_fSumPeriod = 0.0f;
	m_fSumAmplitude = 0.0f;

	m_fUltimateGain = 0.0f;
	m_fUltimatePeriod = 0.0f;
	m_fAmplitude = 0.0f;

	m_State = StateRunning;
}

void CPIDAutoTuner::Cancel (void)
{
	m_State = StateIdle;
	m_fOutput = m_fOutputBase;
}

float CPIDAutoTuner::Update (float fMeasurement, float fDeltaTime)
{
	if (m_State != StateRunning)
	{
		return m_fOutput;
	}

	if (fDeltaTime > 0.0f)
	{
		m_fTime += fDeltaTime;
	}

	if (   m_fTimeout > 0.0f
	    && m_fTime > m_fTimeout)
	{
		m_State = StateFailed;
		m_fOutput = m_fOutputBase;

		return m_fOutput;
	}

	// relay with hysteresis
	if (m_bFirstUpdate)
	{
		m_bFirstUpdate = FALSE;
		m_bRelayHigh = fMeasurement < m_fSetpoint;
	}
	else if (   m_bRelayHigh
	    && fMeasurement > m_fSetpoint + m_fHysteresis)
	{
		m_bRelayHigh = FALSE;

		// a falling edge ends one cycle and starts the next
		if (m_bCycleStarted)
		{
			float fPeriod = m_fTime - m_fCycleStartTime;
			float fAmplitude = (m_fCycleMax - m_fCycleMin) / 2.0f;

			if (++m_nCycles > 1)		// ignore the first (transient) cycle
			{
				m_fSumPeriod += fPeriod;
				m_fSumAmplitude += fAmplitude;
			}

			if (m_nCycles > m_nCyclesRequired)
			{
				Finish ();

				return m_fOutput;
			}
		}

		m_bCycleStarted = TRUE;
		m_fCycleStartTime = m_fTime;
		m_fCycleMax = fMeasurement;
		m_fCycleMin = fMeasurement;
	}
	else if (   !m_bRelayHigh
		 && fMeasurement < m_fSetpoint - m_fHysteresis)
	{
		m_bRelayHigh = TRUE;
	}

	if (m_bCycleStarted)
	{
		if (fMeasurement > m_fCycleMax)
		{
			m_fCycleMax = fMeasurement;
		}

		if (fMeasurement < m_fCycleMin)
		{
			m_fCycleMin = fMeasurement;
		}
	}

	m_fOutput = m_fOutputBase + (m_bRelayHigh ? m_fSign : -m_fSign) * m_fOutputStep;

	return m_fOutput;
}

boolean CPIDAutoTuner::GetTunings (TTuningRule Rule, float *pKp, float *pKi, float *pKd) const
{
	assert (pKp != 0);
	assert (pKi != 0);
	assert (pKd != 0);

	if (   m_State != StateFinished
	    || Rule >= RuleUnknown)
	{
		return FALSE;
	}

	const TTuningRuleParams *pParams = &s_Rules[Rule];

	float fKp = pParams->fKpFactor * m_fUltimateGain;
	float fTi = pParams->fTiFactor * m_fUltimatePeriod;
	float fTd = pParams->fTdFactor * m_fUltimatePeriod;

	*pKp = fKp;
	*pKi = fTi > 0.0f ? fKp / fTi : 0.0f;
	*pKd = fKp * fTd;

	return TRUE;
}

boolean CPIDAutoTuner::ApplyTunings (TTuningRule Rule, CPIDController *pController) const
{
	assert (pController != 0);

	float fKp, fKi, fKd;
	if (!GetTunings (Rule, &fKp, &fKi, &fKd))
	{
		return FALSE;
	}

	pController->SetTunings (fKp, fKi, fKd);

	return TRUE;
}

const char *CPIDAutoTuner::GetRuleName (TTuningRule Rule)
{
	if (Rule >= RuleUnknown)
	{
		return "Unknown";
	}

	return s_Rules[Rule].pName;
}

void CPIDAutoTuner::Finish (void)
{
	unsigned nCycles = m_nCycles - 1;
	assert (nCycles > 0);

	m_fUltimatePeriod = m_fSumPeriod / nCycles;
	m_fAmplitude = m_fSumAmplitude / nCycles;
	m_fOutput = m_fOutputBase;

	// describing function of a relay with hysteresis
	float fSquare = m_fAmplitude * m_fAmplitude - m_fHysteresis * m_fHysteresis;
	if (   fSquare <= 0.0f
	    || m_fUltimatePeriod <= 0.0f)
	{
		m_State = StateFailed;

		return;
	}

	m_fUltimateGain = 4.0f * m_fOutputStep / (PI * SquareRoot (fSquare));

	m_State = StateFinished;
}

float CPIDAutoTuner::SquareRoot (float fValue)
{
	assert (fValue > 0.0f);

	// Newton-Raphson iteration, avoids dependency on the math library
	float fResult = fValue > 1.0f ? fValue : 1.0f;
	for (unsigned i = 0; i < 40; i++)
	{
		float fNext = 0.5f * (fResult + fValue / fResult);
		if (fNext >= fResult)
		{
			break;
		}

		fResult = fNext;
	}

	return fResult;
}
