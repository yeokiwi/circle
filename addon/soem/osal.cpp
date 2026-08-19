//
// osal.cpp
//
// OS abstraction layer of SOEM (Simple Open EtherCAT Master) for Circle
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
#include "soemcpp.h"
#include <circle/timer.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/sched/mutex.h>
#include <circle/multicore.h>
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/alloc.h>
#include <circle/util.h>
#include <circle/types.h>
#include <stdarg.h>
#include <assert.h>

// CScheduler::IsActive() is a global check (is a CScheduler instance
// constructed anywhere?), not a per-core one. On a multi-core system, where
// the scheduler is legitimately used on core 0 (e.g. by networking tasks),
// this OSAL must not use the scheduler, CMutex or CTask when it is called
// from a different core, because those are only safe on core 0 (see
// doc/multicore.txt). CMultiCoreSupport::ThisCore() is only available, when
// ARM_ALLOW_MULTI_CORE is defined.
static inline boolean UseScheduler (void)
{
#ifdef ARM_ALLOW_MULTI_CORE
	if (CMultiCoreSupport::ThisCore () != 0)
	{
		return FALSE;
	}
#endif

	return CScheduler::IsActive ();
}

#define NSEC_PER_SEC	1000000000L
#define NSEC_PER_USEC	1000L

static const char FromSOEM[] = "soem";

//
// Time and timers
//
// The monotonic time is taken from the free running 1 MHz counter of Circle,
// which does not wrap in the 64-bit variant.
//

static void TicksToTime (u64 nTicks, ec_timet *pTime)
{
	assert (pTime != 0);

	pTime->tv_sec = (int64_t) (nTicks / CLOCKHZ);
	pTime->tv_nsec = (long) ((nTicks % CLOCKHZ) * NSEC_PER_USEC);
}

static u64 TimeToTicks (const ec_timet *pTime)
{
	assert (pTime != 0);

	return (u64) pTime->tv_sec * CLOCKHZ + (u64) (pTime->tv_nsec / NSEC_PER_USEC);
}

void osal_get_monotonic_time (ec_timet *ts)
{
	TicksToTime (CTimer::GetClockTicks64 (), ts);
}

ec_timet osal_current_time (void)
{
	ec_timet Result;

	// Returns the time since 1970-01-01, if it has been set (e.g. from a NTP
	// server), the time since system start otherwise.
	unsigned nSeconds, nMicroSeconds;
	CTimer *pTimer = CTimer::Get ();
	if (   pTimer != 0
	    && pTimer->GetLocalTime (&nSeconds, &nMicroSeconds))
	{
		Result.tv_sec = nSeconds;
		Result.tv_nsec = nMicroSeconds * NSEC_PER_USEC;
	}
	else
	{
		osal_get_monotonic_time (&Result);
	}

	return Result;
}

void osal_time_diff (ec_timet *start, ec_timet *end, ec_timet *diff)
{
	assert (start != 0);
	assert (end != 0);
	assert (diff != 0);

	if (end->tv_nsec < start->tv_nsec)
	{
		diff->tv_sec = end->tv_sec - start->tv_sec - 1;
		diff->tv_nsec = NSEC_PER_SEC + end->tv_nsec - start->tv_nsec;
	}
	else
	{
		diff->tv_sec = end->tv_sec - start->tv_sec;
		diff->tv_nsec = end->tv_nsec - start->tv_nsec;
	}
}

void osal_timer_start (osal_timert *self, uint32 timeout_usec)
{
	assert (self != 0);

	TicksToTime (CTimer::GetClockTicks64 () + timeout_usec, &self->stop_time);
}

// ec_boolean is the type, which SOEM declares as "boolean" (see soemcpp.h)
ec_boolean osal_timer_is_expired (osal_timert *self)
{
	assert (self != 0);

	return CTimer::GetClockTicks64 () >= TimeToTicks (&self->stop_time) ? TRUE : FALSE;
}

int osal_usleep (uint32 usec)
{
	if (UseScheduler ())
	{
		if (usec == 0)
		{
			CScheduler::Get ()->Yield ();
		}
		else
		{
			CScheduler::Get ()->usSleep (usec);
		}
	}
	else
	{
		CTimer::SimpleusDelay (usec);
	}

	return 0;
}

int osal_monotonic_sleep (ec_timet *ts)
{
	assert (ts != 0);

	u64 nWakeTicks = TimeToTicks (ts);
	u64 nNowTicks = CTimer::GetClockTicks64 ();

	if (nWakeTicks > nNowTicks)
	{
		osal_usleep ((uint32) (nWakeTicks - nNowTicks));
	}
	else
	{
		// the wake-up time has already been passed, just give other tasks a chance
		osal_usleep (0);
	}

	return 0;
}

//
// Memory
//

void *osal_malloc (size_t size)
{
	return malloc (size);
}

void osal_free (void *ptr)
{
	free (ptr);
}

//
// Threads
//
// A SOEM thread is a Circle task, which runs under control of the cooperative
// non-preemptive scheduler. Because there are no priorities, a real-time
// thread is created in the same way as a normal thread.
//

class CSOEMTask : public CTask
{
public:
	CSOEMTask (void (*pFunction) (void *pParam), void *pParam, unsigned nStackSize)
	:	CTask (nStackSize),
		m_pFunction (pFunction),
		m_pParam (pParam)
	{
		CString Name;
		Name.Format ("soem%u", s_nNextID++);

		SetName (Name);
	}

	void Run (void) override
	{
		assert (m_pFunction != 0);
		(*m_pFunction) (m_pParam);
	}

private:
	void (*m_pFunction) (void *pParam);
	void *m_pParam;

	static unsigned s_nNextID;
};

unsigned CSOEMTask::s_nNextID = 0;

static int CreateTask (void *thandle, int stacksize, void *func, void *param)
{
	if (   !UseScheduler ()
	    || func == 0)
	{
		return 0;
	}

	unsigned nStackSize = (unsigned) stacksize;
	if (nStackSize < TASK_STACK_SIZE)
	{
		nStackSize = TASK_STACK_SIZE;
	}
	nStackSize = (nStackSize + 15) & ~15U;

	CTask *pTask = new CSOEMTask ((void (*) (void *)) func, param, nStackSize);

	if (thandle != 0)
	{
		*(CTask **) thandle = pTask;
	}

	return 1;
}

int osal_thread_create (void *thandle, int stacksize, void *func, void *param)
{
	return CreateTask (thandle, stacksize, func, param);
}

int osal_thread_create_rt (void *thandle, int stacksize, void *func, void *param)
{
	return CreateTask (thandle, stacksize, func, param);
}

//
// Mutexes
//
// The mutexes are nop's, if the scheduler is not used. SOEM can be used from
// one task only in this case.
//

void *osal_mutex_create (void)
{
	if (!UseScheduler ())
	{
		return 0;
	}

	return new CMutex;
}

void osal_mutex_destroy (void *mutex)
{
	delete (CMutex *) mutex;
}

void osal_mutex_lock (void *mutex)
{
	if (mutex != 0)
	{
		((CMutex *) mutex)->Acquire ();
	}
}

void osal_mutex_unlock (void *mutex)
{
	if (mutex != 0)
	{
		((CMutex *) mutex)->Release ();
	}
}

//
// Debug output
//

void ec_circle_print (const char *pFormat, ...)
{
	assert (pFormat != 0);

	va_list var;
	va_start (var, pFormat);

	CString Message;
	Message.FormatV (pFormat, var);
	Message.TrimRight ();

	va_end (var);

	CLogger *pLogger = CLogger::Get ();
	if (pLogger != 0)
	{
		pLogger->Write (FromSOEM, LogDebug, "%s", (const char *) Message);
	}
}

#if STDLIB_SUPPORT < 2

// SOEM formats some strings with snprintf(3), which is not provided by the
// Circle libraries. This implementation supports the subset of the format
// specifiers, which is understood by the class CString.

extern "C" int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...)
{
	assert (pFormat != 0);

	va_list var;
	va_start (var, pFormat);

	CString String;
	String.FormatV (pFormat, var);

	va_end (var);

	size_t nLength = String.GetLength ();

	if (   pBuffer != 0
	    && nSize > 0)
	{
		size_t nCopy = nLength < nSize-1 ? nLength : nSize-1;

		memcpy (pBuffer, (const char *) String, nCopy);
		pBuffer[nCopy] = '\0';
	}

	return (int) nLength;
}

#endif
