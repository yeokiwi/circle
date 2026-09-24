//
// gvcpclient.h
//
// GigE Vision Control Protocol (GVCP) client (application side)
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
#ifndef _gigevision_gvcpclient_h
#define _gigevision_gvcpclient_h

#include <gigevision/gvcp.h>
#include <gigevision/genicamport.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/net/ipaddress.h>
#include <circle/netdevice.h>
#include <circle/sched/mutex.h>
#include <circle/string.h>
#include <circle/types.h>

#define GVCP_DEFAULT_TIMEOUT_MS		200	// per try
#define GVCP_DEFAULT_RETRIES		3

struct TGigEDeviceInfo		/// Information about a device, returned from discovery
{
	u16	SpecVersionMajor;
	u16	SpecVersionMinor;
	u32	DeviceMode;
	u8	MACAddress[6];
	u32	IPConfigOptions;
	u32	IPConfigCurrent;
	CIPAddress IPAddress;
	CIPAddress SubnetMask;
	CIPAddress DefaultGateway;
	char	ManufacturerName[33];
	char	ModelName[33];
	char	DeviceVersion[33];
	char	ManufacturerInfo[49];
	char	SerialNumber[17];
	char	UserDefinedName[17];
};

class CGVCPClient : public CGenICamPort	/// GVCP control channel to one GigE Vision device
{
public:
	/// \param pNetSubSystem Network subsystem to be used (e.g. CNetSubSystem::Get ())
	CGVCPClient (CNetSubSystem *pNetSubSystem);
	~CGVCPClient (void);

	/// \brief Open the control channel to a device
	/// \param rDeviceIP IP address of the device
	/// \return Operation successful?
	/// \note This does not request control privilege, use CGigECamera for that.
	boolean Open (const CIPAddress &rDeviceIP);
	void Close (void);

	boolean IsOpen (void) const		{ return m_pSocket != 0; }
	const CIPAddress &GetDeviceIP (void) const	{ return m_DeviceIP; }

	/// \brief Set timing of the command/acknowledge handshake
	/// \param nTimeoutMs Time to wait for an acknowledge per try
	/// \param nRetries Number of retries after the first try
	void SetTimeout (unsigned nTimeoutMs, unsigned nRetries = GVCP_DEFAULT_RETRIES);

	/// \return Status code of the last acknowledge received (GVCP_STATUS_*)
	u16 GetLastStatus (void) const		{ return m_nLastStatus; }

	boolean ReadRegister (u32 nAddress, u32 *pValue);
	boolean WriteRegister (u32 nAddress, u32 nValue);
	/// \brief Read multiple registers with one command (up to GVCP_MAX_READREG)
	boolean ReadRegisters (const u32 *pAddresses, u32 *pValues, unsigned nCount);

	/// \brief Read device memory (any length, split into multiple READMEM commands)
	/// \note nAddress and nLength do not need to be multiples of 4
	boolean ReadMemory (u32 nAddress, void *pBuffer, unsigned nLength);
	/// \brief Write device memory (WRITEMEM is optional for devices)
	boolean WriteMemory (u32 nAddress, const void *pBuffer, unsigned nLength);

	/// \brief Read a 0-terminated string from bootstrap memory
	boolean ReadString (u32 nAddress, unsigned nMaxLength, CString *pString);

	/// \brief Send a PACKETRESEND_CMD (no acknowledge is expected)
	boolean RequestResend (unsigned nStreamChannel, u16 nBlockID,
			       u32 nFirstPacketID, u32 nLastPacketID);

	// CGenICamPort
	boolean Read (u64 nAddress, void *pBuffer, unsigned nLength) override;
	boolean Write (u64 nAddress, const void *pBuffer, unsigned nLength) override;

	/// \brief Broadcast a DISCOVERY_CMD and collect the answers
	/// \param pNetSubSystem Network subsystem to be used
	/// \param pInfo Array receiving the device information
	/// \param nMaxDevices Size of the pInfo array
	/// \param nTimeoutMs Time to wait for answers
	/// \return Number of devices found (< 0 on error)
	static int Discover (CNetSubSystem *pNetSubSystem, TGigEDeviceInfo *pInfo,
			     unsigned nMaxDevices, unsigned nTimeoutMs = 1000);

	/// \brief Assign a (temporary) IP address to a device, identified by its MAC address
	/// \param pNetSubSystem Network subsystem to be used
	/// \param pMACAddress MAC address of the device (6 bytes)
	/// \param rIP New IP address
	/// \param rSubnetMask New subnet mask
	/// \param rGateway New default gateway (may be 0.0.0.0)
	/// \return Operation successful (acknowledge received)?
	static boolean ForceIP (CNetSubSystem *pNetSubSystem, const u8 *pMACAddress,
				const CIPAddress &rIP, const CIPAddress &rSubnetMask,
				const CIPAddress &rGateway);

	/// \return Text for a GVCP status code
	static const char *GetStatusText (u16 nStatus);

private:
	// Send a command and wait for its acknowledge
	// pAckPayload receives the payload of the ack, *pAckLength its length
	boolean Transaction (u16 nCommand, const void *pPayload, unsigned nPayloadLength,
			     void *pAckPayload, unsigned nAckBufferSize, unsigned *pAckLength);

	boolean ReadMemoryChunk (u32 nAddress, void *pBuffer, unsigned nLength);
	boolean WriteMemoryChunk (u32 nAddress, const void *pBuffer, unsigned nLength);

	u16 NextReqID (void);

	static void ParseDiscoveryAck (const TGVCPDiscoveryAck *pAck, TGigEDeviceInfo *pInfo);

private:
	CNetSubSystem *m_pNetSubSystem;
	CSocket *m_pSocket;
	CIPAddress m_DeviceIP;

	unsigned m_nTimeoutMs;
	unsigned m_nRetries;

	u16 m_nReqID;
	u16 m_nLastStatus;

	CMutex m_Mutex;		// serializes transactions (e.g. from heartbeat task)

	u8 m_TxBuffer[FRAME_BUFFER_SIZE];
	u8 m_RxBuffer[FRAME_BUFFER_SIZE];
};

#endif
