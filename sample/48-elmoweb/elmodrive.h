//
// elmodrive.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2026  R. Stange <rsta2@gmx.net>
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
#ifndef _elmodrive_h
#define _elmodrive_h

#include <circle/sched/task.h>
#include <circle/sched/mutex.h>
#include <circle/string.h>
#include <circle/types.h>

// jsd_t is a typedef of an anonymous struct, so it cannot be forward declared.
// Including the public JSD header here also pulls in the SOEM headers, which
// is uncritical, because addon/jsd/compat/ethercat.h includes soemcpp.h, which
// resolves the different definitions of the type "boolean" in SOEM and Circle.
#include <jsd/jsd_pub.h>

#define ELMO_MAX_DRIVES		8	// drives handled by this application
#define ELMO_MAX_NAME		32

//
// Default drive parameters
//
// THESE VALUES MUST BE ADAPTED TO YOUR MOTOR BEFORE YOU CONNECT ONE. They are
// intentionally very conservative, so that a drive, which is configured with
// them by mistake, cannot produce much torque. The current limits are written
// to the drive by JSD, when the bus goes operational.
//
#define ELMO_PEAK_CURRENT_A		1.0f	// PL[1]
#define ELMO_PEAK_CURRENT_TIME_S	1.0f	// PL[2]
#define ELMO_CONTINUOUS_CURRENT_A	0.5f	// CL[1], must be <= peak current
#define ELMO_MAX_MOTOR_SPEED		100000.0 // counts/s, negative disables
#define ELMO_OVER_SPEED_THRESHOLD	0	// counts/s, 0 disables
#define ELMO_LOW_POSITION_LIMIT		0	// counts, low == high disables
#define ELMO_HIGH_POSITION_LIMIT	0
#define ELMO_BRAKE_ENGAGE_MS		0
#define ELMO_BRAKE_DISENGAGE_MS		0
#define ELMO_SMOOTH_FACTOR		0
#define ELMO_MAX_PROFILE_ACCEL		10000	// counts/s^2
#define ELMO_MAX_PROFILE_DECEL		10000	// counts/s^2
#define ELMO_VELOCITY_TRACKING_ERROR	100000	// counts/s
#define ELMO_POSITION_TRACKING_ERROR	100000	// counts

// Limits, which are enforced by this application for the commands, which come
// in over the web interface. They are clamped against these values and against
// the drive configuration above.
#define ELMO_MAX_JOG_VELOCITY		20000	// counts/s
#define ELMO_MAX_MOVE_VELOCITY		20000	// counts/s
#define ELMO_MAX_TORQUE_A		ELMO_CONTINUOUS_CURRENT_A

// A drive is disarmed and halted, if the web interface does not send a
// heartbeat within this time.
#define ELMO_WATCHDOG_MS		500

/// \brief Drive family, determines which JSD driver is used
enum TElmoFamily
{
	ElmoFamilyGold,				///< Elmo Gold drive (jsd_egd)
	ElmoFamilyPlatinum,			///< Elmo Platinum drive (jsd_epd_nominal)
	ElmoFamilyUnknown
};

/// \brief Command mode of a drive
/// \note An Elmo Gold drive supports one of these modes only, which has to be
///	  selected, before the bus goes operational. A Platinum drive supports
///	  both at the same time.
enum TElmoCommandMode
{
	ElmoCommandModeProfiled,		///< the drive generates the profile
	ElmoCommandModeCyclic			///< the master sends the setpoints
};

/// \brief Commands, which can be requested from the web interface
enum TElmoCommandCode
{
	ElmoCommandNone,
	ElmoCommandEnable,			///< bring the drive to OPERATION ENABLED
	ElmoCommandDisable,			///< disable the drive
	ElmoCommandResetFaults,			///< acknowledge the faults
	ElmoCommandArm,				///< allow motion commands
	ElmoCommandDisarm,			///< disallow motion commands
	ElmoCommandStop,			///< quick stop and disarm
	ElmoCommandJog,				///< velocity move (fValue counts/s)
	ElmoCommandMoveTo,			///< absolute move (nTarget counts)
	ElmoCommandMoveBy,			///< relative move (nTarget counts)
	ElmoCommandTorque,			///< torque command (fValue amps)
	ElmoCommandUnknown
};

/// \brief One command from the web interface to the master task
struct TElmoCommand
{
	TElmoCommandCode Code;
	unsigned nDrive;			///< index into the drive array
	s32	 nTarget;			///< position target in counts
	double	 fValue;			///< velocity in counts/s or torque in amps
};

/// \brief Status of one Elmo drive
struct TElmoDriveStatus
{
	boolean	 bPresent;
	unsigned nSlave;			///< EtherCAT slave number
	char	 Family[ELMO_MAX_NAME];
	char	 CommandMode[ELMO_MAX_NAME];
	boolean	 bCanProfiled;			///< supports profiled commands
	boolean	 bCanCyclic;			///< supports cyclic synchronous commands

	u32	 ProductCode;
	u32	 SerialNumber;

	char	 StateMachine[ELMO_MAX_NAME];	///< CiA 402 state machine state
	char	 ModeOfOperation[ELMO_MAX_NAME];
	char	 FaultString[64];
	unsigned nFaultCode;
	unsigned nEMCYCode;
	boolean	 bFaultUnknown;			///< faulted, but no EMCY message arrived

	s32	 nActualPosition;		///< counts
	s32	 nActualVelocity;		///< counts/s
	s32	 nCommandPosition;
	double	 fActualCurrent;		///< amps
	double	 fBusVoltage;			///< volts
	double	 fDriveTemperature;		///< degrees Celsius

	boolean	 bServoEnabled;
	boolean	 bMotorOn;
	boolean	 bInMotion;
	boolean	 bSTOEngaged;
	boolean	 bWarning;
	boolean	 bTargetReached;
	u16	 usDigitalInputs;		///< bit mask

	boolean	 bArmed;			///< motion commands are allowed
	char	 Motion[ELMO_MAX_NAME];		///< active motion command
	char	 LastError[96];			///< result of the last command

	// the limits, which are enforced for the web commands
	double	 fPeakCurrentLimit;
	double	 fContinuousCurrentLimit;
	double	 fMaxJogVelocity;
	double	 fMaxTorque;
};

/// \brief Status of the EtherCAT bus and of all drives
struct TElmoStatus
{
	char	 InterfaceName[8];
	char	 MACAddress[20];
	char	 LinkSpeed[32];
	boolean	 LinkUp;

	char	 MasterState[ELMO_MAX_NAME];
	char	 LastError[128];
	unsigned nSlaveCount;			///< slaves found on the bus
	unsigned nDriveCount;			///< Elmo drives found

	int	 nWorkCounter;
	int	 nExpectedWorkCounter;
	unsigned nCycleTimeUs;
	u64	 nCycles;
	u64	 nWKCErrors;
	u64	 nMissedDeadlines;		///< cycles, which were too late
	unsigned nMaxCyclePeriodUs;		///< longest measured cycle period
	unsigned nOperationalSeconds;

	TElmoDriveStatus Drives[ELMO_MAX_DRIVES];
};

/// \brief EtherCAT master task, which controls Elmo drives with JSD
class CElmoMaster : public CTask
{
public:
	/// \param nDeviceIndex Zero-based index of the Ethernet device to be used
	/// \param nCycleTimeUs Process data cycle time in microseconds
	CElmoMaster (unsigned nDeviceIndex = 0, unsigned nCycleTimeUs = 2000);
	~CElmoMaster (void);

	void Run (void) override;

	/// \brief Get a consistent copy of the current status
	void GetStatus (TElmoStatus *pStatus);

	/// \brief Queue a command from the web interface
	/// \param rCommand The command to be executed
	/// \param rError Receives an error message, if the command is rejected
	/// \return TRUE if the command has been queued
	boolean QueueCommand (const TElmoCommand &rCommand, CString &rError);

	/// \brief Refresh the watchdog of all drives
	/// \note This is called, when the web interface fetches the status.
	void Heartbeat (void);

	static const char *GetCommandName (TElmoCommandCode Code);
	static TElmoCommandCode GetCommandCode (const char *pName);

private:
	// bus
	boolean Discover (void);		// scan the bus for Elmo drives
	boolean Start (void);			// configure the drives and go operational
	void CyclicProcess (void);
	void Stop (void);

	// drives
	void ServiceDrive (unsigned nDrive);
	void ApplyCommands (void);
	void ApplyCommand (const TElmoCommand &rCommand);
	void StartMotion (unsigned nDrive, const TElmoCommand &rCommand);
	void UpdateCyclicMotion (unsigned nDrive);
	boolean IsServoEnabled (unsigned nDrive);
	void CheckWatchdog (void);
	void HaltDrive (unsigned nDrive, const char *pReason);
	void UpdateStatus (void);

	struct TElmoDrive;
	void SetDriveError (TElmoDrive *pDrive, const char *pMessage);

	void SetError (const char *pMessage);

private:
	unsigned m_nDeviceIndex;
	CString	 m_Interface;
	unsigned m_nCycleTimeUs;

	jsd_t	*m_pJSD;
	boolean	 m_bRunning;			// the bus is operational

	// the drives, which have been found on the bus
	struct TElmoDrive
	{
		boolean		 bPresent;
		unsigned	 nSlave;
		TElmoFamily	 Family;
		TElmoCommandMode Mode;
		u32		 ProductCode;
		u32		 SerialNumber;

		boolean		 bEnableRequested;	// call reset() until enabled
		boolean		 bArmed;
		unsigned	 nHeartbeatTicks;	// last heartbeat from the web UI

		TElmoCommandCode Motion;		// active motion command
		s32		 nMotionTarget;		// counts
		double		 fMotionValue;		// counts/s or amps

		// state of the master generated profile (cyclic modes only)
		double		 fCmdPosition;		// counts
		double		 fCmdVelocity;		// counts/s

		// plain array, so that the struct can be cleared with memset()
		char		 LastError[96];
	};

	TElmoDrive m_Drive[ELMO_MAX_DRIVES];
	unsigned m_nDriveCount;

	// command queue, filled by the web server task
	CMutex	 m_CommandMutex;
	TElmoCommand m_CommandQueue[16];
	unsigned m_nCommandIn;
	unsigned m_nCommandOut;

	// statistics
	u64	 m_nCycles;
	u64	 m_nWKCErrors;
	u64	 m_nMissedDeadlines;
	unsigned m_nMaxCyclePeriodUs;
	unsigned m_nOperationalTicks;

	CString	 m_MasterState;
	CString	 m_LastError;

	CMutex	 m_StatusMutex;
	TElmoStatus *m_pStatus;
};

#endif
