//
// ethercat.h
//
// SOEM 1.4 compatibility header for the Circle port of JSD
//
// JSD is written against SOEM 1.4 (with EC_VER2, the redistributable context
// API), which provides all definitions in one header "ethercat.h". Circle
// comes with a port of SOEM 2.0, where the same definitions are in
// <soem/soem.h>. This header provides "ethercat.h" for the JSD sources and
// maps the few names, which have been changed between both versions.
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
#ifndef _jsd_compat_ethercat_h
#define _jsd_compat_ethercat_h

// The JSD headers use the pthread types in their structs, without including
// <pthread.h> themselves. On Linux this comes in via the SOEM osal header.
#include <pthread.h>

#ifdef __cplusplus
// renames the SOEM type "boolean" to "ec_boolean", see addon/soem/soemcpp.h
#include <soemcpp.h>
#else
#include "soem/soem.h"
#endif

// SOEM 1.4 provides two slave configuration hooks: PO2SOconfig(), which gets
// the slave number only, and PO2SOconfigx(), which gets the context too. SOEM
// 2.0 has the context-aware hook only and calls it PO2SOconfig(). The
// signature is the same, so the JSD device drivers can be used unmodified.
#define PO2SOconfigx	PO2SOconfig

#endif
