//
// stdlib.h
//
// Replacement for the C library header <stdlib.h> for the Circle port of JSD
//
// The public header jsd_pub.h of JSD includes <stdlib.h>, which pulls in
// <sys/types.h> with most toolchains. That header defines the POSIX thread
// types, which collide with the compatibility definitions in compat/pthread.h.
// The JSD sources, which are built for Circle, do not use anything from
// <stdlib.h> themselves, so this header replaces it and declares the memory
// allocation functions of Circle only.
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
#ifndef _jsd_compat_stdlib_h
#define _jsd_compat_stdlib_h

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *malloc (size_t nSize);
void *calloc (size_t nBlocks, size_t nSize);
void *realloc (void *pBlock, size_t nSize);
void free (void *pBlock);

// bsearch(3) is used by JSD to look up the Elmo two letter commands, but is
// not provided by Circle (see circle/jsd_support.cpp).
void *bsearch (const void *pKey, const void *pBase, size_t nElements,
	       size_t nElementSize,
	       int (*pCompare) (const void *pKey, const void *pElement));

#ifdef __cplusplus
}
#endif

#endif
