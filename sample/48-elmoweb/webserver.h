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

#include "elmodrive.h"
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
		    CElmoMaster	  *pElmoMaster,			// the drives to be controlled
		    CSocket	  *pSocket = 0);		// is 0 for 1st created instance
	~CWebServer (void);

	CHTTPDaemon *CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket) override;

	THTTPStatus GetContent (const char  *pPath,
				const char  *pParams,
				const char  *pFormData,
				u8	    *pBuffer,
				unsigned    *pLength,
				const char **ppContentType) override;

private:
	void BuildStatusJSON (CString &rJSON);
	void AppendDriveStatus (CString &rJSON, const TElmoDriveStatus *pDrive,
				unsigned nIndex);
	void AppendSystemStatus (CString &rJSON, const TSystemStatus *pStatus);

	// executes a command from the control interface, returns the JSON response
	void HandleControl (const char *pFormData, CString &rJSON);

	static boolean GetParam (const char *pFormData, const char *pName,
				 CString &rValue);
	static boolean GetParamNumber (const char *pFormData, const char *pName,
				       double *pValue);

	static void AppendString (CString &rJSON, const char *pName, const char *pValue);
	static void AppendNumber (CString &rJSON, const char *pName, u64 nValue);
	static void AppendSigned (CString &rJSON, const char *pName, s64 nValue);
	static void AppendFloat (CString &rJSON, const char *pName, double fValue);
	static void AppendBoolean (CString &rJSON, const char *pName, boolean bValue);
	static void EscapeString (CString &rResult, const char *pString);
	static void RemoveTrailingComma (CString &rJSON);

private:
	CNetSubSystem	*m_pNet;
	CElmoMaster	*m_pElmoMaster;
};

#endif
