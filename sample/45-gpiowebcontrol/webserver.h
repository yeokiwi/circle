//
// webserver.h
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
#ifndef _webserver_h
#define _webserver_h

#include <circle/net/httpdaemon.h>
#include <circle/string.h>
#include <circle/types.h>
#include "gpiocontroller.h"
#include "filemanager.h"

class CWebServer : public CHTTPDaemon
{
public:
	CWebServer (CNetSubSystem	*pNetSubSystem,
		    CGPIOController	*pController,		// GPIO controller (shared)
		    CFileManager	*pFileManager,		// SD card file manager (shared)
		    volatile boolean	*pRebootRequested,	// set to request a reboot
		    CSocket		*pSocket = 0);		// is 0 for 1st instance (listener)
	~CWebServer (void);

	// creates an instance of our derived webserver class
	CHTTPDaemon *CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket);

	// provides our content
	THTTPStatus GetContent (const char  *pPath,		// path of the file to be sent
				const char  *pParams,		// parameters to GET ("" for none)
				const char  *pFormData,		// form data from POST ("" for none)
				u8	    *pBuffer,		// copy your content here
				unsigned    *pLength,		// in: buffer size, out: content length
				const char **ppContentType);	// set this if not "text/html"

private:
	// handle GPIO/PWM/delete/reboot actions given in the query string
	void HandleActions (const char *pQuery, CString &rMessage);

	// handle a file upload from multipart POST form data
	void HandleUpload (CString &rMessage);

	// serve a file from the SD card for download
	THTTPStatus ServeDownload (const char *pQuery, u8 *pBuffer, unsigned *pLength,
				   const char **ppContentType);

	// build the complete HTML page into rString
	void BuildPage (CString &rString, const CString &rMessage);
	void BuildHardwarePWMSection (CString &rString);
	void BuildSoftPWMSection (CString &rString);
	void BuildGPIOSection (CString &rString);
	void BuildFileSection (CString &rString);

	// extract an unsigned integer parameter from a query string
	static boolean GetParam (const char *pQuery, const char *pName, unsigned *pValue);

	// extract a string parameter from a query string (URL-decoded)
	static boolean GetStringParam (const char *pQuery, const char *pName, CString &rValue);

	// extract the file name from a multipart part header (Content-Disposition)
	static boolean ExtractFileName (const char *pHeader, CString &rName);

	// text conversion helpers
	static void URLDecode (CString &rResult, const char *pSource);
	static void URLEncode (CString &rResult, const char *pSource);
	static void HTMLEscape (CString &rResult, const char *pSource);

private:
	CGPIOController	 *m_pController;
	CFileManager	 *m_pFileManager;
	volatile boolean *m_pRebootRequested;
};

#endif
