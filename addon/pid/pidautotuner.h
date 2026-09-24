//
// pidautotuner.h
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
#ifndef _pid_pidautotuner_h
#define _pid_pidautotuner_h

#include <pid/pidcontroller.h>
#include <circle/types.h>

/// \brief Relay feedback auto-tuner
///
/// \details While running, the auto-tuner replaces the PID controller and drives
/// the process with a relay (bang-bang) output of OutputBase +/- OutputStep around
/// the setpoint. This lets the process oscillate at its ultimate period Tu. From
/// the period and the amplitude of the oscillation the ultimate gain Ku is
/// determined, from which PID gains are derived with the selected tuning rule.\n
/// The first oscillation cycle is ignored, the following cycles are averaged.\n
/// The hysteresis should be larger than the measurement noise band.
class CPIDAutoTuner
{
public:
	enum TState
	{
		StateIdle,
		StateRunning,
		StateFinished,		///< results available
		StateFailed		///< timeout or no usable oscillation
	};

	enum TTuningRule
	{
		RuleZieglerNicholsPI,
		RuleZieglerNicholsPID,
		RuleTyreusLuybenPI,	///< less aggressive, good for lag dominant processes
		RuleTyreusLuybenPID,
		RuleSomeOvershootPID,
		RuleNoOvershootPID,
		RuleUnknown
	};

public:
	CPIDAutoTuner (void);
	~CPIDAutoTuner (void);

	/// \brief Start the auto-tuning
	/// \param fSetpoint Process value around which the process oscillates
	/// \param fOutputBase Center output value of the relay
	/// \param fOutputStep Relay amplitude (> 0), the output toggles between Base-Step and Base+Step
	/// \param fHysteresis Relay hysteresis in process units (>= 0)
	/// \param nCycles Number of oscillation cycles to be averaged (>= 1)
	/// \param fTimeout Abort after this number of seconds (0 for no timeout)
	/// \param Direction Direction of the process (as for CPIDController)
	void Start (float fSetpoint, float fOutputBase, float fOutputStep,
		    float fHysteresis = 0.0f, unsigned nCycles = 3, float fTimeout = 0.0f,
		    CPIDController::TDirection Direction = CPIDController::DirectionDirect);

	/// \brief Stop the auto-tuning, state becomes StateIdle
	void Cancel (void);

	/// \brief Execute one step of the auto-tuning
	/// \param fMeasurement Current process value
	/// \param fDeltaTime Time since the last call in seconds (> 0)
	/// \return Output value to be applied to the process
	float Update (float fMeasurement, float fDeltaTime);

	TState GetState (void) const		{ return m_State; }
	boolean IsRunning (void) const		{ return m_State == StateRunning; }

	/// \return Last output value
	float GetOutput (void) const		{ return m_fOutput; }

	/// \return Number of completed oscillation cycles (including the ignored first one)
	unsigned GetCycles (void) const		{ return m_nCycles; }

	/// \return Elapsed time since Start() in seconds
	float GetElapsedTime (void) const	{ return m_fTime; }

	/// \return Ultimate gain Ku (valid in StateFinished)
	float GetUltimateGain (void) const	{ return m_fUltimateGain; }
	/// \return Ultimate period Tu in seconds (valid in StateFinished)
	float GetUltimatePeriod (void) const	{ return m_fUltimatePeriod; }
	/// \return Oscillation amplitude of the process value (valid in StateFinished)
	float GetAmplitude (void) const		{ return m_fAmplitude; }

	/// \brief Calculate PID gains from the measured process characteristics
	/// \param Rule Tuning rule to be used
	/// \param pKp Returns the proportional gain
	/// \param pKi Returns the integral gain (Kp/Ti)
	/// \param pKd Returns the derivative gain (Kp*Td)
	/// \return Operation successful? (FALSE if not in StateFinished)
	boolean GetTunings (TTuningRule Rule, float *pKp, float *pKi, float *pKd) const;

	/// \brief Apply the calculated gains to a PID controller
	/// \return Operation successful? (FALSE if not in StateFinished)
	boolean ApplyTunings (TTuningRule Rule, CPIDController *pController) const;

	/// \return Name of a tuning rule
	static const char *GetRuleName (TTuningRule Rule);

private:
	void Finish (void);

	static float SquareRoot (float fValue);

private:
	TState m_State;

	float m_fSetpoint;
	float m_fOutputBase;
	float m_fOutputStep;
	float m_fHysteresis;
	unsigned m_nCyclesRequired;
	float m_fTimeout;
	float m_fSign;

	float m_fTime;
	float m_fOutput;
	boolean m_bFirstUpdate;
	boolean m_bRelayHigh;

	boolean m_bCycleStarted;	// a falling relay edge has been seen
	float m_fCycleStartTime;
	float m_fCycleMax;
	float m_fCycleMin;
	unsigned m_nCycles;

	float m_fSumPeriod;
	float m_fSumAmplitude;

	float m_fUltimateGain;
	float m_fUltimatePeriod;
	float m_fAmplitude;
};

#endif
