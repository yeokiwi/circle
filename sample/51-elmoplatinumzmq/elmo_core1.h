//
// elmo_core1.h
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
#ifndef _elmo_core1_h
#define _elmo_core1_h

#include <circle/spinlock.h>
#include <circle/string.h>
#include <circle/types.h>

// This header file does not include the SOEM headers, so that it can be used
// by the ZeroMQ client too (see sample/47-ethercatweb/ethercat.h for the same
// reasoning).
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

// Conservative motion limits (Profile Velocity mode), enforced by the drive
// itself. There is no velocity-command path in this sample (target velocity
// is always 0, see elmo_core1.cpp), these only bound whatever a future
// motion-command extension would request.
#define ELMO_MAX_PROFILE_VELOCITY	5000	// counts/s, object 0x607F
#define ELMO_PROFILE_ACCEL		10000	// counts/s^2, object 0x6083
#define ELMO_PROFILE_DECEL		10000	// counts/s^2, object 0x6084

// Explicit delay between reaching SAFE_OP and requesting OPERATIONAL, as
// required by this application (not a SOEM/EtherCAT requirement by itself).
#define SAFE_OP_TO_OPERATIONAL_DELAY_US	5000

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

/// \brief Command, which can be submitted from another core (the ZeroMQ task)
///	  to arm or disarm the drive (CiA 402 level, not the EtherCAT AL state:
///	  the bus is always brought to OPERATIONAL, only the controlword
///	  written every cycle depends on this)
enum TElmoCommand
{
	ElmoCommandNone,
	ElmoCommandStart,	///< arm: run the CiA 402 enable ladder toward Operation Enabled
	ElmoCommandStop		///< disarm: command Shutdown, drive lands in Ready to switch on
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
	s32	 TargetVelocity;		///< always 0 in this sample

	boolean	 bArmed;			///< mirrors the current Start/Stop intent

	int	 nWorkCounter;
	int	 nExpectedWorkCounter;
	unsigned nCycleTimeUs;
	u64	 nCycles;
	u64	 nWKCErrors;
	unsigned nOperationalSeconds;

	char	 LastError[128];
};

/// \brief Controls one Elmo Platinum drive via raw SOEM, with explicit control
///	  over the AL state transitions and the CiA 402 enable sequence.
///
/// Unlike sample/49-elmoplatinum's CElmoPlatinumMaster, this class is NOT a
/// CTask. Its Run() method is called directly from CMultiCoreSupport::Run(1)
/// (see kernel.h) and never returns; it must only ever be used from that one
/// core. Cross-core access from other cores (the ZeroMQ task on core 0) is
/// limited to GetStatus() and SubmitCommand(), which are the only methods
/// protected by spin locks (see sample/50-ethercatzmq/ethercat_core1.h for
/// the same core-isolation pattern this class follows).
class CElmoPlatinumCore1Master
{
public:
	/// \param nDeviceIndex Zero-based index of the Ethernet device to be used
	/// \param nCycleTimeUs Process data cycle time in microseconds
	CElmoPlatinumCore1Master (unsigned nDeviceIndex = 0, unsigned nCycleTimeUs = 4000);
	~CElmoPlatinumCore1Master (void);

	/// \brief Runs the EtherCAT/drive master loop. Never returns. Call this
	///	   only from the core dedicated to it (see kernel.h).
	void Run (void);

	/// \brief Get a consistent copy of the current status
	/// \note May be called from any core.
	void GetStatus (TElmoPlatinumStatus *pStatus);

	/// \brief Submit a Start (arm) or Stop (disarm) command
	/// \note May be called from any core (e.g. the ZeroMQ task on core 0).
	void SubmitCommand (TElmoCommand Command);

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

	TElmoCommand FetchAndClearCommand (void);
	void HandleCommand (void);		// called once per cycle from CyclicProcess()

private:
	unsigned m_nDeviceIndex;
	CString	 m_Interface;
	unsigned m_nCycleTimeUs;

	ecx_context *m_pContext;
	u8	*m_pIOMap;
	boolean	 m_bInitialized;		// ecx_init() was successful
	boolean	 m_bConfigured;			// the bus has been configured
	boolean	 m_bRunning;			// armed intent, starts FALSE (safety default)

	int	 m_nExpectedWKC;
	int	 m_nWorkCounter;
	u64	 m_nCycles;
	u64	 m_nWKCErrors;
	unsigned m_nOperationalTicks;

	TElmoMasterState m_State;
	CString	 m_LastError;
	CString	 m_LoggedError;			// last message written to the system log

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

	CSpinLock m_StatusSpinLock;
	TElmoPlatinumStatus *m_pStatus;

	CSpinLock m_CommandSpinLock;
	TElmoCommand m_Command;
};

#endif
