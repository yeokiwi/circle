//
// kernel.cpp
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
#include <circle/string.h>
#include <assert.h>

// Network configuration
#define USE_DHCP

#ifndef USE_DHCP
static const u8 IPAddress[]      = {192, 168, 0, 250};
static const u8 NetMask[]        = {255, 255, 255, 0};
static const u8 DefaultGateway[] = {192, 168, 0, 1};
static const u8 DNSServer[]      = {192, 168, 0, 1};
#endif

// Our PUB socket, subscribers connect to "tcp://<ip-of-raspberry-pi>:5556"
#define PUBLISHER_PORT		5556

// The SUB socket connects to a PUB socket on this host (option "zmqpub=" in
// cmdline.txt), e.g. the Python script "publisher.py" in this directory
#define REMOTE_PUBLISHER_HOST	"192.168.0.10"
#define REMOTE_PUBLISHER_PORT	5557

static const char FromKernel[] = "kernel";

CKernel::CKernel (void)
:	m_Screen (m_Options.GetWidth (), m_Options.GetHeight ()),
	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_USBHCI (&m_Interrupt, &m_Timer),
#ifndef USE_DHCP
	m_Net (IPAddress, NetMask, DefaultGateway, DNSServer),
#endif
	m_pPublisher (0),
	m_pSubscriber (0)
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

	// PUB socket: publishes the topics "circle.uptime" and "circle.counter"
	m_pPublisher = new CZMQPublisher (&m_Net);
	if (!m_pPublisher->Bind (PUBLISHER_PORT))
	{
		return ShutdownHalt;
	}

	m_Logger.Write (FromKernel, LogNotice, "Publishing on tcp://%s:%u",
			(const char *) IPString, PUBLISHER_PORT);

	// SUB socket: receives the topic "pc." from a remote publisher
	const char *pRemoteHost = m_Options.GetAppOptionString ("zmqpub", REMOTE_PUBLISHER_HOST);
	unsigned nRemotePort = m_Options.GetAppOptionDecimal ("zmqport", REMOTE_PUBLISHER_PORT);

	m_pSubscriber = new CZMQSubscriber (&m_Net);
	m_pSubscriber->RegisterMessageHandler (MessageHandler, this);
	m_pSubscriber->Subscribe ("pc.");
	m_pSubscriber->Connect (pRemoteHost, (u16) nRemotePort);

	m_Logger.Write (FromKernel, LogNotice, "Subscribing to tcp://%s:%u",
			pRemoteHost, nRemotePort);

	for (unsigned nCount = 1; 1; nCount++)
	{
		m_Scheduler.MsSleep (1000);

		CString Uptime;
		Uptime.Format ("%u", m_Timer.GetUptime ());
		m_pPublisher->Publish ("circle.uptime", Uptime);

		// a multipart message with three frames
		CString Counter;
		Counter.Format ("%u", nCount);

		CZMQMessage Message;
		Message.AddFrame ("circle.counter");
		Message.AddFrame (Counter);
		Message.AddFrame ((const char *) IPString);

		unsigned nSubscribers = m_pPublisher->Publish (Message);

		if (nCount % 10 == 0)
		{
			m_Logger.Write (FromKernel, LogNotice,
					"%u peer(s) connected, counter sent to %u subscriber(s)",
					m_pPublisher->GetPeerCount (), nSubscribers);
		}
	}

	return ShutdownHalt;
}

void CKernel::MessageHandler (const CZMQMessage &rMessage, void *pParam)
{
	CKernel *pThis = (CKernel *) pParam;
	assert (pThis != 0);

	char Topic[40];
	rMessage.GetFrameString (0, Topic, sizeof Topic);

	char Data[80] = "";
	if (rMessage.GetFrameCount () > 1)
	{
		rMessage.GetFrameString (1, Data, sizeof Data);
	}

	pThis->m_Logger.Write (FromKernel, LogNotice, "Received [%s] %s (%u frame(s))",
			       Topic, Data, rMessage.GetFrameCount ());
}
