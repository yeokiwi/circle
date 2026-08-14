//
// jsd_time.h
//
// Time functions of JSD for Circle
//
// This header file replaces the file src/jsd_time.h of JSD, which uses
// clock_gettime(2). The time is taken from the Circle system timer instead.
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
#ifndef JSD_TIME_H_
#define JSD_TIME_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "ethercat.h"

/// \return Seconds since 1970-01-01, if the system time has been set,
///	    seconds since system start otherwise
double jsd_circle_time_sec (void);

/// \return Seconds since system start (monotonic)
double jsd_circle_mono_time_sec (void);

/**
 * @brief Get the system's clock time since the Unix Epoch.
 * @return Number of seconds since Unix Epoch.
 */
static inline double jsd_time_get_time_sec (void)
{
	return jsd_circle_time_sec ();
}

/**
 * @brief Get monotonic time since unspecified fixed point. This function is
 * used to compute elapsed time.
 * @return Number of seconds since fixed point.
 */
static inline double jsd_time_get_mono_time_sec (void)
{
	return jsd_circle_mono_time_sec ();
}

/**
 * @brief Convert SOEM's time type to seconds.
 * @return Seconds representation of SOEM's time type object.
 * @note SOEM 1.4 defines ec_timet with the fields sec and usec, the Circle
 *	 port of SOEM 2.0 uses tv_sec and tv_nsec instead.
 */
static inline double ectime_to_sec (ec_timet t)
{
	return (double) t.tv_sec + (double) t.tv_nsec * 1.0e-9;
}

#ifdef __cplusplus
}
#endif

#endif
