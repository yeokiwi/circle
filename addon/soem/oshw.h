//
// oshw.h
//
// Hardware abstraction layer of SOEM (Simple Open EtherCAT Master) for Circle
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2026  R. Stange <rsta2@gmx.net>
//
// This file is part of the Circle port of SOEM (Simple Open EtherCAT Master),
// which is dual-licensed under GPLv3 and a commercial license. See the file
// LICENSE.md in the SOEM sources for full license information.
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
#ifndef _oshw_
#define _oshw_

#ifdef __cplusplus
#include "soemcpp.h"
#else
#include "soem/soem.h"
#endif

#include "nicdrv.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Host to Network byte order (i.e. to big endian).
 * Note that EtherCAT uses little endian byte order, except for the Ethernet
 * header, which is big endian as usual.
 */
uint16 oshw_htons(uint16 host);

/** Network (i.e. big endian) to Host byte order. */
uint16 oshw_ntohs(uint16 network);

/** Create list over available network adapters.
 * @return First element in linked list of adapters
 */
ec_adaptert *oshw_find_adapters(void);

/** Free allocated memory used by adapter collection.
 * @param[in] adapter = First element in linked list of adapters
 */
void oshw_free_adapters(ec_adaptert *adapter);

#ifdef __cplusplus
}
#endif

#endif
