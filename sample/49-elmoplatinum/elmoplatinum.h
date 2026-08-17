//
// elmoplatinum.h
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
#ifndef _elmoplatinum_h
#define _elmoplatinum_h

#include <circle/sched/task.h>
#include <circle/sched/mutex.h>
#include <circle/string.h>
#include <circle/macros.h>
#include <circle/types.h>

// This header file does not include the SOEM headers, so that it can be used
// without pulling in soemcpp.h everywhere (see sample/47-ethercatweb/ethercat.h
// for the same reasoning).
struct ecx_context;

#define ELMO_MAX_NAME		41		// same as EC_MAXNAME+1

// Size of the process data image (generous, this application maps 6+6 bytes only)
#define ELMO_IOMAP_SIZE		(4 * 1024)

// Well known Elmo Motion Control identity, used only to log a warning, if the
// connected slave does not look like an Elmo Platinum drive. The application
// still tries to drive it, because this is not a reliable safety check.
#define ELMO_VENDOR_ID			0x0000009A
#define ELMO_PLATINUM_PRODUCT_CODE	0x00100002	// standard firmware
#define ELMO_PLATINUM_SAFETY_PRODUCT_CODE 0x01100002	// safety firmware

//
// Conservative demo motion parameters
//
// THESE VALUES MUST BE ADAPTED TO YOUR MOTOR before you connect one. Set
// ELMO_ENABLE_DEMO_MOTION to 0, if you only want to test the EtherCAT state
// machine and the CiA 402 enable sequence, without commanding any motion.
//
#define ELMO_ENABLE_DEMO_MOTION		1
#define ELMO_DEMO_VELOCITY_CPS		1000	// counts/s, intentionally very slow
#define ELMO_DEMO_MOVE_MS		2000	// duration of the demo move
#define ELMO_MAX_PROFILE_VELOCITY	5000	// counts/s, object 0x607F
#define ELMO_PROFILE_ACCEL		10000	// counts/s^2, object 0x6083
#define ELMO_PROFILE_DECEL		10000	// counts/s^2, object 0x6084

/// \brief AL state of the EtherCAT master/slave, as tracked by this application
enum TElmoMasterState
{
	ElmoMasterInit,				///< master is starting up
	ElmoMasterNoLink,			///< the Ethernet link is down
	ElmoMasterNoSlaves,			///< no slaves have been found
	ElmoMasterPreOp,			///< slave is in PRE_OP, being configured
	ElmoMasterSafeOp,			///< slave reached SAFE_OP
	ElmoMasterOperational,			///< slave reached OPERATIONAL
	ElmoMasterError,			///< see the error message
	ElmoMasterUnknown
};

/// \brief CiA 402 (DS402) state machine state of the drive, decoded from the
///	  statusword (object 0x6041)
enum TCiA402State
{
	CiA402NotReadyToSwitchOn,
	CiA402SwitchOnDisabled,
	CiA402ReadyToSwitchOn,
	CiA402SwitchedOn,
	CiA402OperationEnabled,
	CiA402QuickStopActive,
	CiA402FaultReactionActive,
	CiA402Fault,
	CiA402Unknown
};

/// \brief Status of the EtherCAT master and of the Elmo Platinum drive
struct TElmoPlatinumStatus
{
	TElmoMasterState MasterState;
	char	 InterfaceName[8];
	char	 MACAddress[20];
	char	 LinkSpeed[32];
	boolean	 LinkUp;

	boolean	 bSlaveFound;
	char	 SlaveName[ELMO_MAX_NAME];
	u32	 VendorID;
	u32	 ProductCode;
	boolean	 bIsElmoPlatinum;

	TCiA402State DriveState;
	u16	 StatusWord;
	u16	 ControlWord;
	s32	 ActualVelocity;
	s32	 TargetVelocity;

	boolean	 bDemoMotionActive;

	int	 nWorkCounter;
	int	 nExpectedWorkCounter;
	unsigned nCycleTimeUs;
	u64	 nCycles;
	u64	 nWKCErrors;
	unsigned nOperationalSeconds;

	char	 LastError[128];
};

/// \brief EtherCAT master task, which uses raw SOEM to control one Elmo
///	  Platinum drive, with explicit control over the AL state transitions
class CElmoPlatinumMaster : public CTask
{
public:
	/// \param nDeviceIndex Zero-based index of the Ethernet device to be used
	/// \param nCycleTimeUs Process data cycle time in microseconds
	CElmoPlatinumMaster (unsigned nDeviceIndex = 0, unsigned nCycleTimeUs = 4000);
	~CElmoPlatinumMaster (void);

	void Run (void) override;

	/// \brief Get a consistent copy of the current status
	void GetStatus (TElmoPlatinumStatus *pStatus);

	static const char *GetMasterStateString (TElmoMasterState State);
	static const char *GetDriveStateString (TCiA402State State);

private:
	boolean Configure (void);		// bring the slave from INIT to OPERATIONAL
	boolean ConfigureDrive (void);		// CoE/SDO configuration, done in PRE_OP
	void CyclicProcess (void);		// exchange process data, drive the CiA 402 FSM
	void ReadSlaveInfo (void);
	void ReadSlaveSDOString (u16 usIndex, char *pString, size_t nSize);
	static TCiA402State DecodeStatusWord (u16 usStatusWord);
	void UpdateStatus (void);
	void SetError (const char *pMessage);
	void ClearError (void);
	void SetState (TElmoMasterState State);

private:
	unsigned m_nDeviceIndex;
	CString	 m_Interface;
	unsigned m_nCycleTimeUs;

	ecx_context *m_pContext;
	u8	*m_pIOMap;
	boolean	 m_bInitialized;		// ecx_init() was successful
	boolean	 m_bConfigured;			// the bus has been configured

	int	 m_nExpectedWKC;
	int	 m_nWorkCounter;
	u64	 m_nCycles;
	u64	 m_nWKCErrors;
	unsigned m_nOperationalTicks;

	TElmoMasterState m_State;
	CString	 m_LastError;

	// identity of the slave, read once while it is in PRE_OP
	boolean	 m_bSlaveFound;
	CString	 m_SlaveName;
	u32	 m_VendorID;
	u32	 m_ProductCode;
	boolean	 m_bIsElmoPlatinum;

	// CiA 402 drive state, updated every cycle
	TCiA402State m_DriveState;
	u16	 m_usStatusWord;
	u16	 m_usControlWord;
	s32	 m_nActualVelocity;
	s32	 m_nTargetVelocity;

	// demo motion state machine
	boolean	 m_bDemoMotionActive;
	boolean	 m_bDemoMotionDone;
	unsigned m_nDemoMotionStartTicks;

	CMutex	 m_StatusMutex;
	TElmoPlatinumStatus *m_pStatus;
};

#endif
