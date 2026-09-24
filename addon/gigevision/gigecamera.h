//
// gigecamera.h
//
// High-level access to a GigE Vision camera: control channel with heartbeat,
// GenICam feature access and image streaming
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
#ifndef _gigevision_gigecamera_h
#define _gigevision_gigecamera_h

#include <gigevision/gvcpclient.h>
#include <gigevision/gvspreceiver.h>
#include <gigevision/genicam.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/ipaddress.h>
#include <circle/string.h>
#include <circle/types.h>

class CGigEHeartbeatTask;

class CGigECamera	/// A GigE Vision camera, controlled by this application
{
public:
	/// \param pNetSubSystem Network subsystem to be used
	CGigECamera (CNetSubSystem *pNetSubSystem);
	~CGigECamera (void);

	/// \brief Open the control channel and take control privilege
	/// \param rDeviceIP IP address of the camera
	/// \param bExclusive Request exclusive access (no other application can read)
	/// \return Operation successful?
	boolean Open (const CIPAddress &rDeviceIP, boolean bExclusive = FALSE);
	/// \brief Stop streaming, release control privilege and close the control channel
	void Close (void);

	boolean IsOpen (void) const		{ return m_bOpen; }

	/// \brief Set the heartbeat timeout in the device (default is device specific, often 3000ms)
	boolean SetHeartbeatTimeout (unsigned nTimeoutMs);

	/// \brief Read identification strings from the bootstrap registers
	boolean GetManufacturerName (CString *pString);
	boolean GetModelName (CString *pString);
	boolean GetDeviceVersion (CString *pString);
	boolean GetSerialNumber (CString *pString);
	boolean GetUserDefinedName (CString *pString);

	/// \brief Download the GenICam XML description from the device and parse it
	/// \return Operation successful?
	/// \note Only "Local:" URLs are supported (the file is stored in the device).
	boolean LoadDescription (void);
	/// \brief Use a GenICam XML description, which was loaded from elsewhere
	/// \param pXML XML text (ownership is taken, allocated with new [nLength+1])
	boolean LoadDescription (char *pXML, size_t nLength);

	/// \return Node map for feature access (0 if description is not loaded)
	CGenICamNodeMap *GetNodeMap (void)	{ return m_pNodeMap; }

	/// \return Raw register access
	CGVCPClient *GetControl (void)		{ return &m_GVCP; }

	/// \brief Configure a stream channel to send to this host
	/// \param nChannel Stream channel index
	/// \param nPacketSize Requested packet size (IP datagram size, max. 1500 w/o jumbo frames)
	/// \return Operation successful?
	boolean OpenStream (unsigned nChannel = 0, unsigned nPacketSize = 1500);
	void CloseStream (void);

	/// \brief Set the delay between stream packets (SCPD register)
	/// \param nDelay Delay in timestamp ticks of the device (see GEV_REG_TIMESTAMP_FREQ_*)
	/// \note Increase this, if many packets get lost, because the receiver is too slow.
	boolean SetPacketDelay (u32 nDelay);

	/// \return Stream receiver (0 if stream is not open)
	CGVSPReceiver *GetStream (void)		{ return m_pStream; }

	/// \brief Lock transport layer parameters and execute "AcquisitionStart"
	boolean StartAcquisition (void);
	/// \brief Execute "AcquisitionStop" and unlock transport layer parameters
	boolean StopAcquisition (void);

	/// \return Size of the payload of one frame in bytes (0 on error)
	size_t GetPayloadSize (void);

private:
	boolean ParseURL (const char *pURL, CString *pFileName, u32 *pAddress, u32 *pLength);

private:
	CNetSubSystem *m_pNetSubSystem;
	CGVCPClient m_GVCP;
	boolean m_bOpen;

	CGigEHeartbeatTask *m_pHeartbeat;

	CGenICamNodeMap *m_pNodeMap;

	CGVSPReceiver *m_pStream;
	unsigned m_nStreamChannel;

	boolean m_bAcquiring;
};

#endif
