//
// ethercat_core1.h
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
#ifndef _ethercat_core1_h
#define _ethercat_core1_h

#include <circle/spinlock.h>
#include <circle/string.h>
#include <circle/types.h>

// This header file does not include the SOEM headers, so that it can be used
// by the ZeroMQ client too. See addon/soem/soemcpp.h for the reason.
struct ecx_context;

#define ETHERCAT_MAX_SLAVES	16		// number of slaves in the status
#define ETHERCAT_MAX_IO_BYTES	16		// process data bytes in the status
#define ETHERCAT_MAX_NAME	41		// same as EC_MAXNAME+1

// Size of the process data image. SOEM does not know the size of this buffer,
// when it maps the process data, so it is generously dimensioned here. The
// used size is checked afterwards.
#define ETHERCAT_IOMAP_SIZE	(32 * 1024)

/// \brief State of the EtherCAT master
enum TEtherCATMasterState
{
	EtherCATMasterInit,			///< master is starting up
	EtherCATMasterNoLink,			///< the Ethernet link is down
	EtherCATMasterNoSlaves,			///< no slaves have been found
	EtherCATMasterConfiguring,		///< slaves are being configured
	EtherCATMasterSafeOp,			///< inputs are valid, outputs are not
	EtherCATMasterOperational,		///< process data is exchanged
	EtherCATMasterStopped,			///< bus was stopped by command, parked in SAFE_OP
	EtherCATMasterError,			///< see the error message
	EtherCATMasterUnknown
};

/// \brief Command, which can be submitted from another core (e.g. the ZeroMQ task)
enum TEtherCATCommand
{
	EtherCATCommandNone,
	EtherCATCommandStart,			///< (re-)request the OPERATIONAL state
	EtherCATCommandStop			///< request the SAFE_OP state (outputs stop)
};

/// \brief Information about one EtherCAT slave
struct TEtherCATSlaveInfo
{
	char	 Name[ETHERCAT_MAX_NAME];	///< name from the SII EEPROM
	char	 DeviceName[ETHERCAT_MAX_NAME];	///< CoE object 0x1008 ("" if not available)
	char	 HardwareVersion[ETHERCAT_MAX_NAME];	///< CoE object 0x1009
	char	 SoftwareVersion[ETHERCAT_MAX_NAME];	///< CoE object 0x100A
	u16	 State;				///< AL state
	u16	 ALStatusCode;			///< AL status code
	u16	 ConfigAddress;			///< configured station address
	u16	 AliasAddress;			///< station alias
	u32	 VendorID;			///< from the SII EEPROM
	u32	 ProductCode;
	u32	 RevisionNumber;
	u32	 SerialNumber;
	u16	 OutputBits;			///< size of the outputs of this slave
	u16	 InputBits;			///< size of the inputs of this slave
	u16	 MailboxProtocols;		///< supported mailbox protocols
	boolean	 HasDC;				///< supports distributed clocks
	boolean	 IsLost;			///< does not respond any more
};

/// \brief Status of the EtherCAT master and of the connected slaves
struct TEtherCATStatus
{
	TEtherCATMasterState State;
	char	 InterfaceName[8];		///< net device used (e.g. "eth0")
	char	 MACAddress[20];		///< MAC address of the net device
	char	 LinkSpeed[32];			///< speed of the Ethernet link
	boolean	 LinkUp;

	unsigned nSlaveCount;			///< number of slaves found
	unsigned nSlavesInStatus;		///< number of slaves in the array below
	TEtherCATSlaveInfo Slaves[ETHERCAT_MAX_SLAVES];

	int	 nWorkCounter;			///< work counter of the last cycle
	int	 nExpectedWorkCounter;
	unsigned nCycleTimeUs;			///< requested cycle time
	u64	 nCycles;			///< number of process data cycles
	u64	 nWKCErrors;			///< cycles with an unexpected work counter
	u64	 nTxFrames;			///< frames sent to the net device
	u64	 nTxErrors;			///< frames, which could not be sent
	u64	 nRxFrames;			///< EtherCAT frames received
	u64	 nRxDropped;			///< received frames, which were no EtherCAT frames
	s64	 nDCTime;			///< distributed clock time in ns

	unsigned nOutputBytes;			///< size of the process data outputs
	unsigned nInputBytes;			///< size of the process data inputs
	unsigned nOutputBytesInStatus;		///< number of bytes in the array below
	unsigned nInputBytesInStatus;
	u8	 Outputs[ETHERCAT_MAX_IO_BYTES];
	u8	 Inputs[ETHERCAT_MAX_IO_BYTES];

	unsigned nOperationalSeconds;		///< seconds since the master went operational
	char	 LastError[128];		///< last error message ("" if none)
};

/// \brief EtherCAT master, which uses SOEM on a Circle net device
///
/// Unlike the single-core sample (sample/47-ethercatweb), this class is not a
/// CTask. Its Run() method is called directly from CMultiCoreSupport::Run(1)
/// (see kernel.cpp) and never returns. It must only ever be used from that
/// one core; cross-core access from other cores is limited to GetStatus() and
/// SubmitCommand(), which are the only methods protected by spin locks.
class CEtherCATCore1Master
{
public:
	/// \param nDeviceIndex Zero-based index of the Ethernet device to be used
	/// \param nCycleTimeUs Process data cycle time in microseconds
	CEtherCATCore1Master (unsigned nDeviceIndex = 0, unsigned nCycleTimeUs = 4000);
	~CEtherCATCore1Master (void);

	/// \brief Runs the EtherCAT master loop. Never returns. Call this only
	///	   from the core, which is dedicated to EtherCAT (see kernel.cpp).
	void Run (void);

	/// \brief Get a consistent copy of the current status
	/// \param pStatus The status will be written here
	/// \note May be called from any core.
	void GetStatus (TEtherCATStatus *pStatus);

	/// \brief Submit a Start or Stop command, handled once per cycle by Run()
	/// \note May be called from any core (e.g. the ZeroMQ task on core 0).
	void SubmitCommand (TEtherCATCommand Command);

	static const char *GetMasterStateString (TEtherCATMasterState State);
	static const char *GetSlaveStateString (u16 nState);

private:
	boolean Configure (void);		// scan the bus and go to OPERATIONAL
	void CyclicProcess (void);		// exchange process data
	boolean CheckSlaveStates (void);	// returns FALSE, if reconfiguration is needed
	void ReadSlaveInfo (void);		// read SII and CoE information from the slaves
	void ReadSlaveSDOString (unsigned nSlave, u16 usIndex, char *pString, size_t nSize);
	void UpdateStatus (void);
	void SetError (const char *pMessage);
	void ClearError (void);
	void SetState (TEtherCATMasterState State);

	boolean RequestState (u16 usALState, unsigned nRetries); // request an AL state for all slaves
	TEtherCATCommand FetchAndClearCommand (void);
	void HandleCommand (void);		// called once per cycle from CyclicProcess()

private:
	unsigned m_nDeviceIndex;
	CString	 m_Interface;
	unsigned m_nCycleTimeUs;

	ecx_context *m_pContext;
	u8	*m_pIOMap;
	boolean	 m_bInitialized;		// ecx_init() was successful
	boolean	 m_bConfigured;			// the bus has been configured
	boolean	 m_bRunning;			// FALSE while parked in SAFE_OP by a Stop command

	int	 m_nExpectedWKC;
	int	 m_nWorkCounter;
	u64	 m_nCycles;
	u64	 m_nWKCErrors;
	unsigned m_nOperationalTicks;		// time stamp, when the master went operational

	TEtherCATMasterState m_State;
	CString	 m_LastError;
	CString	 m_LoggedError;			// last message written to the system log

	// the static slave information, which is only read, when the bus is scanned
	TEtherCATSlaveInfo *m_pSlaveInfo;

	CSpinLock m_StatusSpinLock;
	TEtherCATStatus *m_pStatus;

	CSpinLock m_CommandSpinLock;
	TEtherCATCommand m_Command;
};

#endif
