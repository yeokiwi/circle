//
// zmqpublisher.h
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
#ifndef _zeromq_zmqpublisher_h
#define _zeromq_zmqpublisher_h

#include <zeromq/zmqsocket.h>
#include <zeromq/zmqmessage.h>
#include <circle/net/netsubsystem.h>
#include <circle/types.h>

/// \brief ZeroMQ PUB socket
///
/// Distributes messages to all connected subscribers (ZeroMQ SUB or XSUB
/// sockets, e.g. a libzmq/pyzmq SUB socket or an XSUB/XPUB proxy). The
/// subscriptions are filtered at the publisher side: a message is sent to a
/// subscriber only, if one of its subscribed topics is a prefix of the first
/// frame of the message.
///
/// Like with libzmq, messages are not queued, while no subscriber is
/// connected, they are simply dropped.
class CZMQPublisher : public CZMQSocket
{
public:
	/// \param pNetSubSystem Pointer to the network subsystem
	CZMQPublisher (CNetSubSystem *pNetSubSystem);
	~CZMQPublisher (void);

	/// \brief Publish a multipart message
	/// \param rMessage The message, the first frame is the topic
	/// \return Number of subscribers, to which the message has been sent
	unsigned Publish (const CZMQMessage &rMessage);

	/// \brief Publish a two-part message [topic, data]
	/// \param pTopic Topic (first frame)
	/// \param pData Data (second frame)
	/// \param nLength Length of the data in bytes
	/// \return Number of subscribers, to which the message has been sent
	unsigned Publish (const char *pTopic, const void *pData, size_t nLength);

	/// \brief Publish a two-part message [topic, string]
	/// \param pTopic Topic (first frame)
	/// \param pString C string (second frame, without the terminating null byte)
	/// \return Number of subscribers, to which the message has been sent
	unsigned Publish (const char *pTopic, const char *pString);

	/// \return TRUE, if at least one subscriber would receive a message with this topic
	boolean HasSubscribers (const char *pTopic);

private:
	void OnPeerMessage (unsigned nPeer, CZMQMessage &rMessage) override;
	void OnPeerRemoved (unsigned nPeer) override;

private:
	CZMQTopicSet m_Subscriptions[ZMQ_MAX_PEERS];
};

#endif
