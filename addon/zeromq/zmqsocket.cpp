//
// zmqsocket.cpp
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
#include <zeromq/zmqsocket.h>
#include <circle/net/socket.h>
#include <circle/net/dnsclient.h>
#include <circle/net/in.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

#define MAX_EVENTS_PER_POLL	16	// per peer and poll round, to be fair to all peers

static const char FromZeroMQ[] = "zmq";

/// \brief Helper task, which accepts connections on a bound port or
///	   (re-)connects to a remote endpoint in the background
class CZMQEndpointTask : public CTask
{
public:
	// Bind() endpoint, pListenSocket is bound and listening
	CZMQEndpointTask (CZMQSocket *pOwner, CSocket *pListenSocket)
	:	m_pOwner (pOwner),
		m_nEndpoint (0),
		m_pListenSocket (pListenSocket),
		m_pNetSubSystem (0),
		m_nPort (0)
	{
		SetName ("zmqaccept");
	}

	// Connect() endpoint
	CZMQEndpointTask (CZMQSocket *pOwner, unsigned nEndpoint,
			  CNetSubSystem *pNetSubSystem, const char *pHost, u16 nPort)
	:	m_pOwner (pOwner),
		m_nEndpoint (nEndpoint),
		m_pListenSocket (0),
		m_pNetSubSystem (pNetSubSystem),
		m_Host (pHost),
		m_nPort (nPort)
	{
		SetName ("zmqconnect");
	}

	void Run (void) override
	{
		if (m_pListenSocket != 0)
		{
			RunAcceptor ();
		}
		else
		{
			RunConnector ();
		}
	}

private:
	void RunAcceptor (void)
	{
		while (1)
		{
			CIPAddress ForeignIP;
			u16 nForeignPort;
			CSocket *pSocket = m_pListenSocket->Accept (&ForeignIP, &nForeignPort);
			if (pSocket == 0)
			{
				CScheduler::Get ()->MsSleep (100);

				continue;
			}

			m_pOwner->AddPeer (pSocket, -1, ForeignIP, nForeignPort);
		}
	}

	void RunConnector (void)
	{
		boolean bFailureLogged = FALSE;

		while (1)
		{
			if (!m_pOwner->IsEndpointConnected (m_nEndpoint))
			{
				CSocket *pSocket = TryConnect ();
				if (pSocket != 0)
				{
					bFailureLogged = FALSE;

					m_pOwner->AddPeer (pSocket, m_nEndpoint, m_IPAddress, m_nPort);
				}
				else if (!bFailureLogged)
				{
					bFailureLogged = TRUE;

					CLogger::Get ()->Write (FromZeroMQ, LogWarning,
								"Cannot connect to %s:%u (retrying)",
								(const char *) m_Host, m_nPort);
				}
			}

			CScheduler::Get ()->MsSleep (ZMQ_RECONNECT_MS);
		}
	}

	CSocket *TryConnect (void)
	{
		CDNSClient DNSClient (m_pNetSubSystem);
		if (!DNSClient.Resolve (m_Host, &m_IPAddress))
		{
			return 0;
		}

		CSocket *pSocket = new CSocket (m_pNetSubSystem, IPPROTO_TCP);
		if (pSocket == 0)
		{
			return 0;
		}

		if (pSocket->Connect (m_IPAddress, m_nPort) < 0)
		{
			delete pSocket;

			return 0;
		}

		return pSocket;
	}

private:
	CZMQSocket *m_pOwner;
	unsigned m_nEndpoint;

	CSocket *m_pListenSocket;

	CNetSubSystem *m_pNetSubSystem;
	CString m_Host;
	u16 m_nPort;
	CIPAddress m_IPAddress;
};

CZMQSocket::CZMQSocket (CNetSubSystem *pNetSubSystem, const char *pSocketType,
			const char *const *ppPeerTypes)
:	m_pNetSubSystem (pNetSubSystem),
	m_pSocketType (pSocketType),
	m_ppPeerTypes (ppPeerTypes),
	m_nSendTimeoutMs (ZMQ_SEND_TIMEOUT_MS),
	m_nEndpoints (0)
{
	assert (m_pNetSubSystem != 0);
	assert (m_pSocketType != 0);
	assert (m_ppPeerTypes != 0);

	for (unsigned i = 0; i < ZMQ_MAX_PEERS; i++)
	{
		m_Peer[i].pConnection = 0;
		m_Peer[i].nEndpoint = -1;
		m_Peer[i].bReady = FALSE;
	}

	for (unsigned i = 0; i < ZMQ_MAX_ENDPOINTS; i++)
	{
		m_Endpoint[i].bConnected = FALSE;
		m_Endpoint[i].pTask = 0;
	}

	SetName (FromZeroMQ);
}

CZMQSocket::~CZMQSocket (void)
{
	// The endpoint tasks cannot be terminated, while they are blocked in
	// the network stack. Therefore a ZeroMQ socket must not be deleted.
	assert (0);
}

boolean CZMQSocket::Bind (u16 nPort)
{
	if (m_nEndpoints >= ZMQ_MAX_ENDPOINTS)
	{
		return FALSE;
	}

	CSocket *pSocket = new CSocket (m_pNetSubSystem, IPPROTO_TCP);
	assert (pSocket != 0);

	if (   pSocket->Bind (nPort) < 0
	    || pSocket->Listen (ZMQ_MAX_PEERS) < 0)
	{
		CLogger::Get ()->Write (FromZeroMQ, LogError, "Cannot bind to port %u", nPort);

		delete pSocket;

		return FALSE;
	}

	TEndpoint *pEndpoint = &m_Endpoint[m_nEndpoints++];
	pEndpoint->pTask = new CZMQEndpointTask (this, pSocket);
	assert (pEndpoint->pTask != 0);

	CLogger::Get ()->Write (FromZeroMQ, LogDebug, "%s socket bound to port %u",
				m_pSocketType, nPort);

	return TRUE;
}

boolean CZMQSocket::Connect (const char *pHost, u16 nPort)
{
	assert (pHost != 0);

	if (m_nEndpoints >= ZMQ_MAX_ENDPOINTS)
	{
		return FALSE;
	}

	unsigned nEndpoint = m_nEndpoints++;
	TEndpoint *pEndpoint = &m_Endpoint[nEndpoint];
	pEndpoint->bConnected = FALSE;
	pEndpoint->pTask = new CZMQEndpointTask (this, nEndpoint, m_pNetSubSystem, pHost, nPort);
	assert (pEndpoint->pTask != 0);

	return TRUE;
}

unsigned CZMQSocket::GetPeerCount (void)
{
	unsigned nCount = 0;
	for (unsigned i = 0; i < ZMQ_MAX_PEERS; i++)
	{
		if (m_Peer[i].bReady)
		{
			nCount++;
		}
	}

	return nCount;
}

void CZMQSocket::SetSendTimeout (unsigned nMilliSeconds)
{
	m_Mutex.Acquire ();

	m_nSendTimeoutMs = nMilliSeconds;

	for (unsigned i = 0; i < ZMQ_MAX_PEERS; i++)
	{
		if (m_Peer[i].pConnection != 0)
		{
			m_Peer[i].pConnection->SetSendTimeout (m_nSendTimeoutMs * 1000);
		}
	}

	m_Mutex.Release ();
}

void CZMQSocket::Run (void)
{
	while (1)
	{
		PollPeers ();

		CScheduler::Get ()->usSleep (ZMQ_POLL_INTERVAL_US);
	}
}

CZMTPConnection *CZMQSocket::GetReadyPeer (unsigned nPeer)
{
	assert (nPeer < ZMQ_MAX_PEERS);
	TPeer *pPeer = &m_Peer[nPeer];

	if (   !pPeer->bReady
	    || pPeer->pConnection->IsFailed ())
	{
		return 0;
	}

	return pPeer->pConnection;
}

boolean CZMQSocket::SendToPeer (unsigned nPeer, const CZMQMessage &rMessage)
{
	CZMTPConnection *pConnection = GetReadyPeer (nPeer);
	if (pConnection == 0)
	{
		return FALSE;
	}

	// a failed connection is removed by the next PollPeers()
	return pConnection->Send (rMessage);
}

void CZMQSocket::AddPeer (CSocket *pSocket, int nEndpoint, const CIPAddress &rIPAddress, u16 nPort)
{
	assert (pSocket != 0);

	m_Mutex.Acquire ();

	CString IPString;
	rIPAddress.Format (&IPString);

	unsigned nPeer;
	for (nPeer = 0; nPeer < ZMQ_MAX_PEERS; nPeer++)
	{
		if (m_Peer[nPeer].pConnection == 0)
		{
			break;
		}
	}

	if (nPeer >= ZMQ_MAX_PEERS)
	{
		m_Mutex.Release ();

		CLogger::Get ()->Write (FromZeroMQ, LogWarning,
					"Too many peers, rejecting %s:%u",
					(const char *) IPString, nPort);

		delete pSocket;

		return;
	}

	TPeer *pPeer = &m_Peer[nPeer];
	pPeer->pConnection = new CZMTPConnection (pSocket, m_pSocketType);
	assert (pPeer->pConnection != 0);
	pPeer->nEndpoint = nEndpoint;
	pPeer->bReady = FALSE;
	pPeer->Name.Format ("%s:%u", (const char *) IPString, nPort);

	if (nEndpoint >= 0)
	{
		assert (nEndpoint < (int) m_nEndpoints);
		m_Endpoint[nEndpoint].bConnected = TRUE;
	}

	pPeer->pConnection->SetSendTimeout (m_nSendTimeoutMs * 1000);

	// a failure is detected by the next PollPeers()
	pPeer->pConnection->Start ();

	m_Mutex.Release ();
}

boolean CZMQSocket::IsEndpointConnected (unsigned nEndpoint) const
{
	assert (nEndpoint < m_nEndpoints);

	return m_Endpoint[nEndpoint].bConnected;
}

void CZMQSocket::PollPeers (void)
{
	CZMQMessage Message;

	m_Mutex.Acquire ();

	for (unsigned nPeer = 0; nPeer < ZMQ_MAX_PEERS; nPeer++)
	{
		TPeer *pPeer = &m_Peer[nPeer];

		for (unsigned nEvent = 0;
		     pPeer->pConnection != 0 && nEvent < MAX_EVENTS_PER_POLL;
		     nEvent++)
		{
			TZMTPEvent Event = pPeer->pConnection->Poll (&Message);

			if (Event == ZMTPEventNone)
			{
				if (   !pPeer->bReady
				    && CTimer::Get ()->GetTicks () - pPeer->pConnection->GetStartTicks ()
					>= MSEC2HZ (ZMQ_HANDSHAKE_MS))
				{
					CLogger::Get ()->Write (FromZeroMQ, LogWarning,
								"Handshake with %s timed out",
								(const char *) pPeer->Name);

					RemovePeer (nPeer);
				}

				break;
			}

			switch (Event)
			{
			case ZMTPEventReady:
				if (!IsAcceptedPeerType (pPeer->pConnection->GetPeerSocketType ()))
				{
					CLogger::Get ()->Write (FromZeroMQ, LogWarning,
								"%s: Incompatible socket type %s",
								(const char *) pPeer->Name,
								pPeer->pConnection->GetPeerSocketType ());

					RemovePeer (nPeer);

					break;
				}

				pPeer->bReady = TRUE;

				CLogger::Get ()->Write (FromZeroMQ, LogNotice, "%s connected (%s)",
							(const char *) pPeer->Name,
							pPeer->pConnection->GetPeerSocketType ());

				OnPeerReady (nPeer);
				break;

			case ZMTPEventMessage:
				OnPeerMessage (nPeer, Message);
				Message.Clear ();
				break;

			case ZMTPEventError:
				RemovePeer (nPeer);
				break;

			default:
				assert (0);
				break;
			}
		}
	}

	m_Mutex.Release ();
}

void CZMQSocket::RemovePeer (unsigned nPeer)
{
	assert (nPeer < ZMQ_MAX_PEERS);
	TPeer *pPeer = &m_Peer[nPeer];
	assert (pPeer->pConnection != 0);

	if (pPeer->bReady)
	{
		OnPeerRemoved (nPeer);

		CLogger::Get ()->Write (FromZeroMQ, LogNotice, "%s disconnected",
					(const char *) pPeer->Name);
	}

	delete pPeer->pConnection;
	pPeer->pConnection = 0;
	pPeer->bReady = FALSE;

	if (pPeer->nEndpoint >= 0)
	{
		assert (pPeer->nEndpoint < (int) m_nEndpoints);
		m_Endpoint[pPeer->nEndpoint].bConnected = FALSE;

		pPeer->nEndpoint = -1;
	}
}

boolean CZMQSocket::IsAcceptedPeerType (const char *pType) const
{
	assert (pType != 0);

	for (const char *const *ppPeerType = m_ppPeerTypes; *ppPeerType != 0; ppPeerType++)
	{
		if (strcmp (pType, *ppPeerType) == 0)
		{
			return TRUE;
		}
	}

	return FALSE;
}
