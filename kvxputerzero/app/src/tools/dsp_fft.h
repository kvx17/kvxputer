#pragma once

#include <vector>

namespace tools::dsp {

// In-place iterative radix-2 FFT. re and im must be the same length, a power
// of two. im is zero for real input.
void fft(std::vector<float>& re, std::vector<float>& im);

// Hann-windowed magnitude spectrum of real samples. samples.size() is rounded
// down to the nearest power of two; mags gets that/2 bins (bin 0 = DC).
void magnitude_spectrum(const std::vector<float>& samples, std::vector<float>& mags);

// Dominant frequency in Hz via the strongest spectral bin with parabolic
// interpolation, searched within [min_hz, max_hz]. Returns 0 on silence.
float dominant_freq(const std::vector<float>& samples, int sample_rate, float min_hz = 50.f,
                    float max_hz = 2000.f);

} // namespace tools::dsp
