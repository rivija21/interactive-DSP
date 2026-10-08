#pragma once
#include "IIRDesign.h"
#include <optional>
#include <vector>

// FIR design by the window method (windowed sinc), matching scipy.signal.firwin.

enum class WindowType { rectangular, hann, hamming, blackman, kaiser };
inline constexpr WindowType kAllWindows[] = {WindowType::rectangular, WindowType::hann, WindowType::hamming,
                                             WindowType::blackman, WindowType::kaiser};

const char* windowTitle(WindowType w);
const char* windowRawValue(WindowType w);
/// Approximate peak sidelobe level of the designed filter (stopband attenuation), in dB.
std::optional<double> windowTypicalStopbandDB(WindowType w);

/// Zeroth-order modified Bessel function of the first kind (power series).
double besselI0(double x);
/// Symmetric window of length n.
std::vector<double> makeWindow(WindowType type, int n, double beta = 8.6);
double sinc(double x);
/// High-pass and band-stop FIR filters need an odd number of taps (a type I filter):
/// an even-length symmetric filter always has a zero at Nyquist.
inline bool firNeedsOddTaps(BandType band) { return band == BandType::highpass || band == BandType::bandstop; }
/// Window-method FIR. Frequencies in Hz. Returns the taps h[0...n-1].
std::vector<double> designFIR(BandType band, int n, double f1, double f2, WindowType window, double beta, double fs);
/// Kaiser's formula for the stopband attenuation (dB) that a given beta achieves.
double kaiserAttenuation(double beta);
