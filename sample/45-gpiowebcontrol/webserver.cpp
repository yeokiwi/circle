//
// webserver.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2025  R. Stange <rsta2@o2online.de>
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
#include "webserver.h"
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

#define TIMEOUT_SECONDS		10			// receive timeout

#define MAX_CONTENT_SIZE	(64 * 1024)		// per worker; also caps download size

#define MAX_UPLOAD_SIZE		(16 * 1024 * 1024)	// maximum size of an uploaded file

#define MAX_FILE_ENTRIES	128			// files shown in the file listing

static const char FromWebServer[] = "webserver";

static int HexDigit (char chChar)
{
	if ('0' <= chChar && chChar <= '9') return chChar - '0';
	if ('a' <= chChar && chChar <= 'f') return chChar - 'a' + 10;
	if ('A' <= chChar && chChar <= 'F') return chChar - 'A' + 10;
	return -1;
}

CWebServer::CWebServer (CNetSubSystem *pNetSubSystem, CGPIOController *pController,
			CFileManager *pFileManager, volatile boolean *pRebootRequested,
			CSocket *pSocket)
:	CHTTPDaemon (pNetSubSystem, pSocket, MAX_CONTENT_SIZE, HTTP_PORT, MAX_UPLOAD_SIZE,
		     TIMEOUT_SECONDS),
	m_pController (pController),
	m_pFileManager (pFileManager),
	m_pRebootRequested (pRebootRequested)
{
}

CWebServer::~CWebServer (void)
{
	m_pController = 0;
	m_pFileManager = 0;
	m_pRebootRequested = 0;
}

CHTTPDaemon *CWebServer::CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket)
{
	return new CWebServer (pNetSubSystem, m_pController, m_pFileManager,
			       m_pRebootRequested, pSocket);
}

THTTPStatus CWebServer::GetContent (const char  *pPath,
				    const char  *pParams,
				    const char  *pFormData,
				    u8	        *pBuffer,
				    unsigned    *pLength,
				    const char **ppContentType)
{
	assert (pPath != 0);
	assert (pParams != 0);
	assert (pFormData != 0);
	assert (ppContentType != 0);
	assert (m_pController != 0);
	assert (m_pFileManager != 0);

	// File download is served directly (raw file content)
	if (strcmp (pPath, "/download") == 0)
	{
		return ServeDownload (pParams, pBuffer, pLength, ppContentType);
	}

	if (   strcmp (pPath, "/") != 0
	    && strcmp (pPath, "/index.html") != 0)
	{
		return HTTPNotFound;
	}

	// Apply any requested action and collect a status message for the user.
	CString Message;
	HandleActions (pParams, Message);
	HandleUpload (Message);

	CString Page;
	BuildPage (Page, Message);

	unsigned nLength = Page.GetLength ();

	assert (pLength != 0);
	if (*pLength < nLength)
	{
		CLogger::Get ()->Write (FromWebServer, LogError,
					"Increase MAX_CONTENT_SIZE to at least %u", nLength);

		return HTTPInternalServerError;
	}

	assert (pBuffer != 0);
	memcpy (pBuffer, (const char *) Page, nLength);

	*pLength = nLength;

	*ppContentType = "text/html; charset=utf-8";

	return HTTPOK;
}

void CWebServer::HandleActions (const char *pQuery, CString &rMessage)
{
	assert (pQuery != 0);
	assert (m_pController != 0);

	// Reboot request
	unsigned nReboot;
	if (GetParam (pQuery, "reboot", &nReboot) && nReboot == 1)
	{
		assert (m_pRebootRequested != 0);
		*m_pRebootRequested = TRUE;

		rMessage = "The Raspberry Pi is rebooting now ...";

		return;
	}

	// GPIO output control: "pin=<bcm>&val=<0|1>"
	unsigned nPinNumber;
	unsigned nValue;
	if (   GetParam (pQuery, "pin", &nPinNumber)
	    && GetParam (pQuery, "val", &nValue))
	{
		int nIndex = m_pController->GetPinIndex (nPinNumber);
		if (nIndex >= 0)
		{
			m_pController->SetPinLevel (nIndex, nValue);

			rMessage.Format ("GPIO%u set to %s", nPinNumber, nValue ? "HIGH" : "LOW");
		}
	}

	// Hardware PWM control: "hwapply=1&hwfreq=<hz>&hw1en=<0|1>&hw1pin=<bcm>"
	// "&hw1duty=<pct>&hw2en=<0|1>&hw2pin=<bcm>&hw2duty=<pct>"
	unsigned nHWApply;
	if (GetParam (pQuery, "hwapply", &nHWApply) && nHWApply == 1)
	{
		unsigned nFreq = 0;
		GetParam (pQuery, "hwfreq", &nFreq);

		boolean  bEnable[HWPWM_CHANNELS];
		unsigned nPin[HWPWM_CHANNELS];
		unsigned nDuty[HWPWM_CHANNELS];
		boolean  bAnyEnabled = FALSE;

		for (unsigned c = 0; c < HWPWM_CHANNELS; c++)
		{
			CString Name;

			unsigned nEn = 0;
			Name.Format ("hw%uen", c + 1);
			GetParam (pQuery, (const char *) Name, &nEn);
			bEnable[c] = nEn ? TRUE : FALSE;
			bAnyEnabled |= bEnable[c];

			nPin[c] = m_pController->GetHWPinNumber (c, 0);
			Name.Format ("hw%upin", c + 1);
			GetParam (pQuery, (const char *) Name, &nPin[c]);

			nDuty[c] = 0;
			Name.Format ("hw%uduty", c + 1);
			GetParam (pQuery, (const char *) Name, &nDuty[c]);
		}

		if (!bAnyEnabled)
		{
			m_pController->StopHardwarePWM ();

			rMessage = "Hardware PWM disabled";
		}
		else
		{
			unsigned nActual = 0;
			if (m_pController->SetHardwarePWM (nFreq, bEnable, nPin, nDuty, &nActual))
			{
				rMessage.Format ("Hardware PWM running at %u Hz "
						 "(requested %u Hz)", nActual, nFreq);
			}
			else
			{
				rMessage = "Cannot start hardware PWM "
					   "(check pin selection and frequency)";
			}
		}
	}

	// Software PWM control: "spwm=<bcm>&sfreq=<hz>&sduty=<pct>"
	unsigned nSoftPin;
	unsigned nSoftFreq;
	unsigned nSoftDuty;
	if (   GetParam (pQuery, "spwm", &nSoftPin)
	    && GetParam (pQuery, "sfreq", &nSoftFreq)
	    && GetParam (pQuery, "sduty", &nSoftDuty))
	{
		int nIndex = m_pController->GetPinIndex (nSoftPin);
		if (nIndex < 0)
		{
			rMessage.Format ("GPIO%u is not controllable", nSoftPin);
		}
		else if (m_pController->IsHardwarePWMPin ((unsigned) nIndex))
		{
			rMessage.Format ("GPIO%u is reserved for hardware PWM", nSoftPin);
		}
		else
		{
			unsigned nActual = 0;
			if (m_pController->StartSoftPWM ((unsigned) nIndex, nSoftFreq,
							 nSoftDuty, &nActual))
			{
				rMessage.Format ("Software PWM on GPIO%u: %u Hz, %u%% duty",
						 nSoftPin, nActual, nSoftDuty > 100 ? 100 : nSoftDuty);
			}
			else
			{
				rMessage.Format ("Cannot start software PWM on GPIO%u", nSoftPin);
			}
		}
	}

	// Software PWM off: "spwmoff=<bcm>"
	unsigned nSoftOff;
	if (GetParam (pQuery, "spwmoff", &nSoftOff))
	{
		int nIndex = m_pController->GetPinIndex (nSoftOff);
		if (nIndex >= 0)
		{
			m_pController->StopSoftPWM ((unsigned) nIndex);

			rMessage.Format ("Software PWM on GPIO%u disabled", nSoftOff);
		}
	}

	// File deletion: "delete=<name>"
	CString FileName;
	if (GetStringParam (pQuery, "delete", FileName))
	{
		if (m_pFileManager->DeleteFile ((const char *) FileName))
		{
			rMessage.Format ("Deleted file \"%s\"", (const char *) FileName);
		}
		else
		{
			rMessage.Format ("Cannot delete file \"%s\"", (const char *) FileName);
		}
	}
}

void CWebServer::HandleUpload (CString &rMessage)
{
	assert (m_pFileManager != 0);

	const char *pPartHeader;
	const u8 *pPartData;
	unsigned nPartLength;
	while (GetMultipartFormPart (&pPartHeader, &pPartData, &nPartLength))
	{
		assert (pPartHeader != 0);

		CString FileName;
		if (   !ExtractFileName (pPartHeader, FileName)
		    || FileName.GetLength () == 0
		    || nPartLength == 0)
		{
			continue;			// not a (non-empty) file part
		}

		if (!CFileManager::IsValidName ((const char *) FileName))
		{
			rMessage.Format ("Invalid file name \"%s\"", (const char *) FileName);
			continue;
		}

		assert (pPartData != 0);
		if (m_pFileManager->WriteFile ((const char *) FileName, pPartData, nPartLength))
		{
			rMessage.Format ("Uploaded file \"%s\" (%u bytes)",
					 (const char *) FileName, nPartLength);
		}
		else
		{
			rMessage.Format ("Cannot write file \"%s\"", (const char *) FileName);
		}
	}
}

THTTPStatus CWebServer::ServeDownload (const char *pQuery, u8 *pBuffer, unsigned *pLength,
				       const char **ppContentType)
{
	assert (pQuery != 0);
	assert (pBuffer != 0);
	assert (pLength != 0);
	assert (ppContentType != 0);
	assert (m_pFileManager != 0);

	CString FileName;
	if (!GetStringParam (pQuery, "name", FileName))
	{
		return HTTPNotFound;
	}

	int nResult = m_pFileManager->ReadFile ((const char *) FileName, pBuffer, *pLength);
	if (nResult == -2)
	{
		// File is larger than the content buffer: return a plain text notice.
		CString Msg;
		Msg.Format ("File \"%s\" is too large to download (limit %u bytes).\n",
			    (const char *) FileName, (unsigned) MAX_CONTENT_SIZE);

		unsigned nLength = Msg.GetLength ();
		if (*pLength < nLength)
		{
			return HTTPInternalServerError;
		}

		memcpy (pBuffer, (const char *) Msg, nLength);
		*pLength = nLength;
		*ppContentType = "text/plain";

		return HTTPOK;
	}

	if (nResult < 0)
	{
		return HTTPNotFound;
	}

	*pLength = (unsigned) nResult;
	*ppContentType = "application/octet-stream";

	return HTTPOK;
}

void CWebServer::BuildPage (CString &rString, const CString &rMessage)
{
	rString =
		"<!DOCTYPE html>\n"
		"<html>\n"
		"<head>\n"
		"<meta charset=\"utf-8\" />\n"
		"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\" />\n"
		"<title>Raspberry Pi GPIO &amp; SD Card Web Control</title>\n"
		"<style>\n"
		"body{font-family:helvetica,arial,sans-serif;font-size:14px;margin:20px;}\n"
		"h1{font-size:20px;} h2{font-size:16px;margin-top:24px;}\n"
		"table{border-collapse:collapse;}\n"
		"th,td{border:1px solid #ccc;padding:6px 10px;text-align:center;}\n"
		"th{background:#eeeeff;}\n"
		".on{background:#c8f0c8;} .off{background:#f0f0f0;} .wave{background:#ffe0b0;}\n"
		"a.btn{display:inline-block;padding:3px 10px;border:1px solid #88a;"
		"border-radius:4px;text-decoration:none;color:#004;background:#eef;}\n"
		"a.btn:hover{background:#dde;}\n"
		"a.del{border-color:#c88;color:#600;background:#fee;}\n"
		"select,button{font-size:14px;padding:3px 6px;margin:2px;}\n"
		"#msg{background:#ffffcc;border:1px solid #cc9;padding:8px;margin:12px 0;}\n"
		".small{font-size:11px;color:#666;}\n"
		"</style>\n"
		"</head>\n"
		"<body>\n"
		"<h1>Raspberry Pi GPIO &amp; SD Card Web Control</h1>\n";

	if (rMessage.GetLength () > 0)
	{
		CString Escaped;
		HTMLEscape (Escaped, (const char *) rMessage);

		CString Line;
		Line.Format ("<div id=\"msg\">%s</div>\n", (const char *) Escaped);
		rString.Append (Line);
	}

	BuildHardwarePWMSection (rString);
	BuildSoftPWMSection (rString);
	BuildGPIOSection (rString);
	BuildFileSection (rString);

	// System section
	rString.Append ("<h2>System</h2>\n");
	rString.Append ("<p><a class=\"btn del\" href=\"/?reboot=1\" "
			"onclick=\"return confirm('Reboot the Raspberry Pi?');\">"
			"Reboot Raspberry Pi</a></p>\n");

	rString.Append ("<hr />\n");
	rString.Append ("<p class=\"small\">Based on the "
			"<a href=\"https://github.com/rsta2/circle\">Circle</a> "
			"C++ bare metal environment.</p>\n");
	rString.Append ("</body>\n</html>\n");
}

void CWebServer::BuildHardwarePWMSection (CString &rString)
{
	assert (m_pController != 0);

	CString Line;

	rString.Append ("<h2>Hardware PWM (high frequency)</h2>\n");

	if (m_pController->IsHardwarePWMActive ())
	{
		Line.Format ("<p>Active: requested <b>%u Hz</b>, generated "
			     "<b>%u Hz</b>.</p>\n",
			     m_pController->GetHWPWMFrequency (),
			     m_pController->GetHWPWMActualFrequency ());
		rString.Append (Line);
	}
	else
	{
		rString.Append ("<p>Currently disabled.</p>\n");
	}

	// A default frequency to preset the input with.
	unsigned nFreq = m_pController->GetHWPWMFrequency ();
	if (nFreq == 0)
	{
		nFreq = 1000000;			// 1 MHz
	}

	rString.Append ("<form method=\"get\" action=\"/\">\n");
	rString.Append ("<input type=\"hidden\" name=\"hwapply\" value=\"1\" />\n");

	Line.Format ("Frequency (Hz, %u .. %u): "
		     "<input type=\"number\" name=\"hwfreq\" min=\"%u\" max=\"%u\" "
		     "value=\"%u\" /> (shared by both channels)<br />\n",
		     (unsigned) HWPWM_MIN_FREQ, (unsigned) HWPWM_MAX_FREQ,
		     (unsigned) HWPWM_MIN_FREQ, (unsigned) HWPWM_MAX_FREQ, nFreq);
	rString.Append (Line);

	rString.Append ("<table>\n");
	rString.Append ("<tr><th>Channel</th><th>State</th><th>Pin</th>"
			"<th>Duty (%)</th></tr>\n");

	for (unsigned c = 0; c < HWPWM_CHANNELS; c++)
	{
		boolean  bEnabled = m_pController->GetHWChannelEnabled (c);
		unsigned nPin     = m_pController->GetHWChannelPin (c);
		unsigned nDuty    = m_pController->GetHWChannelDuty (c);

		Line.Format ("<tr><td>%u</td>"
			     "<td><select name=\"hw%uen\">"
			     "<option value=\"0\"%s>Off</option>"
			     "<option value=\"1\"%s>On</option></select></td>"
			     "<td><select name=\"hw%upin\">\n",
			     c + 1, c + 1,
			     bEnabled ? "" : " selected", bEnabled ? " selected" : "",
			     c + 1);
		rString.Append (Line);

		for (unsigned s = 0; s < m_pController->GetHWPinCount (c); s++)
		{
			unsigned nOpt = m_pController->GetHWPinNumber (c, s);
			Line.Format ("<option value=\"%u\"%s>GPIO%u</option>\n",
				     nOpt,
				     (bEnabled && nOpt == nPin) || (!bEnabled && s == 0)
					     ? " selected" : "",
				     nOpt);
			rString.Append (Line);
		}

		Line.Format ("</select></td>"
			     "<td><input type=\"number\" name=\"hw%uduty\" min=\"0\" "
			     "max=\"100\" value=\"%u\" /></td></tr>\n",
			     c + 1, nDuty);
		rString.Append (Line);
	}

	rString.Append ("</table>\n");
	rString.Append ("<button type=\"submit\">Apply hardware PWM</button>\n");
	rString.Append ("</form>\n");

	rString.Append ("<p class=\"small\">Both channels share one frequency but "
			"have an independent duty cycle. Near the top of the range the "
			"duty cycle resolution becomes coarse. Set both channels to Off "
			"and apply to disable the hardware PWM.</p>\n");
}

void CWebServer::BuildSoftPWMSection (CString &rString)
{
	assert (m_pController != 0);

	CString Line;

	rString.Append ("<h2>Software PWM (any header pin)</h2>\n");

	// Configuration form.
	rString.Append ("<form method=\"get\" action=\"/\">\n");
	rString.Append ("Pin: <select name=\"spwm\">\n");
	for (unsigned i = 0; i < m_pController->GetPinCount (); i++)
	{
		if (m_pController->IsHardwarePWMPin (i))
		{
			continue;			// reserved for hardware PWM
		}

		Line.Format ("<option value=\"%u\">GPIO%u</option>\n",
			     m_pController->GetPinNumber (i),
			     m_pController->GetPinNumber (i));
		rString.Append (Line);
	}
	rString.Append ("</select>\n");

	Line.Format (" Frequency (Hz, %u .. %u): "
		     "<input type=\"number\" name=\"sfreq\" min=\"%u\" max=\"%u\" "
		     "value=\"1000\" />\n",
		     (unsigned) SOFTPWM_MIN_FREQ, (unsigned) SOFTPWM_MAX_FREQ,
		     (unsigned) SOFTPWM_MIN_FREQ, (unsigned) SOFTPWM_MAX_FREQ);
	rString.Append (Line);

	rString.Append (" Duty (%): <input type=\"number\" name=\"sduty\" min=\"0\" "
			"max=\"100\" value=\"50\" />\n");
	rString.Append ("<button type=\"submit\">Set software PWM</button>\n");
	rString.Append ("</form>\n");

	// List of the active software PWM pins.
	boolean bAny = FALSE;
	for (unsigned i = 0; i < m_pController->GetPinCount (); i++)
	{
		if (m_pController->GetPinMode (i) != PinModeSoftPWM)
		{
			continue;
		}

		if (!bAny)
		{
			rString.Append ("<table>\n");
			rString.Append ("<tr><th>Pin</th><th>Frequency</th>"
					"<th>Duty</th><th>Off</th></tr>\n");
			bAny = TRUE;
		}

		unsigned nPin = m_pController->GetPinNumber (i);
		Line.Format ("<tr><td>GPIO%u</td><td>%u Hz</td><td>%u%%</td>"
			     "<td><a class=\"btn del\" href=\"/?spwmoff=%u\">Off</a></td></tr>\n",
			     nPin, m_pController->GetSoftPWMFrequency (i),
			     m_pController->GetSoftPWMDuty (i), nPin);
		rString.Append (Line);
	}

	if (bAny)
	{
		rString.Append ("</table>\n");
	}
	else
	{
		rString.Append ("<p>No software PWM pins active.</p>\n");
	}

	rString.Append ("<p class=\"small\">Software PWM is generated by a timer "
			"interrupt with a 1 microsecond time base, so it is limited to "
			"lower frequencies with some jitter. For clean, high frequency "
			"output use the hardware PWM above.</p>\n");
}

void CWebServer::BuildGPIOSection (CString &rString)
{
	assert (m_pController != 0);

	CString Line;

	rString.Append ("<h2>GPIO output control</h2>\n");
	rString.Append ("<table>\n");
	rString.Append ("<tr><th>GPIO</th><th>State</th><th colspan=\"2\">Set</th></tr>\n");

	for (unsigned i = 0; i < m_pController->GetPinCount (); i++)
	{
		unsigned nPin  = m_pController->GetPinNumber (i);
		TPinMode Mode  = m_pController->GetPinMode (i);

		const char *pStateClass;
		const char *pStateText;
		boolean     bManual = FALSE;
		if (Mode == PinModeHardwarePWM)
		{
			pStateClass = "wave";
			pStateText  = "HW PWM";
		}
		else if (Mode == PinModeSoftPWM)
		{
			pStateClass = "wave";
			pStateText  = "SW PWM";
		}
		else if (m_pController->GetPinLevel (i) == HIGH)
		{
			pStateClass = "on";
			pStateText  = "HIGH";
			bManual     = TRUE;
		}
		else
		{
			pStateClass = "off";
			pStateText  = "LOW";
			bManual     = TRUE;
		}

		if (bManual)
		{
			Line.Format ("<tr><td>GPIO%u</td><td class=\"%s\">%s</td>"
				     "<td><a class=\"btn\" href=\"/?pin=%u&amp;val=1\">HIGH</a></td>"
				     "<td><a class=\"btn\" href=\"/?pin=%u&amp;val=0\">LOW</a></td></tr>\n",
				     nPin, pStateClass, pStateText, nPin, nPin);
		}
		else
		{
			// A pin driven by hardware or software PWM ignores manual control.
			Line.Format ("<tr><td>GPIO%u</td><td class=\"%s\">%s</td>"
				     "<td colspan=\"2\">(PWM active)</td></tr>\n",
				     nPin, pStateClass, pStateText);
		}
		rString.Append (Line);
	}

	rString.Append ("</table>\n");
	rString.Append ("<p class=\"small\">A pin marked HW PWM or SW PWM is driven by a PWM "
			"generator and ignores manual HIGH/LOW until that PWM is switched off.</p>\n");
}

void CWebServer::BuildFileSection (CString &rString)
{
	assert (m_pFileManager != 0);

	CString Line;

	rString.Append ("<h2>SD card files</h2>\n");

	// Upload form (multipart POST)
	rString.Append ("<form method=\"post\" action=\"/\" "
			"enctype=\"multipart/form-data\">\n");
	rString.Append ("<input type=\"file\" name=\"file\" />\n");
	rString.Append ("<button type=\"submit\">Upload to SD card</button>\n");
	rString.Append ("</form>\n");

	// File listing
	TFileManagerEntry *pEntries = new TFileManagerEntry[MAX_FILE_ENTRIES];
	if (pEntries == 0)
	{
		rString.Append ("<p>Out of memory.</p>\n");

		return;
	}

	unsigned nCount = m_pFileManager->ListFiles (pEntries, MAX_FILE_ENTRIES);

	if (nCount == 0)
	{
		rString.Append ("<p>No files (or SD card not available).</p>\n");
	}
	else
	{
		rString.Append ("<table>\n");
		rString.Append ("<tr><th>File</th><th>Size (bytes)</th>"
				"<th>Download</th><th>Delete</th></tr>\n");

		for (unsigned i = 0; i < nCount; i++)
		{
			CString Escaped;
			HTMLEscape (Escaped, pEntries[i].Name);

			CString Encoded;
			URLEncode (Encoded, pEntries[i].Name);

			Line.Format ("<tr><td>%s</td><td>%u</td>"
				     "<td><a class=\"btn\" href=\"/download?name=%s\">Download</a></td>"
				     "<td><a class=\"btn del\" href=\"/?delete=%s\" "
				     "onclick=\"return confirm('Delete this file?');\">Delete</a></td>"
				     "</tr>\n",
				     (const char *) Escaped, pEntries[i].nSize,
				     (const char *) Encoded, (const char *) Encoded);
			rString.Append (Line);
		}

		rString.Append ("</table>\n");
	}

	delete [] pEntries;

	Line.Format ("<p class=\"small\">Only files in the root directory are shown. "
		     "Downloads are limited to %u bytes.</p>\n", (unsigned) MAX_CONTENT_SIZE);
	rString.Append (Line);
}

boolean CWebServer::GetParam (const char *pQuery, const char *pName, unsigned *pValue)
{
	assert (pQuery != 0);
	assert (pName != 0);
	assert (pValue != 0);

	size_t nNameLen = strlen (pName);

	const char *p = pQuery;
	while (*p != '\0')
	{
		if (   strncmp (p, pName, nNameLen) == 0
		    && p[nNameLen] == '=')
		{
			p += nNameLen + 1;

			unsigned nValue = 0;
			boolean bAnyDigit = FALSE;
			while ('0' <= *p && *p <= '9')
			{
				nValue = nValue * 10 + (unsigned) (*p - '0');
				bAnyDigit = TRUE;
				p++;
			}

			if (!bAnyDigit)
			{
				return FALSE;
			}

			*pValue = nValue;

			return TRUE;
		}

		while (*p != '\0' && *p != '&')
		{
			p++;
		}
		if (*p == '&')
		{
			p++;
		}
	}

	return FALSE;
}

boolean CWebServer::GetStringParam (const char *pQuery, const char *pName, CString &rValue)
{
	assert (pQuery != 0);
	assert (pName != 0);

	size_t nNameLen = strlen (pName);

	const char *p = pQuery;
	while (*p != '\0')
	{
		if (   strncmp (p, pName, nNameLen) == 0
		    && p[nNameLen] == '=')
		{
			p += nNameLen + 1;

			// copy the raw (still URL-encoded) value up to '&' or end
			CString Raw;
			while (*p != '\0' && *p != '&')
			{
				Raw.Append (*p);
				p++;
			}

			URLDecode (rValue, (const char *) Raw);

			return TRUE;
		}

		while (*p != '\0' && *p != '&')
		{
			p++;
		}
		if (*p == '&')
		{
			p++;
		}
	}

	return FALSE;
}

boolean CWebServer::ExtractFileName (const char *pHeader, CString &rName)
{
	assert (pHeader != 0);

	const char *p = strstr (pHeader, "filename=\"");
	if (p == 0)
	{
		return FALSE;
	}

	p += 10;				// skip 'filename="'

	CString Full;
	while (*p != '\0' && *p != '\"')
	{
		Full.Append (*p);
		p++;
	}

	// keep only the base name (strip any path sent by the browser)
	const char *pBase = (const char *) Full;
	for (const char *q = (const char *) Full; *q != '\0'; q++)
	{
		if (*q == '/' || *q == '\\')
		{
			pBase = q + 1;
		}
	}

	rName = pBase;

	return TRUE;
}

void CWebServer::URLDecode (CString &rResult, const char *pSource)
{
	assert (pSource != 0);

	rResult = "";

	while (*pSource != '\0')
	{
		char chChar = *pSource++;

		if (chChar == '+')
		{
			rResult.Append (' ');
		}
		else if (chChar == '%')
		{
			char chHi = pSource[0];
			char chLo = chHi != '\0' ? pSource[1] : '\0';

			int nHi = HexDigit (chHi);
			int nLo = HexDigit (chLo);
			if (nHi >= 0 && nLo >= 0)
			{
				rResult.Append ((char) ((nHi << 4) | nLo));
				pSource += 2;
			}
			else
			{
				rResult.Append (chChar);	// malformed, keep '%'
			}
		}
		else
		{
			rResult.Append (chChar);
		}
	}
}

void CWebServer::URLEncode (CString &rResult, const char *pSource)
{
	assert (pSource != 0);

	static const char Hex[] = "0123456789ABCDEF";

	rResult = "";

	while (*pSource != '\0')
	{
		unsigned char chChar = (unsigned char) *pSource++;

		if (   ('0' <= chChar && chChar <= '9')
		    || ('A' <= chChar && chChar <= 'Z')
		    || ('a' <= chChar && chChar <= 'z')
		    || chChar == '-' || chChar == '_'
		    || chChar == '.' || chChar == '~')
		{
			rResult.Append ((char) chChar);
		}
		else
		{
			rResult.Append ('%');
			rResult.Append (Hex[chChar >> 4]);
			rResult.Append (Hex[chChar & 0x0F]);
		}
	}
}

void CWebServer::HTMLEscape (CString &rResult, const char *pSource)
{
	assert (pSource != 0);

	rResult = "";

	while (*pSource != '\0')
	{
		char chChar = *pSource++;

		switch (chChar)
		{
		case '&':	rResult.Append ("&amp;");	break;
		case '<':	rResult.Append ("&lt;");	break;
		case '>':	rResult.Append ("&gt;");	break;
		case '\"':	rResult.Append ("&quot;");	break;
		default:	rResult.Append (chChar);	break;
		}
	}
}
