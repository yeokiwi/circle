//
// jsd_print.h
//
// Logging macros of JSD for Circle
//
// This header file replaces the file src/jsd_print.h of JSD, which writes all
// messages with fprintf(stderr). Circle does not provide the stdio functions,
// so the messages are written to the Circle system log instead. This is the
// single point, where all output of the JSD library goes through.
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
#ifndef JSD_PRINT_H_
#define JSD_PRINT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <jsd/jsd_time.h>
#include <stddef.h>

// The original jsd_print.h includes <stdio.h>, which provides snprintf(3) to
// the JSD sources. Circle does not have <stdio.h>, but snprintf() is provided
// by the Circle port of SOEM (see addon/soem/osal.cpp).
int snprintf (char *pBuffer, size_t nSize, const char *pFormat, ...);

// same values as TLogSeverity in <circle/logger.h>
#define JSD_LOG_PANIC	0
#define JSD_LOG_ERROR	1
#define JSD_LOG_WARNING	2
#define JSD_LOG_NOTICE	3
#define JSD_LOG_DEBUG	4

/// \brief Write one message to the Circle system log
/// \param nSeverity One of the JSD_LOG_* values
/// \param pFile Source file name of the message
/// \param nLine Line number of the message
/// \param pFormat Format string, supports the subset of printf(3), which is
///	   understood by the class CString ("%z" is translated to "%l")
void jsd_circle_log (unsigned nSeverity, const char *pFile, unsigned nLine,
		     const char *pFormat, ...);

#define MSG(M, ...)	jsd_circle_log (JSD_LOG_NOTICE, __FILE__, __LINE__, \
					M, ##__VA_ARGS__)

#define WARNING(M, ...)	jsd_circle_log (JSD_LOG_WARNING, __FILE__, __LINE__, \
					M, ##__VA_ARGS__)

#define ERROR(M, ...)	jsd_circle_log (JSD_LOG_ERROR, __FILE__, __LINE__, \
					M, ##__VA_ARGS__)

#define SUCCESS(M, ...)	jsd_circle_log (JSD_LOG_NOTICE, __FILE__, __LINE__, \
					M, ##__VA_ARGS__)

#ifdef DEBUG
#define MSG_DEBUG(M, ...) jsd_circle_log (JSD_LOG_DEBUG, __FILE__, __LINE__, \
					  M, ##__VA_ARGS__)
#else
#define MSG_DEBUG(M, ...)
#endif

#ifdef __cplusplus
}
#endif

#endif
