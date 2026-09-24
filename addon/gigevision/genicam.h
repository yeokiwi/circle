//
// genicam.h
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
#ifndef _gigevision_genicam_h
#define _gigevision_genicam_h

#include <gigevision/genicamport.h>
#include <gigevision/xmlparser.h>
#include <circle/string.h>
#include <circle/types.h>

enum TGenICamNodeType
{
	GenICamNodeUnknown,
	GenICamNodeCategory,
	GenICamNodeInteger,		// Integer, IntReg, MaskedIntReg, StructEntry,
					// IntSwissKnife, IntConverter
	GenICamNodeFloat,		// Float, FloatReg, SwissKnife, Converter
	GenICamNodeBoolean,
	GenICamNodeEnumeration,
	GenICamNodeCommand,
	GenICamNodeString,		// String, StringReg
	GenICamNodeRegister,
	GenICamNodePort,
	GenICamNodeUnknownType
};

class CGenICamNodeMap	/// Subset of the GenICam GenApi standard for feature access
{
public:
	/// \param pPort Device register space (e.g. a CGVCPClient)
	CGenICamNodeMap (CGenICamPort *pPort);
	~CGenICamNodeMap (void);

	/// \brief Load a device description
	/// \param pXML XML text (ownership is taken, must be allocated with new [nLength+1])
	/// \param nLength Length of the XML text in bytes
	/// \return Operation successful?
	boolean Load (char *pXML, size_t nLength);

	/// \return Number of named nodes
	unsigned GetNodeCount (void) const		{ return m_nNodes; }

	/// \brief Get attribute of the RegisterDescription root (e.g. "ModelName", "VendorName")
	const char *GetDescriptionAttribute (const char *pName) const;

	boolean IsFeature (const char *pName) const;
	TGenICamNodeType GetNodeType (const char *pName) const;

	/// \return Is the feature implemented (and available)? Evaluates pIsImplemented/pIsAvailable.
	boolean IsAvailable (const char *pName);

	boolean GetInteger (const char *pName, s64 *pValue);
	boolean SetInteger (const char *pName, s64 nValue);
	/// \brief Get min/max/increment (from <Min>/<pMin> etc.; defaults if not specified)
	boolean GetIntegerRange (const char *pName, s64 *pMin, s64 *pMax, s64 *pInc);

	boolean GetFloat (const char *pName, double *pValue);
	boolean SetFloat (const char *pName, double fValue);

	boolean GetBoolean (const char *pName, boolean *pValue);
	boolean SetBoolean (const char *pName, boolean bValue);

	/// \brief Get the symbolic name of the current enumeration entry
	boolean GetEnum (const char *pName, CString *pEntry);
	/// \brief Set enumeration by entry name (e.g. SetEnum ("PixelFormat", "Mono8"))
	boolean SetEnum (const char *pName, const char *pEntry);
	/// \brief Get integer value of the enumeration
	boolean GetEnumValue (const char *pName, s64 *pValue);
	/// \brief Iterate the entry names of an enumeration (nIndex = 0, 1, ... until FALSE)
	boolean GetEnumEntry (const char *pName, unsigned nIndex, CString *pEntry, s64 *pValue = 0);

	boolean ExecuteCommand (const char *pName);
	/// \return Command still executing? (FALSE on error)
	boolean IsCommandDone (const char *pName);

	boolean GetString (const char *pName, CString *pValue);
	boolean SetString (const char *pName, const char *pValue);

	/// \brief Get any feature value formatted as string (for display)
	boolean GetValueAsString (const char *pName, CString *pValue);

	/// \brief Iterate the features of a category (nIndex = 0, 1, ... until 0 is returned)
	/// \param pCategory Category name (e.g. "Root")
	/// \return Feature name or 0 if no more features
	const char *GetCategoryFeature (const char *pCategory, unsigned nIndex) const;

private:
	CXMLElement *Lookup (const char *pName) const;
	CXMLElement *Lookup (const char *pName, unsigned nLength) const;
	CXMLElement *LookupRef (const CXMLElement *pNode, const char *pChildName) const;

	void AddNodes (CXMLElement *pElement);
	void Insert (const char *pName, CXMLElement *pElement);
	static unsigned Hash (const char *pName, unsigned nLength);

	static TGenICamNodeType GetType (const CXMLElement *pNode);

	// evaluation (recursive with depth check)
	boolean GetInt (CXMLElement *pNode, s64 *pValue, unsigned nDepth);
	boolean SetInt (CXMLElement *pNode, s64 nValue, unsigned nDepth);
	boolean GetFlt (CXMLElement *pNode, double *pValue, unsigned nDepth);
	boolean SetFlt (CXMLElement *pNode, double fValue, unsigned nDepth);

	// get value from <Name> (constant) or <pName> (reference) child
	boolean GetIntChild (CXMLElement *pNode, const char *pConst, const char *pRef,
			     s64 *pValue, unsigned nDepth);
	boolean GetFltChild (CXMLElement *pNode, const char *pConst, const char *pRef,
			     double *pValue, unsigned nDepth);

	// register access
	const char *GetRegisterProperty (CXMLElement *pNode, const char *pName) const;
	boolean GetRegisterAddress (CXMLElement *pNode, u64 *pAddress, unsigned nDepth);
	boolean GetRegisterLength (CXMLElement *pNode, unsigned *pLength, unsigned nDepth);
	boolean IsBigEndian (CXMLElement *pNode) const;
	boolean CheckPort (CXMLElement *pNode) const;
	boolean ReadRegister (CXMLElement *pNode, u64 *pRaw, unsigned *pLength, unsigned nDepth);
	boolean WriteRegister (CXMLElement *pNode, u64 nRaw, unsigned nLength, unsigned nDepth);
	boolean GetBitField (CXMLElement *pNode, unsigned nLength, unsigned *pShift, unsigned *pWidth);

	// formulas
	struct TFormulaContext;
	boolean EvaluateFormula (CXMLElement *pNode, const char *pFormula,
				 const char *pSpecialName, double fSpecialValue,
				 double *pResult, unsigned nDepth);
	static boolean ResolveVariable (const char *pName, unsigned nNameLength,
					double *pValue, void *pParam);

	boolean IsTrue (CXMLElement *pNode, const char *pRef, boolean bDefault, unsigned nDepth);

	static boolean ParseInt (const char *pText, s64 *pValue);
	static boolean ParseFloat (const char *pText, double *pValue);

private:
	CGenICamPort *m_pPort;
	CXMLDocument m_Document;

	struct THashEntry
	{
		const char	*pName;
		CXMLElement	*pElement;
		THashEntry	*pNext;
	};

	static const unsigned HashSize = 4096;
	THashEntry *m_pHash[HashSize];
	THashEntry *m_pEntries;
	unsigned m_nNodes;
	unsigned m_nMaxNodes;
};

#endif
