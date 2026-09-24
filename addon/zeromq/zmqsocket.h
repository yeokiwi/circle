//
// zmqsocket.h
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
#ifndef _zeromq_zmqsocket_h
#define _zeromq_zmqsocket_h

#include <zeromq/zmtpconnection.h>
#include <zeromq/zmqmessage.h>
#include <circle/sched/task.h>
#include <circle/sched/mutex.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/ipaddress.h>
#include <circle/string.h>
#include <circle/types.h>

#define ZMQ_MAX_PEERS		8	///< Maximum number of simultaneous connections
#define ZMQ_MAX_ENDPOINTS	4	///< Maximum number of Bind() plus Connect() calls

#define ZMQ_RECONNECT_MS	2000	///< Delay before a lost connection is re-established
#define ZMQ_HANDSHAKE_MS	5000	///< Maximum duration of the ZMTP handshake
#define ZMQ_SEND_TIMEOUT_MS	1000	///< Default send timeout (see SetSendTimeout())
#define ZMQ_POLL_INTERVAL_US	1000	///< Receive polling interval of the I/O task

class CZMQEndpointTask;

/// \brief Base class of the ZeroMQ sockets (CZMQPublisher, CZMQSubscriber)
///
/// A ZeroMQ socket is a CTask, which serves up to ZMQ_MAX_PEERS connections
/// to peers. Like with libzmq, it can Bind() to a TCP port (peers connect to
/// it) and/or Connect() to one or more remote endpoints. Outgoing
/// connections are re-established automatically, when they get lost.
///
/// All methods must be called on core 0 (the Circle network stack and
/// scheduler are not multi-core safe, see doc/multicore.txt).
class CZMQSocket : public CTask
{
public:
	/// \param pNetSubSystem Pointer to the network subsystem
	/// \param pSocketType	 Our ZeroMQ socket type (e.g. "PUB")
	/// \param ppPeerTypes	 0-terminated list of accepted peer socket types
	CZMQSocket (CNetSubSystem *pNetSubSystem, const char *pSocketType,
		    const char *const *ppPeerTypes);
	~CZMQSocket (void);

	/// \brief Accept connections from peers on a TCP port (e.g. "tcp://*:5556")
	/// \param nPort TCP port number
	/// \return FALSE on failure
	boolean Bind (u16 nPort);

	/// \brief Connect to a remote endpoint (e.g. "tcp://192.168.0.10:5556")
	/// \param pHost Host name or IP address (dotted string)
	/// \param nPort TCP port number
	/// \return FALSE, if too many endpoints are defined
	/// \note Connecting is done in the background and repeated, until it
	///	  succeeds. It is re-established, when the connection gets lost.
	boolean Connect (const char *pHost, u16 nPort);

	/// \return Number of peers, which completed the handshake
	unsigned GetPeerCount (void);

	/// \brief Set the timeout for sending to a peer
	/// \param nMilliSeconds Timeout (0 to wait forever)
	/// \note A peer, which does not accept data within this time, while
	///	  more than 64 KByte are queued for it already, is disconnected.
	void SetSendTimeout (unsigned nMilliSeconds);

	void Run (void) override;

protected:
	/// \brief Called, when a peer completed the handshake
	virtual void OnPeerReady (unsigned nPeer) {}

	/// \brief Called for each message received from a peer
	virtual void OnPeerMessage (unsigned nPeer, CZMQMessage &rMessage) = 0;

	/// \brief Called, before a ready peer is removed
	virtual void OnPeerRemoved (unsigned nPeer) {}

	/// \return Connection of the peer with this index, if it is ready, or 0
	/// \note Must be called with m_Mutex acquired
	CZMTPConnection *GetReadyPeer (unsigned nPeer);

	/// \brief Send a message to one peer (must be called with m_Mutex acquired)
	boolean SendToPeer (unsigned nPeer, const CZMQMessage &rMessage);

protected:
	CMutex m_Mutex;		///< protects the peer table, must be held while sending

private:
	friend class CZMQEndpointTask;
	void AddPeer (CSocket *pSocket, int nEndpoint, const CIPAddress &rIPAddress, u16 nPort);

	boolean IsEndpointConnected (unsigned nEndpoint) const;
	void PollPeers (void);
	void RemovePeer (unsigned nPeer);

	boolean IsAcceptedPeerType (const char *pType) const;

private:
	CNetSubSystem *m_pNetSubSystem;
	const char *m_pSocketType;
	const char *const *m_ppPeerTypes;
	unsigned m_nSendTimeoutMs;

	struct TPeer
	{
		CZMTPConnection	*pConnection;
		int		 nEndpoint;	// index into m_Endpoint[], -1 for accepted peers
		boolean		 bReady;	// OnPeerReady() has been called
		CString		 Name;		// "ip:port" for log messages
	};

	TPeer m_Peer[ZMQ_MAX_PEERS];

	struct TEndpoint
	{
		boolean		  bConnected;	// Connect(): a peer in m_Peer[] belongs to it
		CZMQEndpointTask *pTask;	// accepts or (re-)connects in the background
	};

	TEndpoint m_Endpoint[ZMQ_MAX_ENDPOINTS];
	unsigned m_nEndpoints;
};

#endif
