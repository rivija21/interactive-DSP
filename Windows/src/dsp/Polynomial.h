#pragma once
#include "Complex.h"
#include <vector>

// Polynomials are stored as coefficient arrays in descending powers:
// [c0, c1, ..., cn] means c0*x^n + c1*x^(n-1) + ... + cn.

/// Multiplies out prod(x - r_i).
std::vector<Complex> polyFromRoots(const std::vector<Complex>& roots);

/// Real coefficients of prod(x - r_i) for a root set that is closed under conjugation.
std::vector<double> realPolyFromRoots(const std::vector<Complex>& roots);

Complex horner(const std::vector<Complex>& c, Complex z);
Complex horner(const std::vector<double>& c, Complex z);

/// All roots of a real polynomial.
std::vector<Complex> polyRoots(const std::vector<double>& coeffs);

/// All roots of a complex polynomial, found with the Aberth-Ehrlich method.
std::vector<Complex> polyRootsComplex(std::vector<Complex> c);

/// For roots of a real polynomial: snaps near-real roots onto the real axis and makes
/// each complex root's partner its exact conjugate. Never changes the number of roots.
std::vector<Complex> pairConjugates(const std::vector<Complex>& roots, double tolerance = 1e-8);
