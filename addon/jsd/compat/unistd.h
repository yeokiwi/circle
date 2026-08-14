//
// unistd.h
//
// Replacement for the POSIX header <unistd.h> for the Circle port of JSD
//
// The public header jsd_pub.h of JSD includes <unistd.h>, which may pull in
// the POSIX thread types, colliding with compat/pthread.h. None of the JSD
// sources, which are built for Circle, uses a function from this header, so it
// is empty here.
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
#ifndef _jsd_compat_unistd_h
#define _jsd_compat_unistd_h

#include <stddef.h>

#endif
