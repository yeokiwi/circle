//
// gvcpclient.cpp
//
// GigE Vision Control Protocol (GVCP) client (application side)
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
#include <gigevision/gvcpclient.h>
#include <circle/net/in.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

static const char FromGVCP[] = "gvcp";

CGVCPClient::CGVCPClient (CNetSubSystem *pNetSubSystem)
:	m_pNetSubSystem (pNetSubSystem),
	m_pSocket (0),
	m_nTimeoutMs (GVCP_DEFAULT_TIMEOUT_MS),
	m_nRetries (GVCP_DEFAULT_RETRIES),
	m_nReqID (0),
	m_nLastStatus (GVCP_STATUS_SUCCESS)
{
	assert (m_pNetSubSystem != 0);
}

CGVCPClient::~CGVCPClient (void)
{
	Close ();

	m_pNetSubSystem = 0;
}

boolean CGVCPClient::Open (const CIPAddress &rDeviceIP)
{
	assert (m_pSocket == 0);

	m_DeviceIP.Set (rDeviceIP);

	m_pSocket = new CSocket (m_pNetSubSystem, IPPROTO_UDP);
	assert (m_pSocket != 0);

	if (m_pSocket->Bind (0) < 0)
	{
		CLogger::Get ()->Write (FromGVCP, LogError, "Cannot bind socket");

		delete m_pSocket;
		m_pSocket = 0;

		return FALSE;
	}

	return TRUE;
}

void CGVCPClient::Close (void)
{
	m_Mutex.Acquire ();

	delete m_pSocket;
	m_pSocket = 0;

	m_Mutex.Release ();
}

void CGVCPClient::SetTimeout (unsigned nTimeoutMs, unsigned nRetries)
{
	assert (nTimeoutMs > 0);
	m_nTimeoutMs = nTimeoutMs;
	m_nRetries = nRetries;
}

boolean CGVCPClient::ReadRegister (u32 nAddress, u32 *pValue)
{
	return ReadRegisters (&nAddress, pValue, 1);
}

boolean CGVCPClient::WriteRegister (u32 nAddress, u32 nValue)
{
	u32 Payload[2] = {le2be32 (nAddress), le2be32 (nValue)};

	u8 Ack[4];
	unsigned nAckLength;
	if (!Transaction (GVCP_WRITEREG_CMD, Payload, sizeof Payload,
			  Ack, sizeof Ack, &nAckLength))
	{
		return FALSE;
	}

	return TRUE;
}

boolean CGVCPClient::ReadRegisters (const u32 *pAddresses, u32 *pValues, unsigned nCount)
{
	assert (pAddresses != 0);
	assert (pValues != 0);

	if (   nCount == 0
	    || nCount > GVCP_MAX_READREG)
	{
		return FALSE;
	}

	u32 Payload[GVCP_MAX_READREG];
	for (unsigned i = 0; i < nCount; i++)
	{
		if (pAddresses[i] & 3)
		{
			return FALSE;
		}

		Payload[i] = le2be32 (pAddresses[i]);
	}

	u32 Ack[GVCP_MAX_READREG];
	unsigned nAckLength;
	if (!Transaction (GVCP_READREG_CMD, Payload, nCount * sizeof (u32),
			  Ack, sizeof Ack, &nAckLength))
	{
		return FALSE;
	}

	if (nAckLength < nCount * sizeof (u32))
	{
		return FALSE;
	}

	for (unsigned i = 0; i < nCount; i++)
	{
		pValues[i] = be2le32 (Ack[i]);
	}

	return TRUE;
}

boolean CGVCPClient::ReadMemory (u32 nAddress, void *pBuffer, unsigned nLength)
{
	assert (pBuffer != 0);
	u8 *pDest = (u8 *) pBuffer;

	while (nLength > 0)
	{
		// READMEM needs 4-byte aligned address and count
		u32 nAlignedAddress = nAddress & ~3U;
		unsigned nSkip = nAddress - nAlignedAddress;

		unsigned nChunk = nSkip + nLength;
		if (nChunk > GVCP_MAX_READMEM)
		{
			nChunk = GVCP_MAX_READMEM;
		}
		nChunk = (nChunk + 3) & ~3U;

		u8 Buffer[GVCP_MAX_READMEM];
		if (!ReadMemoryChunk (nAlignedAddress, Buffer, nChunk))
		{
			return FALSE;
		}

		unsigned nCopy = nChunk - nSkip;
		if (nCopy > nLength)
		{
			nCopy = nLength;
		}

		memcpy (pDest, Buffer + nSkip, nCopy);

		pDest += nCopy;
		nAddress += nCopy;
		nLength -= nCopy;
	}

	return TRUE;
}

boolean CGVCPClient::WriteMemory (u32 nAddress, const void *pBuffer, unsigned nLength)
{
	assert (pBuffer != 0);
	const u8 *pSrc = (const u8 *) pBuffer;

	if (   (nAddress & 3)
	    || (nLength & 3))
	{
		// read-modify-write of the unaligned edges
		u32 nAlignedAddress = nAddress & ~3U;
		unsigned nAlignedLength = ((nAddress + nLength + 3) & ~3U) - nAlignedAddress;

		u8 *pTemp = new u8[nAlignedLength];
		assert (pTemp != 0);

		if (   !ReadMemory (nAlignedAddress, pTemp, 4)
		    || !ReadMemory (nAlignedAddress + nAlignedLength - 4,
				    pTemp + nAlignedLength - 4, 4))
		{
			delete [] pTemp;

			return FALSE;
		}

		memcpy (pTemp + (nAddress - nAlignedAddress), pSrc, nLength);

		boolean bOK = WriteMemory (nAlignedAddress, pTemp, nAlignedLength);

		delete [] pTemp;

		return bOK;
	}

	while (nLength > 0)
	{
		unsigned nChunk = nLength;
		if (nChunk > GVCP_MAX_READMEM)
		{
			nChunk = GVCP_MAX_READMEM;
		}

		if (!WriteMemoryChunk (nAddress, pSrc, nChunk))
		{
			return FALSE;
		}

		pSrc += nChunk;
		nAddress += nChunk;
		nLength -= nChunk;
	}

	return TRUE;
}

boolean CGVCPClient::ReadString (u32 nAddress, unsigned nMaxLength, CString *pString)
{
	assert (pString != 0);
	assert (nMaxLength > 0);

	char *pBuffer = new char[nMaxLength + 1];
	assert (pBuffer != 0);

	if (!ReadMemory (nAddress, pBuffer, nMaxLength))
	{
		delete [] pBuffer;

		return FALSE;
	}

	pBuffer[nMaxLength] = '\0';
	*pString = pBuffer;

	delete [] pBuffer;

	return TRUE;
}

boolean CGVCPClient::RequestResend (unsigned nStreamChannel, u16 nBlockID,
				    u32 nFirstPacketID, u32 nLastPacketID)
{
	if (m_pSocket == 0)
	{
		return FALSE;
	}

	m_Mutex.Acquire ();

	TGVCPCommandHeader *pHeader = (TGVCPCommandHeader *) m_TxBuffer;
	pHeader->Key = GVCP_KEY;
	pHeader->Flags = 0;			// no acknowledge
	pHeader->Command = le2be16 (GVCP_PACKETRESEND_CMD);
	pHeader->Length = le2be16 (sizeof (TGVCPPacketResendCmd));
	pHeader->ReqID = le2be16 (NextReqID ());

	TGVCPPacketResendCmd *pCmd = (TGVCPPacketResendCmd *) (m_TxBuffer + sizeof *pHeader);
	pCmd->StreamChannel = le2be16 (nStreamChannel);
	pCmd->BlockID = le2be16 (nBlockID);
	pCmd->FirstPacketID = le2be32 (nFirstPacketID & 0xFFFFFF);
	pCmd->LastPacketID = le2be32 (nLastPacketID & 0xFFFFFF);

	int nResult = m_pSocket->SendTo (m_TxBuffer, sizeof *pHeader + sizeof *pCmd, 0,
					 m_DeviceIP, GVCP_PORT);

	m_Mutex.Release ();

	return nResult > 0;
}

boolean CGVCPClient::Read (u64 nAddress, void *pBuffer, unsigned nLength)
{
	if (nAddress > 0xFFFFFFFFU)
	{
		return FALSE;
	}

	// Registers are preferably accessed with READREG, which every device supports
	if (   nLength == 4
	    && !(nAddress & 3))
	{
		u32 nValue;
		if (!ReadRegister ((u32) nAddress, &nValue))
		{
			return FALSE;
		}

		nValue = le2be32 (nValue);	// back to device (big endian) byte order
		memcpy (pBuffer, &nValue, 4);

		return TRUE;
	}

	return ReadMemory ((u32) nAddress, pBuffer, nLength);
}

boolean CGVCPClient::Write (u64 nAddress, const void *pBuffer, unsigned nLength)
{
	if (nAddress > 0xFFFFFFFFU)
	{
		return FALSE;
	}

	// WRITEMEM is optional, so use WRITEREG for 32-bit registers
	if (   nLength == 4
	    && !(nAddress & 3))
	{
		u32 nValue;
		memcpy (&nValue, pBuffer, 4);

		return WriteRegister ((u32) nAddress, be2le32 (nValue));
	}

	return WriteMemory ((u32) nAddress, pBuffer, nLength);
}

int CGVCPClient::Discover (CNetSubSystem *pNetSubSystem, TGigEDeviceInfo *pInfo,
			   unsigned nMaxDevices, unsigned nTimeoutMs)
{
	assert (pNetSubSystem != 0);
	assert (pInfo != 0);

	CSocket Socket (pNetSubSystem, IPPROTO_UDP);
	if (   Socket.Bind (0) < 0
	    || Socket.SetOptionBroadcast (TRUE) < 0)
	{
		return -1;
	}

	TGVCPCommandHeader Header;
	Header.Key = GVCP_KEY;
	Header.Flags = GVCP_FLAG_ACK_REQUIRED | GVCP_FLAG_DISCOVERY_BROADCAST_ACK;
	Header.Command = le2be16 (GVCP_DISCOVERY_CMD);
	Header.Length = 0;
	Header.ReqID = le2be16 (1);

	CIPAddress Broadcast;
	Broadcast.SetBroadcast ();

	if (Socket.SendTo (&Header, sizeof Header, 0, Broadcast, GVCP_PORT) < 0)
	{
		return -1;
	}

	u8 *pBuffer = new u8[FRAME_BUFFER_SIZE];
	assert (pBuffer != 0);

	unsigned nDevices = 0;

	u64 nDeadline = CTimer::GetClockTicks64 () + (u64) nTimeoutMs * (CLOCKHZ / 1000);
	while (nDevices < nMaxDevices)
	{
		u64 nNow = CTimer::GetClockTicks64 ();
		if (nNow >= nDeadline)
		{
			break;
		}

		Socket.SetOptionReceiveTimeout ((unsigned) (nDeadline - nNow));

		CIPAddress Sender;
		u16 nSenderPort;
		int nLength = Socket.ReceiveFrom (pBuffer, FRAME_BUFFER_SIZE, 0, &Sender, &nSenderPort);
		if (nLength <= 0)
		{
			continue;
		}

		const TGVCPAckHeader *pAck = (const TGVCPAckHeader *) pBuffer;
		if (   (unsigned) nLength < sizeof *pAck + sizeof (TGVCPDiscoveryAck)
		    || be2le16 (pAck->Acknowledge) != GVCP_DISCOVERY_ACK
		    || be2le16 (pAck->Status) != GVCP_STATUS_SUCCESS
		    || be2le16 (pAck->AckID) != 1)
		{
			continue;
		}

		TGigEDeviceInfo Info;
		ParseDiscoveryAck ((const TGVCPDiscoveryAck *) (pBuffer + sizeof *pAck), &Info);

		// devices may answer twice (unicast and broadcast)
		boolean bDuplicate = FALSE;
		for (unsigned i = 0; i < nDevices; i++)
		{
			if (memcmp (pInfo[i].MACAddress, Info.MACAddress, 6) == 0)
			{
				bDuplicate = TRUE;

				break;
			}
		}

		if (!bDuplicate)
		{
			pInfo[nDevices++] = Info;
		}
	}

	delete [] pBuffer;

	return (int) nDevices;
}

boolean CGVCPClient::ForceIP (CNetSubSystem *pNetSubSystem, const u8 *pMACAddress,
			      const CIPAddress &rIP, const CIPAddress &rSubnetMask,
			      const CIPAddress &rGateway)
{
	assert (pNetSubSystem != 0);
	assert (pMACAddress != 0);

	CSocket Socket (pNetSubSystem, IPPROTO_UDP);
	if (   Socket.Bind (0) < 0
	    || Socket.SetOptionBroadcast (TRUE) < 0)
	{
		return FALSE;
	}

	struct
	{
		TGVCPCommandHeader	Header;
		TGVCPForceIPCmd		Cmd;
	}
	PACKED Packet;

	memset (&Packet, 0, sizeof Packet);
	Packet.Header.Key = GVCP_KEY;
	Packet.Header.Flags = GVCP_FLAG_ACK_REQUIRED;
	Packet.Header.Command = le2be16 (GVCP_FORCEIP_CMD);
	Packet.Header.Length = le2be16 (sizeof (TGVCPForceIPCmd));
	Packet.Header.ReqID = le2be16 (1);

	Packet.Cmd.MACHigh = le2be16 (  (u16) pMACAddress[0] << 8 | pMACAddress[1]);
	Packet.Cmd.MACLow  = le2be32 (  (u32) pMACAddress[2] << 24 | (u32) pMACAddress[3] << 16
				      | (u32) pMACAddress[4] << 8  | pMACAddress[5]);
	rIP.CopyTo ((u8 *) &Packet.Cmd.StaticIP);
	rSubnetMask.CopyTo ((u8 *) &Packet.Cmd.SubnetMask);
	rGateway.CopyTo ((u8 *) &Packet.Cmd.DefaultGateway);

	CIPAddress Broadcast;
	Broadcast.SetBroadcast ();

	u8 *pBuffer = new u8[FRAME_BUFFER_SIZE];
	assert (pBuffer != 0);

	boolean bOK = FALSE;
	for (unsigned nTry = 0; !bOK && nTry <= GVCP_DEFAULT_RETRIES; nTry++)
	{
		if (Socket.SendTo (&Packet, sizeof Packet, 0, Broadcast, GVCP_PORT) < 0)
		{
			break;
		}

		// the device may need some time to reconfigure its interface
		u64 nDeadline = CTimer::GetClockTicks64 () + 1000 * (CLOCKHZ / 1000);
		while (!bOK)
		{
			u64 nNow = CTimer::GetClockTicks64 ();
			if (nNow >= nDeadline)
			{
				break;
			}

			Socket.SetOptionReceiveTimeout ((unsigned) (nDeadline - nNow));

			CIPAddress Sender;
			u16 nSenderPort;
			int nLength = Socket.ReceiveFrom (pBuffer, FRAME_BUFFER_SIZE, 0,
							  &Sender, &nSenderPort);
			if ((unsigned) nLength < sizeof (TGVCPAckHeader))
			{
				continue;
			}

			const TGVCPAckHeader *pAck = (const TGVCPAckHeader *) pBuffer;
			if (   be2le16 (pAck->Acknowledge) == GVCP_FORCEIP_ACK
			    && be2le16 (pAck->AckID) == 1)
			{
				bOK = be2le16 (pAck->Status) == GVCP_STATUS_SUCCESS;
				if (!bOK)
				{
					CLogger::Get ()->Write (FromGVCP, LogWarning,
								"FORCEIP failed (%s)",
								GetStatusText (be2le16 (pAck->Status)));
					nTry = GVCP_DEFAULT_RETRIES;

					break;
				}
			}
		}
	}

	delete [] pBuffer;

	return bOK;
}

const char *CGVCPClient::GetStatusText (u16 nStatus)
{
	switch (nStatus)
	{
	case GVCP_STATUS_SUCCESS:			return "Success";
	case GVCP_STATUS_PACKET_RESEND:			return "Packet resend";
	case GVCP_STATUS_NOT_IMPLEMENTED:		return "Not implemented";
	case GVCP_STATUS_INVALID_PARAMETER:		return "Invalid parameter";
	case GVCP_STATUS_INVALID_ADDRESS:		return "Invalid address";
	case GVCP_STATUS_WRITE_PROTECT:			return "Write protect";
	case GVCP_STATUS_BAD_ALIGNMENT:			return "Bad alignment";
	case GVCP_STATUS_ACCESS_DENIED:			return "Access denied";
	case GVCP_STATUS_BUSY:				return "Busy";
	case GVCP_STATUS_PACKET_UNAVAILABLE:		return "Packet unavailable";
	case GVCP_STATUS_DATA_OVERRUN:			return "Data overrun";
	case GVCP_STATUS_INVALID_HEADER:		return "Invalid header";
	case GVCP_STATUS_PACKET_NOT_YET_AVAILABLE:	return "Packet not yet available";
	case GVCP_STATUS_PACKET_AND_PREV_REMOVED:	return "Packet and previous removed";
	case GVCP_STATUS_PACKET_REMOVED:		return "Packet removed";
	case GVCP_STATUS_NO_REF_TIME:			return "No reference time";
	case GVCP_STATUS_PACKET_TEMP_UNAVAILABLE:	return "Packet temporarily unavailable";
	case GVCP_STATUS_OVERFLOW:			return "Overflow";
	case GVCP_STATUS_ACTION_LATE:			return "Action late";
	case GVCP_STATUS_ERROR:				return "Error";
	default:					return "Unknown status";
	}
}

boolean CGVCPClient::Transaction (u16 nCommand, const void *pPayload, unsigned nPayloadLength,
				  void *pAckPayload, unsigned nAckBufferSize, unsigned *pAckLength)
{
	assert (nPayloadLength <= GVCP_MAX_PAYLOAD);
	assert (pAckLength != 0);

	m_Mutex.Acquire ();

	if (m_pSocket == 0)
	{
		m_Mutex.Release ();

		return FALSE;
	}

	u16 nReqID = NextReqID ();

	TGVCPCommandHeader *pHeader = (TGVCPCommandHeader *) m_TxBuffer;
	pHeader->Key = GVCP_KEY;
	pHeader->Flags = GVCP_FLAG_ACK_REQUIRED;
	pHeader->Command = le2be16 (nCommand);
	pHeader->Length = le2be16 (nPayloadLength);
	pHeader->ReqID = le2be16 (nReqID);

	if (nPayloadLength > 0)
	{
		assert (pPayload != 0);
		memcpy (m_TxBuffer + sizeof *pHeader, pPayload, nPayloadLength);
	}

	u16 nExpectedAck = nCommand + 1;
	boolean bOK = FALSE;
	boolean bDone = FALSE;

	// retries use the same request ID, so that the device can detect duplicates
	for (unsigned nTry = 0; !bDone && nTry <= m_nRetries; nTry++)
	{
		if (m_pSocket->SendTo (m_TxBuffer, sizeof *pHeader + nPayloadLength, 0,
				       m_DeviceIP, GVCP_PORT) < 0)
		{
			break;
		}

		u64 nDeadline = CTimer::GetClockTicks64 () + (u64) m_nTimeoutMs * (CLOCKHZ / 1000);
		while (!bDone)
		{
			u64 nNow = CTimer::GetClockTicks64 ();
			if (nNow >= nDeadline)
			{
				break;
			}

			m_pSocket->SetOptionReceiveTimeout ((unsigned) (nDeadline - nNow));

			CIPAddress Sender;
			u16 nSenderPort;
			int nLength = m_pSocket->ReceiveFrom (m_RxBuffer, sizeof m_RxBuffer, 0,
							      &Sender, &nSenderPort);
			if (   nLength < (int) sizeof (TGVCPAckHeader)
			    || Sender != m_DeviceIP)
			{
				continue;
			}

			const TGVCPAckHeader *pAck = (const TGVCPAckHeader *) m_RxBuffer;
			if (be2le16 (pAck->AckID) != nReqID)
			{
				continue;		// stale acknowledge from an earlier try
			}

			u16 nAck = be2le16 (pAck->Acknowledge);
			unsigned nAckLength = be2le16 (pAck->Length);
			if (nAckLength > nLength - sizeof *pAck)
			{
				nAckLength = nLength - sizeof *pAck;
			}

			if (nAck == GVCP_PENDING_ACK)
			{
				// device needs more time, extend the deadline
				unsigned nPendingMs = 0;
				if (nAckLength >= sizeof (TGVCPPendingAck))
				{
					const TGVCPPendingAck *pPending =
						(const TGVCPPendingAck *) (m_RxBuffer + sizeof *pAck);
					nPendingMs = be2le16 (pPending->TimeToCompletion);
				}

				nDeadline =   CTimer::GetClockTicks64 ()
					    + (u64) (nPendingMs + m_nTimeoutMs) * (CLOCKHZ / 1000);

				continue;
			}

			if (nAck != nExpectedAck)
			{
				continue;
			}

			m_nLastStatus = be2le16 (pAck->Status);
			bOK = m_nLastStatus == GVCP_STATUS_SUCCESS;
			bDone = TRUE;

			if (nAckLength > nAckBufferSize)
			{
				nAckLength = nAckBufferSize;
			}

			if (nAckLength > 0)
			{
				assert (pAckPayload != 0);
				memcpy (pAckPayload, m_RxBuffer + sizeof *pAck, nAckLength);
			}

			*pAckLength = nAckLength;
		}
	}

	m_Mutex.Release ();

	if (!bDone)
	{
		CLogger::Get ()->Write (FromGVCP, LogDebug, "Command 0x%04X timed out", nCommand);
	}
	else if (!bOK)
	{
		CLogger::Get ()->Write (FromGVCP, LogDebug, "Command 0x%04X failed (%s)",
					nCommand, GetStatusText (m_nLastStatus));
	}

	return bOK;
}

boolean CGVCPClient::ReadMemoryChunk (u32 nAddress, void *pBuffer, unsigned nLength)
{
	assert (!(nAddress & 3));
	assert (!(nLength & 3));
	assert (nLength <= GVCP_MAX_READMEM);

	struct
	{
		u32	Address;
		u16	Reserved;
		u16	Count;
	}
	PACKED Cmd;

	Cmd.Address = le2be32 (nAddress);
	Cmd.Reserved = 0;
	Cmd.Count = le2be16 (nLength);

	u8 Ack[4 + GVCP_MAX_READMEM];
	unsigned nAckLength;
	if (!Transaction (GVCP_READMEM_CMD, &Cmd, sizeof Cmd, Ack, sizeof Ack, &nAckLength))
	{
		return FALSE;
	}

	if (   nAckLength < 4 + nLength
	    || memcmp (Ack, &Cmd.Address, 4) != 0)
	{
		return FALSE;
	}

	memcpy (pBuffer, Ack + 4, nLength);

	return TRUE;
}

boolean CGVCPClient::WriteMemoryChunk (u32 nAddress, const void *pBuffer, unsigned nLength)
{
	assert (!(nAddress & 3));
	assert (!(nLength & 3));
	assert (nLength <= GVCP_MAX_READMEM);

	u8 Cmd[4 + GVCP_MAX_READMEM];
	u32 nAddressBE = le2be32 (nAddress);
	memcpy (Cmd, &nAddressBE, 4);
	memcpy (Cmd + 4, pBuffer, nLength);

	u8 Ack[4];
	unsigned nAckLength;

	return Transaction (GVCP_WRITEMEM_CMD, Cmd, 4 + nLength, Ack, sizeof Ack, &nAckLength);
}

u16 CGVCPClient::NextReqID (void)
{
	if (++m_nReqID == 0)		// 0 is not allowed
	{
		m_nReqID = 1;
	}

	return m_nReqID;
}

void CGVCPClient::ParseDiscoveryAck (const TGVCPDiscoveryAck *pAck, TGigEDeviceInfo *pInfo)
{
	assert (pAck != 0);
	assert (pInfo != 0);

	pInfo->SpecVersionMajor = be2le16 (pAck->SpecVersionMajor);
	pInfo->SpecVersionMinor = be2le16 (pAck->SpecVersionMinor);
	pInfo->DeviceMode = be2le32 (pAck->DeviceMode);

	u16 nMACHigh = be2le16 (pAck->MACHigh);
	u32 nMACLow = be2le32 (pAck->MACLow);
	pInfo->MACAddress[0] = nMACHigh >> 8;
	pInfo->MACAddress[1] = nMACHigh & 0xFF;
	pInfo->MACAddress[2] = nMACLow >> 24;
	pInfo->MACAddress[3] = (nMACLow >> 16) & 0xFF;
	pInfo->MACAddress[4] = (nMACLow >> 8) & 0xFF;
	pInfo->MACAddress[5] = nMACLow & 0xFF;

	pInfo->IPConfigOptions = be2le32 (pAck->IPConfigOptions);
	pInfo->IPConfigCurrent = be2le32 (pAck->IPConfigCurrent);

	// IP addresses are in network byte order on the wire, like in CIPAddress
	pInfo->IPAddress.Set ((const u8 *) &pAck->CurrentIP);
	pInfo->SubnetMask.Set ((const u8 *) &pAck->SubnetMask);
	pInfo->DefaultGateway.Set ((const u8 *) &pAck->DefaultGateway);

#define COPY_STRING(field)	memcpy (pInfo->field, pAck->field, sizeof pAck->field); \
				pInfo->field[sizeof pAck->field] = '\0';
	COPY_STRING (ManufacturerName);
	COPY_STRING (ModelName);
	COPY_STRING (DeviceVersion);
	COPY_STRING (ManufacturerInfo);
	COPY_STRING (SerialNumber);
	COPY_STRING (UserDefinedName);
#undef COPY_STRING
}
