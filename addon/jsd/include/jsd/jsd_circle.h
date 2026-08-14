//
// jsd_circle.h
//
// Additions of the Circle port of JSD
//
// These functions are specific to the Circle port and are not part of the
// JSD API. They replace the background SDO thread, which is created with
// POSIX threads by the original implementation.
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
#ifndef JSD_CIRCLE_H
#define JSD_CIRCLE_H

#include <jsd/jsd_pub.h>

#ifdef __cplusplus
extern "C" {
#endif

/// \brief Maximum number of mailboxes, which are serviced per process data
///	   cycle by the SOEM mailbox handler
#define JSD_MBX_HANDLER_LIMIT	4

/// \brief Interval, in which the SDO handler task looks for new requests (ms)
#define JSD_SDO_HANDLER_INTERVAL_MS	10

/// \brief Start the task, which services the asynchronous SDO requests and
///	   collects the emergency (EMCY) messages from the slaves
/// \param self JSD context
/// \return true on success
/// \note This is called by jsd_init(), an application does not have to call it.
bool jsd_sdo_start_handler (jsd_t *self);

/// \brief Stop the SDO handler task and wait for its termination
/// \param self JSD context
void jsd_sdo_stop_handler (jsd_t *self);

#ifdef __cplusplus
}
#endif

#endif
