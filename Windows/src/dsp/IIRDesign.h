#pragma once
#include "Complex.h"
#include <string>
#include <vector>

// Classic IIR design: analog prototype -> frequency transformation -> bilinear transform.
// The steps mirror scipy.signal.iirfilter so results can be checked against SciPy.

enum class BandType { lowpass, highpass, bandpass, bandstop };
inline constexpr BandType kAllBandTypes[] = {BandType::lowpass, BandType::highpass, BandType::bandpass, BandType::bandstop};

const char* bandTitle(BandType b);
const char* bandRawValue(BandType b);
inline bool bandIsBand(BandType b) { return b == BandType::bandpass || b == BandType::bandstop; }

enum class IIRFamily { butterworth, chebyshev1, chebyshev2, elliptic, bessel };
inline constexpr IIRFamily kAllFamilies[] = {IIRFamily::butterworth, IIRFamily::chebyshev1, IIRFamily::chebyshev2,
                                             IIRFamily::elliptic, IIRFamily::bessel};

const char* familyTitle(IIRFamily f);
const char* familyRawValue(IIRFamily f);
inline bool familyUsesPassbandRipple(IIRFamily f) { return f == IIRFamily::chebyshev1 || f == IIRFamily::elliptic; }
inline bool familyUsesStopbandAttenuation(IIRFamily f) { return f == IIRFamily::chebyshev2 || f == IIRFamily::elliptic; }
/// What the cutoff frequency means for this family.
const char* familyCutoffMeaning(IIRFamily f);

/// Zeros, poles and gain of a continuous-time (s-domain) filter.
struct AnalogZPK {
    std::vector<Complex> z;
    std::vector<Complex> p;
    double k = 1;
};

struct ZPK;

AnalogZPK buttap(int n);
AnalogZPK cheb1ap(int n, double rp);
AnalogZPK cheb2ap(int n, double rs);
AnalogZPK ellipap(int n, double rp, double rs);
AnalogZPK besselap(int n);
AnalogZPK analogPrototype(IIRFamily family, int order, double rp, double rs);

AnalogZPK lp2lp(const AnalogZPK& a, double wo);
AnalogZPK lp2hp(const AnalogZPK& a, double wo);
AnalogZPK lp2bp(const AnalogZPK& a, double wo, double bw);
AnalogZPK lp2bs(const AnalogZPK& a, double wo, double bw);

/// s -> z with s = 2 fs (z - 1)/(z + 1).
ZPK bilinear(const AnalogZPK& a, double fs);

/// Pre-warped analog frequency (rad/s) that the bilinear transform maps onto f (Hz).
inline double prewarp(double f, double fs) { return 2 * fs * std::tan(kPi * f / fs); }

/// Designs a digital IIR filter. For band filters the prototype order is doubled.
ZPK designIIR(BandType band, IIRFamily family, int order, double f1, double f2, double rp, double rs, double fs);
