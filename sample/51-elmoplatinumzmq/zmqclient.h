//
// zmqclient.h
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
#ifndef _zmqclient_h
#define _zmqclient_h

#include "elmo_core1.h"
#include "zmtp.h"
#include <circle/sched/task.h>
#include <circle/net/netsubsystem.h>
#include <circle/net/socket.h>
#include <circle/string.h>
#include <circle/types.h>

/// \brief Publishes the Elmo Platinum drive status to a ZeroMQ broker and
///	   receives Start (arm) / Stop (disarm) commands from it.
///
/// This task runs entirely on core 0 (it is a CTask, uses CScheduler-based
/// sleeps and the CSocket/CNetSubSystem stack, all of which are documented as
/// core-0-only in doc/multicore.txt). The drive master itself runs on core 1
/// (see CElmoPlatinumCore1Master); the only cross-core touch points are
/// CElmoPlatinumCore1Master::GetStatus() and ::SubmitCommand(), which are
/// spin-lock protected.
///
/// This client acts as a ZMTP DEALER connecting to a ROUTER-based broker, see
/// zmtp.h for the protocol scope/limitations. Two message "topics" are
/// multiplexed over the one connection, tagged by the first frame:
///   ["status",  <JSON>]   sent periodically (device -> broker)
///   ["command", <JSON>]   received asynchronously (broker -> device),
///			    body is {"cmd":"start"} (arm) or {"cmd":"stop"} (disarm)
class CZeroMQClient : public CTask
{
public:
	/// \param pNetSubSystem     Pointer to the network subsystem (bound to the
	///			     USB Ethernet / "eth1" net device)
	/// \param pElmo	     The drive master to publish the status of and
	///			     to submit received arm/disarm commands to
	/// \param nStatusIntervalMs Interval between two status messages
	CZeroMQClient (CNetSubSystem *pNetSubSystem, CElmoPlatinumCore1Master *pElmo,
		       unsigned nStatusIntervalMs = 500);
	~CZeroMQClient (void);

	/// \brief Set the ZeroMQ broker to connect (and reconnect on failure) to
	/// \param pHost Host name or IP address (dotted string) of the broker
	/// \param usPort TCP port number of the broker's ROUTER socket
	void Connect (const char *pHost, u16 usPort);

	void Run (void) override;

private:
	boolean TryConnect (void);
	void Disconnect (void);

	void PublishStatus (void);
	void ReceiveCommands (void);
	void HandleCommandMessage (const u8 *pPayload, size_t nLength);

	static void BuildStatusJSON (CString &rJSON, const TElmoPlatinumStatus *pStatus);

	static void AppendString (CString &rJSON, const char *pName, const char *pValue);
	static void AppendNumber (CString &rJSON, const char *pName, u64 nValue);
	static void AppendSigned (CString &rJSON, const char *pName, s64 nValue);
	static void AppendBoolean (CString &rJSON, const char *pName, boolean bValue);
	static void EscapeString (CString &rResult, const char *pString);

private:
	CNetSubSystem *m_pNetSubSystem;
	CElmoPlatinumCore1Master *m_pElmo;
	unsigned m_nStatusIntervalMs;

	CString m_Host;
	u16 m_usPort;
	boolean m_bConnectRequested;

	CSocket *m_pSocket;
	CZMTPConnection *m_pZMTP;
	boolean m_bConnected;
	CString m_IPServerString;

	unsigned m_nLastStatusTicks;
	unsigned m_nLastConnectAttemptTicks;
};

#endif
