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

unsigned CFileManager::ListFiles (TFileManagerEntry *pEntries, unsigned nMaxEntries)
{
	assert (pEntries != 0);

	m_Mutex.Acquire ();

	unsigned nCount = 0;

	if (m_bMounted)
	{
		DIR Directory;
		FILINFO FileInfo;
		FRESULT Result = f_findfirst (&Directory, &FileInfo, FILE_DRIVE "/", "*");

		while (Result == FR_OK && FileInfo.fname[0] != '\0' && nCount < nMaxEntries)
		{
			// skip directories, hidden and system entries
			if (!(FileInfo.fattrib & (AM_DIR | AM_HID | AM_SYS)))
			{
				strncpy (pEntries[nCount].Name, FileInfo.fname, FF_MAX_LFN);
				pEntries[nCount].Name[FF_MAX_LFN] = '\0';
				pEntries[nCount].nSize = (unsigned) FileInfo.fsize;

				nCount++;
			}

			Result = f_findnext (&Directory, &FileInfo);
		}

		f_closedir (&Directory);
	}

	m_Mutex.Release ();

	return nCount;
}

boolean CFileManager::WriteFile (const char *pName, const u8 *pData, unsigned nLength)
{
	assert (pName != 0);
	assert (pData != 0 || nLength == 0);

	if (!IsValidName (pName))
	{
		return FALSE;
	}

	CString Path;
	MakePath (Path, pName);

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
		CLogger::Get ()->Write (FromFileManager, LogError, "Cannot write file %s", pName);
	}

	return bOK;
}

int CFileManager::ReadFile (const char *pName, u8 *pBuffer, unsigned nMaxLength)
{
	assert (pName != 0);
	assert (pBuffer != 0);

	if (!IsValidName (pName))
	{
		return -1;
	}

	CString Path;
	MakePath (Path, pName);

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

boolean CFileManager::DeleteFile (const char *pName)
{
	assert (pName != 0);

	if (!IsValidName (pName))
	{
		return FALSE;
	}

	CString Path;
	MakePath (Path, pName);

	m_Mutex.Acquire ();

	boolean bOK = FALSE;

	if (m_bMounted)
	{
		bOK = f_unlink ((const char *) Path) == FR_OK;
	}

	m_Mutex.Release ();

	if (!bOK)
	{
		CLogger::Get ()->Write (FromFileManager, LogWarning, "Cannot delete file %s", pName);
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

void CFileManager::MakePath (CString &rPath, const char *pName)
{
	rPath = FILE_DRIVE "/";
	rPath.Append (pName);
}
