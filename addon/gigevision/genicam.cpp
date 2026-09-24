//
// genicam.cpp
//
// GenICam node map: feature access by name, based on the device description XML
//
// Circle - A C++ bare metal environment for Raspberry Pi
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
#include <gigevision/genicam.h>
#include <gigevision/genicamformula.h>
#include <circle/logger.h>
#include <circle/util.h>
#include <assert.h>

#define MAX_DEPTH	32		// max. recursion depth of node references

#define S64_MIN		(-0x7FFFFFFFFFFFFFFFLL - 1)
#define S64_MAX		0x7FFFFFFFFFFFFFFFLL

static const char FromGenICam[] = "genicam";

static boolean IsTag (const CXMLElement *pNode, const char *pTag)
{
	return strcmp (pNode->GetName (), pTag) == 0;
}

static s64 RoundToInt (double f)
{
	return (s64) CGenICamFormula::Round (f);
}

CGenICamNodeMap::CGenICamNodeMap (CGenICamPort *pPort)
:	m_pPort (pPort),
	m_pEntries (0),
	m_nNodes (0),
	m_nMaxNodes (0)
{
	assert (m_pPort != 0);

	for (unsigned i = 0; i < HashSize; i++)
	{
		m_pHash[i] = 0;
	}
}

CGenICamNodeMap::~CGenICamNodeMap (void)
{
	delete [] m_pEntries;
	m_pEntries = 0;

	m_pPort = 0;
}

boolean CGenICamNodeMap::Load (char *pXML, size_t nLength)
{
	assert (pXML != 0);

	delete [] m_pEntries;
	m_pEntries = 0;
	m_nNodes = 0;
	for (unsigned i = 0; i < HashSize; i++)
	{
		m_pHash[i] = 0;
	}

	if (!m_Document.Parse (pXML, nLength))
	{
		CLogger::Get ()->Write (FromGenICam, LogError, "XML syntax error");

		return FALSE;
	}

	CXMLElement *pRoot = m_Document.GetRoot ();
	assert (pRoot != 0);
	if (!IsTag (pRoot, "RegisterDescription"))
	{
		CLogger::Get ()->Write (FromGenICam, LogError, "Not a GenICam description (<%s>)",
					pRoot->GetName ());

		return FALSE;
	}

	m_nMaxNodes = m_Document.GetElementCount ();
	m_pEntries = new THashEntry[m_nMaxNodes];
	assert (m_pEntries != 0);

	AddNodes (pRoot);

	return TRUE;
}

const char *CGenICamNodeMap::GetDescriptionAttribute (const char *pName) const
{
	CXMLElement *pRoot = m_Document.GetRoot ();
	if (pRoot == 0)
	{
		return 0;
	}

	return pRoot->GetAttribute (pName);
}

boolean CGenICamNodeMap::IsFeature (const char *pName) const
{
	return Lookup (pName) != 0;
}

TGenICamNodeType CGenICamNodeMap::GetNodeType (const char *pName) const
{
	CXMLElement *pNode = Lookup (pName);
	if (pNode == 0)
	{
		return GenICamNodeUnknown;
	}

	return GetType (pNode);
}

boolean CGenICamNodeMap::IsAvailable (const char *pName)
{
	CXMLElement *pNode = Lookup (pName);
	if (pNode == 0)
	{
		return FALSE;
	}

	return    IsTrue (pNode, "pIsImplemented", TRUE, 0)
	       && IsTrue (pNode, "pIsAvailable", TRUE, 0);
}

boolean CGenICamNodeMap::GetInteger (const char *pName, s64 *pValue)
{
	CXMLElement *pNode = Lookup (pName);
	if (pNode == 0)
	{
		return FALSE;
	}

	return GetInt (pNode, pValue, 0);
}

boolean CGenICamNodeMap::SetInteger (const char *pName, s64 nValue)
{
	CXMLElement *pNode = Lookup (pName);
	if (pNode == 0)
	{
		return FALSE;
	}

	return SetInt (pNode, nValue, 0);
}

boolean CGenICamNodeMap::GetIntegerRange (const char *pName, s64 *pMin, s64 *pMax, s64 *pInc)
{
	assert (pMin != 0);
	assert (pMax != 0);
	assert (pInc != 0);

	CXMLElement *pNode = Lookup (pName);
	if (pNode == 0)
	{
		return FALSE;
	}

	// follow the pValue chain to find a node with range information
	unsigned nDepth = 0;
	while (   IsTag (pNode, "Integer")
	       && pNode->FindChild ("Min") == 0
	       && pNode->FindChild ("pMin") == 0
	       && pNode->FindChild ("pValue") != 0
	       && nDepth++ < MAX_DEPTH)
	{
		pNode = LookupRef (pNode, "pValue");
		if (pNode == 0)
		{
			return FALSE;
		}
	}

	*pMin = S64_MIN;
	*pMax = S64_MAX;
	*pInc = 1;

	if (   IsTag (pNode, "IntReg")
	    || IsTag (pNode, "MaskedIntReg")
	    || IsTag (pNode, "StructEntry"))
	{
		unsigned nLength;
		if (!GetRegisterLength (pNode, &nLength, 0))
		{
			return FALSE;
		}

		unsigned nShift, nWidth;
		if (!GetBitField (pNode, nLength, &nShift, &nWidth))
		{
			return FALSE;
		}

		const char *pSign = GetRegisterProperty (pNode, "Sign");
		boolean bSigned = pSign != 0 && strcmp (pSign, "Signed") == 0;

		if (nWidth < 64)
		{
			if (bSigned)
			{
				*pMin = -((s64) 1 << (nWidth - 1));
				*pMax = ((s64) 1 << (nWidth - 1)) - 1;
			}
			else
			{
				*pMin = 0;
				*pMax = ((s64) 1 << nWidth) - 1;
			}
		}
		else if (!bSigned)
		{
			*pMin = 0;
		}
	}

	s64 nValue;
	if (GetIntChild (pNode, "Min", "pMin", &nValue, 0))
	{
		*pMin = nValue;
	}

	if (GetIntChild (pNode, "Max", "pMax", &nValue, 0))
	{
		*pMax = nValue;
	}

	if (   GetIntChild (pNode, "Inc", "pInc", &nValue, 0)
	    && nValue > 0)
	{
		*pInc = nValue;
	}

	return TRUE;
}

boolean CGenICamNodeMap::GetFloat (const char *pName, double *pValue)
{
	CXMLElement *pNode = Lookup (pName);
	if (pNode == 0)
	{
		return FALSE;
	}

	return GetFlt (pNode, pValue, 0);
}

boolean CGenICamNodeMap::SetFloat (const char *pName, double fValue)
{
	CXMLElement *pNode = Lookup (pName);
	if (pNode == 0)
	{
		return FALSE;
	}

	return SetFlt (pNode, fValue, 0);
}

boolean CGenICamNodeMap::GetBoolean (const char *pName, boolean *pValue)
{
	assert (pValue != 0);

	CXMLElement *pNode = Lookup (pName);
	if (   pNode == 0
	    || !IsTag (pNode, "Boolean"))
	{
		return FALSE;
	}

	s64 nValue;
	if (!GetIntChild (pNode, "Value", "pValue", &nValue, 0))
	{
		return FALSE;
	}

	s64 nOnValue = 1;
	const char *pOnValue = pNode->GetChildText ("OnValue");
	if (   pOnValue != 0
	    && !ParseInt (pOnValue, &nOnValue))
	{
		return FALSE;
	}

	*pValue = nValue == nOnValue;

	return TRUE;
}

boolean CGenICamNodeMap::SetBoolean (const char *pName, boolean bValue)
{
	CXMLElement *pNode = Lookup (pName);
	if (   pNode == 0
	    || !IsTag (pNode, "Boolean"))
	{
		return FALSE;
	}

	s64 nValue = bValue ? 1 : 0;
	const char *pText = pNode->GetChildText (bValue ? "OnValue" : "OffValue");
	if (   pText != 0
	    && !ParseInt (pText, &nValue))
	{
		return FALSE;
	}

	CXMLElement *pRef = LookupRef (pNode, "pValue");
	if (pRef == 0)
	{
		return FALSE;
	}

	return SetInt (pRef, nValue, 1);
}

boolean CGenICamNodeMap::GetEnum (const char *pName, CString *pEntry)
{
	assert (pEntry != 0);

	s64 nValue;
	if (!GetEnumValue (pName, &nValue))
	{
		return FALSE;
	}

	CXMLElement *pNode = Lookup (pName);
	assert (pNode != 0);

	for (CXMLElement *pEntryNode = pNode->FindChild ("EnumEntry"); pEntryNode != 0;
	     pEntryNode = pEntryNode->FindNextSibling ("EnumEntry"))
	{
		s64 nEntryValue;
		if (   GetIntChild (pEntryNode, "Value", "pValue", &nEntryValue, 1)
		    && nEntryValue == nValue)
		{
			const char *pEntryName = pEntryNode->GetAttribute ("Name");
			*pEntry = pEntryName != 0 ? pEntryName : "";

			return TRUE;
		}
	}

	pEntry->Format ("(%lld)", (long long) nValue);	// value without entry

	return TRUE;
}

boolean CGenICamNodeMap::SetEnum (const char *pName, const char *pEntry)
{
	assert (pEntry != 0);

	CXMLElement *pNode = Lookup (pName);
	if (   pNode == 0
	    || !IsTag (pNode, "Enumeration"))
	{
		return FALSE;
	}

	for (CXMLElement *pEntryNode = pNode->FindChild ("EnumEntry"); pEntryNode != 0;
	     pEntryNode = pEntryNode->FindNextSibling ("EnumEntry"))
	{
		const char *pEntryName = pEntryNode->GetAttribute ("Name");
		if (   pEntryName == 0
		    || strcmp (pEntryName, pEntry) != 0)
		{
			continue;
		}

		s64 nValue;
		if (!GetIntChild (pEntryNode, "Value", "pValue", &nValue, 1))
		{
			return FALSE;
		}

		CXMLElement *pRef = LookupRef (pNode, "pValue");
		if (pRef == 0)
		{
			return FALSE;
		}

		return SetInt (pRef, nValue, 1);
	}

	CLogger::Get ()->Write (FromGenICam, LogWarning, "%s: No such entry: %s", pName, pEntry);

	return FALSE;
}

boolean CGenICamNodeMap::GetEnumValue (const char *pName, s64 *pValue)
{
	CXMLElement *pNode = Lookup (pName);
	if (   pNode == 0
	    || !IsTag (pNode, "Enumeration"))
	{
		return FALSE;
	}

	return GetIntChild (pNode, "Value", "pValue", pValue, 0);
}

boolean CGenICamNodeMap::GetEnumEntry (const char *pName, unsigned nIndex, CString *pEntry,
				       s64 *pValue)
{
	assert (pEntry != 0);

	CXMLElement *pNode = Lookup (pName);
	if (   pNode == 0
	    || !IsTag (pNode, "Enumeration"))
	{
		return FALSE;
	}

	for (CXMLElement *pEntryNode = pNode->FindChild ("EnumEntry"); pEntryNode != 0;
	     pEntryNode = pEntryNode->FindNextSibling ("EnumEntry"))
	{
		if (nIndex-- > 0)
		{
			continue;
		}

		const char *pEntryName = pEntryNode->GetAttribute ("Name");
		*pEntry = pEntryName != 0 ? pEntryName : "";

		if (pValue != 0)
		{
			return GetIntChild (pEntryNode, "Value", "pValue", pValue, 1);
		}

		return TRUE;
	}

	return FALSE;
}

boolean CGenICamNodeMap::ExecuteCommand (const char *pName)
{
	CXMLElement *pNode = Lookup (pName);
	if (   pNode == 0
	    || !IsTag (pNode, "Command"))
	{
		return FALSE;
	}

	s64 nValue;
	if (!GetIntChild (pNode, "CommandValue", "pCommandValue", &nValue, 0))
	{
		return FALSE;
	}

	CXMLElement *pRef = LookupRef (pNode, "pValue");
	if (pRef == 0)
	{
		return FALSE;
	}

	return SetInt (pRef, nValue, 1);
}

boolean CGenICamNodeMap::IsCommandDone (const char *pName)
{
	CXMLElement *pNode = Lookup (pName);
	if (   pNode == 0
	    || !IsTag (pNode, "Command"))
	{
		return FALSE;
	}

	s64 nCommandValue, nValue;
	if (   !GetIntChild (pNode, "CommandValue", "pCommandValue", &nCommandValue, 0)
	    || !GetIntChild (pNode, "Value", "pValue", &nValue, 0))
	{
		return FALSE;
	}

	return nValue != nCommandValue;
}

boolean CGenICamNodeMap::GetString (const char *pName, CString *pValue)
{
	assert (pValue != 0);

	CXMLElement *pNode = Lookup (pName);
	for (unsigned nDepth = 0; pNode != 0 && nDepth < MAX_DEPTH; nDepth++)
	{
		if (IsTag (pNode, "StringReg"))
		{
			if (!CheckPort (pNode))
			{
				return FALSE;
			}

			u64 nAddress;
			unsigned nLength;
			if (   !GetRegisterAddress (pNode, &nAddress, 0)
			    || !GetRegisterLength (pNode, &nLength, 0)
			    || nLength == 0
			    || nLength > 4096)
			{
				return FALSE;
			}

			char *pBuffer = new char[nLength + 1];
			assert (pBuffer != 0);

			assert (m_pPort != 0);
			if (!m_pPort->Read (nAddress, pBuffer, nLength))
			{
				delete [] pBuffer;

				return FALSE;
			}

			pBuffer[nLength] = '\0';
			*pValue = pBuffer;

			delete [] pBuffer;

			return TRUE;
		}

		if (!IsTag (pNode, "String"))
		{
			return FALSE;
		}

		const char *pText = pNode->GetChildText ("Value");
		if (pText != 0)
		{
			*pValue = pText;

			return TRUE;
		}

		pNode = LookupRef (pNode, "pValue");
	}

	return FALSE;
}

boolean CGenICamNodeMap::SetString (const char *pName, const char *pValue)
{
	assert (pValue != 0);

	CXMLElement *pNode = Lookup (pName);
	for (unsigned nDepth = 0; pNode != 0 && nDepth < MAX_DEPTH; nDepth++)
	{
		if (IsTag (pNode, "StringReg"))
		{
			if (!CheckPort (pNode))
			{
				return FALSE;
			}

			u64 nAddress;
			unsigned nLength;
			if (   !GetRegisterAddress (pNode, &nAddress, 0)
			    || !GetRegisterLength (pNode, &nLength, 0)
			    || nLength == 0
			    || nLength > 4096)
			{
				return FALSE;
			}

			size_t nStringLength = strlen (pValue);
			if (nStringLength > nLength)
			{
				return FALSE;
			}

			char *pBuffer = new char[nLength];
			assert (pBuffer != 0);

			memset (pBuffer, 0, nLength);
			memcpy (pBuffer, pValue, nStringLength);

			assert (m_pPort != 0);
			boolean bOK = m_pPort->Write (nAddress, pBuffer, nLength);

			delete [] pBuffer;

			return bOK;
		}

		if (!IsTag (pNode, "String"))
		{
			return FALSE;
		}

		pNode = LookupRef (pNode, "pValue");
	}

	return FALSE;
}

boolean CGenICamNodeMap::GetValueAsString (const char *pName, CString *pValue)
{
	assert (pValue != 0);

	switch (GetNodeType (pName))
	{
	case GenICamNodeInteger: {
		s64 nValue;
		if (!GetInteger (pName, &nValue))
		{
			return FALSE;
		}

		pValue->Format ("%lld", (long long) nValue);
		} return TRUE;

	case GenICamNodeFloat: {
		double fValue;
		if (!GetFloat (pName, &fValue))
		{
			return FALSE;
		}

		pValue->Format ("%.3f", fValue);
		} return TRUE;

	case GenICamNodeBoolean: {
		boolean bValue;
		if (!GetBoolean (pName, &bValue))
		{
			return FALSE;
		}

		*pValue = bValue ? "true" : "false";
		} return TRUE;

	case GenICamNodeEnumeration:
		return GetEnum (pName, pValue);

	case GenICamNodeString:
		return GetString (pName, pValue);

	case GenICamNodeCommand:
		*pValue = "(command)";
		return TRUE;

	case GenICamNodeCategory:
		*pValue = "(category)";
		return TRUE;

	default:
		return FALSE;
	}
}

const char *CGenICamNodeMap::GetCategoryFeature (const char *pCategory, unsigned nIndex) const
{
	CXMLElement *pNode = Lookup (pCategory);
	if (   pNode == 0
	    || !IsTag (pNode, "Category"))
	{
		return 0;
	}

	for (CXMLElement *pFeature = pNode->FindChild ("pFeature"); pFeature != 0;
	     pFeature = pFeature->FindNextSibling ("pFeature"))
	{
		if (nIndex-- == 0)
		{
			return pFeature->GetText ();
		}
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Node lookup

CXMLElement *CGenICamNodeMap::Lookup (const char *pName) const
{
	assert (pName != 0);

	return Lookup (pName, strlen (pName));
}

CXMLElement *CGenICamNodeMap::Lookup (const char *pName, unsigned nLength) const
{
	for (THashEntry *pEntry = m_pHash[Hash (pName, nLength)]; pEntry != 0;
	     pEntry = pEntry->pNext)
	{
		if (   strncmp (pEntry->pName, pName, nLength) == 0
		    && pEntry->pName[nLength] == '\0')
		{
			return pEntry->pElement;
		}
	}

	return 0;
}

CXMLElement *CGenICamNodeMap::LookupRef (const CXMLElement *pNode, const char *pChildName) const
{
	assert (pNode != 0);

	const char *pRefName = pNode->GetChildText (pChildName);
	if (   pRefName == 0
	    || *pRefName == '\0')
	{
		return 0;
	}

	CXMLElement *pRef = Lookup (pRefName);
	if (pRef == 0)
	{
		CLogger::Get ()->Write (FromGenICam, LogDebug, "Unresolved reference: %s", pRefName);
	}

	return pRef;
}

void CGenICamNodeMap::AddNodes (CXMLElement *pElement)
{
	assert (pElement != 0);

	for (CXMLElement *pChild = pElement->GetFirstChild (); pChild != 0;
	     pChild = pChild->GetNextSibling ())
	{
		if (IsTag (pChild, "Group"))
		{
			AddNodes (pChild);	// groups only structure the file

			continue;
		}

		const char *pName = pChild->GetAttribute ("Name");
		if (pName != 0)
		{
			Insert (pName, pChild);
		}

		// a StructReg usually has no name itself, but its entries have
		if (IsTag (pChild, "StructReg"))
		{
			for (CXMLElement *pEntry = pChild->FindChild ("StructEntry"); pEntry != 0;
			     pEntry = pEntry->FindNextSibling ("StructEntry"))
			{
				const char *pEntryName = pEntry->GetAttribute ("Name");
				if (pEntryName != 0)
				{
					Insert (pEntryName, pEntry);
				}
			}
		}
	}
}

void CGenICamNodeMap::Insert (const char *pName, CXMLElement *pElement)
{
	unsigned nLength = strlen (pName);
	if (Lookup (pName, nLength) != 0)
	{
		return;				// first definition wins
	}

	if (m_nNodes >= m_nMaxNodes)
	{
		return;
	}

	THashEntry *pEntry = &m_pEntries[m_nNodes++];
	pEntry->pName = pName;
	pEntry->pElement = pElement;

	unsigned nHash = Hash (pName, nLength);
	pEntry->pNext = m_pHash[nHash];
	m_pHash[nHash] = pEntry;
}

unsigned CGenICamNodeMap::Hash (const char *pName, unsigned nLength)
{
	u32 nHash = 2166136261U;		// FNV-1a
	while (nLength--)
	{
		nHash ^= (u8) *pName++;
		nHash *= 16777619U;
	}

	return nHash % HashSize;
}

TGenICamNodeType CGenICamNodeMap::GetType (const CXMLElement *pNode)
{
	static const struct
	{
		const char *pTag;
		TGenICamNodeType Type;
	}
	Types[] =
	{
		{"Category",		GenICamNodeCategory},
		{"Integer",		GenICamNodeInteger},
		{"IntReg",		GenICamNodeInteger},
		{"MaskedIntReg",	GenICamNodeInteger},
		{"StructEntry",		GenICamNodeInteger},
		{"IntSwissKnife",	GenICamNodeInteger},
		{"IntConverter",	GenICamNodeInteger},
		{"Float",		GenICamNodeFloat},
		{"FloatReg",		GenICamNodeFloat},
		{"SwissKnife",		GenICamNodeFloat},
		{"Converter",		GenICamNodeFloat},
		{"Boolean",		GenICamNodeBoolean},
		{"Enumeration",		GenICamNodeEnumeration},
		{"Command",		GenICamNodeCommand},
		{"String",		GenICamNodeString},
		{"StringReg",		GenICamNodeString},
		{"Register",		GenICamNodeRegister},
		{"Port",		GenICamNodePort}
	};

	for (unsigned i = 0; i < sizeof Types / sizeof Types[0]; i++)
	{
		if (IsTag (pNode, Types[i].pTag))
		{
			return Types[i].Type;
		}
	}

	return GenICamNodeUnknownType;
}

////////////////////////////////////////////////////////////////////////////////
// Evaluation

boolean CGenICamNodeMap::GetInt (CXMLElement *pNode, s64 *pValue, unsigned nDepth)
{
	assert (pNode != 0);
	assert (pValue != 0);

	if (nDepth > MAX_DEPTH)
	{
		return FALSE;
	}

	if (   IsTag (pNode, "Integer")
	    || IsTag (pNode, "Boolean")
	    || IsTag (pNode, "Enumeration")
	    || IsTag (pNode, "Command"))
	{
		return GetIntChild (pNode, "Value", "pValue", pValue, nDepth + 1);
	}

	if (   IsTag (pNode, "IntReg")
	    || IsTag (pNode, "MaskedIntReg")
	    || IsTag (pNode, "StructEntry"))
	{
		u64 nRaw;
		unsigned nLength;
		if (!ReadRegister (pNode, &nRaw, &nLength, nDepth + 1))
		{
			return FALSE;
		}

		unsigned nShift, nWidth;
		if (!GetBitField (pNode, nLength, &nShift, &nWidth))
		{
			return FALSE;
		}

		u64 nMask = nWidth < 64 ? ((u64) 1 << nWidth) - 1 : ~(u64) 0;
		u64 nField = (nRaw >> nShift) & nMask;

		const char *pSign = GetRegisterProperty (pNode, "Sign");
		if (   pSign != 0
		    && strcmp (pSign, "Signed") == 0
		    && nWidth < 64
		    && (nField & ((u64) 1 << (nWidth - 1))))
		{
			nField |= ~nMask;	// sign extension
		}

		*pValue = (s64) nField;

		return TRUE;
	}

	if (IsTag (pNode, "IntSwissKnife"))
	{
		double fResult;
		if (!EvaluateFormula (pNode, pNode->GetChildText ("Formula"), 0, 0,
				      &fResult, nDepth + 1))
		{
			return FALSE;
		}

		*pValue = RoundToInt (fResult);

		return TRUE;
	}

	if (GetType (pNode) == GenICamNodeFloat)
	{
		double fValue;
		if (!GetFlt (pNode, &fValue, nDepth + 1))
		{
			return FALSE;
		}

		*pValue = RoundToInt (fValue);

		return TRUE;
	}

	if (IsTag (pNode, "IntConverter"))
	{
		double fValue;
		if (!GetFlt (pNode, &fValue, nDepth + 1))
		{
			return FALSE;
		}

		*pValue = RoundToInt (fValue);

		return TRUE;
	}

	return FALSE;
}

boolean CGenICamNodeMap::SetInt (CXMLElement *pNode, s64 nValue, unsigned nDepth)
{
	assert (pNode != 0);

	if (nDepth > MAX_DEPTH)
	{
		return FALSE;
	}

	if (   IsTag (pNode, "Integer")
	    || IsTag (pNode, "Boolean")
	    || IsTag (pNode, "Enumeration")
	    || IsTag (pNode, "Command"))
	{
		CXMLElement *pRef = LookupRef (pNode, "pValue");
		if (pRef == 0)
		{
			return FALSE;		// constant value
		}

		return SetInt (pRef, nValue, nDepth + 1);
	}

	if (   IsTag (pNode, "IntReg")
	    || IsTag (pNode, "MaskedIntReg")
	    || IsTag (pNode, "StructEntry"))
	{
		unsigned nLength;
		if (   !GetRegisterLength (pNode, &nLength, nDepth + 1)
		    || nLength == 0
		    || nLength > 8)
		{
			return FALSE;
		}

		unsigned nShift, nWidth;
		if (!GetBitField (pNode, nLength, &nShift, &nWidth))
		{
			return FALSE;
		}

		u64 nRaw = (u64) nValue;
		if (nWidth < nLength * 8)
		{
			// read-modify-write of a bit field
			u64 nOld;
			unsigned nOldLength;
			if (!ReadRegister (pNode, &nOld, &nOldLength, nDepth + 1))
			{
				return FALSE;
			}

			u64 nMask = (((u64) 1 << nWidth) - 1) << nShift;
			nRaw = (nOld & ~nMask) | (((u64) nValue << nShift) & nMask);
		}

		return WriteRegister (pNode, nRaw, nLength, nDepth + 1);
	}

	if (   IsTag (pNode, "IntConverter")
	    || GetType (pNode) == GenICamNodeFloat)
	{
		return SetFlt (pNode, (double) nValue, nDepth + 1);
	}

	return FALSE;
}

boolean CGenICamNodeMap::GetFlt (CXMLElement *pNode, double *pValue, unsigned nDepth)
{
	assert (pNode != 0);
	assert (pValue != 0);

	if (nDepth > MAX_DEPTH)
	{
		return FALSE;
	}

	if (IsTag (pNode, "Float"))
	{
		return GetFltChild (pNode, "Value", "pValue", pValue, nDepth + 1);
	}

	if (IsTag (pNode, "FloatReg"))
	{
		u64 nRaw;
		unsigned nLength;
		if (!ReadRegister (pNode, &nRaw, &nLength, nDepth + 1))
		{
			return FALSE;
		}

		if (nLength == 4)
		{
			u32 nRaw32 = (u32) nRaw;
			float fValue;
			memcpy (&fValue, &nRaw32, sizeof fValue);
			*pValue = fValue;
		}
		else if (nLength == 8)
		{
			memcpy (pValue, &nRaw, sizeof *pValue);
		}
		else
		{
			return FALSE;
		}

		return TRUE;
	}

	if (IsTag (pNode, "SwissKnife"))
	{
		return EvaluateFormula (pNode, pNode->GetChildText ("Formula"), 0, 0,
					pValue, nDepth + 1);
	}

	if (   IsTag (pNode, "Converter")
	    || IsTag (pNode, "IntConverter"))
	{
		// FormulaFrom converts the value of pValue (TO) into the converter value
		CXMLElement *pRef = LookupRef (pNode, "pValue");
		if (pRef == 0)
		{
			return FALSE;
		}

		double fTo;
		if (!GetFlt (pRef, &fTo, nDepth + 1))
		{
			return FALSE;
		}

		return EvaluateFormula (pNode, pNode->GetChildText ("FormulaFrom"), "TO", fTo,
					pValue, nDepth + 1);
	}

	if (GetType (pNode) == GenICamNodeInteger
	    || IsTag (pNode, "Boolean")
	    || IsTag (pNode, "Enumeration"))
	{
		s64 nValue;
		if (!GetInt (pNode, &nValue, nDepth + 1))
		{
			return FALSE;
		}

		*pValue = (double) nValue;

		return TRUE;
	}

	return FALSE;
}

boolean CGenICamNodeMap::SetFlt (CXMLElement *pNode, double fValue, unsigned nDepth)
{
	assert (pNode != 0);

	if (nDepth > MAX_DEPTH)
	{
		return FALSE;
	}

	if (IsTag (pNode, "Float"))
	{
		CXMLElement *pRef = LookupRef (pNode, "pValue");
		if (pRef == 0)
		{
			return FALSE;
		}

		return SetFlt (pRef, fValue, nDepth + 1);
	}

	if (IsTag (pNode, "FloatReg"))
	{
		unsigned nLength;
		if (!GetRegisterLength (pNode, &nLength, nDepth + 1))
		{
			return FALSE;
		}

		u64 nRaw;
		if (nLength == 4)
		{
			float f = (float) fValue;
			u32 nRaw32;
			memcpy (&nRaw32, &f, sizeof nRaw32);
			nRaw = nRaw32;
		}
		else if (nLength == 8)
		{
			memcpy (&nRaw, &fValue, sizeof nRaw);
		}
		else
		{
			return FALSE;
		}

		return WriteRegister (pNode, nRaw, nLength, nDepth + 1);
	}

	if (   IsTag (pNode, "Converter")
	    || IsTag (pNode, "IntConverter"))
	{
		// FormulaTo converts the converter value (FROM) into the value of pValue
		CXMLElement *pRef = LookupRef (pNode, "pValue");
		if (pRef == 0)
		{
			return FALSE;
		}

		double fTo;
		if (!EvaluateFormula (pNode, pNode->GetChildText ("FormulaTo"), "FROM", fValue,
				      &fTo, nDepth + 1))
		{
			return FALSE;
		}

		if (GetType (pRef) == GenICamNodeFloat)
		{
			return SetFlt (pRef, fTo, nDepth + 1);
		}

		return SetInt (pRef, RoundToInt (fTo), nDepth + 1);
	}

	if (   GetType (pNode) == GenICamNodeInteger
	    || IsTag (pNode, "Boolean")
	    || IsTag (pNode, "Enumeration"))
	{
		return SetInt (pNode, RoundToInt (fValue), nDepth + 1);
	}

	return FALSE;	// SwissKnife is read-only
}

boolean CGenICamNodeMap::GetIntChild (CXMLElement *pNode, const char *pConst, const char *pRef,
				      s64 *pValue, unsigned nDepth)
{
	assert (pNode != 0);

	if (pNode->FindChild (pRef) != 0)
	{
		CXMLElement *pRefNode = LookupRef (pNode, pRef);
		if (pRefNode == 0)
		{
			return FALSE;
		}

		return GetInt (pRefNode, pValue, nDepth + 1);
	}

	const char *pText = pNode->GetChildText (pConst);
	if (pText == 0)
	{
		return FALSE;
	}

	return ParseInt (pText, pValue);
}

boolean CGenICamNodeMap::GetFltChild (CXMLElement *pNode, const char *pConst, const char *pRef,
				      double *pValue, unsigned nDepth)
{
	assert (pNode != 0);

	if (pNode->FindChild (pRef) != 0)
	{
		CXMLElement *pRefNode = LookupRef (pNode, pRef);
		if (pRefNode == 0)
		{
			return FALSE;
		}

		return GetFlt (pRefNode, pValue, nDepth + 1);
	}

	const char *pText = pNode->GetChildText (pConst);
	if (pText == 0)
	{
		return FALSE;
	}

	return ParseFloat (pText, pValue);
}

////////////////////////////////////////////////////////////////////////////////
// Register access

// StructEntry inherits the register properties of its StructReg
const char *CGenICamNodeMap::GetRegisterProperty (CXMLElement *pNode, const char *pName) const
{
	assert (pNode != 0);

	const char *pText = pNode->GetChildText (pName);
	if (   pText == 0
	    && IsTag (pNode, "StructEntry")
	    && pNode->GetParent () != 0)
	{
		pText = pNode->GetParent ()->GetChildText (pName);
	}

	return pText;
}

boolean CGenICamNodeMap::GetRegisterAddress (CXMLElement *pNode, u64 *pAddress, unsigned nDepth)
{
	assert (pNode != 0);
	assert (pAddress != 0);

	if (IsTag (pNode, "StructEntry"))
	{
		pNode = pNode->GetParent ();
		assert (pNode != 0);
	}

	u64 nAddress = 0;
	boolean bHasAddress = FALSE;

	for (CXMLElement *pChild = pNode->GetFirstChild (); pChild != 0;
	     pChild = pChild->GetNextSibling ())
	{
		if (IsTag (pChild, "Address"))
		{
			s64 nValue;
			if (!ParseInt (pChild->GetText (), &nValue))
			{
				return FALSE;
			}

			nAddress += nValue;
			bHasAddress = TRUE;
		}
		else if (IsTag (pChild, "pAddress"))
		{
			CXMLElement *pRef = Lookup (pChild->GetText ());
			s64 nValue;
			if (   pRef == 0
			    || !GetInt (pRef, &nValue, nDepth + 1))
			{
				return FALSE;
			}

			nAddress += nValue;
			bHasAddress = TRUE;
		}
		else if (   IsTag (pChild, "IntSwissKnife")
			 || IsTag (pChild, "pIndex"))
		{
			if (IsTag (pChild, "IntSwissKnife"))
			{
				// embedded address calculation
				s64 nValue;
				if (!GetInt (pChild, &nValue, nDepth + 1))
				{
					return FALSE;
				}

				nAddress += nValue;
				bHasAddress = TRUE;

				continue;
			}

			// indexed register: Address + Index * Offset
			CXMLElement *pIndexNode = Lookup (pChild->GetText ());
			s64 nIndex;
			if (   pIndexNode == 0
			    || !GetInt (pIndexNode, &nIndex, nDepth + 1))
			{
				return FALSE;
			}

			s64 nOffset = 0;
			const char *pOffset = pChild->GetAttribute ("Offset");
			const char *ppOffset = pChild->GetAttribute ("pOffset");
			if (pOffset != 0)
			{
				if (!ParseInt (pOffset, &nOffset))
				{
					return FALSE;
				}
			}
			else if (ppOffset != 0)
			{
				CXMLElement *pOffsetNode = Lookup (ppOffset);
				if (   pOffsetNode == 0
				    || !GetInt (pOffsetNode, &nOffset, nDepth + 1))
				{
					return FALSE;
				}
			}
			else
			{
				unsigned nLength;
				if (!GetRegisterLength (pNode, &nLength, nDepth + 1))
				{
					return FALSE;
				}

				nOffset = nLength;
			}

			nAddress += nIndex * nOffset;
		}
	}

	if (!bHasAddress)
	{
		return FALSE;
	}

	*pAddress = nAddress;

	return TRUE;
}

boolean CGenICamNodeMap::GetRegisterLength (CXMLElement *pNode, unsigned *pLength, unsigned nDepth)
{
	assert (pNode != 0);
	assert (pLength != 0);

	s64 nLength;

	const char *pText = GetRegisterProperty (pNode, "Length");
	if (pText != 0)
	{
		if (!ParseInt (pText, &nLength))
		{
			return FALSE;
		}
	}
	else
	{
		CXMLElement *pBase = pNode;
		if (   IsTag (pNode, "StructEntry")
		    && pNode->FindChild ("pLength") == 0)
		{
			pBase = pNode->GetParent ();
		}

		CXMLElement *pRef = LookupRef (pBase, "pLength");
		if (   pRef == 0
		    || !GetInt (pRef, &nLength, nDepth + 1))
		{
			return FALSE;
		}
	}

	if (   nLength <= 0
	    || nLength > 0x1000000)
	{
		return FALSE;
	}

	*pLength = (unsigned) nLength;

	return TRUE;
}

boolean CGenICamNodeMap::IsBigEndian (CXMLElement *pNode) const
{
	const char *pEndianess = GetRegisterProperty (pNode, "Endianess");

	return    pEndianess != 0
	       && strcmp (pEndianess, "BigEndian") == 0;
}

boolean CGenICamNodeMap::CheckPort (CXMLElement *pNode) const
{
	assert (pNode != 0);

	CXMLElement *pBase = pNode;
	if (   IsTag (pNode, "StructEntry")
	    && pNode->FindChild ("pPort") == 0)
	{
		pBase = pNode->GetParent ();
	}

	const char *pPortName = pBase->GetChildText ("pPort");
	if (pPortName == 0)
	{
		return TRUE;
	}

	CXMLElement *pPort = Lookup (pPortName);
	if (pPort == 0)
	{
		return FALSE;
	}

	// only the device port is supported (no chunk data ports)
	if (pPort->FindChild ("ChunkID") != 0)
	{
		return FALSE;
	}

	return TRUE;
}

boolean CGenICamNodeMap::ReadRegister (CXMLElement *pNode, u64 *pRaw, unsigned *pLength,
				       unsigned nDepth)
{
	assert (pRaw != 0);
	assert (pLength != 0);

	if (!CheckPort (pNode))
	{
		return FALSE;
	}

	u64 nAddress;
	unsigned nLength;
	if (   !GetRegisterAddress (pNode, &nAddress, nDepth)
	    || !GetRegisterLength (pNode, &nLength, nDepth)
	    || nLength > 8)
	{
		return FALSE;
	}

	u8 Buffer[8];
	assert (m_pPort != 0);
	if (!m_pPort->Read (nAddress, Buffer, nLength))
	{
		return FALSE;
	}

	u64 nRaw = 0;
	if (IsBigEndian (pNode))
	{
		for (unsigned i = 0; i < nLength; i++)
		{
			nRaw = (nRaw << 8) | Buffer[i];
		}
	}
	else
	{
		for (unsigned i = 0; i < nLength; i++)
		{
			nRaw |= (u64) Buffer[i] << (8 * i);
		}
	}

	*pRaw = nRaw;
	*pLength = nLength;

	return TRUE;
}

boolean CGenICamNodeMap::WriteRegister (CXMLElement *pNode, u64 nRaw, unsigned nLength,
					unsigned nDepth)
{
	if (!CheckPort (pNode))
	{
		return FALSE;
	}

	u64 nAddress;
	if (   !GetRegisterAddress (pNode, &nAddress, nDepth)
	    || nLength == 0
	    || nLength > 8)
	{
		return FALSE;
	}

	u8 Buffer[8];
	if (IsBigEndian (pNode))
	{
		for (unsigned i = 0; i < nLength; i++)
		{
			Buffer[nLength - 1 - i] = (u8) (nRaw >> (8 * i));
		}
	}
	else
	{
		for (unsigned i = 0; i < nLength; i++)
		{
			Buffer[i] = (u8) (nRaw >> (8 * i));
		}
	}

	assert (m_pPort != 0);

	return m_pPort->Write (nAddress, Buffer, nLength);
}

// Bit numbering depends on the register byte order: with LittleEndian bit 0 is the
// least significant bit, with BigEndian bit 0 is the most significant bit.
boolean CGenICamNodeMap::GetBitField (CXMLElement *pNode, unsigned nLength,
				      unsigned *pShift, unsigned *pWidth)
{
	assert (pNode != 0);
	assert (pShift != 0);
	assert (pWidth != 0);

	unsigned nBits = nLength * 8;
	if (   nBits == 0
	    || nBits > 64)
	{
		return FALSE;
	}

	boolean bBigEndian = IsBigEndian (pNode);

	s64 nLSB, nMSB;
	const char *pBit = pNode->GetChildText ("Bit");
	if (pBit != 0)
	{
		if (!ParseInt (pBit, &nLSB))
		{
			return FALSE;
		}

		nMSB = nLSB;
	}
	else
	{
		const char *pLSB = pNode->GetChildText ("LSB");
		const char *pMSB = pNode->GetChildText ("MSB");
		if (   pLSB == 0
		    || pMSB == 0)
		{
			*pShift = 0;
			*pWidth = nBits;

			return TRUE;
		}

		if (   !ParseInt (pLSB, &nLSB)
		    || !ParseInt (pMSB, &nMSB))
		{
			return FALSE;
		}
	}

	if (   nLSB < 0 || nLSB >= (s64) nBits
	    || nMSB < 0 || nMSB >= (s64) nBits)
	{
		return FALSE;
	}

	if (bBigEndian)
	{
		if (nLSB < nMSB)
		{
			return FALSE;
		}

		*pShift = nBits - 1 - nLSB;
		*pWidth = nLSB - nMSB + 1;
	}
	else
	{
		if (nMSB < nLSB)
		{
			return FALSE;
		}

		*pShift = nLSB;
		*pWidth = nMSB - nLSB + 1;
	}

	return TRUE;
}

////////////////////////////////////////////////////////////////////////////////
// Formulas

struct CGenICamNodeMap::TFormulaContext
{
	CGenICamNodeMap	*pThis;
	CXMLElement	*pNode;
	const char	*pSpecialName;	// "TO" or "FROM" in converters
	double		 fSpecialValue;
	unsigned	 nDepth;
};

boolean CGenICamNodeMap::EvaluateFormula (CXMLElement *pNode, const char *pFormula,
					  const char *pSpecialName, double fSpecialValue,
					  double *pResult, unsigned nDepth)
{
	if (   pFormula == 0
	    || nDepth > MAX_DEPTH)
	{
		return FALSE;
	}

	TFormulaContext Context = {this, pNode, pSpecialName, fSpecialValue, nDepth};

	if (!CGenICamFormula::Evaluate (pFormula, ResolveVariable, &Context, pResult))
	{
		const char *pName = pNode->GetAttribute ("Name");
		CLogger::Get ()->Write (FromGenICam, LogDebug, "%s: Cannot evaluate \"%s\"",
					pName != 0 ? pName : pNode->GetName (), pFormula);

		return FALSE;
	}

	return TRUE;
}

boolean CGenICamNodeMap::ResolveVariable (const char *pName, unsigned nNameLength,
					  double *pValue, void *pParam)
{
	TFormulaContext *pContext = (TFormulaContext *) pParam;
	assert (pContext != 0);

	if (   pContext->pSpecialName != 0
	    && strlen (pContext->pSpecialName) == nNameLength
	    && strncmp (pContext->pSpecialName, pName, nNameLength) == 0)
	{
		*pValue = pContext->fSpecialValue;

		return TRUE;
	}

	CGenICamNodeMap *pThis = pContext->pThis;

	for (CXMLElement *pChild = pContext->pNode->GetFirstChild (); pChild != 0;
	     pChild = pChild->GetNextSibling ())
	{
		const char *pVarName = pChild->GetAttribute ("Name");
		if (   pVarName == 0
		    || strlen (pVarName) != nNameLength
		    || strncmp (pVarName, pName, nNameLength) != 0)
		{
			continue;
		}

		if (IsTag (pChild, "pVariable"))
		{
			CXMLElement *pRef = pThis->Lookup (pChild->GetText ());
			if (pRef == 0)
			{
				return FALSE;
			}

			return pThis->GetFlt (pRef, pValue, pContext->nDepth + 1);
		}

		if (IsTag (pChild, "Constant"))
		{
			return ParseFloat (pChild->GetText (), pValue);
		}

		if (IsTag (pChild, "Expression"))
		{
			return pThis->EvaluateFormula (pContext->pNode, pChild->GetText (),
						       pContext->pSpecialName,
						       pContext->fSpecialValue,
						       pValue, pContext->nDepth + 1);
		}
	}

	return FALSE;
}

boolean CGenICamNodeMap::IsTrue (CXMLElement *pNode, const char *pRef, boolean bDefault,
				 unsigned nDepth)
{
	if (pNode->FindChild (pRef) == 0)
	{
		return bDefault;
	}

	CXMLElement *pRefNode = LookupRef (pNode, pRef);
	s64 nValue;
	if (   pRefNode == 0
	    || !GetInt (pRefNode, &nValue, nDepth + 1))
	{
		return FALSE;
	}

	return nValue != 0;
}

boolean CGenICamNodeMap::ParseInt (const char *pText, s64 *pValue)
{
	assert (pText != 0);
	assert (pValue != 0);

	while (*pText == ' ' || *pText == '\t')
	{
		pText++;
	}

	boolean bNegative = FALSE;
	if (*pText == '-')
	{
		bNegative = TRUE;
		pText++;
	}
	else if (*pText == '+')
	{
		pText++;
	}

	int nBase = 10;
	if (   pText[0] == '0'
	    && (pText[1] == 'x' || pText[1] == 'X'))
	{
		nBase = 16;
		pText += 2;
	}

	if (*pText == '\0')
	{
		return FALSE;
	}

	char *pEnd;
	unsigned long long ullValue = strtoull (pText, &pEnd, nBase);
	while (*pEnd == ' ' || *pEnd == '\t')
	{
		pEnd++;
	}

	if (*pEnd != '\0')
	{
		return FALSE;
	}

	*pValue = bNegative ? -(s64) ullValue : (s64) ullValue;

	return TRUE;
}

boolean CGenICamNodeMap::ParseFloat (const char *pText, double *pValue)
{
	assert (pText != 0);

	return CGenICamFormula::Evaluate (pText, 0, 0, pValue);
}
