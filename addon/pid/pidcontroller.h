//
// pidcontroller.h
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
#ifndef _pid_pidcontroller_h
#define _pid_pidcontroller_h

#include <circle/types.h>

/// \brief Discrete PID controller (parallel form)
///
/// \details The controller computes\n
///	u = Kp * (b*r - y) + Ki * integral (r - y) dt + Kd * d/dt (c*r - y) + FF\n
/// with r being the setpoint, y the measurement, b and c the setpoint weights
/// and FF an optional feed-forward term.\n
/// Features:
/// - Derivative on measurement by default (c = 0), no derivative kick on setpoint steps
/// - First-order low-pass filter on the derivative term
/// - Output limits with selectable anti-windup (conditional integration or back-calculation)
/// - Output rate limit (slew rate)
/// - Integration deadband
/// - Direct or reverse acting
/// - Manual mode with bumpless transfer to automatic mode (integrator tracking)
/// - Online gain changes without output bump (integrator stores Ki*e integral)
///
/// The controller does not use the math library and does not allocate memory,
/// so it can be used from IRQ/FIQ handlers or on a dedicated core.
/// It is not thread-safe, all calls for one instance have to come from the
/// same execution context.
class CPIDController
{
public:
	enum TDirection
	{
		DirectionDirect,	///< output increases when measurement < setpoint (e.g. heater)
		DirectionReverse	///< output increases when measurement > setpoint (e.g. cooler)
	};

	enum TAntiWindup
	{
		AntiWindupNone,		///< integrator is never limited (not recommended)
		AntiWindupClamp,	///< conditional integration, integrator clamped to output limits
		AntiWindupBackCalculation ///< integrator tracks the saturated output
	};

public:
	/// \param fKp Proportional gain (>= 0)
	/// \param fKi Integral gain (>= 0, Kp/Ti, in 1/s)
	/// \param fKd Derivative gain (>= 0, Kp*Td, in s)
	/// \param fSampleTime Default sample time in seconds, used by Update(fSetpoint, fMeasurement)
	CPIDController (float fKp, float fKi, float fKd, float fSampleTime);

	~CPIDController (void);

	/// \brief Set the controller gains
	/// \note Can be changed while running without output bump
	void SetTunings (float fKp, float fKi, float fKd);
	float GetKp (void) const	{ return m_fKp; }
	float GetKi (void) const	{ return m_fKi; }
	float GetKd (void) const	{ return m_fKd; }

	/// \param fSampleTime Default sample time in seconds (> 0)
	void SetSampleTime (float fSampleTime);
	float GetSampleTime (void) const { return m_fSampleTime; }

	/// \brief Limit the controller output to [fMin, fMax] (default: no limits)
	void SetOutputLimits (float fMin, float fMax);

	/// \param Mode Anti-windup method (default: AntiWindupClamp)
	/// \param fTrackingTime Tracking time constant Tt in seconds for back-calculation\n
	///	   (0 selects Tt = Ti = Kp/Ki, or the sample time if Kp or Ki is 0)
	void SetAntiWindup (TAntiWindup Mode, float fTrackingTime = 0.0f);

	/// \param fTimeConstant Time constant of the derivative low-pass filter in seconds
	///	   (0 disables the filter, default)
	void SetDerivativeFilter (float fTimeConstant);

	/// \param fB Setpoint weight for the proportional term (default 1.0)
	/// \param fC Setpoint weight for the derivative term (default 0.0, derivative on measurement)
	void SetSetpointWeights (float fB, float fC);

	/// \param fMaxRate Maximum output change per second (0 disables the limit, default)
	void SetOutputRateLimit (float fMaxRate);

	/// \param fDeadband Integration is suspended while |r - y| <= fDeadband (default 0)
	void SetDeadband (float fDeadband);

	/// \param Direction Controller action (default: DirectionDirect)
	void SetDirection (TDirection Direction);
	TDirection GetDirection (void) const	{ return m_Direction; }

	/// \param fFeedForward Value added to the controller output before limiting
	void SetFeedForward (float fFeedForward);

	/// \brief Switch between automatic (closed-loop) and manual (open-loop) mode
	/// \note Transfer from manual to automatic mode is bumpless.
	void SetAutomatic (boolean bAutomatic);
	boolean IsAutomatic (void) const	{ return m_bAutomatic; }

	/// \brief Set the output value used in manual mode
	void SetManualOutput (float fOutput);

	/// \brief Reset the controller state (integrator, derivative filter, history)
	void Reset (void);

	/// \brief Preset the controller state for a bumpless start
	/// \param fMeasurement Current process measurement
	/// \param fOutput Current actuator output, which should be kept
	void Initialize (float fMeasurement, float fOutput);

	/// \brief Execute one control step using the default sample time
	/// \param fSetpoint Desired process value
	/// \param fMeasurement Current process value
	/// \return New controller output
	float Update (float fSetpoint, float fMeasurement);

	/// \brief Execute one control step
	/// \param fSetpoint Desired process value
	/// \param fMeasurement Current process value
	/// \param fDeltaTime Time since the last call in seconds (> 0)
	/// \return New controller output
	float Update (float fSetpoint, float fMeasurement, float fDeltaTime);

	/// \return Last controller output
	float GetOutput (void) const		{ return m_fOutput; }
	/// \return Last control error (r - y, not affected by direction)
	float GetError (void) const		{ return m_fError; }
	/// \return Last proportional term
	float GetPTerm (void) const		{ return m_fPTerm; }
	/// \return Current integral term
	float GetITerm (void) const		{ return m_fITerm; }
	/// \return Last derivative term
	float GetDTerm (void) const		{ return m_fDTerm; }
	/// \return TRUE if the last output was limited by the output or rate limit
	boolean IsSaturated (void) const	{ return m_bSaturated; }

private:
	float TrackingTime (void) const;

	static float Clamp (float fValue, float fMin, float fMax);
	static float Abs (float fValue)		{ return fValue < 0.0f ? -fValue : fValue; }

private:
	float m_fKp;
	float m_fKi;
	float m_fKd;
	float m_fSampleTime;

	float m_fOutputMin;
	float m_fOutputMax;
	TAntiWindup m_AntiWindup;
	float m_fTrackingTime;
	float m_fDerivativeFilter;
	float m_fSetpointWeightP;
	float m_fSetpointWeightD;
	float m_fMaxRate;
	float m_fDeadband;
	TDirection m_Direction;
	float m_fFeedForward;

	boolean m_bAutomatic;
	float m_fManualOutput;

	// state
	boolean m_bFirstRun;
	boolean m_bPrevSetpointValid;
	float m_fPrevSetpoint;
	float m_fPrevMeasurement;
	boolean m_bTrackPending;		// next Update() starts bumpless from m_fTrackOutput
	float m_fTrackOutput;
	float m_fError;
	float m_fPTerm;
	float m_fITerm;
	float m_fDTerm;
	float m_fOutput;
	boolean m_bSaturated;
};

#endif
