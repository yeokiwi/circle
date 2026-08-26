//
// zmtp.h
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
#ifndef _zmtp_h
#define _zmtp_h

#include <circle/net/socket.h>
#include <circle/types.h>

#define ZMTP_MAX_FRAMES		4	// max frames handled per multipart message

// Size of the receive staging buffer. CSocket::Receive() returns one whole
// TCP segment and discards whatever does not fit into the caller's buffer
// (see lib/net/socket.cpp), so the socket is always read with a buffer big
// enough for a full segment and the bytes are handed out from there. Must
// be at least the MSS of the link (1460 bytes for a 1500 byte MTU).
#define ZMTP_RX_STAGE_SIZE	2048

/// \brief Minimal ZMTP 3.0 (NULL security mechanism) client, DEALER socket type
///
/// This implements just enough of the ZMTP 3.0 wire protocol (see RFC 23,
/// https://rfc.zeromq.org/spec/23/) to complete the handshake with a
/// ROUTER-based broker as a DEALER peer and to exchange plain multipart
/// messages. It is intentionally minimal:
///  - Only the NULL security mechanism is supported (no PLAIN/CURVE).
///  - Only the DEALER role is implemented (no SUBSCRIBE/CANCEL handling, so
///    it will not interoperate with a PUB/SUB or XPUB/XSUB proxy broker).
///  - Frames are limited to a moderate size, sufficient for the small JSON
///    status/command payloads used by this sample.
/// The hand-written greeting has been checked against the RFC, but was not
/// validated against a packet capture of a real libzmq peer; verify it
/// against your actual broker before relying on it in production.
class CZMTPConnection
{
public:
	/// \param pSocket A connected TCP CSocket, owned by the caller
	CZMTPConnection (CSocket *pSocket);
	~CZMTPConnection (void);

	/// \brief Perform the ZMTP greeting and READY command exchange
	/// \return FALSE on a protocol error, a timeout, or an unsupported
	///	    mechanism offered by the peer
	boolean Handshake (void);

	/// \brief Send a multipart application message
	/// \param ppFrame Array of pointers to the frame payloads
	/// \param pLength Array of frame lengths
	/// \param nFrames Number of frames (1..ZMTP_MAX_FRAMES)
	/// \return FALSE on a socket error
	boolean SendMessage (const void *const *ppFrame, const size_t *pLength, unsigned nFrames);

	/// \brief Try to receive one multipart application message (non-blocking)
	///
	/// May be called repeatedly (e.g. once per client loop iteration); it
	/// keeps its progress between calls, so a message, which arrives in
	/// several TCP segments, is assembled over multiple calls without
	/// blocking the caller.
	///
	/// \param ppFrame    On success, ppFrame[i] receives a newly allocated
	///		      buffer with the frame payload (caller must delete [] it)
	/// \param pLength    On success, pLength[i] receives the frame length
	/// \param nMaxFrames Size of the ppFrame/pLength arrays
	/// \return Number of received frames (> 0), 0 if no complete message
	///	    is available yet, or < 0 if the connection is broken
	int ReceiveMessage (u8 *ppFrame[], size_t pLength[], unsigned nMaxFrames);

private:
	boolean SendAll (const void *pBuffer, size_t nLength);
	boolean ReceiveExact (void *pBuffer, size_t nLength);	// blocking, with timeout

	// Drop-in replacement for m_pSocket->Receive(), which never loses the
	// tail of a TCP segment. Same return convention: number of bytes read,
	// 0 if nothing is available (bDontWait) or the timeout expired, < 0 on
	// error.
	int ReadBytes (void *pBuffer, size_t nLength, boolean bDontWait);
	int FillStage (boolean bDontWait);	// refill m_RxStage from the socket

	boolean SendFrame (const void *pBuffer, size_t nLength, boolean bMore, boolean bCommand);
	boolean ReceiveFrameBlocking (u8 *pBuffer, size_t nBufferSize, size_t *pLength,
				      boolean *pbCommand);	// used by Handshake() only

	void StartBody (void);		// (re)allocates m_pRxBody for m_nRxBodyLen bytes

private:
	CSocket *m_pSocket;

	// staging buffer for all socket reads, see ZMTP_RX_STAGE_SIZE above
	u8	 m_RxStage[ZMTP_RX_STAGE_SIZE];
	unsigned m_nStageHead;
	unsigned m_nStageTail;

	enum TRxPhase { RxPhaseFlags, RxPhaseLenShort, RxPhaseLenLong, RxPhaseBody };

	TRxPhase m_RxPhase;
	u8	 m_RxFlags;
	u8	 m_RxLenBuf[8];
	unsigned m_nRxLenGot;
	u64	 m_nRxBodyLen;
	u8	*m_pRxBody;
	unsigned m_nRxBodyGot;

	// frames of the multipart message, which is currently being assembled
	u8	*m_ppPendingFrame[ZMTP_MAX_FRAMES];
	size_t	 m_nPendingFrameLength[ZMTP_MAX_FRAMES];
	unsigned m_nPendingFrames;
};

#endif
