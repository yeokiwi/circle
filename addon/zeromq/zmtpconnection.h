//
// zmtpconnection.h
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
#ifndef _zeromq_zmtpconnection_h
#define _zeromq_zmtpconnection_h

#include <zeromq/zmqmessage.h>
#include <circle/net/socket.h>
#include <circle/netdevice.h>
#include <circle/types.h>

#define ZMQ_MAX_FRAME_SIZE	0x40000		///< Largest accepted frame (256 KByte)

#define ZMTP_SOCKET_TYPE_SIZE	16

enum TZMTPEvent
{
	ZMTPEventNone,		///< Nothing (more) to do at the moment
	ZMTPEventReady,		///< Handshake completed, peer socket type is known
	ZMTPEventMessage,	///< A complete message has been received
	ZMTPEventError		///< Connection closed or protocol error, delete it
};

/// \brief One ZMTP 3.0 connection (NULL security mechanism) over a TCP socket
///
/// This class implements the wire protocol of ZeroMQ (see RFC 23,
/// https://rfc.zeromq.org/spec/23/ and RFC 37 for the ZMTP 3.1 commands). It
/// is used by CZMQPublisher and CZMQSubscriber and normally not directly by
/// applications.
///
/// All receive operations are non-blocking, so that one task can serve many
/// connections. Call Poll() periodically: it drives the handshake (greeting
/// and READY command exchange), answers PING commands and returns the
/// received messages. The ZMTP 3.1 commands SUBSCRIBE and CANCEL are
/// returned as ZMTP 3.0 subscription messages (one frame, starting with the
/// byte 1 or 0, followed by the topic), so that a publisher has to handle one
/// format only.
class CZMTPConnection
{
public:
	/// \param pSocket     A connected TCP socket (ownership is taken over)
	/// \param pSocketType Our own ZeroMQ socket type (e.g. "PUB" or "SUB")
	CZMTPConnection (CSocket *pSocket, const char *pSocketType);
	~CZMTPConnection (void);

	/// \brief Send our greeting, must be called once before Poll()
	/// \return FALSE on socket error
	boolean Start (void);

	/// \brief Process received data (non-blocking)
	/// \param pMessage Receives a message with ZMTPEventMessage (cleared before)
	/// \return Event, call again until ZMTPEventNone is returned
	TZMTPEvent Poll (CZMQMessage *pMessage);

	/// \return TRUE, if the handshake has been completed
	boolean IsReady (void) const;

	/// \return TRUE, if the connection is broken and should be deleted
	boolean IsFailed (void) const;

	/// \return Socket type sent by the peer in the READY command ("" before)
	const char *GetPeerSocketType (void) const;

	/// \return Timer ticks (CTimer::GetTicks(), HZ) at the time of Start()
	unsigned GetStartTicks (void) const;

	/// \brief Send a message (must be ready)
	/// \return FALSE on error (the connection is marked as failed then)
	/// \note The message is sent in one piece, so that it cannot be
	///	  interleaved with messages sent by other tasks.
	boolean Send (const CZMQMessage &rMessage);

	/// \brief Set the timeout for sending (default 0, wait forever)
	/// \note The TCP stack blocks the sender only, if more than 64 KByte
	///	  are queued already (i.e. for a slow or stalled peer).
	void SetSendTimeout (unsigned nMicroSeconds);

private:
	boolean SendCommand (const char *pName, const void *pData, size_t nLength);
	boolean SendRaw (const void *pBuffer, size_t nLength);

	// read up to nLength bytes (non-blocking), < 0 on error
	int Read (void *pBuffer, size_t nLength);

	// returns ZMTPEventNone, if the frame has been consumed internally
	TZMTPEvent FrameReceived (CZMQMessage *pMessage);
	TZMTPEvent CommandReceived (CZMQMessage *pMessage);
	boolean ParseReady (const u8 *pBody, size_t nLength);

	void ResetFrame (void);

private:
	CSocket *m_pSocket;
	char	 m_SocketType[ZMTP_SOCKET_TYPE_SIZE];
	char	 m_PeerSocketType[ZMTP_SOCKET_TYPE_SIZE];

	enum TState
	{
		StateInit,
		StateGreeting,
		StateReady,
		StateActive,
		StateFailed
	};

	TState	 m_State;
	unsigned m_nStartTicks;

	u8	 m_PeerGreeting[64];
	unsigned m_nGreetingGot;

	// frame receiver
	enum TRxPhase
	{
		RxPhaseFlags,
		RxPhaseLength,
		RxPhaseBody
	};

	TRxPhase m_RxPhase;
	u8	 m_uchRxFlags;
	u8	 m_RxLength[8];
	unsigned m_nRxLengthSize;
	unsigned m_nRxLengthGot;
	size_t	 m_nRxBodyLength;
	size_t	 m_nRxBodyGot;
	u8	*m_pRxBody;

	CZMQMessage m_RxMessage;	// message being assembled
	boolean  m_bRxDiscard;		// current message is too big, drop it

	// staging buffer: CSocket::Receive() returns a whole TCP segment and
	// discards, what does not fit into the buffer, so it must be big enough
	u8	 m_RxStage[FRAME_BUFFER_SIZE];
	unsigned m_nStageHead;
	unsigned m_nStageTail;
};

#endif
