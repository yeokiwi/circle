//
// pthread.h
//
// Minimal POSIX threads compatibility layer for the Circle port of JSD
//
// JSD declares pthread objects in its public structs and locks a mutex in the
// error and SDO request queues. Circle does not provide POSIX threads, so the
// types are defined as opaque pointers here and the few required functions are
// implemented on top of the Circle class CMutex (see circle/jsd_support.cpp).
//
// Only the subset, which is used by the JSD sources built for Circle, is
// provided. Threads themselves are not emulated: the background SDO handler is
// a CTask, which is created by the Circle variant of jsd_init().
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
#ifndef _jsd_compat_pthread_h
#define _jsd_compat_pthread_h

#ifdef __cplusplus
extern "C" {
#endif

/// \brief Handle of a task (unused, the SDO handler is a CTask)
typedef void *pthread_t;

/// \brief Pointer to a CMutex object, created on first use
typedef void *pthread_mutex_t;

/// \brief Condition variables are not emulated
typedef void *pthread_cond_t;

#define PTHREAD_MUTEX_INITIALIZER	0

/// \brief Acquire the mutex, create it on first use
/// \note The mutex is created on the first call, which is safe, because the
///	  Circle scheduler is cooperative and does not switch tasks in between.
int pthread_mutex_lock (pthread_mutex_t *mutex);

/// \brief Release the mutex
int pthread_mutex_unlock (pthread_mutex_t *mutex);

/// \brief Destroy the mutex
int pthread_mutex_destroy (pthread_mutex_t *mutex);

#ifdef __cplusplus
}
#endif

#endif
