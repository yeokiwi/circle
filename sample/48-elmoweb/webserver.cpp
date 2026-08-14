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

#define MAX_CONTENT_SIZE	(40 * 1024)	// per worker

static const char FromWebServer[] = "webserver";

// The dashboard is a static page, which fetches the status from /status.json
// and sends the commands to /control.
static const char s_Index[] =
R"PAGE(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Elmo Drive Control</title>
<style>
* { box-sizing: border-box; }
body { margin: 0; padding: 1rem; font-family: system-ui, sans-serif;
       background: #f4f5f7; color: #202124; }
h1 { font-size: 1.4rem; margin: 0 0 .2rem 0; }
h2 { font-size: 1rem; margin: 0 0 .6rem 0; color: #5f6368; text-transform: uppercase;
     letter-spacing: .05em; }
h3 { font-size: .85rem; margin: 1rem 0 .4rem 0; color: #5f6368; }
#updated { margin: 0 0 1rem 0; color: #5f6368; font-size: .85rem; }
#note { margin: 0 0 1rem 0; padding: .6rem .8rem; border-radius: .4rem; display: none;
        background: #fce8e6; color: #c5221f; font-size: .9rem; }
.grid { display: grid; gap: 1rem; grid-template-columns: repeat(auto-fit, minmax(22rem, 1fr)); }
.card { background: #fff; border: 1px solid #dadce0; border-radius: .5rem;
        padding: 1rem; margin-bottom: 1rem; overflow-x: auto; }
.card.armed { border-color: #b06000; box-shadow: 0 0 0 2px #fef7e0; }
table { border-collapse: collapse; width: 100%; font-size: .9rem; }
th, td { text-align: left; padding: .25rem .5rem; border-bottom: 1px solid #eceff1;
         white-space: nowrap; }
tbody tr:last-child th, tbody tr:last-child td { border-bottom: none; }
th { color: #5f6368; font-weight: 500; }
.mono { font-family: ui-monospace, monospace; }
.scroll { overflow-x: auto; }
.hint { font-size: .8rem; color: #5f6368; margin: .5rem 0 0 0; white-space: normal; }
.tag { display: inline-block; padding: .1rem .5rem; border-radius: 1rem; font-size: .8rem;
       background: #e8eaed; color: #202124; }
.ok { background: #e6f4ea; color: #137333; }
.warn { background: #fef7e0; color: #b06000; }
.err { background: #fce8e6; color: #c5221f; }
button { font: inherit; padding: .35rem .8rem; margin: .15rem .15rem .15rem 0;
         border: 1px solid #dadce0; border-radius: .3rem; background: #fff;
         color: #202124; cursor: pointer; }
button:hover:enabled { background: #f1f3f4; }
button:disabled { opacity: .4; cursor: not-allowed; }
button.primary { background: #1a73e8; border-color: #1a73e8; color: #fff; }
button.danger { background: #d93025; border-color: #d93025; color: #fff; }
button.stopall { font-size: 1.05rem; padding: .6rem 1.4rem; }
input[type=number] { font: inherit; width: 7rem; padding: .3rem; border: 1px solid #dadce0;
                     border-radius: .3rem; background: #fff; color: #202124; }
label { font-size: .85rem; color: #5f6368; margin-right: .3rem; }
.row { display: flex; flex-wrap: wrap; align-items: center; gap: .3rem; margin: .3rem 0; }
@media (prefers-color-scheme: dark) {
body { background: #202124; color: #e8eaed; }
h2, h3, th, label, #updated, .hint { color: #9aa0a6; }
.card { background: #292a2d; border-color: #3c4043; }
th, td { border-bottom-color: #3c4043; }
.tag { background: #3c4043; color: #e8eaed; }
.ok { background: #12341f; color: #81c995; }
.warn { background: #3b2f00; color: #fdd663; }
.err { background: #3f1512; color: #f28b82; }
#note { background: #3f1512; color: #f28b82; }
button { background: #292a2d; border-color: #5f6368; color: #e8eaed; }
button:hover:enabled { background: #3c4043; }
button.primary { background: #8ab4f8; border-color: #8ab4f8; color: #202124; }
button.danger { background: #f28b82; border-color: #f28b82; color: #202124; }
input[type=number] { background: #292a2d; border-color: #5f6368; color: #e8eaed; }
}
</style>
</head>
<body>
<h1>Elmo Drive Control</h1>
<p id="updated">connecting ...</p>
<p id="note"></p>
<div class="card">
<button class="danger stopall" onclick="stopAll()">STOP ALL DRIVES</button>
<span class="tag" id="armstate">no drive armed</span>
</div>
<div class="grid">
<div class="card"><h2>EtherCAT bus</h2><table><tbody id="bus"></tbody></table></div>
<div class="card"><h2>System</h2><table><tbody id="system"></tbody></table></div>
</div>
<div id="drives"></div>
<div class="card"><h2>Bus scan</h2>
<div class="scroll"><table id="scan"><tbody></tbody></table></div>
<p class="hint">Every slave, which was found on the bus, with its identity from the
EEPROM and from the CoE objects 0x1008, 0x1009 and 0x100A, and why it is driven or
not. If an Elmo product code is not known to JSD, see the section "UNKNOWN PRODUCT
CODES" in addon/jsd/README.</p>
</div>
<script>
var armedAny = false;
var timer = null;
var builtSig = '';

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
function hex(value, digits) {
	return '0x' + value.toString(16).toUpperCase().padStart(digits, '0');
}
function note(text) {
	var el = document.getElementById('note');
	el.textContent = text;
	el.style.display = text ? 'block' : 'none';
}
function num(id) {
	var el = document.getElementById(id);
	if (!el) return 0;
	var v = parseFloat(el.value);
	return isNaN(v) ? 0 : v;
}
function send(drive, cmd, params) {
	var body = 'drive=' + drive + '&cmd=' + cmd;
	for (var k in params) {
		body += '&' + k + '=' + encodeURIComponent(params[k]);
	}
	return fetch('/control', {
			method: 'POST',
			headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
			body: body
		})
		.then(function (r) { return r.json(); })
		.then(function (d) {
			note(d.ok ? '' : (d.error || 'command failed'));
			update();
		})
		.catch(function () { note('connection lost'); });
}
function stopAll() {
	var cards = document.querySelectorAll('[data-drive]');
	for (var i = 0; i < cards.length; i++) {
		send(cards[i].getAttribute('data-drive'), 'stop', {});
	}
}
function torque(drive) {
	var amps = num('torque' + drive);
	if (!confirm('Apply ' + amps + ' A of torque to drive ' + drive
		     + '?\nTorque mode has no position or velocity limit in the drive.')) {
		return;
	}
	send(drive, 'torque', { value: amps });
}
function statusRows(d) {
	return    row('State machine', tag(d.statemachine,
			d.statemachine.indexOf('OPERATION_ENABLED') >= 0 ? 'ok'
			: (d.statemachine.indexOf('FAULT') >= 0 ? 'err' : 'warn')))
		+ row('Mode of operation', esc(d.modeofoperation))
		+ row('Fault', tag(d.faultstring, d.faultcode ? 'err' : 'ok')
			+ (d.faultunknown ? ' ' + tag('EMCY missing', 'warn') : ''))
		+ row('EMCY code', '<span class="mono">' + hex(d.emcycode, 4) + '</span>')
		+ row('Flags', tag('servo', d.servoenabled ? 'ok' : '')
			+ ' ' + tag('motor', d.motoron ? 'ok' : '')
			+ ' ' + tag('moving', d.inmotion ? 'warn' : '')
			+ ' ' + tag('STO', d.stoengaged ? 'err' : 'ok')
			+ (d.warning ? ' ' + tag('warning', 'warn') : '')
			+ (d.targetreached ? ' ' + tag('target', 'ok') : ''))
		+ row('Position', '<span class="mono">' + d.position + '</span> cnt (cmd '
			+ d.cmdposition + ')')
		+ row('Velocity', '<span class="mono">' + d.velocity + '</span> cnt/s')
		+ row('Current', d.current.toFixed(3) + ' A of ' + d.contcurrent.toFixed(2)
			+ ' A cont.')
		+ row('Bus voltage', d.busvoltage.toFixed(1) + ' V')
		+ row('Temperature', d.temperature.toFixed(0) + ' &deg;C')
		+ row('Digital inputs', '<span class="mono">'
			+ d.digitalinputs.toString(2).padStart(8, '0') + '</span>')
		+ row('Motion', esc(d.motion))
		+ (d.lasterror ? row('Last error', tag(d.lasterror, 'err')) : '');
}
function driveCard(d, i) {
	// The controls are built once and are not touched by the status updates,
	// so that a value, which is being typed in, does not get overwritten.
	return '<div class="card" id="card' + i + '" data-drive="' + i + '">'
		+ '<h2>Drive ' + i + ' &mdash; Elmo ' + esc(d.family)
		+ ' (slave ' + d.slave + ', ' + esc(d.commandmode) + ' mode)</h2>'
		+ '<table><tbody id="status' + i + '"></tbody></table>'
		+ '<h3>Drive state</h3><div class="row">'
		+ '<button class="primary" onclick="send(' + i + ',\'enable\',{})">Enable</button>'
		+ '<button onclick="send(' + i + ',\'disable\',{})">Disable</button>'
		+ '<button onclick="send(' + i + ',\'resetfaults\',{})">Reset faults</button>'
		+ '<button id="arm' + i + '"></button>'
		+ '<button class="danger" onclick="send(' + i + ',\'stop\',{})">STOP</button>'
		+ '<span class="tag" id="armtag' + i + '"></span>'
		+ '</div>'
		+ '<h3>Jog (max ' + d.maxjogvelocity + ' cnt/s)</h3><div class="row">'
		+ '<label>velocity</label>'
		+ '<input type="number" id="jog' + i + '" value="1000" step="100">'
		+ '<button class="motion' + i + '" onclick="send(' + i
			+ ',\'jog\',{value:-num(\'jog' + i + '\')})">&minus; Jog</button>'
		+ '<button class="motion' + i + '" onclick="send(' + i
			+ ',\'jog\',{value:num(\'jog' + i + '\')})">Jog +</button>'
		+ '<button class="motion' + i + '" onclick="send(' + i
			+ ',\'jog\',{value:0})">Zero</button>'
		+ '</div>'
		+ '<h3>Position move</h3><div class="row">'
		+ '<label>target</label><input type="number" id="pos' + i + '" value="0" step="1000">'
		+ '<label>velocity</label><input type="number" id="posvel' + i
			+ '" value="1000" step="100">'
		+ '<button class="motion' + i + '" onclick="send(' + i
			+ ',\'moveto\',{target:num(\'pos' + i + '\'),value:num(\'posvel' + i
			+ '\')})">Move to</button>'
		+ '<button class="motion' + i + '" onclick="send(' + i
			+ ',\'moveby\',{target:num(\'pos' + i + '\'),value:num(\'posvel' + i
			+ '\')})">Move by</button>'
		+ '</div>'
		+ '<h3>Torque (max ' + d.maxtorque.toFixed(2) + ' A)</h3><div class="row">'
		+ '<label>amps</label><input type="number" id="torque' + i
			+ '" value="0" step="0.05">'
		+ '<button class="danger motion' + i + '" onclick="torque(' + i
			+ ')">Apply torque</button>'
		+ '</div></div>';
}
function updateCard(d, i) {
	document.getElementById('status' + i).innerHTML = statusRows(d);

	document.getElementById('card' + i).className = 'card' + (d.armed ? ' armed' : '');

	var arm = document.getElementById('arm' + i);
	arm.textContent = d.armed ? 'Disarm' : 'Arm for motion';
	arm.className = d.armed ? 'danger' : '';
	arm.disabled = !d.armed && !d.servoenabled;
	arm.onclick = function () { send(i, d.armed ? 'disarm' : 'arm', {}); };

	var armtag = document.getElementById('armtag' + i);
	armtag.textContent = d.armed ? 'ARMED' : '';
	armtag.className = 'tag ' + (d.armed ? 'warn' : '');

	// motion is only possible, while the drive is enabled and armed
	var canMove = d.armed && d.servoenabled;
	var buttons = document.querySelectorAll('.motion' + i);
	for (var k = 0; k < buttons.length; k++) {
		buttons[k].disabled = !canMove;
	}
}
function scanTable(slaves) {
	if (!slaves || !slaves.length) {
		return '<tbody><tr><td>The bus has not been scanned yet.</td></tr></tbody>';
	}
	var out = '<thead><tr><th>#</th><th>EEPROM name</th><th>Vendor</th>'
		+ '<th>Product code</th><th>Revision</th><th>Serial</th>'
		+ '<th>Device name</th><th>Hardware</th><th>Software</th>'
		+ '<th>PDO in/out</th><th>Status</th></tr></thead><tbody>';
	for (var i = 0; i < slaves.length; i++) {
		var s = slaves[i];
		out += '<tr><td>' + s.slave + '</td>'
			+ '<td>' + esc(s.name) + '</td>'
			+ '<td><span class="mono">' + hex(s.vendor, 8) + '</span>'
				+ (s.elmo ? ' ' + tag('Elmo', '') : '') + '</td>'
			+ '<td class="mono">' + hex(s.productcode, 8) + '</td>'
			+ '<td class="mono">' + hex(s.revision, 8) + '</td>'
			+ '<td class="mono">' + hex(s.serial, 8) + '</td>'
			+ '<td>' + (s.coe ? esc(s.devicename) : tag('no CoE', '')) + '</td>'
			+ '<td>' + esc(s.hardware) + '</td>'
			+ '<td>' + esc(s.software) + '</td>'
			+ '<td class="mono">' + (s.inbytes || s.outbytes
				? s.inbytes + ' / ' + s.outbytes : '&mdash;') + '</td>'
			+ '<td>' + tag(s.note || (s.driven ? 'driven' : 'not driven'),
				s.driven ? 'ok' : (s.elmo ? 'warn' : '')) + '</td></tr>';
	}
	return out + '</tbody>';
}
function render(data) {
	var b = data.bus, s = data.system;

	document.getElementById('bus').innerHTML =
		  row('Master state', tag(b.state, b.state === 'Operational' ? 'ok' : 'warn'))
		+ (b.error ? row('Last error', tag(b.error, 'err')) : '')
		+ row('Interface', esc(b.interface) + ' (' + esc(b.mac) + ')')
		+ row('Link', tag(b.linkspeed, b.linkup ? 'ok' : 'err'))
		+ row('Slaves / drives', b.slavecount + ' / ' + b.drivecount)
		+ row('Work counter', b.wkc + ' of ' + b.expectedwkc)
		+ row('Cycle time', (b.cycletime / 1000).toFixed(1) + ' ms')
		+ row('Cycles', b.cycles.toLocaleString())
		+ row('WKC errors', b.wkcerrors.toLocaleString())
		+ row('Missed deadlines', b.misseddeadlines.toLocaleString()
			+ (b.misseddeadlines ? ' ' + tag('jitter', 'warn') : ''))
		+ row('Longest cycle', (b.maxperiod / 1000).toFixed(2) + ' ms')
		+ row('Operational since', b.opseconds + ' s');

	document.getElementById('system').innerHTML =
		  row('Model', esc(s.machine))
		+ row('SoC', esc(s.soc))
		+ row('CPU clock', (s.cpuclock / 1000000).toFixed(0) + ' MHz')
		+ row('SoC temperature', s.temperature + ' &deg;C')
		+ row('Heap free', (s.heapfree / (1024 * 1024)).toFixed(1) + ' MByte')
		+ row('Uptime', s.uptime + ' s')
		+ row('IP address', '<span class="mono">' + esc(s.ip) + '</span>')
		+ row('Tasks', s.taskcount)
		+ row('Circle version', esc(s.version));

	document.getElementById('scan').innerHTML = scanTable(b.slaves);

	// rebuild the drive cards only, when the drives on the bus have changed
	var sig = b.drives.map(function (d) { return d.slave + d.family; }).join('|');
	if (sig !== builtSig) {
		builtSig = sig;
		document.getElementById('drives').innerHTML =
			  b.drives.length
			? b.drives.map(driveCard).join('')
			: '<div class="card">No Elmo drives found on the bus.</div>';
	}

	armedAny = false;
	for (var i = 0; i < b.drives.length; i++) {
		updateCard(b.drives[i], i);
		if (b.drives[i].armed) armedAny = true;
	}

	var armstate = document.getElementById('armstate');
	armstate.textContent = armedAny ? 'a drive is ARMED for motion' : 'no drive armed';
	armstate.className = 'tag ' + (armedAny ? 'warn' : '');

	document.getElementById('updated').textContent =
		'updated ' + new Date().toLocaleTimeString()
		+ (armedAny ? ' - heartbeat active, motion stops if this page closes' : '');

	schedule();
}
function schedule() {
	// The master halts an armed drive, if it does not see a request within
	// 500 ms, so the page has to poll faster, while a drive is armed.
	if (timer) clearTimeout(timer);
	timer = setTimeout(update, armedAny ? 200 : 1000);
}
function update() {
	fetch('/status.json?t=' + Date.now())
		.then(function (r) { return r.json(); })
		.then(render)
		.catch(function () {
			document.getElementById('updated').textContent = 'connection lost ...';
			schedule();
		});
}
update();
</script>
</body>
</html>
)PAGE";

CWebServer::CWebServer (CNetSubSystem *pNetSubSystem, CElmoMaster *pElmoMaster,
			CSocket *pSocket)
:	CHTTPDaemon (pNetSubSystem, pSocket, MAX_CONTENT_SIZE, HTTP_PORT, 0, TIMEOUT_SECONDS),
	m_pNet (pNetSubSystem),
	m_pElmoMaster (pElmoMaster)
{
}

CWebServer::~CWebServer (void)
{
	m_pNet = 0;
	m_pElmoMaster = 0;
}

CHTTPDaemon *CWebServer::CreateWorker (CNetSubSystem *pNetSubSystem, CSocket *pSocket)
{
	return new CWebServer (pNetSubSystem, m_pElmoMaster, pSocket);
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
	else if (strcmp (pPath, "/control") == 0)
	{
		HandleControl (pFormData != 0 ? pFormData : "", JSON);

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

//
// Control interface
//

void CWebServer::HandleControl (const char *pFormData, CString &rJSON)
{
	assert (pFormData != 0);

	CString CommandName;
	double fDrive = 0.0;

	if (   !GetParam (pFormData, "cmd", CommandName)
	    || !GetParamNumber (pFormData, "drive", &fDrive))
	{
		rJSON = "{\"ok\":false,\"error\":\"Missing parameter\"}";

		return;
	}

	TElmoCommand Command;
	Command.Code = CElmoMaster::GetCommandCode ((const char *) CommandName);
	Command.nDrive = (unsigned) fDrive;
	Command.nTarget = 0;
	Command.fValue = 0.0;

	if (Command.Code == ElmoCommandUnknown)
	{
		rJSON = "{\"ok\":false,\"error\":\"Unknown command\"}";

		return;
	}

	double fValue;
	if (GetParamNumber (pFormData, "value", &fValue))
	{
		Command.fValue = fValue;
	}

	double fTarget;
	if (GetParamNumber (pFormData, "target", &fTarget))
	{
		Command.nTarget = (s32) fTarget;
	}

	assert (m_pElmoMaster != 0);

	CString Error;
	if (!m_pElmoMaster->QueueCommand (Command, Error))
	{
		CString Escaped;
		EscapeString (Escaped, (const char *) Error);

		rJSON.Format ("{\"ok\":false,\"error\":\"%s\"}", (const char *) Escaped);

		return;
	}

	rJSON = "{\"ok\":true}";
}

boolean CWebServer::GetParam (const char *pFormData, const char *pName, CString &rValue)
{
	assert (pFormData != 0);
	assert (pName != 0);

	size_t nNameLength = strlen (pName);

	const char *p = pFormData;
	while (*p != '\0')
	{
		if (   strncmp (p, pName, nNameLength) == 0
		    && p[nNameLength] == '=')
		{
			p += nNameLength + 1;

			rValue = "";

			// decode the URL encoding, while the value is copied
			while (   *p != '\0'
			       && *p != '&')
			{
				if (*p == '+')
				{
					rValue.Append (' ');
					p++;
				}
				else if (   *p == '%'
					 && p[1] != '\0'
					 && p[2] != '\0')
				{
					char Hex[3] = {p[1], p[2], '\0'};

					rValue.Append ((char) strtoul (Hex, 0, 16));
					p += 3;
				}
				else
				{
					rValue.Append (*p++);
				}
			}

			return TRUE;
		}

		// skip to the next parameter
		while (   *p != '\0'
		       && *p != '&')
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

boolean CWebServer::GetParamNumber (const char *pFormData, const char *pName,
				    double *pValue)
{
	assert (pValue != 0);

	CString Value;
	if (!GetParam (pFormData, pName, Value))
	{
		return FALSE;
	}

	// a small decimal number parser, strtod() is not available
	const char *p = (const char *) Value;

	while (   *p == ' '
	       || *p == '\t')
	{
		p++;
	}

	double fSign = 1.0;
	if (   *p == '-'
	    || *p == '+')
	{
		fSign = *p == '-' ? -1.0 : 1.0;
		p++;
	}

	boolean bDigit = FALSE;
	double fResult = 0.0;

	while (   '0' <= *p
	       && *p <= '9')
	{
		fResult = fResult * 10.0 + (*p++ - '0');
		bDigit = TRUE;
	}

	if (*p == '.')
	{
		p++;

		double fFactor = 0.1;
		while (   '0' <= *p
		       && *p <= '9')
		{
			fResult += (*p++ - '0') * fFactor;
			fFactor /= 10.0;
			bDigit = TRUE;
		}
	}

	if (!bDigit)
	{
		return FALSE;
	}

	*pValue = fSign * fResult;

	return TRUE;
}

//
// Status
//

void CWebServer::BuildStatusJSON (CString &rJSON)
{
	TElmoStatus *pElmoStatus = new TElmoStatus;
	TSystemStatus *pSystemStatus = new TSystemStatus;

	if (   pElmoStatus == 0
	    || pSystemStatus == 0)
	{
		delete pElmoStatus;
		delete pSystemStatus;

		rJSON = "{}";

		return;
	}

	memset (pElmoStatus, 0, sizeof *pElmoStatus);

	assert (m_pElmoMaster != 0);
	m_pElmoMaster->GetStatus (pElmoStatus);

	// fetching the status is the heartbeat of the control interface
	m_pElmoMaster->Heartbeat ();

	GetSystemStatus (pSystemStatus, m_pNet);

	rJSON = "{\"bus\":{";

	AppendString (rJSON, "state", pElmoStatus->MasterState);
	AppendString (rJSON, "error", pElmoStatus->LastError);
	AppendString (rJSON, "interface", pElmoStatus->InterfaceName);
	AppendString (rJSON, "mac", pElmoStatus->MACAddress);
	AppendString (rJSON, "linkspeed", pElmoStatus->LinkSpeed);
	AppendBoolean (rJSON, "linkup", pElmoStatus->LinkUp);
	AppendNumber (rJSON, "slavecount", pElmoStatus->nSlaveCount);
	AppendNumber (rJSON, "drivecount", pElmoStatus->nDriveCount);
	AppendSigned (rJSON, "wkc", pElmoStatus->nWorkCounter);
	AppendSigned (rJSON, "expectedwkc", pElmoStatus->nExpectedWorkCounter);
	AppendNumber (rJSON, "cycletime", pElmoStatus->nCycleTimeUs);
	AppendNumber (rJSON, "cycles", pElmoStatus->nCycles);
	AppendNumber (rJSON, "wkcerrors", pElmoStatus->nWKCErrors);
	AppendNumber (rJSON, "misseddeadlines", pElmoStatus->nMissedDeadlines);
	AppendNumber (rJSON, "maxperiod", pElmoStatus->nMaxCyclePeriodUs);
	AppendNumber (rJSON, "opseconds", pElmoStatus->nOperationalSeconds);

	rJSON.Append ("\"drives\":[");

	unsigned nShown = 0;
	for (unsigned i = 0; i < ELMO_MAX_DRIVES; i++)
	{
		if (!pElmoStatus->Drives[i].bPresent)
		{
			continue;
		}

		if (nShown++ > 0)
		{
			rJSON.Append (",");
		}

		AppendDriveStatus (rJSON, &pElmoStatus->Drives[i], i);
	}

	rJSON.Append ("],\"slaves\":[");

	for (unsigned i = 0;
	         i < pElmoStatus->nSlaveInfoCount && i < ELMO_MAX_SLAVES;
	         i++)
	{
		if (i > 0)
		{
			rJSON.Append (",");
		}

		AppendSlaveInfo (rJSON, &pElmoStatus->SlaveInfo[i]);
	}

	rJSON.Append ("]},");

	AppendSystemStatus (rJSON, pSystemStatus);

	rJSON.Append ("}");

	delete pSystemStatus;
	delete pElmoStatus;
}

void CWebServer::AppendDriveStatus (CString &rJSON, const TElmoDriveStatus *pDrive,
				    unsigned nIndex)
{
	assert (pDrive != 0);

	rJSON.Append ("{");

	AppendNumber (rJSON, "index", nIndex);
	AppendNumber (rJSON, "slave", pDrive->nSlave);
	AppendString (rJSON, "family", pDrive->Family);
	AppendString (rJSON, "commandmode", pDrive->CommandMode);
	AppendBoolean (rJSON, "canprofiled", pDrive->bCanProfiled);
	AppendBoolean (rJSON, "cancyclic", pDrive->bCanCyclic);
	AppendNumber (rJSON, "productcode", pDrive->ProductCode);
	AppendNumber (rJSON, "serial", pDrive->SerialNumber);

	AppendString (rJSON, "statemachine", pDrive->StateMachine);
	AppendString (rJSON, "modeofoperation", pDrive->ModeOfOperation);
	AppendString (rJSON, "faultstring", pDrive->FaultString);
	AppendNumber (rJSON, "faultcode", pDrive->nFaultCode);
	AppendNumber (rJSON, "emcycode", pDrive->nEMCYCode);
	AppendBoolean (rJSON, "faultunknown", pDrive->bFaultUnknown);

	AppendSigned (rJSON, "position", pDrive->nActualPosition);
	AppendSigned (rJSON, "velocity", pDrive->nActualVelocity);
	AppendSigned (rJSON, "cmdposition", pDrive->nCommandPosition);
	AppendFloat (rJSON, "current", pDrive->fActualCurrent);
	AppendFloat (rJSON, "busvoltage", pDrive->fBusVoltage);
	AppendFloat (rJSON, "temperature", pDrive->fDriveTemperature);

	AppendBoolean (rJSON, "servoenabled", pDrive->bServoEnabled);
	AppendBoolean (rJSON, "motoron", pDrive->bMotorOn);
	AppendBoolean (rJSON, "inmotion", pDrive->bInMotion);
	AppendBoolean (rJSON, "stoengaged", pDrive->bSTOEngaged);
	AppendBoolean (rJSON, "warning", pDrive->bWarning);
	AppendBoolean (rJSON, "targetreached", pDrive->bTargetReached);
	AppendNumber (rJSON, "digitalinputs", pDrive->usDigitalInputs);

	AppendBoolean (rJSON, "armed", pDrive->bArmed);
	AppendString (rJSON, "motion", pDrive->Motion);
	AppendString (rJSON, "lasterror", pDrive->LastError);

	AppendFloat (rJSON, "peakcurrent", pDrive->fPeakCurrentLimit);
	AppendFloat (rJSON, "contcurrent", pDrive->fContinuousCurrentLimit);
	AppendFloat (rJSON, "maxjogvelocity", pDrive->fMaxJogVelocity);
	AppendFloat (rJSON, "maxtorque", pDrive->fMaxTorque);

	RemoveTrailingComma (rJSON);

	rJSON.Append ("}");
}

void CWebServer::AppendSlaveInfo (CString &rJSON, const TElmoSlaveInfo *pInfo)
{
	assert (pInfo != 0);

	rJSON.Append ("{");

	AppendNumber (rJSON, "slave", pInfo->nSlave);
	AppendString (rJSON, "name", pInfo->Name);
	AppendNumber (rJSON, "vendor", pInfo->VendorID);
	AppendNumber (rJSON, "productcode", pInfo->ProductCode);
	AppendNumber (rJSON, "revision", pInfo->Revision);
	AppendNumber (rJSON, "serial", pInfo->Serial);
	AppendString (rJSON, "devicename", pInfo->DeviceName);
	AppendString (rJSON, "hardware", pInfo->HardwareVersion);
	AppendString (rJSON, "software", pInfo->SoftwareVersion);
	AppendBoolean (rJSON, "coe", pInfo->bHasCoE);
	AppendBoolean (rJSON, "elmo", pInfo->bElmo);
	AppendBoolean (rJSON, "driven", pInfo->bDriven);
	AppendString (rJSON, "note", pInfo->Note);
	AppendNumber (rJSON, "inbytes", pInfo->nInputBytes);
	AppendNumber (rJSON, "outbytes", pInfo->nOutputBytes);

	RemoveTrailingComma (rJSON);

	rJSON.Append ("}");
}

void CWebServer::AppendSystemStatus (CString &rJSON, const TSystemStatus *pStatus)
{
	assert (pStatus != 0);

	rJSON.Append ("\"system\":{");

	AppendString (rJSON, "machine", pStatus->MachineName);
	AppendString (rJSON, "soc", pStatus->SoCName);
	AppendNumber (rJSON, "ramsize", pStatus->nRAMSizeMBytes);
	AppendNumber (rJSON, "cpuclock", pStatus->nCPUClockRate);
	AppendNumber (rJSON, "temperature", pStatus->nTemperature);
	AppendNumber (rJSON, "throttled", pStatus->ThrottledState);
	AppendNumber (rJSON, "heapfree", pStatus->nHeapFreeSpace);
	AppendNumber (rJSON, "uptime", pStatus->nUptimeSeconds);
	AppendString (rJSON, "ip", pStatus->IPAddress);
	AppendString (rJSON, "hostname", pStatus->Hostname);
	AppendNumber (rJSON, "taskcount", pStatus->nTaskCount);
	AppendString (rJSON, "version", pStatus->CircleVersion);

	RemoveTrailingComma (rJSON);

	rJSON.Append ("}");
}

//
// JSON helpers
//

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

void CWebServer::AppendFloat (CString &rJSON, const char *pName, double fValue)
{
	assert (pName != 0);

	CString Field;
	Field.Format ("\"%s\":%.3f,", pName, fValue);

	rJSON.Append ((const char *) Field);
}

void CWebServer::AppendBoolean (CString &rJSON, const char *pName, boolean bValue)
{
	assert (pName != 0);

	CString Field;
	Field.Format ("\"%s\":%s,", pName, bValue ? "true" : "false");

	rJSON.Append ((const char *) Field);
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
