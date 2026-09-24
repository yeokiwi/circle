//
// gvspreceiver.h
//
// GigE Vision Stream Protocol (GVSP) receiver, reassembles blocks (frames)
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
#ifndef _gigevision_gvspreceiver_h
#define _gigevision_gvspreceiver_h

#include <gigevision/gvcp.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/netdevice.h>
#include <circle/types.h>

class CGVCPClient;

struct TGVSPFrameInfo		/// Information about a received block
{
	u64	BlockID;
	u16	PayloadType;		// GVSP_PAYLOAD_*
	u64	Timestamp;		// device timestamp ticks
	u32	PixelFormat;		// image payload only (GVSP_PIX_*)
	u32	SizeX;
	u32	SizeY;
	u32	OffsetX;
	u32	OffsetY;
	u16	PaddingX;
	u16	PaddingY;
	size_t	DataSize;		// bytes of payload data written to the buffer
	boolean	Complete;		// all packets (leader, payload, trailer) received?
	unsigned MissingPackets;	// number of payload packets not received
	unsigned ResentPackets;		// number of packets received due to resend requests
};

struct TGVSPStatistics
{
	unsigned CompleteFrames;
	unsigned IncompleteFrames;
	unsigned Packets;
	unsigned MissingPackets;
	unsigned ResendRequests;
	unsigned ResentPackets;
	unsigned IgnoredPackets;
};

class CGVSPReceiver	/// Receives the stream of one stream channel
{
public:
	/// \param pNetSubSystem Network subsystem to be used
	/// \param pResendClient GVCP client used to request lost packets (0 to disable resends)
	/// \param nStreamChannel Stream channel index (for resend requests)
	CGVSPReceiver (CNetSubSystem *pNetSubSystem, CGVCPClient *pResendClient = 0,
		       unsigned nStreamChannel = 0);
	~CGVSPReceiver (void);

	/// \brief Open the receive socket
	/// \param nPort UDP port (0 to use an ephemeral port)
	/// \return Operation successful?
	boolean Initialize (u16 nPort = 0);

	/// \return Local UDP port, which has to be written to the SCP register of the device
	u16 GetPort (void) const;

	/// \brief Set the packet size configured in the device (SCPS register)
	/// \param nPacketSize Size of the IP datagram (including IP, UDP and GVSP headers)
	void SetPacketSize (unsigned nPacketSize);

	/// \brief Configure resend handling
	/// \param bEnable Request lost packets with PACKETRESEND_CMD
	/// \param nTimeoutMs Time to wait for resent packets after the trailer was received
	void SetResend (boolean bEnable, unsigned nTimeoutMs = 50);

	/// \brief Wait for and receive the next block
	/// \param pInfo Information about the block is returned here
	/// \param pBuffer Payload data is written here
	/// \param nBufferSize Size of the buffer (should be at least the PayloadSize of the device)
	/// \param nTimeoutMs Maximum time to wait
	/// \return TRUE if a block was received (check pInfo->Complete), FALSE on timeout
	boolean ReceiveFrame (TGVSPFrameInfo *pInfo, void *pBuffer, size_t nBufferSize,
			      unsigned nTimeoutMs = 1000);

	/// \brief Drop all packets currently queued on the socket and the partial block
	void Flush (void);

	const TGVSPStatistics &GetStatistics (void) const	{ return m_Stats; }

private:
	enum TPacketResult
	{
		PacketContinue,		// packet processed, block not finished
		PacketFrameDone,	// block finished with this packet
		PacketPushedBack	// block finished, packet belongs to the next block
	};

	TPacketResult ProcessPacket (const u8 *pPacket, unsigned nLength,
				     TGVSPFrameInfo *pInfo, u8 *pBuffer, size_t nBufferSize);

	void StartBlock (u64 nBlockID, size_t nBufferSize, TGVSPFrameInfo *pInfo);
	void FinishBlock (TGVSPFrameInfo *pInfo);
	boolean IsBlockComplete (void) const;
	void RequestResends (void);
	boolean IsNewerBlock (u64 nBlockID) const;

private:
	CNetSubSystem *m_pNetSubSystem;
	CGVCPClient *m_pResendClient;
	unsigned m_nStreamChannel;

	CSocket *m_pSocket;

	unsigned m_nPacketSize;
	boolean m_bResendEnabled;
	unsigned m_nResendTimeoutMs;

	// current block
	boolean m_bActive;
	boolean m_bExtendedID;
	u64 m_nBlockID;
	boolean m_bLeaderReceived;
	boolean m_bTrailerReceived;
	boolean m_bOverflow;
	u32 m_nLastPacketID;		// last payload packet ID (known from trailer)
	unsigned m_nReceivedPackets;	// distinct payload packets received
	unsigned m_nResentPackets;
	boolean m_bResendRequested;
	unsigned m_nPayloadPerPacket;

	u8 *m_pBitmap;			// received payload packets
	unsigned m_nBitmapPackets;	// capacity of the bitmap in packets

	boolean m_bHaveFinished;
	u64 m_nLastFinishedBlockID;

	boolean m_bPushBack;		// m_Packet holds an unprocessed packet
	unsigned m_nPacketLength;
	u8 m_Packet[FRAME_BUFFER_SIZE];

	TGVSPStatistics m_Stats;
};

#endif
