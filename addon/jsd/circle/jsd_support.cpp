//
// jsd_support.cpp
//
// OS support functions of the Circle port of JSD
//
// Implements the logging and time functions, which are declared by the Circle
// variants of jsd_print.h and jsd_time.h, the small POSIX mutex compatibility
// layer from compat/pthread.h and strdup(3), which is used by JSD, but is not
// provided by Circle.
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
#include <jsd/jsd_print.h>
#include <pthread.h>
#include <circle/logger.h>
#include <circle/timer.h>
#include <circle/sched/scheduler.h>
#include <circle/sched/mutex.h>
#include <circle/string.h>
#include <circle/alloc.h>
#include <circle/util.h>
#include <stdarg.h>
#include <assert.h>

static const char FromJSD[] = "jsd";

//
// Logging
//

/// \brief Copy a printf(3) format string, translating the length modifier "z",
///	   which is not supported by the class CString, to "l"
static void TranslateFormat (CString &rResult, const char *pFormat)
{
	assert (pFormat != 0);

	rResult = "";

	boolean bInFormat = FALSE;

	for (const char *p = pFormat; *p != '\0'; p++)
	{
		if (!bInFormat)
		{
			rResult.Append (*p);

			if (*p == '%')
			{
				bInFormat = TRUE;
			}

			continue;
		}

		// size_t has the same width as long on all Circle targets
		rResult.Append (*p == 'z' ? 'l' : *p);

		// the conversion specifier ends the format
		if (   ('a' <= *p && *p <= 'z' && *p != 'h' && *p != 'l' && *p != 'z')
		    || ('A' <= *p && *p <= 'Z')
		    || *p == '%')
		{
			bInFormat = FALSE;
		}
	}
}

/// \return Pointer to the file name part of a path
static const char *BaseName (const char *pPath)
{
	if (pPath == 0)
	{
		return "?";
	}

	const char *pResult = pPath;
	for (const char *p = pPath; *p != '\0'; p++)
	{
		if (*p == '/')
		{
			pResult = p+1;
		}
	}

	return pResult;
}

void jsd_circle_log (unsigned nSeverity, const char *pFile, unsigned nLine,
		     const char *pFormat, ...)
{
	CLogger *pLogger = CLogger::Get ();
	if (   pLogger == 0
	    || pFormat == 0)
	{
		return;
	}

	CString Format;
	TranslateFormat (Format, pFormat);

	va_list var;
	va_start (var, pFormat);

	CString Message;
	Message.FormatV ((const char *) Format, var);

	va_end (var);

	Message.TrimRight ();

	// LogPanic would halt the system, which JSD does not intend
	if (nSeverity < LogError)
	{
		nSeverity = LogError;
	}

	pLogger->Write (FromJSD, (TLogSeverity) nSeverity, "(%s:%u) %s",
			BaseName (pFile), nLine, (const char *) Message);
}

//
// Time
//

double jsd_circle_time_sec (void)
{
	CTimer *pTimer = CTimer::Get ();
	if (pTimer == 0)
	{
		return 0.0;
	}

	unsigned nSeconds, nMicroSeconds;
	if (!pTimer->GetLocalTime (&nSeconds, &nMicroSeconds))
	{
		return jsd_circle_mono_time_sec ();
	}

	return (double) nSeconds + (double) nMicroSeconds * 1.0e-6;
}

double jsd_circle_mono_time_sec (void)
{
	return (double) CTimer::GetClockTicks64 () / (double) CLOCKHZ;
}

//
// POSIX mutex compatibility
//

int pthread_mutex_lock (pthread_mutex_t *mutex)
{
	if (   mutex == 0
	    || !CScheduler::IsActive ())
	{
		return 0;
	}

	// The mutex is created on first use. This is safe, because the Circle
	// scheduler is cooperative and does not switch tasks in between.
	if (*mutex == 0)
	{
		*mutex = new CMutex;
		if (*mutex == 0)
		{
			return -1;
		}
	}

	((CMutex *) *mutex)->Acquire ();

	return 0;
}

int pthread_mutex_unlock (pthread_mutex_t *mutex)
{
	if (   mutex == 0
	    || *mutex == 0)
	{
		return 0;
	}

	((CMutex *) *mutex)->Release ();

	return 0;
}

int pthread_mutex_destroy (pthread_mutex_t *mutex)
{
	if (mutex != 0)
	{
		delete (CMutex *) *mutex;

		*mutex = 0;
	}

	return 0;
}

//
// bsearch(3) is used by JSD to look up the Elmo two letter commands, but is
// not provided by Circle
//

extern "C" void *bsearch (const void *pKey, const void *pBase, size_t nElements,
			  size_t nElementSize,
			  int (*pCompare) (const void *pKey, const void *pElement))
{
	assert (pKey != 0);
	assert (pBase != 0);
	assert (pCompare != 0);

	size_t nLow = 0;
	size_t nHigh = nElements;

	while (nLow < nHigh)
	{
		size_t nMiddle = nLow + (nHigh - nLow) / 2;

		const void *pElement = (const char *) pBase + nMiddle * nElementSize;

		int nResult = (*pCompare) (pKey, pElement);

		if (nResult == 0)
		{
			return (void *) pElement;
		}

		if (nResult < 0)
		{
			nHigh = nMiddle;
		}
		else
		{
			nLow = nMiddle + 1;
		}
	}

	return 0;
}

//
// strdup(3) is used by JSD, but is not provided by Circle
//

extern "C" char *strdup (const char *pString)
{
	if (pString == 0)
	{
		return 0;
	}

	size_t nLength = strlen (pString) + 1;

	char *pResult = (char *) malloc (nLength);
	if (pResult != 0)
	{
		memcpy (pResult, pString, nLength);
	}

	return pResult;
}
