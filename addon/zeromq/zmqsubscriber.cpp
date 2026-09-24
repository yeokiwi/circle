//
// zmqsubscriber.cpp
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
#include <zeromq/zmqsubscriber.h>
#include <circle/util.h>
#include <assert.h>

static const char *const s_PeerTypes[] = {"PUB", "XPUB", 0};

CZMQSubscriber::CZMQSubscriber (CNetSubSystem *pNetSubSystem)
:	CZMQSocket (pNetSubSystem, "SUB", s_PeerTypes),
	m_pHandler (0),
	m_pHandlerParam (0)
{
}

CZMQSubscriber::~CZMQSubscriber (void)
{
	m_pHandler = 0;
}

boolean CZMQSubscriber::Subscribe (const char *pTopic)
{
	assert (pTopic != 0);

	return Subscribe (pTopic, strlen (pTopic));
}

boolean CZMQSubscriber::Subscribe (const void *pTopic, size_t nLength)
{
	m_Mutex.Acquire ();

	// the publishers get each topic only once, the reference count is
	// maintained here, so that they stay in sync after a reconnect
	boolean bNew;
	boolean bOK = m_Subscriptions.Add (pTopic, nLength, &bNew);
	if (bNew)
	{
		for (unsigned nPeer = 0; nPeer < ZMQ_MAX_PEERS; nPeer++)
		{
			SendSubscription (nPeer, TRUE, pTopic, nLength);
		}
	}

	m_Mutex.Release ();

	return bOK;
}

boolean CZMQSubscriber::Unsubscribe (const char *pTopic)
{
	assert (pTopic != 0);

	return Unsubscribe (pTopic, strlen (pTopic));
}

boolean CZMQSubscriber::Unsubscribe (const void *pTopic, size_t nLength)
{
	m_Mutex.Acquire ();

	boolean bDeleted;
	boolean bOK = m_Subscriptions.Remove (pTopic, nLength, &bDeleted);
	if (bDeleted)
	{
		for (unsigned nPeer = 0; nPeer < ZMQ_MAX_PEERS; nPeer++)
		{
			SendSubscription (nPeer, FALSE, pTopic, nLength);
		}
	}

	m_Mutex.Release ();

	return bOK;
}

void CZMQSubscriber::RegisterMessageHandler (TZMQMessageHandler *pHandler, void *pParam)
{
	m_pHandlerParam = pParam;
	m_pHandler = pHandler;
}

void CZMQSubscriber::OnMessage (const CZMQMessage &rMessage)
{
	if (m_pHandler != 0)
	{
		(*m_pHandler) (rMessage, m_pHandlerParam);
	}
}

void CZMQSubscriber::OnPeerReady (unsigned nPeer)
{
	// send all subscriptions to the new publisher (each topic once)
	unsigned nCount = m_Subscriptions.GetCount ();
	for (unsigned i = 0; i < nCount; i++)
	{
		size_t nLength;
		const u8 *pTopic = m_Subscriptions.GetTopic (i, &nLength);

		SendSubscription (nPeer, TRUE, pTopic, nLength);
	}
}

void CZMQSubscriber::OnPeerMessage (unsigned nPeer, CZMQMessage &rMessage)
{
	// filter here too, because an XPUB peer may send everything
	if (   rMessage.GetFrameCount () == 0
	    || !m_Subscriptions.Matches (rMessage.GetFrame (0), rMessage.GetFrameSize (0)))
	{
		return;
	}

	OnMessage (rMessage);
}

void CZMQSubscriber::SendSubscription (unsigned nPeer, boolean bSubscribe,
				       const void *pTopic, size_t nLength)
{
	if (GetReadyPeer (nPeer) == 0)
	{
		return;
	}

	u8 *pBuffer = new u8[1 + nLength];
	if (pBuffer == 0)
	{
		return;
	}

	pBuffer[0] = bSubscribe ? 1 : 0;
	if (nLength > 0)
	{
		memcpy (&pBuffer[1], pTopic, nLength);
	}

	CZMQMessage Message;
	Message.AddFrameOwned (pBuffer, 1 + nLength);

	SendToPeer (nPeer, Message);
}
