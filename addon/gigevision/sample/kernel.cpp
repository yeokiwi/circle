//
// kernel.cpp
//
// GigE Vision camera sample: discovers a camera, shows its features,
// receives images and draws them on the screen.
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
#include "kernel.h"
#include <gigevision/gvcpclient.h>
#include <circle/string.h>
#include <circle/util.h>

#define USE_DHCP

#ifndef USE_DHCP
static const u8 IPAddress[]      = {192, 168, 0, 250};
static const u8 NetMask[]        = {255, 255, 255, 0};
static const u8 DefaultGateway[] = {192, 168, 0, 1};
static const u8 DNSServer[]      = {192, 168, 0, 1};
#endif

// If the camera is not on our subnet (e.g. it uses a link-local address), it is
// assigned a temporary IP address in our subnet with this host part (last byte).
#define FORCE_IP_HOST		222

// Image size is limited to this (0 to keep the camera settings)
#define MAX_WIDTH		640
#define MAX_HEIGHT		480

#define PACKET_SIZE		1500		// max. without jumbo frames
#define STATUS_INTERVAL_SECS	5
#define RUN_TIME_SECS		0		// 0 to run forever

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_USBHCI (&m_Interrupt, &m_Timer)
#ifndef USE_DHCP
	, m_Net (IPAddress, NetMask, DefaultGateway, DNSServer)
#endif
{
	m_ActLED.Blink (5);	// show we are alive
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Screen.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}

	if (bOK)
	{
		CDevice *pTarget = m_DeviceNameService.GetDevice (m_Options.GetLogDevice (), FALSE);
		if (pTarget == 0)
		{
			pTarget = &m_Screen;
		}

		bOK = m_Logger.Initialize (pTarget);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		bOK = m_USBHCI.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Net.Initialize ();
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice, "Compile time: " __DATE__ " " __TIME__);

	CString IPString;
	m_Net.GetConfig ()->GetIPAddress ()->Format (&IPString);
	m_Logger.Write (FromKernel, LogNotice, "Our IP address is %s", (const char *) IPString);

	CIPAddress CameraIP;
	if (!FindCamera (&CameraIP))
	{
		return ShutdownHalt;
	}

	CGigECamera Camera (&m_Net);
	if (!Camera.Open (CameraIP))
	{
		m_Logger.Write (FromKernel, LogError, "Cannot open camera");

		return ShutdownHalt;
	}

	if (!Camera.LoadDescription ())
	{
		m_Logger.Write (FromKernel, LogError, "Cannot load GenICam description");

		return ShutdownHalt;
	}

	CGenICamNodeMap *pNodeMap = Camera.GetNodeMap ();
	m_Logger.Write (FromKernel, LogNotice, "GenICam description of %s %s (%u nodes)",
			pNodeMap->GetDescriptionAttribute ("VendorName") ?: "?",
			pNodeMap->GetDescriptionAttribute ("ModelName") ?: "?",
			pNodeMap->GetNodeCount ());

	ShowFeatures (pNodeMap, "Root", 0);

	ConfigureCamera (pNodeMap);

	size_t nPayloadSize = Camera.GetPayloadSize ();
	if (nPayloadSize == 0)
	{
		m_Logger.Write (FromKernel, LogError, "Cannot get payload size");

		return ShutdownHalt;
	}

	u8 *pBuffer = new u8[nPayloadSize];
	if (pBuffer == 0)
	{
		m_Logger.Write (FromKernel, LogError, "Not enough memory");

		return ShutdownHalt;
	}

	if (!Camera.OpenStream (0, PACKET_SIZE))
	{
		m_Logger.Write (FromKernel, LogError, "Cannot open stream channel");

		return ShutdownHalt;
	}

	if (!Camera.StartAcquisition ())
	{
		return ShutdownHalt;
	}

	m_Logger.Write (FromKernel, LogNotice, "Acquisition started (payload %u bytes)",
			(unsigned) nPayloadSize);

	CGVSPReceiver *pStream = Camera.GetStream ();

	unsigned nStartTime = m_Timer.GetUptime ();
	unsigned nLastStatus = nStartTime;
	unsigned nLastComplete = 0;
	while (   RUN_TIME_SECS == 0
	       || m_Timer.GetUptime () - nStartTime < RUN_TIME_SECS)
	{
		TGVSPFrameInfo Info;
		if (pStream->ReceiveFrame (&Info, pBuffer, nPayloadSize, 2000))
		{
			if (Info.Complete)
			{
				DrawImage (Info, pBuffer);
			}
		}
		else
		{
			m_Logger.Write (FromKernel, LogWarning, "No image received");
		}

		unsigned nNow = m_Timer.GetUptime ();
		if (nNow - nLastStatus >= STATUS_INTERVAL_SECS)
		{
			const TGVSPStatistics &rStats = pStream->GetStatistics ();

			m_Logger.Write (FromKernel, LogNotice,
					"%u fps, %u complete, %u incomplete, %u missing, %u resent",
					(rStats.CompleteFrames - nLastComplete) / (nNow - nLastStatus),
					rStats.CompleteFrames, rStats.IncompleteFrames,
					rStats.MissingPackets, rStats.ResentPackets);

			nLastComplete = rStats.CompleteFrames;
			nLastStatus = nNow;
		}
	}

	Camera.StopAcquisition ();
	Camera.Close ();

	delete [] pBuffer;

	m_Logger.Write (FromKernel, LogNotice, "Done");

	return ShutdownHalt;
}

boolean CKernel::FindCamera (CIPAddress *pCameraIP)
{
	const unsigned MaxDevices = 8;
	TGigEDeviceInfo Devices[MaxDevices];

	int nDevices = 0;
	for (unsigned nTry = 1; nDevices <= 0; nTry++)
	{
		m_Logger.Write (FromKernel, LogNotice, "Searching for GigE Vision devices (%u)", nTry);

		nDevices = CGVCPClient::Discover (&m_Net, Devices, MaxDevices, 1000);
		if (nDevices <= 0)
		{
			m_Scheduler.Sleep (2);
		}
	}

	for (int i = 0; i < nDevices; i++)
	{
		const TGigEDeviceInfo &rDev = Devices[i];

		CString IPString;
		rDev.IPAddress.Format (&IPString);

		m_Logger.Write (FromKernel, LogNotice,
				"%d: %s %s (S/N %s) at %s, MAC %02X:%02X:%02X:%02X:%02X:%02X, GEV %u.%u",
				i, rDev.ManufacturerName, rDev.ModelName, rDev.SerialNumber,
				(const char *) IPString,
				rDev.MACAddress[0], rDev.MACAddress[1], rDev.MACAddress[2],
				rDev.MACAddress[3], rDev.MACAddress[4], rDev.MACAddress[5],
				rDev.SpecVersionMajor, rDev.SpecVersionMinor);
	}

	// use the first device
	const TGigEDeviceInfo &rCamera = Devices[0];
	pCameraIP->Set (rCamera.IPAddress);

	CNetConfig *pConfig = m_Net.GetConfig ();
	const CIPAddress *pOwnIP = pConfig->GetIPAddress ();
	if (pOwnIP->OnSameNetwork (rCamera.IPAddress, pConfig->GetNetMask ()))
	{
		return TRUE;
	}

	// The camera is not reachable from our subnet, assign a temporary address
	u8 NewIP[IP_ADDRESS_SIZE];
	pOwnIP->CopyTo (NewIP);
	NewIP[3] = NewIP[3] != FORCE_IP_HOST ? FORCE_IP_HOST : FORCE_IP_HOST + 1;

	CIPAddress ForcedIP (NewIP);
	CIPAddress SubnetMask (pConfig->GetNetMask ());
	CIPAddress Gateway;
	if (pConfig->GetDefaultGateway ()->IsSet ())
	{
		Gateway.Set (*pConfig->GetDefaultGateway ());
	}
	else
	{
		Gateway.Set ((u32) 0);
	}

	CString IPString;
	ForcedIP.Format (&IPString);
	m_Logger.Write (FromKernel, LogNotice, "Camera is on another subnet, forcing IP %s",
			(const char *) IPString);

	if (!CGVCPClient::ForceIP (&m_Net, rCamera.MACAddress, ForcedIP, SubnetMask, Gateway))
	{
		m_Logger.Write (FromKernel, LogError, "FORCEIP failed");

		return FALSE;
	}

	pCameraIP->Set (ForcedIP);

	m_Scheduler.MsSleep (500);	// give the device some time to reconfigure

	return TRUE;
}

void CKernel::ShowFeatures (CGenICamNodeMap *pNodeMap, const char *pCategory, unsigned nLevel)
{
	for (unsigned i = 0; ; i++)
	{
		const char *pFeature = pNodeMap->GetCategoryFeature (pCategory, i);
		if (pFeature == 0)
		{
			break;
		}

		if (!pNodeMap->IsAvailable (pFeature))
		{
			continue;
		}

		TGenICamNodeType Type = pNodeMap->GetNodeType (pFeature);
		if (Type == GenICamNodeCategory)
		{
			m_Logger.Write (FromKernel, LogNotice, "%*s[%s]", nLevel * 2, "", pFeature);

			if (nLevel < 1)
			{
				ShowFeatures (pNodeMap, pFeature, nLevel + 1);
			}

			continue;
		}

		if (Type == GenICamNodeCommand)
		{
			continue;
		}

		CString Value;
		if (!pNodeMap->GetValueAsString (pFeature, &Value))
		{
			Value = "(not readable)";
		}

		m_Logger.Write (FromKernel, LogNotice, "%*s%s = %s", nLevel * 2, "",
				pFeature, (const char *) Value);
	}
}

void CKernel::ConfigureCamera (CGenICamNodeMap *pNodeMap)
{
	// 8-bit pixels can be shown on the screen directly
	if (!pNodeMap->SetEnum ("PixelFormat", "Mono8"))
	{
		m_Logger.Write (FromKernel, LogWarning, "Cannot set PixelFormat to Mono8");
	}

	if (pNodeMap->IsFeature ("AcquisitionMode"))
	{
		pNodeMap->SetEnum ("AcquisitionMode", "Continuous");
	}

	if (pNodeMap->IsFeature ("TriggerMode"))
	{
		pNodeMap->SetEnum ("TriggerMode", "Off");	// free running
	}

	static const struct
	{
		const char *pFeature;
		s64 nMax;
	}
	Limits[] = {{"Width", MAX_WIDTH}, {"Height", MAX_HEIGHT}};

	for (unsigned i = 0; i < sizeof Limits / sizeof Limits[0]; i++)
	{
		s64 nValue, nMin, nMax, nInc;
		if (   Limits[i].nMax == 0
		    || !pNodeMap->GetInteger (Limits[i].pFeature, &nValue)
		    || nValue <= Limits[i].nMax
		    || !pNodeMap->GetIntegerRange (Limits[i].pFeature, &nMin, &nMax, &nInc))
		{
			continue;
		}

		s64 nNewValue = Limits[i].nMax - (Limits[i].nMax - nMin) % nInc;
		if (!pNodeMap->SetInteger (Limits[i].pFeature, nNewValue))
		{
			m_Logger.Write (FromKernel, LogWarning, "Cannot set %s", Limits[i].pFeature);
		}
	}

	s64 nWidth = 0, nHeight = 0;
	pNodeMap->GetInteger ("Width", &nWidth);
	pNodeMap->GetInteger ("Height", &nHeight);

	CString PixelFormat;
	pNodeMap->GetEnum ("PixelFormat", &PixelFormat);

	m_Logger.Write (FromKernel, LogNotice, "Image format %lldx%lld %s",
			(long long) nWidth, (long long) nHeight, (const char *) PixelFormat);
}

void CKernel::DrawImage (const TGVSPFrameInfo &rInfo, const u8 *pData)
{
#if DEPTH == 16 || DEPTH == 32
	if (   (rInfo.PayloadType & ~GVSP_PAYLOAD_EXTENDED_CHUNK) != GVSP_PAYLOAD_IMAGE
	    || GVSP_PIX_BITS_PER_PIXEL (rInfo.PixelFormat) != 8
	    || rInfo.SizeX == 0
	    || rInfo.SizeY == 0
	    || (size_t) rInfo.SizeX * rInfo.SizeY > rInfo.DataSize)
	{
		return;
	}

	// draw into the upper right quarter of the screen, scaled down if necessary
	unsigned nAreaWidth = m_Screen.GetWidth () / 2;
	unsigned nAreaHeight = m_Screen.GetHeight () / 2;

	unsigned nScale = 1;
	while (   rInfo.SizeX / nScale > nAreaWidth
	       || rInfo.SizeY / nScale > nAreaHeight)
	{
		nScale++;
	}

	unsigned nWidth = rInfo.SizeX / nScale;
	unsigned nHeight = rInfo.SizeY / nScale;
	unsigned nPosX = m_Screen.GetWidth () - nWidth;
	unsigned nLineLength = rInfo.SizeX + rInfo.PaddingX;

	for (unsigned y = 0; y < nHeight; y++)
	{
		const u8 *pLine = pData + (size_t) y * nScale * nLineLength;

		for (unsigned x = 0; x < nWidth; x++)
		{
			u8 nGray = pLine[x * nScale];
#if DEPTH == 16
			TScreenColor Color = COLOR16 (nGray >> 3, nGray >> 3, nGray >> 3);
#else
			TScreenColor Color = COLOR32 (nGray, nGray, nGray, 0xFF);
#endif
			m_Screen.SetPixel (nPosX + x, y, Color);
		}
	}
#endif
}
