//
// zmqsubscriber.h
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
#ifndef _zeromq_zmqsubscriber_h
#define _zeromq_zmqsubscriber_h

#include <zeromq/zmqsocket.h>
#include <zeromq/zmqmessage.h>
#include <circle/net/netsubsystem.h>
#include <circle/types.h>

typedef void TZMQMessageHandler (const CZMQMessage &rMessage, void *pParam);

/// \brief ZeroMQ SUB socket
///
/// Receives messages from publishers (ZeroMQ PUB or XPUB sockets, e.g. a
/// libzmq/pyzmq PUB socket or an XSUB/XPUB proxy), which first frame starts
/// with one of the subscribed topics. Without any subscription, no message
/// is received. Subscribe to "" to receive all messages.
///
/// Received messages are delivered from the task of this socket, either by
/// overriding OnMessage() or by registering a message handler.
class CZMQSubscriber : public CZMQSocket
{
public:
	/// \param pNetSubSystem Pointer to the network subsystem
	CZMQSubscriber (CNetSubSystem *pNetSubSystem);
	~CZMQSubscriber (void);

	/// \brief Subscribe to all messages, which first frame starts with pTopic
	/// \param pTopic Topic prefix ("" for all messages)
	/// \return FALSE on insufficient memory
	boolean Subscribe (const char *pTopic);
	/// \brief Subscribe to a binary topic prefix
	boolean Subscribe (const void *pTopic, size_t nLength);

	/// \brief Remove a subscription, which has been added before
	/// \return FALSE, if the topic was not subscribed
	boolean Unsubscribe (const char *pTopic);
	/// \brief Remove a binary subscription, which has been added before
	boolean Unsubscribe (const void *pTopic, size_t nLength);

	/// \brief Register a function, which is called for each received message
	/// \param pHandler Handler function (0 to unregister)
	/// \param pParam Any parameter, handed over to the handler
	/// \note The handler is called from the task of this socket. It should
	///	  return quickly, because no data is received, while it runs.
	void RegisterMessageHandler (TZMQMessageHandler *pHandler, void *pParam = 0);

protected:
	/// \brief Called for each received message, calls the registered handler
	/// \note Override this in a derived class, as an alternative to
	///	  RegisterMessageHandler().
	virtual void OnMessage (const CZMQMessage &rMessage);

private:
	void OnPeerReady (unsigned nPeer) override;
	void OnPeerMessage (unsigned nPeer, CZMQMessage &rMessage) override;

	// sends a ZMTP 3.0 subscription message to one peer
	void SendSubscription (unsigned nPeer, boolean bSubscribe, const void *pTopic, size_t nLength);

private:
	CZMQTopicSet m_Subscriptions;

	TZMQMessageHandler *m_pHandler;
	void *m_pHandlerParam;
};

#endif
