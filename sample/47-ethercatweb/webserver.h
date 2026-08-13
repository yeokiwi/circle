//
// webserver.h
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2026  R. Stange <rsta2@gmx.net>
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

#include "ethercat.h"
#include "sysinfo.h"
#include <circle/net/httpdaemon.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/string.h>
#include <circle/types.h>

class CWebServer : public CHTTPDaemon
{
public:
	CWebServer (CNetSubSystem *pNetSubSystem,
		    CEtherCATMaster *pEtherCAT,			// the EtherCAT master to be shown
		    CSocket	  *pSocket = 0);		// is 0 for 1st created instance (listener)
	~CWebServer (void);

	// creates an instance of our derived webserver class
	CHTTPDaemon *CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket) override;

	// provides our content
	THTTPStatus GetContent (const char  *pPath,		// path of the file to be sent
				const char  *pParams,		// parameters to GET ("" for none)
				const char  *pFormData,		// form data from POST ("" for none)
				u8	    *pBuffer,		// copy your content here
				unsigned    *pLength,		// in: buffer size, out: content length
				const char **ppContentType) override; // set this if not "text/html"

private:
	void BuildStatusJSON (CString &rJSON);
	void AppendEtherCATStatus (CString &rJSON, const TEtherCATStatus *pStatus);
	void AppendSystemStatus (CString &rJSON, const TSystemStatus *pStatus);

	static void AppendString (CString &rJSON, const char *pName, const char *pValue);
	static void AppendNumber (CString &rJSON, const char *pName, u64 nValue);
	static void AppendSigned (CString &rJSON, const char *pName, s64 nValue);
	static void AppendBoolean (CString &rJSON, const char *pName, boolean bValue);
	static void AppendHexBytes (CString &rJSON, const char *pName,
				    const u8 *pBytes, unsigned nCount);
	static void EscapeString (CString &rResult, const char *pString);
	static void RemoveTrailingComma (CString &rJSON);

private:
	CNetSubSystem	*m_pNet;
	CEtherCATMaster *m_pEtherCAT;
};

#endif
