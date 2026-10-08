#pragma once
#include "Complex.h"

// Jacobi elliptic functions and integrals, used by the elliptic (Cauer) filter design.
// The parameter m is the square of the modulus k.

struct SnCnDn {
    double sn, cn, dn;
};

/// Arithmetic-geometric mean.
double agm(double a0, double b0);
/// Complete elliptic integral of the first kind, K(m).
double ellipK(double m);
/// K(1 - p), accurate when p is tiny.
double ellipKm1(double p);
/// Jacobi elliptic functions sn, cn, dn of real argument u (Cephes ellpj).
SnCnDn ellipj(double u, double m);
/// Solves the degree equation: the modulus m with K'(m)/K(m) = N K'(m1)/K(m1).
double ellipDegree(int n, double m1);
/// Inverse Jacobi sn for complex w, via the descending Landen transformation.
Complex arcJacSn(Complex w, double m);
/// Real inverse of Jacobi sc with complementary parameter: Im(arcsn(i w, m)).
double arcJacSc1(double w, double m);
