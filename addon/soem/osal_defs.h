//
// osal_defs.h
//
// Platform specific definitions for the SOEM OS abstraction layer (Circle port)
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
#ifndef _osal_defs_
#define _osal_defs_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Debug output is written to the Circle system log, if EC_DEBUG is defined.
void ec_circle_print (const char *pFormat, ...);

#ifdef EC_DEBUG
#define EC_PRINT ec_circle_print
#else
#define EC_PRINT(...)	\
	do		\
	{		\
	} while (0)
#endif

#ifndef OSAL_PACKED
#define OSAL_PACKED_BEGIN
#define OSAL_PACKED	__attribute__ ((__packed__))
#define OSAL_PACKED_END
#endif

// Circle does not provide "struct timespec" in all configurations, so SOEM
// uses this own time definition here. The field names are the same as those
// of "struct timespec", because they are accessed by the SOEM sources.
struct ec_circle_time
{
	int64_t	tv_sec;			///< seconds
	long	tv_nsec;		///< nanoseconds (0 .. 999999999)
};

#define ec_timet		struct ec_circle_time

// A thread handle is a pointer to the CTask object, which has been created.
#define OSAL_THREAD_HANDLE	void *
#define OSAL_THREAD_FUNC	void
#define OSAL_THREAD_FUNC_RT	void

// A mutex is referenced by an opaque pointer to a CMutex object.
#define osal_mutext		void

#ifdef __cplusplus
}
#endif

#endif
