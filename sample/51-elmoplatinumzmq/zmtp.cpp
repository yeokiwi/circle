//
// zmtp.cpp
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
#include "zmtp.h"
#include <circle/net/in.h>
#include <circle/util.h>
#include <assert.h>

#define HANDSHAKE_TIMEOUT_US	(5 * 1000 * 1000)	// 5 seconds
#define ZMTP_MAX_FRAME_SIZE	(64 * 1024)	// guards against a bad peer's huge length field

#define ZMTP_FLAG_MORE		0x01
#define ZMTP_FLAG_LONG		0x02
#define ZMTP_FLAG_COMMAND	0x04

// ZMTP 3.0 signature, see RFC 23 (https://rfc.zeromq.org/spec/23/)
static const u8 s_Signature[10] = { 0xFF, 0, 0, 0, 0, 0, 0, 0, 1, 0x7F };

CZMTPConnection::CZMTPConnection (CSocket *pSocket)
:	m_pSocket (pSocket),
	m_RxPhase (RxPhaseFlags),
	m_RxFlags (0),
	m_nRxLenGot (0),
	m_nRxBodyLen (0),
	m_pRxBody (0),
	m_nRxBodyGot (0),
	m_nPendingFrames (0)
{
	assert (m_pSocket != 0);
}

CZMTPConnection::~CZMTPConnection (void)
{
	delete [] m_pRxBody;
	m_pRxBody = 0;

	for (unsigned i = 0; i < m_nPendingFrames; i++)
	{
		delete [] m_ppPendingFrame[i];
	}
	m_nPendingFrames = 0;

	m_pSocket = 0;
}

boolean CZMTPConnection::Handshake (void)
{
	assert (m_pSocket != 0);

	m_pSocket->SetOptionReceiveTimeout (HANDSHAKE_TIMEOUT_US);

	// --- send our greeting (64 bytes) ---

	u8 Greeting[64];
	memset (Greeting, 0, sizeof Greeting);

	memcpy (&Greeting[0], s_Signature, sizeof s_Signature);	// signature
	Greeting[10] = 3;						// version-major
	Greeting[11] = 0;						// version-minor
	memcpy (&Greeting[12], "NULL", 4);				// mechanism (20 bytes, rest 0)
	Greeting[32] = 0;						// as-server (we are a client)
	// Greeting[33..63] filler, already zeroed

	if (!SendAll (Greeting, sizeof Greeting))
	{
		return FALSE;
	}

	// --- receive the peer's greeting (64 bytes) ---

	u8 PeerGreeting[64];
	if (!ReceiveExact (PeerGreeting, sizeof PeerGreeting))
	{
		return FALSE;
	}

	if (PeerGreeting[0] != 0xFF)
	{
		return FALSE;			// not a ZMTP peer
	}

	if (PeerGreeting[10] < 3)
	{
		return FALSE;			// ZMTP version too old
	}

	if (memcmp (&PeerGreeting[12], "NULL", 4) != 0)
	{
		return FALSE;			// only the NULL mechanism is supported
	}

	// --- send READY command (Socket-Type: DEALER) ---

	static const char CommandName[] = "READY";
	static const char PropName[] = "Socket-Type";
	static const char PropValue[] = "DEALER";

	u8 Body[1 + (sizeof CommandName - 1)
	      + 1 + (sizeof PropName - 1) + 4 + (sizeof PropValue - 1)];
	unsigned i = 0;

	Body[i++] = sizeof CommandName - 1;
	memcpy (&Body[i], CommandName, sizeof CommandName - 1);
	i += sizeof CommandName - 1;

	Body[i++] = sizeof PropName - 1;
	memcpy (&Body[i], PropName, sizeof PropName - 1);
	i += sizeof PropName - 1;

	u32 nValueLength = sizeof PropValue - 1;
	Body[i++] = (u8) (nValueLength >> 24);
	Body[i++] = (u8) (nValueLength >> 16);
	Body[i++] = (u8) (nValueLength >> 8);
	Body[i++] = (u8) nValueLength;
	memcpy (&Body[i], PropValue, sizeof PropValue - 1);
	i += sizeof PropValue - 1;

	assert (i == sizeof Body);

	if (!SendFrame (Body, sizeof Body, FALSE, TRUE))
	{
		return FALSE;
	}

	// --- receive the peer's READY command ---

	u8 PeerBody[256];
	size_t nPeerLength;
	boolean bCommand;
	if (!ReceiveFrameBlocking (PeerBody, sizeof PeerBody, &nPeerLength, &bCommand))
	{
		return FALSE;
	}

	if (   !bCommand
	    || nPeerLength < 1
	    || PeerBody[0] != sizeof CommandName - 1
	    || nPeerLength < 1 + (sizeof CommandName - 1)
	    || memcmp (&PeerBody[1], CommandName, sizeof CommandName - 1) != 0)
	{
		return FALSE;			// did not get READY, give up
	}

	// the properties sent by the peer (e.g. its own Socket-Type) are not
	// evaluated, this client works the same way with any ROUTER-based peer

	m_pSocket->SetOptionReceiveTimeout (0);	// back to blocking-forever / non-blocking use

	return TRUE;
}

boolean CZMTPConnection::SendMessage (const void *const *ppFrame, const size_t *pLength,
				      unsigned nFrames)
{
	assert (ppFrame != 0);
	assert (pLength != 0);
	assert (nFrames >= 1);

	for (unsigned i = 0; i < nFrames; i++)
	{
		if (!SendFrame (ppFrame[i], pLength[i], i+1 < nFrames, FALSE))
		{
			return FALSE;
		}
	}

	return TRUE;
}

int CZMTPConnection::ReceiveMessage (u8 *ppFrame[], size_t pLength[], unsigned nMaxFrames)
{
	assert (ppFrame != 0);
	assert (pLength != 0);

	while (1)
	{
		switch (m_RxPhase)
		{
		case RxPhaseFlags: {
			int nResult = m_pSocket->Receive (&m_RxFlags, 1, MSG_DONTWAIT);
			if (nResult == 0)
			{
				return 0;
			}
			if (nResult < 0)
			{
				return -1;
			}

			m_nRxLenGot = 0;
			m_RxPhase = m_RxFlags & ZMTP_FLAG_LONG ? RxPhaseLenLong : RxPhaseLenShort;
			} break;

		case RxPhaseLenShort: {
			int nResult = m_pSocket->Receive (&m_RxLenBuf[0], 1, MSG_DONTWAIT);
			if (nResult == 0)
			{
				return 0;
			}
			if (nResult < 0)
			{
				return -1;
			}

			m_nRxBodyLen = m_RxLenBuf[0];
			if (m_nRxBodyLen > ZMTP_MAX_FRAME_SIZE)
			{
				return -1;
			}
			StartBody ();
			} break;

		case RxPhaseLenLong: {
			int nResult = m_pSocket->Receive (&m_RxLenBuf[m_nRxLenGot],
							  8 - m_nRxLenGot, MSG_DONTWAIT);
			if (nResult == 0)
			{
				return 0;
			}
			if (nResult < 0)
			{
				return -1;
			}

			m_nRxLenGot += (unsigned) nResult;
			if (m_nRxLenGot < 8)
			{
				break;
			}

			m_nRxBodyLen = 0;
			for (unsigned i = 0; i < 8; i++)
			{
				m_nRxBodyLen = (m_nRxBodyLen << 8) | m_RxLenBuf[i];
			}
			if (m_nRxBodyLen > ZMTP_MAX_FRAME_SIZE)
			{
				return -1;
			}
			StartBody ();
			} break;

		case RxPhaseBody: {
			if (m_nRxBodyGot < m_nRxBodyLen)
			{
				int nResult = m_pSocket->Receive (m_pRxBody + m_nRxBodyGot,
					(unsigned) (m_nRxBodyLen - m_nRxBodyGot), MSG_DONTWAIT);
				if (nResult == 0)
				{
					return 0;
				}
				if (nResult < 0)
				{
					delete [] m_pRxBody;
					m_pRxBody = 0;

					return -1;
				}

				m_nRxBodyGot += (unsigned) nResult;
				if (m_nRxBodyGot < m_nRxBodyLen)
				{
					break;
				}
			}

			boolean bCommand = !!(m_RxFlags & ZMTP_FLAG_COMMAND);
			boolean bMore = !!(m_RxFlags & ZMTP_FLAG_MORE);

			m_RxPhase = RxPhaseFlags;

			if (bCommand)
			{
				// an unexpected command frame in the data stream
				// (e.g. a keep-alive PING) is silently discarded
				delete [] m_pRxBody;
				m_pRxBody = 0;

				break;
			}

			if (m_nPendingFrames < ZMTP_MAX_FRAMES)
			{
				m_ppPendingFrame[m_nPendingFrames] = m_pRxBody;
				m_nPendingFrameLength[m_nPendingFrames] = (size_t) m_nRxBodyLen;
				m_nPendingFrames++;
			}
			else
			{
				delete [] m_pRxBody;	// drop frames beyond ZMTP_MAX_FRAMES
			}
			m_pRxBody = 0;

			if (bMore)
			{
				break;
			}

			unsigned nFrames =
				m_nPendingFrames < nMaxFrames ? m_nPendingFrames : nMaxFrames;

			for (unsigned i = 0; i < nFrames; i++)
			{
				ppFrame[i] = m_ppPendingFrame[i];
				pLength[i] = m_nPendingFrameLength[i];
			}

			for (unsigned i = nFrames; i < m_nPendingFrames; i++)
			{
				delete [] m_ppPendingFrame[i];
			}

			m_nPendingFrames = 0;

			return (int) nFrames;
			}
		}
	}
}

void CZMTPConnection::StartBody (void)
{
	delete [] m_pRxBody;
	m_pRxBody = m_nRxBodyLen > 0 ? new u8[(size_t) m_nRxBodyLen] : 0;
	m_nRxBodyGot = 0;
	m_RxPhase = RxPhaseBody;
}

boolean CZMTPConnection::SendAll (const void *pBuffer, size_t nLength)
{
	const u8 *p = (const u8 *) pBuffer;

	while (nLength > 0)
	{
		int nResult = m_pSocket->Send (p, (unsigned) nLength, 0);
		if (nResult <= 0)
		{
			return FALSE;
		}

		p += nResult;
		nLength -= (size_t) nResult;
	}

	return TRUE;
}

boolean CZMTPConnection::ReceiveExact (void *pBuffer, size_t nLength)
{
	u8 *p = (u8 *) pBuffer;

	while (nLength > 0)
	{
		int nResult = m_pSocket->Receive (p, (unsigned) nLength, 0);
		if (nResult <= 0)
		{
			return FALSE;		// error, timeout or peer closed
		}

		p += nResult;
		nLength -= (size_t) nResult;
	}

	return TRUE;
}

boolean CZMTPConnection::SendFrame (const void *pBuffer, size_t nLength,
				    boolean bMore, boolean bCommand)
{
	u8 uchFlags = 0;
	if (bMore)
	{
		uchFlags |= ZMTP_FLAG_MORE;
	}
	if (bCommand)
	{
		uchFlags |= ZMTP_FLAG_COMMAND;
	}

	if (nLength > 255)
	{
		uchFlags |= ZMTP_FLAG_LONG;

		if (!SendAll (&uchFlags, 1))
		{
			return FALSE;
		}

		u8 LenBuf[8];
		u64 nLen64 = nLength;
		for (int i = 7; i >= 0; i--)
		{
			LenBuf[i] = (u8) nLen64;
			nLen64 >>= 8;
		}

		if (!SendAll (LenBuf, sizeof LenBuf))
		{
			return FALSE;
		}
	}
	else
	{
		if (!SendAll (&uchFlags, 1))
		{
			return FALSE;
		}

		u8 uchLength = (u8) nLength;
		if (!SendAll (&uchLength, 1))
		{
			return FALSE;
		}
	}

	if (nLength > 0)
	{
		if (!SendAll (pBuffer, nLength))
		{
			return FALSE;
		}
	}

	return TRUE;
}

boolean CZMTPConnection::ReceiveFrameBlocking (u8 *pBuffer, size_t nBufferSize,
					       size_t *pLength, boolean *pbCommand)
{
	assert (pBuffer != 0);
	assert (pLength != 0);
	assert (pbCommand != 0);

	u8 uchFlags;
	if (!ReceiveExact (&uchFlags, 1))
	{
		return FALSE;
	}

	u64 nLength;
	if (uchFlags & ZMTP_FLAG_LONG)
	{
		u8 LenBuf[8];
		if (!ReceiveExact (LenBuf, sizeof LenBuf))
		{
			return FALSE;
		}

		nLength = 0;
		for (unsigned i = 0; i < 8; i++)
		{
			nLength = (nLength << 8) | LenBuf[i];
		}
	}
	else
	{
		u8 uchLength;
		if (!ReceiveExact (&uchLength, 1))
		{
			return FALSE;
		}

		nLength = uchLength;
	}

	if (nLength > nBufferSize)
	{
		return FALSE;		// caller's buffer is too small
	}

	if (   nLength > 0
	    && !ReceiveExact (pBuffer, (size_t) nLength))
	{
		return FALSE;
	}

	*pLength = (size_t) nLength;
	*pbCommand = !!(uchFlags & ZMTP_FLAG_COMMAND);

	return TRUE;
}
