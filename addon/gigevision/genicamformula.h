//
// genicamformula.h
//
// Evaluator for GenICam SwissKnife / Converter formulas
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
#ifndef _gigevision_genicamformula_h
#define _gigevision_genicamformula_h

#include <circle/types.h>

class CGenICamFormula
{
public:
	/// \brief Callback resolving a variable name to its value
	/// \param pName Variable name (not 0-terminated!)
	/// \param nNameLength Length of the variable name
	/// \param pValue Value is returned here
	/// \param pParam User parameter
	/// \return Variable known and value available?
	typedef boolean TVariableResolver (const char *pName, unsigned nNameLength,
					   double *pValue, void *pParam);

	/// \brief Evaluate a formula
	/// \param pFormula Formula text (GenICam syntax, e.g. "(VAR1 + 4) & 0xFF")
	/// \param pResolver Callback resolving variables
	/// \param pParam User parameter handed to the callback
	/// \param pResult Result is returned here
	/// \return Operation successful?
	/// \note Supported operators: ( ) + - * / % ** & | ^ ~ << >> = <> < > <= >= && || ! ?:\n
	///	  Functions: SGN NEG ABS SQRT TRUNC FLOOR CEIL ROUND EXP LN LG SIN COS TAN ATAN\n
	///	  Constants: PI E
	static boolean Evaluate (const char *pFormula, TVariableResolver *pResolver, void *pParam,
				 double *pResult);

	// math helpers (no libm required)
	static double Sqrt (double x);
	static double Exp (double x);
	static double Ln (double x);
	static double Sin (double x);
	static double Cos (double x);
	static double Atan (double x);
	static double Pow (double x, double y);
	static double Floor (double x);
	static double Ceil (double x);
	static double Trunc (double x);
	static double Round (double x);
};

#endif
