//
// filemanager.cpp
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2025  R. Stange <rsta2@o2online.de>
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
#include "filemanager.h"
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

static const char FromFileManager[] = "filemgr";

CFileManager::CFileManager (void)
:	m_bMounted (FALSE)
{
}

CFileManager::~CFileManager (void)
{
	if (m_bMounted)
	{
		f_mount (0, FILE_DRIVE, 0);
		m_bMounted = FALSE;
	}
}

boolean CFileManager::Mount (void)
{
	m_Mutex.Acquire ();

	if (!m_bMounted)
	{
		if (f_mount (&m_FileSystem, FILE_DRIVE, 1) == FR_OK)
		{
			m_bMounted = TRUE;
		}
		else
		{
			CLogger::Get ()->Write (FromFileManager, LogError,
						"Cannot mount drive %s", FILE_DRIVE);
		}
	}

	boolean bResult = m_bMounted;

	m_Mutex.Release ();

	return bResult;
}

unsigned CFileManager::ListFiles (const char *pDirectory, TFileManagerEntry *pEntries,
				 unsigned nMaxEntries)
{
	assert (pDirectory != 0);
	assert (pEntries != 0);

	if (!IsValidPath (pDirectory))
	{
		return 0;
	}

	CString Path;
	MakePath (Path, pDirectory);

	m_Mutex.Acquire ();

	unsigned nCount = 0;

	if (m_bMounted)
	{
		DIR Directory;
		FILINFO FileInfo;
		FRESULT Result = f_findfirst (&Directory, &FileInfo, (const char *) Path, "*");

		while (Result == FR_OK && FileInfo.fname[0] != '\0' && nCount < nMaxEntries)
		{
			// skip hidden and system entries
			if (!(FileInfo.fattrib & (AM_HID | AM_SYS)))
			{
				strncpy (pEntries[nCount].Name, FileInfo.fname, FF_MAX_LFN);
				pEntries[nCount].Name[FF_MAX_LFN] = '\0';
				pEntries[nCount].bDirectory = !!(FileInfo.fattrib & AM_DIR);
				pEntries[nCount].nSize =   pEntries[nCount].bDirectory
							 ? 0 : (unsigned) FileInfo.fsize;

				nCount++;
			}

			Result = f_findnext (&Directory, &FileInfo);
		}

		f_closedir (&Directory);
	}

	m_Mutex.Release ();

	// Sort the entries: directories first, each group by name
	for (unsigned i = 0; i + 1 < nCount; i++)
	{
		unsigned nMin = i;
		for (unsigned j = i + 1; j < nCount; j++)
		{
			if (   (   pEntries[j].bDirectory
				&& !pEntries[nMin].bDirectory)
			    || (   pEntries[j].bDirectory == pEntries[nMin].bDirectory
				&& strcmp (pEntries[j].Name, pEntries[nMin].Name) < 0))
			{
				nMin = j;
			}
		}

		if (nMin != i)
		{
			TFileManagerEntry Temp = pEntries[i];
			pEntries[i] = pEntries[nMin];
			pEntries[nMin] = Temp;
		}
	}

	return nCount;
}

boolean CFileManager::WriteFile (const char *pPath, const u8 *pData, unsigned nLength)
{
	assert (pPath != 0);
	assert (pData != 0 || nLength == 0);

	if (!IsValidPath (pPath) || pPath[0] == '\0')
	{
		return FALSE;
	}

	CString Path;
	MakePath (Path, pPath);

	m_Mutex.Acquire ();

	boolean bOK = FALSE;

	if (m_bMounted)
	{
		FIL File;
		if (f_open (&File, (const char *) Path, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK)
		{
			bOK = TRUE;

			unsigned nRemaining = nLength;
			const u8 *pPtr = pData;
			while (nRemaining > 0)
			{
				UINT nWritten = 0;
				if (   f_write (&File, pPtr, nRemaining, &nWritten) != FR_OK
				    || nWritten == 0)
				{
					bOK = FALSE;
					break;
				}

				pPtr += nWritten;
				nRemaining -= nWritten;
			}

			if (f_close (&File) != FR_OK)
			{
				bOK = FALSE;
			}
		}
	}

	m_Mutex.Release ();

	if (!bOK)
	{
		CLogger::Get ()->Write (FromFileManager, LogError, "Cannot write file %s", pPath);
	}

	return bOK;
}

int CFileManager::ReadFile (const char *pPath, u8 *pBuffer, unsigned nMaxLength)
{
	assert (pPath != 0);
	assert (pBuffer != 0);

	if (!IsValidPath (pPath) || pPath[0] == '\0')
	{
		return -1;
	}

	CString Path;
	MakePath (Path, pPath);

	m_Mutex.Acquire ();

	int nResult = -1;

	if (m_bMounted)
	{
		FIL File;
		if (f_open (&File, (const char *) Path, FA_READ | FA_OPEN_EXISTING) == FR_OK)
		{
			if (f_size (&File) > nMaxLength)
			{
				nResult = -2;		// does not fit into the buffer
			}
			else
			{
				UINT nRead = 0;
				if (f_read (&File, pBuffer, (UINT) f_size (&File), &nRead) == FR_OK)
				{
					nResult = (int) nRead;
				}
			}

			f_close (&File);
		}
	}

	m_Mutex.Release ();

	return nResult;
}

boolean CFileManager::DeleteFile (const char *pPath)
{
	assert (pPath != 0);

	if (!IsValidPath (pPath) || pPath[0] == '\0')
	{
		return FALSE;
	}

	CString Path;
	MakePath (Path, pPath);

	m_Mutex.Acquire ();

	boolean bOK = FALSE;

	if (m_bMounted)
	{
		bOK = f_unlink ((const char *) Path) == FR_OK;
	}

	m_Mutex.Release ();

	if (!bOK)
	{
		CLogger::Get ()->Write (FromFileManager, LogWarning, "Cannot delete file %s", pPath);
	}

	return bOK;
}

boolean CFileManager::IsValidName (const char *pName)
{
	assert (pName != 0);

	if (pName[0] == '\0')
	{
		return FALSE;
	}

	// reject anything that could escape the root directory
	for (const char *p = pName; *p != '\0'; p++)
	{
		if (   *p == '/'
		    || *p == '\\'
		    || *p == ':')
		{
			return FALSE;
		}
	}

	// reject "." and ".."
	if (   strcmp (pName, ".") == 0
	    || strcmp (pName, "..") == 0)
	{
		return FALSE;
	}

	return TRUE;
}

boolean CFileManager::IsValidPath (const char *pPath)
{
	assert (pPath != 0);

	// the empty path is the root directory
	if (pPath[0] == '\0')
	{
		return TRUE;
	}

	// must be relative to the root directory, no drive prefix
	if (pPath[0] == '/')
	{
		return FALSE;
	}

	// check each path component on its own
	const char *pComponent = pPath;
	while (*pComponent != '\0')
	{
		const char *pEnd = pComponent;
		while (*pEnd != '\0' && *pEnd != '/')
		{
			pEnd++;
		}

		size_t nLength = pEnd - pComponent;
		if (nLength == 0)			// "//" or trailing "/"
		{
			return FALSE;
		}

		if (   (nLength == 1 && pComponent[0] == '.')
		    || (nLength == 2 && pComponent[0] == '.' && pComponent[1] == '.'))
		{
			return FALSE;
		}

		for (const char *p = pComponent; p < pEnd; p++)
		{
			if (   *p == '\\'
			    || *p == ':')
			{
				return FALSE;
			}
		}

		pComponent = pEnd;
		if (*pComponent == '/')
		{
			pComponent++;
		}
	}

	return TRUE;
}

void CFileManager::ConcatPath (CString &rPath, const char *pDirectory, const char *pName)
{
	assert (pDirectory != 0);
	assert (pName != 0);

	rPath = pDirectory;

	if (pDirectory[0] != '\0')
	{
		rPath.Append ("/");
	}

	rPath.Append (pName);
}

void CFileManager::MakePath (CString &rPath, const char *pPath)
{
	assert (pPath != 0);

	rPath = FILE_DRIVE "/";
	rPath.Append (pPath);
}
