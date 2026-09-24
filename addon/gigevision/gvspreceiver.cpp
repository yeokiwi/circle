//
// gvspreceiver.cpp
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
#include <gigevision/gvspreceiver.h>
#include <gigevision/gvcpclient.h>
#include <circle/net/in.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

#define IP_UDP_HEADER_SIZE	(20 + 8)
#define DEFAULT_PACKET_SIZE	1500
#define MAX_RESEND_RANGES	16

static const char FromGVSP[] = "gvsp";

CGVSPReceiver::CGVSPReceiver (CNetSubSystem *pNetSubSystem, CGVCPClient *pResendClient,
			      unsigned nStreamChannel)
:	m_pNetSubSystem (pNetSubSystem),
	m_pResendClient (pResendClient),
	m_nStreamChannel (nStreamChannel),
	m_pSocket (0),
	m_nPacketSize (DEFAULT_PACKET_SIZE),
	m_bResendEnabled (pResendClient != 0),
	m_nResendTimeoutMs (50),
	m_bActive (FALSE),
	m_bExtendedID (FALSE),
	m_nBlockID (0),
	m_pBitmap (0),
	m_nBitmapPackets (0),
	m_bHaveFinished (FALSE),
	m_nLastFinishedBlockID (0),
	m_bPushBack (FALSE),
	m_nPacketLength (0)
{
	assert (m_pNetSubSystem != 0);

	memset (&m_Stats, 0, sizeof m_Stats);
}

CGVSPReceiver::~CGVSPReceiver (void)
{
	delete m_pSocket;
	m_pSocket = 0;

	delete [] m_pBitmap;
	m_pBitmap = 0;

	m_pResendClient = 0;
	m_pNetSubSystem = 0;
}

boolean CGVSPReceiver::Initialize (u16 nPort)
{
	assert (m_pSocket == 0);

	m_pSocket = new CSocket (m_pNetSubSystem, IPPROTO_UDP);
	assert (m_pSocket != 0);

	if (m_pSocket->Bind (nPort) < 0)
	{
		CLogger::Get ()->Write (FromGVSP, LogError, "Cannot bind socket");

		delete m_pSocket;
		m_pSocket = 0;

		return FALSE;
	}

	return TRUE;
}

u16 CGVSPReceiver::GetPort (void) const
{
	assert (m_pSocket != 0);

	return m_pSocket->GetOwnPort ();
}

void CGVSPReceiver::SetPacketSize (unsigned nPacketSize)
{
	assert (nPacketSize > IP_UDP_HEADER_SIZE + GVSP_EXT_HEADER_SIZE);

	m_nPacketSize = nPacketSize;
}

void CGVSPReceiver::SetResend (boolean bEnable, unsigned nTimeoutMs)
{
	m_bResendEnabled = bEnable && m_pResendClient != 0;
	m_nResendTimeoutMs = nTimeoutMs;
}

boolean CGVSPReceiver::ReceiveFrame (TGVSPFrameInfo *pInfo, void *pBuffer, size_t nBufferSize,
				     unsigned nTimeoutMs)
{
	assert (pInfo != 0);
	assert (pBuffer != 0);
	assert (m_pSocket != 0);

	u64 nDeadline = CTimer::GetClockTicks64 () + (u64) nTimeoutMs * (CLOCKHZ / 1000);
	boolean bResendWait = FALSE;
	u64 nResendDeadline = 0;

	for (;;)
	{
		if (!m_bPushBack)
		{
			u64 nNow = CTimer::GetClockTicks64 ();

			if (   bResendWait
			    && nNow >= nResendDeadline)
			{
				FinishBlock (pInfo);	// resent packets did not arrive in time

				return TRUE;
			}

			if (nNow >= nDeadline)
			{
				if (!m_bActive)
				{
					return FALSE;
				}

				if (m_bTrailerReceived)
				{
					FinishBlock (pInfo);

					return TRUE;
				}

				// abort the partial block, its data would be in the wrong buffer later
				m_bActive = FALSE;
				m_bHaveFinished = TRUE;
				m_nLastFinishedBlockID = m_nBlockID;
				m_Stats.IncompleteFrames++;

				return FALSE;
			}

			u64 nWaitUntil = nDeadline;
			if (   bResendWait
			    && nResendDeadline < nWaitUntil)
			{
				nWaitUntil = nResendDeadline;
			}

			m_pSocket->SetOptionReceiveTimeout ((unsigned) (nWaitUntil - nNow));

			int nResult = m_pSocket->Receive (m_Packet, sizeof m_Packet, 0);
			if (nResult <= 0)
			{
				continue;
			}

			m_nPacketLength = (unsigned) nResult;
		}

		m_bPushBack = FALSE;

		boolean bResendRequested = m_bActive && m_bResendRequested;

		switch (ProcessPacket (m_Packet, m_nPacketLength, pInfo, (u8 *) pBuffer, nBufferSize))
		{
		case PacketContinue:
			if (   !bResendRequested
			    && m_bActive
			    && m_bResendRequested)
			{
				bResendWait = TRUE;
				nResendDeadline =   CTimer::GetClockTicks64 ()
						  + (u64) m_nResendTimeoutMs * (CLOCKHZ / 1000);
			}
			break;

		case PacketFrameDone:
			return TRUE;

		case PacketPushedBack:
			m_bPushBack = TRUE;
			return TRUE;
		}
	}
}

void CGVSPReceiver::Flush (void)
{
	assert (m_pSocket != 0);

	while (m_pSocket->Receive (m_Packet, sizeof m_Packet, MSG_DONTWAIT) > 0)
	{
		// drop
	}

	m_bPushBack = FALSE;
	m_bActive = FALSE;
	m_bHaveFinished = FALSE;
}

CGVSPReceiver::TPacketResult CGVSPReceiver::ProcessPacket (const u8 *pPacket, unsigned nLength,
							   TGVSPFrameInfo *pInfo,
							   u8 *pBuffer, size_t nBufferSize)
{
	if (nLength < GVSP_HEADER_SIZE)
	{
		m_Stats.IgnoredPackets++;

		return PacketContinue;
	}

	const TGVSPHeader *pHeader = (const TGVSPHeader *) pPacket;
	u16 nStatus = be2le16 (pHeader->Status);
	u32 nWord = be2le32 (pHeader->FormatAndPacketID);

	u64 nBlockID;
	unsigned nFormat;
	u32 nPacketID;
	unsigned nHeaderSize;
	boolean bExtendedID = !!(nWord & GVSP_EI_FLAG);
	if (bExtendedID)
	{
		if (nLength < GVSP_EXT_HEADER_SIZE)
		{
			m_Stats.IgnoredPackets++;

			return PacketContinue;
		}

		const TGVSPExtHeader *pExtHeader = (const TGVSPExtHeader *) pPacket;
		nBlockID =   (u64) be2le32 (pExtHeader->BlockIDHigh) << 32
			   | be2le32 (pExtHeader->BlockIDLow);
		nFormat = GVSP_FORMAT (be2le32 (pExtHeader->Format));
		nPacketID = be2le32 (pExtHeader->PacketID);
		nHeaderSize = GVSP_EXT_HEADER_SIZE;
	}
	else
	{
		nBlockID = be2le16 (pHeader->BlockID);
		nFormat = GVSP_FORMAT (nWord);
		nPacketID = GVSP_PACKET_ID (nWord);
		nHeaderSize = GVSP_HEADER_SIZE;
	}

	if (nBlockID == 0)
	{
		m_Stats.IgnoredPackets++;

		return PacketContinue;
	}

	m_bExtendedID = bExtendedID;

	if (   m_bActive
	    && nBlockID != m_nBlockID)
	{
		if (IsNewerBlock (nBlockID))
		{
			FinishBlock (pInfo);	// the rest of the current block is lost

			return PacketPushedBack;
		}

		m_Stats.IgnoredPackets++;	// late packet of an earlier block

		return PacketContinue;
	}

	if (!m_bActive)
	{
		// Late payload packets of the last blocks are ignored. A leader always starts
		// a new block (unless it is a resent leader of the last block), because some
		// devices restart the block ID when the acquisition is restarted.
		if (   nFormat == GVSP_FORMAT_TRAILER
		    || (   m_bHaveFinished
			&& (nFormat == GVSP_FORMAT_LEADER
			    ? nBlockID == m_nLastFinishedBlockID
			    : !IsNewerBlock (nBlockID))))
		{
			m_Stats.IgnoredPackets++;

			return PacketContinue;
		}

		StartBlock (nBlockID, nBufferSize, pInfo);
	}

	m_Stats.Packets++;

	const u8 *pData = pPacket + nHeaderSize;
	unsigned nDataLength = nLength - nHeaderSize;

	switch (nFormat)
	{
	case GVSP_FORMAT_LEADER: {
		if (nDataLength < sizeof (TGVSPGenericLeader))
		{
			break;
		}

		const TGVSPGenericLeader *pLeader = (const TGVSPGenericLeader *) pData;
		pInfo->PayloadType = be2le16 (pLeader->PayloadType);
		pInfo->Timestamp =   (u64) be2le32 (pLeader->TimestampHigh) << 32
				   | be2le32 (pLeader->TimestampLow);

		if (   (pInfo->PayloadType & ~GVSP_PAYLOAD_EXTENDED_CHUNK) == GVSP_PAYLOAD_IMAGE
		    && nDataLength >= sizeof (TGVSPImageLeader))
		{
			const TGVSPImageLeader *pImage = (const TGVSPImageLeader *) pData;
			pInfo->PixelFormat = be2le32 (pImage->PixelFormat);
			pInfo->SizeX = be2le32 (pImage->SizeX);
			pInfo->SizeY = be2le32 (pImage->SizeY);
			pInfo->OffsetX = be2le32 (pImage->OffsetX);
			pInfo->OffsetY = be2le32 (pImage->OffsetY);
			pInfo->PaddingX = be2le16 (pImage->PaddingX);
			pInfo->PaddingY = be2le16 (pImage->PaddingY);
		}

		m_bLeaderReceived = TRUE;
		} break;

	case GVSP_FORMAT_PAYLOAD: {
		if (nPacketID == 0)
		{
			break;
		}

		unsigned nExpected = m_nPacketSize - IP_UDP_HEADER_SIZE - nHeaderSize;
		if (nDataLength > nExpected)
		{
			// the device uses a bigger packet size than configured
			CLogger::Get ()->Write (FromGVSP, LogWarning,
						"Packet size mismatch (%u > %u)",
						nDataLength, nExpected);

			m_nPacketSize = nDataLength + IP_UDP_HEADER_SIZE + nHeaderSize;
			m_bOverflow = TRUE;	// offsets of this block are unreliable

			break;
		}

		if (m_bTrailerReceived && nPacketID > m_nLastPacketID)
		{
			break;
		}

		size_t nOffset = (size_t) (nPacketID - 1) * nExpected;
		if (   nPacketID >= m_nBitmapPackets
		    || nOffset + nDataLength > nBufferSize)
		{
			m_bOverflow = TRUE;

			break;
		}

		u8 nMask = 1 << (nPacketID & 7);
		if (!(m_pBitmap[nPacketID / 8] & nMask))
		{
			m_pBitmap[nPacketID / 8] |= nMask;
			m_nReceivedPackets++;

			memcpy (pBuffer + nOffset, pData, nDataLength);

			if (nOffset + nDataLength > pInfo->DataSize)
			{
				pInfo->DataSize = nOffset + nDataLength;
			}

			if (   nStatus == GVCP_STATUS_PACKET_RESEND
			    || m_bResendRequested)
			{
				m_nResentPackets++;
			}
		}

		if (   m_bTrailerReceived
		    && IsBlockComplete ())
		{
			FinishBlock (pInfo);

			return PacketFrameDone;
		}
		} break;

	case GVSP_FORMAT_TRAILER: {
		if (nPacketID == 0)
		{
			break;
		}

		m_nLastPacketID = nPacketID - 1;
		m_bTrailerReceived = TRUE;

		if (nDataLength >= sizeof (TGVSPTrailer))
		{
			const TGVSPTrailer *pTrailer = (const TGVSPTrailer *) pData;
			if (   (be2le16 (pTrailer->PayloadType) & ~GVSP_PAYLOAD_EXTENDED_CHUNK)
			    == GVSP_PAYLOAD_IMAGE)
			{
				u32 nSizeY = be2le32 (pTrailer->SizeY);
				if (   nSizeY != 0
				    && nSizeY < pInfo->SizeY)
				{
					pInfo->SizeY = nSizeY;	// variable frame height
				}
			}
		}

		if (IsBlockComplete ())
		{
			FinishBlock (pInfo);

			return PacketFrameDone;
		}

		if (   m_bResendEnabled
		    && !m_bResendRequested
		    && !m_bOverflow
		    && !bExtendedID)
		{
			RequestResends ();
			m_bResendRequested = TRUE;

			return PacketContinue;
		}

		FinishBlock (pInfo);
		} return PacketFrameDone;

	default:
		m_Stats.IgnoredPackets++;	// unsupported packet format (e.g. all-in, multi-part)
		break;
	}

	return PacketContinue;
}

void CGVSPReceiver::StartBlock (u64 nBlockID, size_t nBufferSize, TGVSPFrameInfo *pInfo)
{
	assert (pInfo != 0);

	memset (pInfo, 0, sizeof *pInfo);
	pInfo->BlockID = nBlockID;

	m_bActive = TRUE;
	m_nBlockID = nBlockID;
	m_bLeaderReceived = FALSE;
	m_bTrailerReceived = FALSE;
	m_bOverflow = FALSE;
	m_nLastPacketID = 0;
	m_nReceivedPackets = 0;
	m_nResentPackets = 0;
	m_bResendRequested = FALSE;

	unsigned nPayloadPerPacket = m_nPacketSize - IP_UDP_HEADER_SIZE - GVSP_EXT_HEADER_SIZE;
	unsigned nPackets = nBufferSize / nPayloadPerPacket + 2;
	if (nPackets > m_nBitmapPackets)
	{
		delete [] m_pBitmap;

		m_nBitmapPackets = (nPackets + 7) & ~7U;
		m_pBitmap = new u8[m_nBitmapPackets / 8];
		assert (m_pBitmap != 0);
	}

	memset (m_pBitmap, 0, m_nBitmapPackets / 8);
}

void CGVSPReceiver::FinishBlock (TGVSPFrameInfo *pInfo)
{
	assert (pInfo != 0);
	assert (m_bActive);

	pInfo->Complete =    m_bLeaderReceived
			  && m_bTrailerReceived
			  && !m_bOverflow
			  && IsBlockComplete ();

	pInfo->MissingPackets =   m_bTrailerReceived && m_nLastPacketID > m_nReceivedPackets
				? m_nLastPacketID - m_nReceivedPackets : 0;
	pInfo->ResentPackets = m_nResentPackets;

	if (pInfo->Complete)
	{
		m_Stats.CompleteFrames++;
	}
	else
	{
		m_Stats.IncompleteFrames++;
	}

	m_Stats.MissingPackets += pInfo->MissingPackets;
	m_Stats.ResentPackets += m_nResentPackets;

	m_bActive = FALSE;
	m_bHaveFinished = TRUE;
	m_nLastFinishedBlockID = m_nBlockID;
}

boolean CGVSPReceiver::IsBlockComplete (void) const
{
	return    m_bTrailerReceived
	       && m_nReceivedPackets == m_nLastPacketID;
}

void CGVSPReceiver::RequestResends (void)
{
	assert (m_pResendClient != 0);
	assert (m_pBitmap != 0);

	u32 nLast = m_nLastPacketID;
	if (nLast >= m_nBitmapPackets)
	{
		nLast = m_nBitmapPackets - 1;
	}

	// collect ranges of missing packets
	u32 First[MAX_RESEND_RANGES];
	u32 Last[MAX_RESEND_RANGES];
	unsigned nRanges = 0;
	boolean bTooMany = FALSE;

	for (u32 nID = 1; nID <= nLast; nID++)
	{
		if (m_pBitmap[nID / 8] & (1 << (nID & 7)))
		{
			continue;
		}

		if (   nRanges > 0
		    && Last[nRanges-1] == nID - 1)
		{
			Last[nRanges-1] = nID;
		}
		else if (nRanges < MAX_RESEND_RANGES)
		{
			First[nRanges] = nID;
			Last[nRanges] = nID;
			nRanges++;
		}
		else
		{
			bTooMany = TRUE;
			Last[nRanges-1] = nID;	// extend last range up to here
		}
	}

	if (bTooMany)
	{
		// request everything from the first to the last missing packet at once
		Last[0] = Last[nRanges-1];
		nRanges = 1;
	}

	for (unsigned i = 0; i < nRanges; i++)
	{
		m_pResendClient->RequestResend (m_nStreamChannel, (u16) m_nBlockID, First[i], Last[i]);
		m_Stats.ResendRequests++;
	}
}

boolean CGVSPReceiver::IsNewerBlock (u64 nBlockID) const
{
	u64 nReference = m_bActive ? m_nBlockID : m_nLastFinishedBlockID;

	if (m_bExtendedID)
	{
		return nBlockID > nReference;
	}

	u16 nDiff = (u16) nBlockID - (u16) nReference;

	return nDiff != 0 && nDiff < 0x8000;
}
