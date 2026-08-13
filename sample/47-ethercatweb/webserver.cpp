//
// webserver.cpp
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
#include "webserver.h"
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

#define TIMEOUT_SECONDS		10		// receive timeout

#define MAX_CONTENT_SIZE	(32 * 1024)	// per worker

static const char FromWebServer[] = "webserver";

// The dashboard is a static page, which fetches the status from /status.json
// once per second.
static const char s_Index[] =
R"PAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Circle EtherCAT Master</title>
<style>
* { box-sizing: border-box; }
body { margin: 0; padding: 1rem; font-family: system-ui, sans-serif;
       background: #f4f5f7; color: #202124; }
h1 { font-size: 1.4rem; margin: 0 0 .2rem 0; }
h2 { font-size: 1rem; margin: 0 0 .6rem 0; color: #5f6368; text-transform: uppercase;
     letter-spacing: .05em; }
#updated { margin: 0 0 1rem 0; color: #5f6368; font-size: .85rem; }
.grid { display: grid; gap: 1rem; grid-template-columns: repeat(auto-fit, minmax(20rem, 1fr)); }
.card { background: #fff; border: 1px solid #dadce0; border-radius: .5rem;
        padding: 1rem; margin-bottom: 1rem; overflow-x: auto; }
table { border-collapse: collapse; width: 100%; font-size: .9rem; }
th, td { text-align: left; padding: .3rem .5rem; border-bottom: 1px solid #eceff1;
         white-space: nowrap; }
tbody tr:last-child th, tbody tr:last-child td { border-bottom: none; }
th { color: #5f6368; font-weight: 500; }
td.mono, .mono { font-family: ui-monospace, monospace; }
.tag { display: inline-block; padding: .1rem .5rem; border-radius: 1rem; font-size: .8rem;
       background: #e8eaed; color: #202124; }
.ok { background: #e6f4ea; color: #137333; }
.warn { background: #fef7e0; color: #b06000; }
.err { background: #fce8e6; color: #c5221f; }
@media (prefers-color-scheme: dark) {
body { background: #202124; color: #e8eaed; }
h2, th, #updated { color: #9aa0a6; }
.card { background: #292a2d; border-color: #3c4043; }
th, td { border-bottom-color: #3c4043; }
.tag { background: #3c4043; color: #e8eaed; }
.ok { background: #12341f; color: #81c995; }
.warn { background: #3b2f00; color: #fdd663; }
.err { background: #3f1512; color: #f28b82; }
}
</style>
</head>
<body>
<h1>Circle EtherCAT Master</h1>
<p id="updated">connecting ...</p>
<div class="grid">
<div class="card"><h2>EtherCAT master</h2><table><tbody id="master"></tbody></table></div>
<div class="card"><h2>Statistics</h2><table><tbody id="stats"></tbody></table></div>
<div class="card"><h2>Process data</h2><table><tbody id="pdata"></tbody></table></div>
<div class="card"><h2>System</h2><table><tbody id="system"></tbody></table></div>
</div>
<div class="card"><h2>Slaves</h2><table id="slaves"></table></div>
<div class="card"><h2>Network devices</h2><table id="netdev"></table></div>
<div class="card"><h2>Tasks</h2><table id="tasks"></table></div>
<script>
function esc(s) {
	return String(s).replace(/[&<>"]/g, function (c) {
		return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
	});
}
function row(name, value) {
	return '<tr><th>' + esc(name) + '</th><td>' + value + '</td></tr>';
}
function tag(text, cls) {
	return '<span class="tag ' + cls + '">' + esc(text) + '</span>';
}
function stateClass(state) {
	if (state.indexOf('ERROR') >= 0 || state === 'Error' || state === 'No link') return 'err';
	if (state === 'OPERATIONAL') return 'ok';
	return 'warn';
}
function hex(value, digits) {
	return '0x' + value.toString(16).toUpperCase().padStart(digits, '0');
}
function seconds(value) {
	var h = Math.floor(value / 3600), m = Math.floor(value / 60) % 60, s = value % 60;
	return h + 'h ' + String(m).padStart(2, '0') + 'm ' + String(s).padStart(2, '0') + 's';
}
function table(head, rows) {
	if (!rows.length) return '<tbody><tr><td>none</td></tr></tbody>';
	return '<thead><tr>' + head.map(function (h) { return '<th>' + esc(h) + '</th>'; }).join('')
	       + '</tr></thead><tbody>' + rows.join('') + '</tbody>';
}
function render(d) {
	var e = d.ethercat, s = d.system;

	document.getElementById('master').innerHTML =
		  row('State', tag(e.state, stateClass(e.state)))
		+ row('Interface', esc(e.interface))
		+ row('MAC address', '<span class="mono">' + esc(e.mac) + '</span>')
		+ row('Link', tag(e.linkspeed, e.linkup ? 'ok' : 'err'))
		+ row('Slaves found', e.slavecount)
		+ row('Cycle time', (e.cycletime / 1000).toFixed(1) + ' ms')
		+ row('Operational since', seconds(e.opseconds))
		+ (e.error ? row('Last error', tag(e.error, 'err')) : '');

	document.getElementById('stats').innerHTML =
		  row('Work counter', e.wkc + ' of ' + e.expectedwkc)
		+ row('Cycles', e.cycles.toLocaleString())
		+ row('Work counter errors', e.wkcerrors.toLocaleString())
		+ row('Frames sent', e.txframes.toLocaleString())
		+ row('Send errors', e.txerrors.toLocaleString())
		+ row('Frames received', e.rxframes.toLocaleString())
		+ row('Non-EtherCAT frames', e.rxdropped.toLocaleString())
		+ row('DC time', e.dctime.toLocaleString() + ' ns');

	document.getElementById('pdata').innerHTML =
		  row('Outputs', e.outputbytes + ' byte(s)')
		+ row('Output data', '<span class="mono">' + esc(e.outputs || '-') + '</span>')
		+ row('Inputs', e.inputbytes + ' byte(s)')
		+ row('Input data', '<span class="mono">' + esc(e.inputs || '-') + '</span>');

	document.getElementById('system').innerHTML =
		  row('Model', esc(s.machine))
		+ row('SoC', esc(s.soc) + ' (' + hex(s.revision, 8) + ')')
		+ row('RAM', s.ramsize + ' MByte')
		+ row('Heap free', (s.heapfree / (1024 * 1024)).toFixed(1) + ' MByte')
		+ row('CPU clock', (s.cpuclock / 1000000).toFixed(0) + ' MHz (max '
		      + (s.maxcpuclock / 1000000).toFixed(0) + ' MHz)')
		+ row('SoC temperature', s.temperature + ' &deg;C (max ' + s.maxtemperature + ' &deg;C)')
		+ row('Throttled state', s.throttled ? tag(hex(s.throttled, 8), 'warn') : tag('none', 'ok'))
		+ row('Uptime', seconds(s.uptime))
		+ row('Time', esc(s.time || 'not set'))
		+ row('Hostname', esc(s.hostname))
		+ row('IP address', '<span class="mono">' + esc(s.ip) + '</span>')
		+ row('Circle version', esc(s.version))
		+ row('Built', esc(s.compiletime));

	document.getElementById('slaves').innerHTML = table(
		['#', 'Name', 'State', 'AL status', 'Address', 'Vendor', 'Product', 'Revision',
		 'Out', 'In', 'DC', 'Device name', 'HW', 'SW'],
		e.slaves.map(function (v, i) {
			return '<tr><td>' + (i + 1) + '</td><td>' + esc(v.name) + '</td>'
			     + '<td>' + tag(v.state, stateClass(v.state))
			     + (v.lost ? ' ' + tag('lost', 'err') : '') + '</td>'
			     + '<td class="mono">' + hex(v.alstatus, 4) + '</td>'
			     + '<td class="mono">' + hex(v.address, 4) + '</td>'
			     + '<td class="mono">' + hex(v.vendor, 8) + '</td>'
			     + '<td class="mono">' + hex(v.product, 8) + '</td>'
			     + '<td class="mono">' + hex(v.revision, 8) + '</td>'
			     + '<td>' + v.outputbits + ' bit</td><td>' + v.inputbits + ' bit</td>'
			     + '<td>' + (v.hasdc ? 'yes' : 'no') + '</td>'
			     + '<td>' + esc(v.devicename) + '</td><td>' + esc(v.hardware) + '</td>'
			     + '<td>' + esc(v.software) + '</td></tr>';
		}));

	document.getElementById('netdev').innerHTML = table(
		['Device', 'MAC address', 'Link'],
		s.netdevices.map(function (v) {
			return '<tr><td>' + esc(v.name) + '</td>'
			     + '<td class="mono">' + esc(v.mac) + '</td>'
			     + '<td>' + tag(v.speed, v.linkup ? 'ok' : 'err') + '</td></tr>';
		}));

	document.getElementById('tasks').innerHTML = table(
		['Task', 'State'],
		s.tasks.map(function (v) {
			return '<tr><td>' + esc(v.name) + '</td><td>' + esc(v.state)
			     + (v.suspended ? ' (suspended)' : '') + '</td></tr>';
		}));

	document.getElementById('updated').textContent =
		'updated ' + new Date().toLocaleTimeString() + ', ' + s.tasks.length
		+ ' of ' + s.taskcount + ' tasks shown';
}
function update() {
	fetch('/status.json?t=' + Date.now())
		.then(function (r) { return r.json(); })
		.then(render)
		.catch(function () {
			document.getElementById('updated').textContent = 'connection lost ...';
		});
}
update();
setInterval(update, 1000);
</script>
</body>
</html>
)PAGE";

CWebServer::CWebServer (CNetSubSystem *pNetSubSystem, CEtherCATMaster *pEtherCAT,
			CSocket *pSocket)
:	CHTTPDaemon (pNetSubSystem, pSocket, MAX_CONTENT_SIZE, HTTP_PORT, 0, TIMEOUT_SECONDS),
	m_pNet (pNetSubSystem),
	m_pEtherCAT (pEtherCAT)
{
}

CWebServer::~CWebServer (void)
{
	m_pNet = 0;
	m_pEtherCAT = 0;
}

CHTTPDaemon *CWebServer::CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket)
{
	return new CWebServer (pNetSubSystem, m_pEtherCAT, pSocket);
}

THTTPStatus CWebServer::GetContent (const char  *pPath,
				    const char  *pParams,
				    const char  *pFormData,
				    u8	        *pBuffer,
				    unsigned    *pLength,
				    const char **ppContentType)
{
	assert (pPath != 0);
	assert (pLength != 0);
	assert (pBuffer != 0);
	assert (ppContentType != 0);

	CString JSON;
	const char *pContent;
	unsigned nLength;

	if (   strcmp (pPath, "/") == 0
	    || strcmp (pPath, "/index.html") == 0)
	{
		pContent = s_Index;
		nLength = sizeof s_Index - 1;

		*ppContentType = "text/html; charset=utf-8";
	}
	else if (strcmp (pPath, "/status.json") == 0)
	{
		BuildStatusJSON (JSON);

		pContent = (const char *) JSON;
		nLength = JSON.GetLength ();

		*ppContentType = "application/json";
	}
	else
	{
		return HTTPNotFound;
	}

	if (*pLength < nLength)
	{
		CLogger::Get ()->Write (FromWebServer, LogError,
					"Increase MAX_CONTENT_SIZE to at least %u", nLength);

		return HTTPInternalServerError;
	}

	memcpy (pBuffer, pContent, nLength);

	*pLength = nLength;

	return HTTPOK;
}

void CWebServer::BuildStatusJSON (CString &rJSON)
{
	// the status objects are too big for the stack of a task
	TEtherCATStatus *pEtherCATStatus = new TEtherCATStatus;
	TSystemStatus *pSystemStatus = new TSystemStatus;

	if (   pEtherCATStatus == 0
	    || pSystemStatus == 0)
	{
		delete pEtherCATStatus;
		delete pSystemStatus;

		rJSON = "{}";

		return;
	}

	memset (pEtherCATStatus, 0, sizeof *pEtherCATStatus);

	if (m_pEtherCAT != 0)
	{
		m_pEtherCAT->GetStatus (pEtherCATStatus);
	}

	GetSystemStatus (pSystemStatus, m_pNet);

	rJSON = "{";
	AppendEtherCATStatus (rJSON, pEtherCATStatus);
	AppendSystemStatus (rJSON, pSystemStatus);
	RemoveTrailingComma (rJSON);
	rJSON.Append ("}");

	delete pSystemStatus;
	delete pEtherCATStatus;
}

void CWebServer::AppendEtherCATStatus (CString &rJSON, const TEtherCATStatus *pStatus)
{
	assert (pStatus != 0);

	rJSON.Append ("\"ethercat\":{");

	AppendString (rJSON, "state", CEtherCATMaster::GetMasterStateString (pStatus->State));
	AppendString (rJSON, "interface", pStatus->InterfaceName);
	AppendString (rJSON, "mac", pStatus->MACAddress);
	AppendString (rJSON, "linkspeed", pStatus->LinkSpeed);
	AppendBoolean (rJSON, "linkup", pStatus->LinkUp);
	AppendNumber (rJSON, "slavecount", pStatus->nSlaveCount);
	AppendSigned (rJSON, "wkc", pStatus->nWorkCounter);
	AppendSigned (rJSON, "expectedwkc", pStatus->nExpectedWorkCounter);
	AppendNumber (rJSON, "cycletime", pStatus->nCycleTimeUs);
	AppendNumber (rJSON, "cycles", pStatus->nCycles);
	AppendNumber (rJSON, "wkcerrors", pStatus->nWKCErrors);
	AppendNumber (rJSON, "txframes", pStatus->nTxFrames);
	AppendNumber (rJSON, "txerrors", pStatus->nTxErrors);
	AppendNumber (rJSON, "rxframes", pStatus->nRxFrames);
	AppendNumber (rJSON, "rxdropped", pStatus->nRxDropped);
	AppendSigned (rJSON, "dctime", pStatus->nDCTime);
	AppendNumber (rJSON, "outputbytes", pStatus->nOutputBytes);
	AppendNumber (rJSON, "inputbytes", pStatus->nInputBytes);
	AppendHexBytes (rJSON, "outputs", pStatus->Outputs, pStatus->nOutputBytesInStatus);
	AppendHexBytes (rJSON, "inputs", pStatus->Inputs, pStatus->nInputBytesInStatus);
	AppendNumber (rJSON, "opseconds", pStatus->nOperationalSeconds);
	AppendString (rJSON, "error", pStatus->LastError);

	rJSON.Append ("\"slaves\":[");

	for (unsigned i = 0; i < pStatus->nSlavesInStatus; i++)
	{
		const TEtherCATSlaveInfo *pSlave = &pStatus->Slaves[i];

		rJSON.Append ("{");

		AppendString (rJSON, "name", pSlave->Name);
		AppendString (rJSON, "state", CEtherCATMaster::GetSlaveStateString (pSlave->State));
		AppendNumber (rJSON, "alstatus", pSlave->ALStatusCode);
		AppendNumber (rJSON, "address", pSlave->ConfigAddress);
		AppendNumber (rJSON, "alias", pSlave->AliasAddress);
		AppendNumber (rJSON, "vendor", pSlave->VendorID);
		AppendNumber (rJSON, "product", pSlave->ProductCode);
		AppendNumber (rJSON, "revision", pSlave->RevisionNumber);
		AppendNumber (rJSON, "serial", pSlave->SerialNumber);
		AppendNumber (rJSON, "outputbits", pSlave->OutputBits);
		AppendNumber (rJSON, "inputbits", pSlave->InputBits);
		AppendNumber (rJSON, "mailbox", pSlave->MailboxProtocols);
		AppendBoolean (rJSON, "hasdc", pSlave->HasDC);
		AppendBoolean (rJSON, "lost", pSlave->IsLost);
		AppendString (rJSON, "devicename", pSlave->DeviceName);
		AppendString (rJSON, "hardware", pSlave->HardwareVersion);
		AppendString (rJSON, "software", pSlave->SoftwareVersion);

		RemoveTrailingComma (rJSON);

		rJSON.Append (i+1 < pStatus->nSlavesInStatus ? "}," : "}");
	}

	rJSON.Append ("]},");
}

void CWebServer::AppendSystemStatus (CString &rJSON, const TSystemStatus *pStatus)
{
	assert (pStatus != 0);

	rJSON.Append ("\"system\":{");

	AppendString (rJSON, "machine", pStatus->MachineName);
	AppendString (rJSON, "soc", pStatus->SoCName);
	AppendNumber (rJSON, "revision", pStatus->RevisionRaw);
	AppendNumber (rJSON, "ramsize", pStatus->nRAMSizeMBytes);
	AppendNumber (rJSON, "cpuclock", pStatus->nCPUClockRate);
	AppendNumber (rJSON, "mincpuclock", pStatus->nMinCPUClockRate);
	AppendNumber (rJSON, "maxcpuclock", pStatus->nMaxCPUClockRate);
	AppendNumber (rJSON, "temperature", pStatus->nTemperature);
	AppendNumber (rJSON, "maxtemperature", pStatus->nMaxTemperature);
	AppendNumber (rJSON, "throttled", pStatus->ThrottledState);
	AppendNumber (rJSON, "heapfree", pStatus->nHeapFreeSpace);
	AppendString (rJSON, "version", pStatus->CircleVersion);
	AppendString (rJSON, "compiletime", pStatus->CompileTime);
	AppendNumber (rJSON, "uptime", pStatus->nUptimeSeconds);
	AppendString (rJSON, "time", pStatus->TimeString);
	AppendString (rJSON, "hostname", pStatus->Hostname);
	AppendString (rJSON, "ip", pStatus->IPAddress);
	AppendString (rJSON, "netmask", pStatus->NetMask);
	AppendString (rJSON, "gateway", pStatus->DefaultGateway);
	AppendString (rJSON, "dns", pStatus->DNSServer);
	AppendBoolean (rJSON, "netrunning", pStatus->NetRunning);
	AppendNumber (rJSON, "taskcount", pStatus->nTaskCount);

	rJSON.Append ("\"netdevices\":[");

	for (unsigned i = 0; i < pStatus->nNetDevicesInStatus; i++)
	{
		const TNetDeviceStatus *pDevice = &pStatus->NetDevices[i];

		rJSON.Append ("{");

		AppendString (rJSON, "name", pDevice->Name);
		AppendString (rJSON, "mac", pDevice->MACAddress);
		AppendString (rJSON, "speed", pDevice->LinkSpeed);
		AppendBoolean (rJSON, "linkup", pDevice->LinkUp);

		RemoveTrailingComma (rJSON);

		rJSON.Append (i+1 < pStatus->nNetDevicesInStatus ? "}," : "}");
	}

	rJSON.Append ("],\"tasks\":[");

	for (unsigned i = 0; i < pStatus->nTasksInStatus; i++)
	{
		const TTaskStatus *pTask = &pStatus->Tasks[i];

		rJSON.Append ("{");

		AppendString (rJSON, "name", pTask->Name);
		AppendString (rJSON, "state", pTask->State);
		AppendBoolean (rJSON, "suspended", pTask->Suspended);

		RemoveTrailingComma (rJSON);

		rJSON.Append (i+1 < pStatus->nTasksInStatus ? "}," : "}");
	}

	rJSON.Append ("]}");
}

void CWebServer::AppendString (CString &rJSON, const char *pName, const char *pValue)
{
	assert (pName != 0);
	assert (pValue != 0);

	CString Value;
	EscapeString (Value, pValue);

	CString Field;
	Field.Format ("\"%s\":\"%s\",", pName, (const char *) Value);

	rJSON.Append ((const char *) Field);
}

void CWebServer::AppendNumber (CString &rJSON, const char *pName, u64 nValue)
{
	assert (pName != 0);

	CString Field;
	Field.Format ("\"%s\":%llu,", pName, (unsigned long long) nValue);

	rJSON.Append ((const char *) Field);
}

void CWebServer::AppendSigned (CString &rJSON, const char *pName, s64 nValue)
{
	assert (pName != 0);

	CString Field;
	Field.Format ("\"%s\":%lld,", pName, (long long) nValue);

	rJSON.Append ((const char *) Field);
}

void CWebServer::AppendBoolean (CString &rJSON, const char *pName, boolean bValue)
{
	assert (pName != 0);

	CString Field;
	Field.Format ("\"%s\":%s,", pName, bValue ? "true" : "false");

	rJSON.Append ((const char *) Field);
}

void CWebServer::AppendHexBytes (CString &rJSON, const char *pName,
				 const u8 *pBytes, unsigned nCount)
{
	assert (pName != 0);
	assert (pBytes != 0);

	CString Value;

	for (unsigned i = 0; i < nCount; i++)
	{
		CString Byte;
		Byte.Format (i > 0 ? " %02X" : "%02X", (unsigned) pBytes[i]);

		Value.Append ((const char *) Byte);
	}

	AppendString (rJSON, pName, (const char *) Value);
}

void CWebServer::EscapeString (CString &rResult, const char *pString)
{
	assert (pString != 0);

	rResult = "";

	for (const char *p = pString; *p != '\0'; p++)
	{
		switch (*p)
		{
		case '"':
			rResult.Append ("\\\"");
			break;

		case '\\':
			rResult.Append ("\\\\");
			break;

		default:
			if (' ' <= *p && *p <= '~')
			{
				rResult.Append (*p);
			}
			break;
		}
	}
}

void CWebServer::RemoveTrailingComma (CString &rJSON)
{
	rJSON.TrimRight (",");
}
