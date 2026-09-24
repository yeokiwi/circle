//
// zmqpublisher.cpp
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
#include <zeromq/zmqpublisher.h>
#include <circle/util.h>
#include <assert.h>

static const char *const s_PeerTypes[] = {"SUB", "XSUB", 0};

CZMQPublisher::CZMQPublisher (CNetSubSystem *pNetSubSystem)
:	CZMQSocket (pNetSubSystem, "PUB", s_PeerTypes)
{
}

CZMQPublisher::~CZMQPublisher (void)
{
}

unsigned CZMQPublisher::Publish (const CZMQMessage &rMessage)
{
	if (rMessage.GetFrameCount () == 0)
	{
		return 0;
	}

	const u8 *pTopic = rMessage.GetFrame (0);
	size_t nTopicLength = rMessage.GetFrameSize (0);

	unsigned nCount = 0;

	m_Mutex.Acquire ();

	for (unsigned nPeer = 0; nPeer < ZMQ_MAX_PEERS; nPeer++)
	{
		if (   GetReadyPeer (nPeer) != 0
		    && m_Subscriptions[nPeer].Matches (pTopic, nTopicLength)
		    && SendToPeer (nPeer, rMessage))
		{
			nCount++;
		}
	}

	m_Mutex.Release ();

	return nCount;
}

unsigned CZMQPublisher::Publish (const char *pTopic, const void *pData, size_t nLength)
{
	assert (pTopic != 0);

	if (!HasSubscribers (pTopic))
	{
		return 0;		// do not copy the data for nothing
	}

	CZMQMessage Message;
	if (   !Message.AddFrame (pTopic)
	    || !Message.AddFrame (pData, nLength))
	{
		return 0;
	}

	return Publish (Message);
}

unsigned CZMQPublisher::Publish (const char *pTopic, const char *pString)
{
	assert (pString != 0);

	return Publish (pTopic, pString, strlen (pString));
}

boolean CZMQPublisher::HasSubscribers (const char *pTopic)
{
	assert (pTopic != 0);
	size_t nTopicLength = strlen (pTopic);

	boolean bResult = FALSE;

	m_Mutex.Acquire ();

	for (unsigned nPeer = 0; nPeer < ZMQ_MAX_PEERS; nPeer++)
	{
		if (   GetReadyPeer (nPeer) != 0
		    && m_Subscriptions[nPeer].Matches (pTopic, nTopicLength))
		{
			bResult = TRUE;

			break;
		}
	}

	m_Mutex.Release ();

	return bResult;
}

void CZMQPublisher::OnPeerMessage (unsigned nPeer, CZMQMessage &rMessage)
{
	assert (nPeer < ZMQ_MAX_PEERS);

	// ZMTP 3.0 subscription message: one frame, first byte is 1 (subscribe)
	// or 0 (cancel), followed by the topic. Other messages are ignored.
	if (   rMessage.GetFrameCount () != 1
	    || rMessage.GetFrameSize (0) < 1)
	{
		return;
	}

	const u8 *pFrame = rMessage.GetFrame (0);
	size_t nTopicLength = rMessage.GetFrameSize (0) - 1;

	switch (pFrame[0])
	{
	case 1:
		m_Subscriptions[nPeer].Add (pFrame + 1, nTopicLength);
		break;

	case 0:
		m_Subscriptions[nPeer].Remove (pFrame + 1, nTopicLength);
		break;

	default:
		break;
	}
}

void CZMQPublisher::OnPeerRemoved (unsigned nPeer)
{
	assert (nPeer < ZMQ_MAX_PEERS);

	m_Subscriptions[nPeer].Clear ();
}
