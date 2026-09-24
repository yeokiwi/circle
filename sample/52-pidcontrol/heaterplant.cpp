//
// heaterplant.cpp
//
// Simulated heater process (first order plus dead time)
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
#include "heaterplant.h"
#include <assert.h>

CHeaterPlant::CHeaterPlant (float fAmbient, float fGain, float fTimeConstant, float fDeadTime,
			    float fSampleTime, float fNoise)
:	m_fAmbient (fAmbient),
	m_fGain (fGain),
	m_fTimeConstant (fTimeConstant),
	m_fSampleTime (fSampleTime),
	m_fNoise (fNoise),
	m_fTemperature (fAmbient),
	m_fLoss (0.0f),
	m_nDelayIndex (0),
	m_nRandom (12345)
{
	assert (fTimeConstant > 0.0f);
	assert (fSampleTime > 0.0f);

	m_nDelay = (unsigned) (fDeadTime / fSampleTime + 0.5f);
	if (m_nDelay < 1)
	{
		m_nDelay = 1;
	}
	assert (m_nDelay <= HEATER_MAX_DELAY);

	for (unsigned i = 0; i < HEATER_MAX_DELAY; i++)
	{
		m_Delay[i] = 0.0f;
	}
}

void CHeaterPlant::Update (float fPower)
{
	// the heater power arrives at the sensor after the dead time
	float fDelayedPower = m_Delay[m_nDelayIndex];
	m_Delay[m_nDelayIndex] = fPower;
	m_nDelayIndex = (m_nDelayIndex + 1) % m_nDelay;

	float fDerivative =   (m_fGain * fDelayedPower - m_fLoss - (m_fTemperature - m_fAmbient))
			    / m_fTimeConstant;

	m_fTemperature += fDerivative * m_fSampleTime;
}

float CHeaterPlant::GetMeasurement (void)
{
	// linear congruential generator, uniform noise in [-m_fNoise, m_fNoise]
	m_nRandom = m_nRandom * 1103515245U + 12345U;
	float fRandom = ((m_nRandom >> 16) & 0x7FFF) / 32767.0f;

	return m_fTemperature + (2.0f * fRandom - 1.0f) * m_fNoise;
}
