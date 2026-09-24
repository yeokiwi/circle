//
// heaterplant.h
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
#ifndef _heaterplant_h
#define _heaterplant_h

#include <circle/types.h>

#define HEATER_MAX_DELAY	256		// max. dead time in samples

/// \brief Simulates a heated block:\n
///	tau * dT/dt = Gain * Power(t - DeadTime) - Loss - (T - Ambient)\n
/// Replace this class with your real sensor and actuator in an application.
class CHeaterPlant
{
public:
	/// \param fAmbient Ambient temperature (degrees C)
	/// \param fGain Steady state temperature rise per percent heater power (K/%)
	/// \param fTimeConstant Thermal time constant (s)
	/// \param fDeadTime Transport delay between heater and sensor (s)
	/// \param fSampleTime Simulation step (s)
	/// \param fNoise Peak measurement noise (K)
	CHeaterPlant (float fAmbient, float fGain, float fTimeConstant, float fDeadTime,
		      float fSampleTime, float fNoise);

	/// \brief Advance the simulation by one sample time
	/// \param fPower Heater power in percent (0..100)
	void Update (float fPower);

	/// \return Measured temperature including sensor noise
	float GetMeasurement (void);

	/// \return Real temperature without noise
	float GetTemperature (void) const	{ return m_fTemperature; }

	/// \param fLoss Additional heat loss (K of steady state temperature), e.g. an open lid
	void SetDisturbance (float fLoss)	{ m_fLoss = fLoss; }

private:
	float m_fAmbient;
	float m_fGain;
	float m_fTimeConstant;
	float m_fSampleTime;
	float m_fNoise;

	float m_fTemperature;
	float m_fLoss;

	float m_Delay[HEATER_MAX_DELAY];
	unsigned m_nDelay;
	unsigned m_nDelayIndex;

	u32 m_nRandom;
};

#endif
