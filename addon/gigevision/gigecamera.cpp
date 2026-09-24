//
// gigecamera.cpp
//
// High-level access to a GigE Vision camera: control channel with heartbeat,
// GenICam feature access and image streaming
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
#include <gigevision/gigecamera.h>
#include <gigevision/unzip.h>
#include <circle/sched/task.h>
#include <circle/sched/scheduler.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

#define MAX_DESCRIPTION_SIZE	(16 * 1024 * 1024)
#define MIN_HEARTBEAT_MS	100

static const char FromCamera[] = "gige";

////////////////////////////////////////////////////////////////////////////////
// Heartbeat task: keeps the control privilege by reading the CCP register periodically

class CGigEHeartbeatTask : public CTask
{
public:
	CGigEHeartbeatTask (CGVCPClient *pGVCP, unsigned nIntervalMs)
	:	m_pGVCP (pGVCP),
		m_nIntervalMs (nIntervalMs),
		m_bStop (FALSE),
		m_bLostControl (FALSE)
	{
		SetName ("gige-heartbeat");
	}

	void SetInterval (unsigned nIntervalMs)
	{
		m_nIntervalMs = nIntervalMs;
	}

	void Stop (void)
	{
		m_bStop = TRUE;
		WaitForTermination ();
	}

	void Run (void) override
	{
		CScheduler *pScheduler = CScheduler::Get ();
		assert (pScheduler != 0);

		while (!m_bStop)
		{
			// sleep in short steps to allow a fast Stop()
			for (unsigned nSlept = 0; nSlept < m_nIntervalMs && !m_bStop; nSlept += 50)
			{
				pScheduler->MsSleep (50);
			}

			if (m_bStop)
			{
				break;
			}

			u32 nCCP;
			if (!m_pGVCP->ReadRegister (GEV_REG_CCP, &nCCP))
			{
				CLogger::Get ()->Write (FromCamera, LogWarning, "Heartbeat failed");
			}
			else if (   !(nCCP & (GEV_CCP_CONTROL_ACCESS | GEV_CCP_EXCLUSIVE_ACCESS))
				 && !m_bLostControl)
			{
				CLogger::Get ()->Write (FromCamera, LogWarning, "Control privilege lost");
				m_bLostControl = TRUE;
			}
		}
	}

private:
	CGVCPClient *m_pGVCP;
	volatile unsigned m_nIntervalMs;
	volatile boolean m_bStop;
	boolean m_bLostControl;
};

////////////////////////////////////////////////////////////////////////////////

CGigECamera::CGigECamera (CNetSubSystem *pNetSubSystem)
:	m_pNetSubSystem (pNetSubSystem),
	m_GVCP (pNetSubSystem),
	m_bOpen (FALSE),
	m_pHeartbeat (0),
	m_pNodeMap (0),
	m_pStream (0),
	m_nStreamChannel (0),
	m_bAcquiring (FALSE)
{
}

CGigECamera::~CGigECamera (void)
{
	Close ();

	delete m_pNodeMap;
	m_pNodeMap = 0;
}

boolean CGigECamera::Open (const CIPAddress &rDeviceIP, boolean bExclusive)
{
	assert (!m_bOpen);

	if (!m_GVCP.Open (rDeviceIP))
	{
		return FALSE;
	}

	u32 nCCP = bExclusive ? GEV_CCP_EXCLUSIVE_ACCESS : GEV_CCP_CONTROL_ACCESS;
	if (!m_GVCP.WriteRegister (GEV_REG_CCP, nCCP))
	{
		CString IPString;
		rDeviceIP.Format (&IPString);
		CLogger::Get ()->Write (FromCamera, LogError,
					"Cannot get control of %s (%s)", (const char *) IPString,
					CGVCPClient::GetStatusText (m_GVCP.GetLastStatus ()));

		m_GVCP.Close ();

		return FALSE;
	}

	u32 nHeartbeatMs;
	if (!m_GVCP.ReadRegister (GEV_REG_HEARTBEAT_TIMEOUT, &nHeartbeatMs))
	{
		nHeartbeatMs = 3000;
	}

	unsigned nInterval = nHeartbeatMs / 3;
	if (nInterval < MIN_HEARTBEAT_MS)
	{
		nInterval = MIN_HEARTBEAT_MS;
	}

	m_pHeartbeat = new CGigEHeartbeatTask (&m_GVCP, nInterval);
	assert (m_pHeartbeat != 0);

	m_bOpen = TRUE;

	return TRUE;
}

void CGigECamera::Close (void)
{
	if (!m_bOpen)
	{
		return;
	}

	if (m_bAcquiring)
	{
		StopAcquisition ();
	}

	CloseStream ();

	if (m_pHeartbeat != 0)
	{
		m_pHeartbeat->Stop ();
		m_pHeartbeat = 0;	// task object is deleted by the scheduler
	}

	m_GVCP.WriteRegister (GEV_REG_CCP, 0);		// release control privilege
	m_GVCP.Close ();

	m_bOpen = FALSE;
}

boolean CGigECamera::SetHeartbeatTimeout (unsigned nTimeoutMs)
{
	assert (m_bOpen);

	if (!m_GVCP.WriteRegister (GEV_REG_HEARTBEAT_TIMEOUT, nTimeoutMs))
	{
		return FALSE;
	}

	unsigned nInterval = nTimeoutMs / 3;
	if (nInterval < MIN_HEARTBEAT_MS)
	{
		nInterval = MIN_HEARTBEAT_MS;
	}

	assert (m_pHeartbeat != 0);
	m_pHeartbeat->SetInterval (nInterval);

	return TRUE;
}

boolean CGigECamera::GetManufacturerName (CString *pString)
{
	return m_GVCP.ReadString (GEV_REG_MANUFACTURER_NAME, 32, pString);
}

boolean CGigECamera::GetModelName (CString *pString)
{
	return m_GVCP.ReadString (GEV_REG_MODEL_NAME, 32, pString);
}

boolean CGigECamera::GetDeviceVersion (CString *pString)
{
	return m_GVCP.ReadString (GEV_REG_DEVICE_VERSION, 32, pString);
}

boolean CGigECamera::GetSerialNumber (CString *pString)
{
	return m_GVCP.ReadString (GEV_REG_SERIAL_NUMBER, 16, pString);
}

boolean CGigECamera::GetUserDefinedName (CString *pString)
{
	return m_GVCP.ReadString (GEV_REG_USER_NAME, 16, pString);
}

boolean CGigECamera::LoadDescription (void)
{
	assert (m_bOpen);

	CString URL;
	if (!m_GVCP.ReadString (GEV_REG_FIRST_URL, 512, &URL))
	{
		return FALSE;
	}

	CString FileName;
	u32 nAddress, nLength;
	if (!ParseURL (URL, &FileName, &nAddress, &nLength))
	{
		// try the second URL
		if (   !m_GVCP.ReadString (GEV_REG_SECOND_URL, 512, &URL)
		    || !ParseURL (URL, &FileName, &nAddress, &nLength))
		{
			CLogger::Get ()->Write (FromCamera, LogError,
						"Unsupported description URL: %s", (const char *) URL);

			return FALSE;
		}
	}

	if (   nLength == 0
	    || nLength > MAX_DESCRIPTION_SIZE)
	{
		return FALSE;
	}

	CLogger::Get ()->Write (FromCamera, LogDebug, "Loading %s (%u bytes)",
				(const char *) FileName, nLength);

	u8 *pFile = new u8[nLength + 1];
	if (pFile == 0)
	{
		return FALSE;
	}

	if (!m_GVCP.ReadMemory (nAddress, pFile, nLength))
	{
		CLogger::Get ()->Write (FromCamera, LogError, "Cannot read description file");

		delete [] pFile;

		return FALSE;
	}

	pFile[nLength] = '\0';

	if (CUnzip::IsZipArchive (pFile, nLength))
	{
		u8 *pXML;
		size_t nXMLSize;
		boolean bOK = CUnzip::Extract (pFile, nLength, ".xml", &pXML, &nXMLSize);

		delete [] pFile;

		if (!bOK)
		{
			CLogger::Get ()->Write (FromCamera, LogError, "Cannot unzip description");

			return FALSE;
		}

		return LoadDescription ((char *) pXML, nXMLSize);
	}

	return LoadDescription ((char *) pFile, nLength);
}

boolean CGigECamera::LoadDescription (char *pXML, size_t nLength)
{
	assert (pXML != 0);

	delete m_pNodeMap;

	m_pNodeMap = new CGenICamNodeMap (&m_GVCP);
	assert (m_pNodeMap != 0);

	if (!m_pNodeMap->Load (pXML, nLength))
	{
		delete m_pNodeMap;
		m_pNodeMap = 0;

		return FALSE;
	}

	return TRUE;
}

boolean CGigECamera::OpenStream (unsigned nChannel, unsigned nPacketSize)
{
	assert (m_bOpen);
	assert (m_pStream == 0);

	u32 nChannels;
	if (   !m_GVCP.ReadRegister (GEV_REG_NUM_STREAM_CHANNELS, &nChannels)
	    || nChannel >= nChannels)
	{
		CLogger::Get ()->Write (FromCamera, LogError, "Stream channel %u not available",
					nChannel);

		return FALSE;
	}

	m_nStreamChannel = nChannel;

	m_pStream = new CGVSPReceiver (m_pNetSubSystem, &m_GVCP, nChannel);
	assert (m_pStream != 0);

	if (!m_pStream->Initialize ())
	{
		delete m_pStream;
		m_pStream = 0;

		return FALSE;
	}

	// packet size (the device may round it down)
	if (nPacketSize > 1500)
	{
		nPacketSize = 1500;		// Circle does not support jumbo frames
	}

	u32 nSCPS;
	if (!m_GVCP.ReadRegister (GEV_REG_SCPS (nChannel), &nSCPS))
	{
		nSCPS = 0;
	}

	nSCPS &= GEV_SCPS_BIG_ENDIAN;
	nSCPS |= GEV_SCPS_DO_NOT_FRAGMENT | (nPacketSize & GEV_SCPS_PACKET_SIZE_MASK);
	if (!m_GVCP.WriteRegister (GEV_REG_SCPS (nChannel), nSCPS))
	{
		CLogger::Get ()->Write (FromCamera, LogWarning, "Cannot set packet size");
	}

	if (   m_GVCP.ReadRegister (GEV_REG_SCPS (nChannel), &nSCPS)
	    && (nSCPS & GEV_SCPS_PACKET_SIZE_MASK) > 100)
	{
		m_pStream->SetPacketSize (nSCPS & GEV_SCPS_PACKET_SIZE_MASK);
	}
	else
	{
		m_pStream->SetPacketSize (nPacketSize);
	}

	// destination address (network byte order register value)
	const CIPAddress *pOwnIP = m_pNetSubSystem->GetConfig ()->GetIPAddress ();
	assert (pOwnIP != 0);
	const u8 *pIP = pOwnIP->Get ();
	u32 nDestIP = (u32) pIP[0] << 24 | (u32) pIP[1] << 16 | (u32) pIP[2] << 8 | pIP[3];
	if (!m_GVCP.WriteRegister (GEV_REG_SCDA (nChannel), nDestIP))
	{
		CloseStream ();

		return FALSE;
	}

	// writing the host port enables the channel
	if (!m_GVCP.WriteRegister (GEV_REG_SCP (nChannel), m_pStream->GetPort ()))
	{
		CloseStream ();

		return FALSE;
	}

	return TRUE;
}

void CGigECamera::CloseStream (void)
{
	if (m_pStream == 0)
	{
		return;
	}

	m_GVCP.WriteRegister (GEV_REG_SCP (m_nStreamChannel), 0);	// disable channel

	delete m_pStream;
	m_pStream = 0;
}

boolean CGigECamera::SetPacketDelay (u32 nDelay)
{
	if (m_pStream == 0)
	{
		return FALSE;
	}

	return m_GVCP.WriteRegister (GEV_REG_SCPD (m_nStreamChannel), nDelay);
}

boolean CGigECamera::StartAcquisition (void)
{
	if (m_pNodeMap == 0)
	{
		return FALSE;
	}

	if (m_pNodeMap->IsFeature ("TLParamsLocked"))
	{
		m_pNodeMap->SetInteger ("TLParamsLocked", 1);
	}

	if (m_pStream != 0)
	{
		m_pStream->Flush ();
	}

	if (!m_pNodeMap->ExecuteCommand ("AcquisitionStart"))
	{
		CLogger::Get ()->Write (FromCamera, LogError, "AcquisitionStart failed");

		return FALSE;
	}

	m_bAcquiring = TRUE;

	return TRUE;
}

boolean CGigECamera::StopAcquisition (void)
{
	if (m_pNodeMap == 0)
	{
		return FALSE;
	}

	boolean bOK = m_pNodeMap->ExecuteCommand ("AcquisitionStop");

	if (m_pNodeMap->IsFeature ("TLParamsLocked"))
	{
		m_pNodeMap->SetInteger ("TLParamsLocked", 0);
	}

	m_bAcquiring = FALSE;

	return bOK;
}

size_t CGigECamera::GetPayloadSize (void)
{
	if (m_pNodeMap == 0)
	{
		return 0;
	}

	s64 nValue;
	if (   m_pNodeMap->GetInteger ("PayloadSize", &nValue)
	    && nValue > 0)
	{
		return (size_t) nValue;
	}

	// estimate from the image format
	s64 nWidth, nHeight, nPixelFormat;
	if (   !m_pNodeMap->GetInteger ("Width", &nWidth)
	    || !m_pNodeMap->GetInteger ("Height", &nHeight)
	    || !m_pNodeMap->GetEnumValue ("PixelFormat", &nPixelFormat))
	{
		return 0;
	}

	return (size_t) (nWidth * nHeight * GVSP_PIX_BITS_PER_PIXEL (nPixelFormat) + 7) / 8;
}

// Format: "Local:[///]filename.ext;address;length[?SchemaVersion=x.y.z]"
// address and length are hexadecimal (with or without "0x")
boolean CGigECamera::ParseURL (const char *pURL, CString *pFileName, u32 *pAddress, u32 *pLength)
{
	assert (pURL != 0);

	if (strncasecmp (pURL, "local:", 6) != 0)
	{
		return FALSE;
	}

	const char *p = pURL + 6;
	while (*p == '/')
	{
		p++;
	}

	const char *pSemicolon = strchr (p, ';');
	if (pSemicolon == 0)
	{
		return FALSE;
	}

	CString FileName;
	for (const char *q = p; q < pSemicolon; q++)
	{
		FileName.Append (*q);
	}
	*pFileName = FileName;

	u32 Values[2];
	p = pSemicolon + 1;
	for (unsigned i = 0; i < 2; i++)
	{
		if (   p[0] == '0'
		    && (p[1] == 'x' || p[1] == 'X'))
		{
			p += 2;
		}

		char *pEnd;
		Values[i] = (u32) strtoul (p, &pEnd, 16);
		if (pEnd == p)
		{
			return FALSE;
		}

		p = pEnd;
		if (i == 0)
		{
			if (*p != ';')
			{
				return FALSE;
			}

			p++;
		}
	}

	*pAddress = Values[0];
	*pLength = Values[1];

	return TRUE;
}
