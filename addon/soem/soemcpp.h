//
// soemcpp.h
//
// SOEM main include file for C++ files, which use Circle classes too
//
// SOEM defines the type "boolean" as uint8_t, while Circle defines it as
// bool. Both definitions cannot be active at the same time. That's why this
// header file renames the SOEM type to "ec_boolean" for C++ files. Use
// "ec_boolean" instead of "boolean", when you define a function, which is
// declared by SOEM with this return or parameter type. Both types have the
// same size, so that the C sources of SOEM, which use "boolean", can be
// linked together with the C++ sources without a problem.
//
// Include this file instead of <soem/soem.h> in C++ files!
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
#ifndef _soemcpp_h
#define _soemcpp_h

// must be included first, so that TRUE, FALSE and boolean are defined by Circle
#include <circle/types.h>

#ifdef __cplusplus
#define boolean	ec_boolean
#endif

#include "soem/soem.h"

#ifdef __cplusplus
#undef boolean
#endif

#endif
