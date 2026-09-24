//
// genicamformula.cpp
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
#include <gigevision/genicamformula.h>
#include <circle/util.h>
#include <assert.h>

#define MATH_PI		3.14159265358979323846
#define MATH_E		2.71828182845904523536
#define MATH_LN2	0.69314718055994530942
#define MATH_LN10	2.30258509299404568402

#define MAX_NESTING	64

namespace
{

class CParser	// recursive descent parser, evaluates while parsing
{
public:
	CParser (const char *pFormula, CGenICamFormula::TVariableResolver *pResolver, void *pParam)
	:	m_p (pFormula),
		m_pResolver (pResolver),
		m_pParam (pParam),
		m_bError (FALSE),
		m_nNesting (0)
	{
	}

	boolean Run (double *pResult)
	{
		*pResult = Ternary ();
		SkipSpace ();

		return !m_bError && *m_p == '\0';
	}

private:
	static s64 Int (double x)
	{
		return x < 0 ? -(s64) (-x + 0.5) : (s64) (x + 0.5);	// round to nearest
	}

	void SkipSpace (void)
	{
		while (   *m_p == ' ' || *m_p == '\t'
		       || *m_p == '\r' || *m_p == '\n')
		{
			m_p++;
		}
	}

	boolean Accept (const char *pToken)
	{
		SkipSpace ();

		size_t nLen = strlen (pToken);
		if (strncmp (m_p, pToken, nLen) == 0)
		{
			m_p += nLen;

			return TRUE;
		}

		return FALSE;
	}

	// accept single-char operator, which is not the prefix of a longer one
	boolean AcceptOp (char chOp, const char *pNotFollowedBy)
	{
		SkipSpace ();

		if (   *m_p == chOp
		    && (   m_p[1] == '\0'
			|| strchr (pNotFollowedBy, m_p[1]) == 0))
		{
			m_p++;

			return TRUE;
		}

		return FALSE;
	}

	double Ternary (void)
	{
		if (++m_nNesting > MAX_NESTING)
		{
			m_bError = TRUE;

			return 0;
		}

		double fCond = LogicalOr ();
		if (Accept ("?"))
		{
			double fTrue = Ternary ();
			if (!Accept (":"))
			{
				m_bError = TRUE;
			}
			double fFalse = Ternary ();

			fCond = fCond != 0 ? fTrue : fFalse;
		}

		m_nNesting--;

		return fCond;
	}

	double LogicalOr (void)
	{
		double fValue = LogicalAnd ();
		while (Accept ("||"))
		{
			double fRight = LogicalAnd ();
			fValue = (fValue != 0 || fRight != 0) ? 1 : 0;
		}

		return fValue;
	}

	double LogicalAnd (void)
	{
		double fValue = BitOr ();
		while (Accept ("&&"))
		{
			double fRight = BitOr ();
			fValue = (fValue != 0 && fRight != 0) ? 1 : 0;
		}

		return fValue;
	}

	double BitOr (void)
	{
		double fValue = BitXor ();
		while (AcceptOp ('|', "|"))
		{
			fValue = (double) (Int (fValue) | Int (BitXor ()));
		}

		return fValue;
	}

	double BitXor (void)
	{
		double fValue = BitAnd ();
		while (AcceptOp ('^', ""))
		{
			fValue = (double) (Int (fValue) ^ Int (BitAnd ()));
		}

		return fValue;
	}

	double BitAnd (void)
	{
		double fValue = Equality ();
		while (AcceptOp ('&', "&"))
		{
			fValue = (double) (Int (fValue) & Int (Equality ()));
		}

		return fValue;
	}

	double Equality (void)
	{
		double fValue = Relational ();
		for (;;)
		{
			if (Accept ("<>") || Accept ("!="))
			{
				fValue = fValue != Relational () ? 1 : 0;
			}
			else if (Accept ("==") || AcceptOp ('=', ""))
			{
				fValue = fValue == Relational () ? 1 : 0;
			}
			else
			{
				return fValue;
			}
		}
	}

	double Relational (void)
	{
		double fValue = Shift ();
		for (;;)
		{
			if (Accept ("<="))
			{
				fValue = fValue <= Shift () ? 1 : 0;
			}
			else if (Accept (">="))
			{
				fValue = fValue >= Shift () ? 1 : 0;
			}
			else if (AcceptOp ('<', "<>="))
			{
				fValue = fValue < Shift () ? 1 : 0;
			}
			else if (AcceptOp ('>', ">="))
			{
				fValue = fValue > Shift () ? 1 : 0;
			}
			else
			{
				return fValue;
			}
		}
	}

	double Shift (void)
	{
		double fValue = Additive ();
		for (;;)
		{
			if (Accept ("<<"))
			{
				s64 nShift = Int (Additive ());
				fValue = nShift >= 0 && nShift < 64
					 ? (double) (Int (fValue) << nShift) : 0;
			}
			else if (Accept (">>"))
			{
				s64 nShift = Int (Additive ());
				fValue = nShift >= 0 && nShift < 64
					 ? (double) (Int (fValue) >> nShift) : 0;
			}
			else
			{
				return fValue;
			}
		}
	}

	double Additive (void)
	{
		double fValue = Multiplicative ();
		for (;;)
		{
			if (AcceptOp ('+', ""))
			{
				fValue += Multiplicative ();
			}
			else if (AcceptOp ('-', ""))
			{
				fValue -= Multiplicative ();
			}
			else
			{
				return fValue;
			}
		}
	}

	double Multiplicative (void)
	{
		double fValue = Power ();
		for (;;)
		{
			if (AcceptOp ('*', "*"))
			{
				fValue *= Power ();
			}
			else if (AcceptOp ('/', ""))
			{
				double fDivisor = Power ();
				if (fDivisor == 0)
				{
					m_bError = TRUE;

					return 0;
				}

				fValue /= fDivisor;
			}
			else if (AcceptOp ('%', ""))
			{
				s64 nDivisor = Int (Power ());
				if (nDivisor == 0)
				{
					m_bError = TRUE;

					return 0;
				}

				fValue = (double) (Int (fValue) % nDivisor);
			}
			else
			{
				return fValue;
			}
		}
	}

	double Power (void)
	{
		double fValue = Unary ();
		if (Accept ("**"))
		{
			if (++m_nNesting > MAX_NESTING)
			{
				m_bError = TRUE;

				return 0;
			}

			fValue = CGenICamFormula::Pow (fValue, Power ());	// right associative

			m_nNesting--;
		}

		return fValue;
	}

	double Unary (void)
	{
		if (++m_nNesting > MAX_NESTING)
		{
			m_bError = TRUE;

			return 0;
		}

		double fValue;
		if (AcceptOp ('-', ""))
		{
			fValue = -Unary ();
		}
		else if (AcceptOp ('+', ""))
		{
			fValue = Unary ();
		}
		else if (AcceptOp ('~', ""))
		{
			fValue = (double) ~Int (Unary ());
		}
		else if (AcceptOp ('!', "="))
		{
			fValue = Unary () == 0 ? 1 : 0;
		}
		else
		{
			fValue = Primary ();
		}

		m_nNesting--;

		return fValue;
	}

	double Primary (void)
	{
		SkipSpace ();

		if (Accept ("("))
		{
			double fValue = Ternary ();
			if (!Accept (")"))
			{
				m_bError = TRUE;
			}

			return fValue;
		}

		if (   (*m_p >= '0' && *m_p <= '9')
		    || *m_p == '.')
		{
			return Number ();
		}

		if (   (*m_p >= 'A' && *m_p <= 'Z')
		    || (*m_p >= 'a' && *m_p <= 'z')
		    || *m_p == '_')
		{
			const char *pName = m_p;
			while (   (*m_p >= 'A' && *m_p <= 'Z')
			       || (*m_p >= 'a' && *m_p <= 'z')
			       || (*m_p >= '0' && *m_p <= '9')
			       || *m_p == '_' || *m_p == '.')
			{
				m_p++;
			}
			unsigned nLen = m_p - pName;

			SkipSpace ();
			if (*m_p == '(')
			{
				return Function (pName, nLen);
			}

			// variables take precedence over the built-in constants
			double fValue;
			if (   m_pResolver != 0
			    && (*m_pResolver) (pName, nLen, &fValue, m_pParam))
			{
				return fValue;
			}

			if (nLen == 2 && strncmp (pName, "PI", 2) == 0)
			{
				return MATH_PI;
			}

			if (nLen == 1 && *pName == 'E')
			{
				return MATH_E;
			}
		}

		m_bError = TRUE;

		return 0;
	}

	double Number (void)
	{
		if (   m_p[0] == '0'
		    && (m_p[1] == 'x' || m_p[1] == 'X'))
		{
			char *pEnd;
			unsigned long long ullValue = strtoull (m_p + 2, &pEnd, 16);
			if (pEnd == m_p + 2)
			{
				m_bError = TRUE;
			}
			m_p = pEnd;

			return (double) ullValue;
		}

		double fValue = 0;
		while (*m_p >= '0' && *m_p <= '9')
		{
			fValue = fValue * 10 + (*m_p++ - '0');
		}

		if (*m_p == '.')
		{
			m_p++;

			double fScale = 0.1;
			while (*m_p >= '0' && *m_p <= '9')
			{
				fValue += (*m_p++ - '0') * fScale;
				fScale /= 10;
			}
		}

		if (   (*m_p == 'e' || *m_p == 'E')
		    && (   (m_p[1] >= '0' && m_p[1] <= '9')
			|| ((m_p[1] == '-' || m_p[1] == '+') && m_p[2] >= '0' && m_p[2] <= '9')))
		{
			m_p++;

			boolean bNegative = *m_p == '-';
			if (*m_p == '-' || *m_p == '+')
			{
				m_p++;
			}

			int nExp = 0;
			while (*m_p >= '0' && *m_p <= '9')
			{
				nExp = nExp * 10 + (*m_p++ - '0');
				if (nExp > 400)
				{
					m_bError = TRUE;

					return 0;
				}
			}

			while (nExp--)
			{
				fValue = bNegative ? fValue / 10 : fValue * 10;
			}
		}

		return fValue;
	}

	double Function (const char *pName, unsigned nLen)
	{
		if (!Accept ("("))
		{
			m_bError = TRUE;

			return 0;
		}

		double fArg = Ternary ();
		double fArg2 = 0;
		boolean bArg2 = FALSE;
		if (Accept (","))
		{
			fArg2 = Ternary ();
			bArg2 = TRUE;
		}

		if (!Accept (")"))
		{
			m_bError = TRUE;

			return 0;
		}

#define IS(name)	(nLen == sizeof name - 1 && strncmp (pName, name, nLen) == 0)
		if (IS ("ROUND"))
		{
			if (!bArg2)
			{
				return CGenICamFormula::Round (fArg);
			}

			double fScale = CGenICamFormula::Pow (10, CGenICamFormula::Trunc (fArg2));

			return CGenICamFormula::Round (fArg * fScale) / fScale;
		}

		if (bArg2)
		{
			m_bError = TRUE;

			return 0;
		}

		if (IS ("SGN"))		return fArg > 0 ? 1 : (fArg < 0 ? -1 : 0);
		if (IS ("NEG"))		return -fArg;
		if (IS ("ABS"))		return fArg < 0 ? -fArg : fArg;
		if (IS ("TRUNC"))	return CGenICamFormula::Trunc (fArg);
		if (IS ("FLOOR"))	return CGenICamFormula::Floor (fArg);
		if (IS ("CEIL"))	return CGenICamFormula::Ceil (fArg);
		if (IS ("EXP"))		return CGenICamFormula::Exp (fArg);
		if (IS ("SIN"))		return CGenICamFormula::Sin (fArg);
		if (IS ("COS"))		return CGenICamFormula::Cos (fArg);
		if (IS ("ATAN"))	return CGenICamFormula::Atan (fArg);

		if (IS ("SQRT") || IS ("LN") || IS ("LG") || IS ("TAN"))
		{
			if (IS ("TAN"))
			{
				double fCos = CGenICamFormula::Cos (fArg);
				if (fCos == 0)
				{
					m_bError = TRUE;

					return 0;
				}

				return CGenICamFormula::Sin (fArg) / fCos;
			}

			if (   fArg < 0
			    || (fArg == 0 && !IS ("SQRT")))
			{
				m_bError = TRUE;

				return 0;
			}

			if (IS ("SQRT"))	return CGenICamFormula::Sqrt (fArg);
			if (IS ("LN"))		return CGenICamFormula::Ln (fArg);

			return CGenICamFormula::Ln (fArg) / MATH_LN10;
		}
#undef IS

		m_bError = TRUE;

		return 0;
	}

private:
	const char *m_p;
	CGenICamFormula::TVariableResolver *m_pResolver;
	void *m_pParam;
	boolean m_bError;
	unsigned m_nNesting;
};

}

boolean CGenICamFormula::Evaluate (const char *pFormula, TVariableResolver *pResolver,
				   void *pParam, double *pResult)
{
	assert (pFormula != 0);
	assert (pResult != 0);

	CParser Parser (pFormula, pResolver, pParam);

	return Parser.Run (pResult);
}

double CGenICamFormula::Sqrt (double x)
{
	if (x <= 0)
	{
		return 0;
	}

	// initial guess by halving the exponent
	double fGuess = 1;
	double fTemp = x;
	while (fTemp > 4)	{ fTemp /= 4; fGuess *= 2; }
	while (fTemp < 0.25)	{ fTemp *= 4; fGuess /= 2; }

	for (unsigned i = 0; i < 8; i++)
	{
		fGuess = 0.5 * (fGuess + x / fGuess);
	}

	return fGuess;
}

double CGenICamFormula::Exp (double x)
{
	if (x > 709)
	{
		x = 709;		// avoid overflow
	}
	else if (x < -745)
	{
		return 0;
	}

	// x = k * ln 2 + r, |r| <= ln2 / 2
	s64 k = (s64) (x / MATH_LN2 + (x < 0 ? -0.5 : 0.5));
	double r = x - k * MATH_LN2;

	double fSum = 1;
	double fTerm = 1;
	for (unsigned n = 1; n < 20; n++)
	{
		fTerm *= r / n;
		fSum += fTerm;
	}

	while (k > 0)	{ fSum *= 2; k--; }
	while (k < 0)	{ fSum /= 2; k++; }

	return fSum;
}

double CGenICamFormula::Ln (double x)
{
	if (x <= 0)
	{
		return 0;
	}

	// x = m * 2^k, 1 <= m < 2
	int k = 0;
	while (x >= 2)	{ x /= 2; k++; }
	while (x < 1)	{ x *= 2; k--; }

	// ln m = 2 * atanh ((m-1)/(m+1))
	double y = (x - 1) / (x + 1);
	double y2 = y * y;
	double fSum = 0;
	double fTerm = y;
	for (unsigned n = 1; n < 40; n += 2)
	{
		fSum += fTerm / n;
		fTerm *= y2;
	}

	return 2 * fSum + k * MATH_LN2;
}

double CGenICamFormula::Sin (double x)
{
	// reduce to [-pi, pi]
	double fTurns = Trunc (x / (2 * MATH_PI));
	x -= fTurns * 2 * MATH_PI;
	if (x > MATH_PI)	x -= 2 * MATH_PI;
	if (x < -MATH_PI)	x += 2 * MATH_PI;

	double x2 = x * x;
	double fTerm = x;
	double fSum = x;
	for (unsigned n = 1; n < 12; n++)
	{
		fTerm *= -x2 / ((2 * n) * (2 * n + 1));
		fSum += fTerm;
	}

	return fSum;
}

double CGenICamFormula::Cos (double x)
{
	return Sin (x + MATH_PI / 2);
}

double CGenICamFormula::Atan (double x)
{
	if (x < 0)
	{
		return -Atan (-x);
	}

	if (x > 1)
	{
		return MATH_PI / 2 - Atan (1 / x);
	}

	// argument halving to speed up convergence
	x = x / (1 + Sqrt (1 + x * x));

	double x2 = x * x;
	double fTerm = x;
	double fSum = 0;
	for (unsigned n = 1; n < 40; n += 2)
	{
		fSum += fTerm / n;
		fTerm *= -x2;
	}

	return 2 * fSum;
}

double CGenICamFormula::Pow (double x, double y)
{
	double fIntY = Trunc (y);
	if (   fIntY == y
	    && fIntY > -1024 && fIntY < 1024)
	{
		s64 n = (s64) fIntY;
		boolean bNegative = n < 0;
		if (bNegative)
		{
			n = -n;
		}

		double fResult = 1;
		double fBase = x;
		while (n > 0)
		{
			if (n & 1)
			{
				fResult *= fBase;
			}

			fBase *= fBase;
			n >>= 1;
		}

		return bNegative ? 1 / fResult : fResult;
	}

	if (x <= 0)
	{
		return 0;
	}

	return Exp (y * Ln (x));
}

double CGenICamFormula::Trunc (double x)
{
	if (   x > 9.0e18
	    || x < -9.0e18)
	{
		return x;		// has no fractional part anyway
	}

	return (double) (s64) x;
}

double CGenICamFormula::Floor (double x)
{
	double t = Trunc (x);

	return t > x ? t - 1 : t;
}

double CGenICamFormula::Ceil (double x)
{
	double t = Trunc (x);

	return t < x ? t + 1 : t;
}

double CGenICamFormula::Round (double x)
{
	return x < 0 ? -Floor (-x + 0.5) : Floor (x + 0.5);
}
