#include "dsp_fft.h"

#include <cmath>
#include <cstddef>

namespace tools::dsp {
namespace {

size_t floor_pow2(size_t n) {
    size_t p = 1;
    while (p * 2 <= n) p *= 2;
    return p;
}

} // namespace

void fft(std::vector<float>& re, std::vector<float>& im) {
    const size_t n = re.size();
    if (n < 2 || im.size() != n || (n & (n - 1)) != 0) return;

    // Bit-reversal permutation.
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.f * 3.14159265358979323846f / static_cast<float>(len);
        const float wr = std::cos(ang);
        const float wi = std::sin(ang);
        for (size_t i = 0; i < n; i += len) {
            float cur_r = 1.f, cur_i = 0.f;
            for (size_t k = 0; k < len / 2; ++k) {
                const size_t a = i + k;
                const size_t b = i + k + len / 2;
                const float ur = re[a];
                const float ui = im[a];
                const float vr = re[b] * cur_r - im[b] * cur_i;
                const float vi = re[b] * cur_i + im[b] * cur_r;
                re[a] = ur + vr;
                im[a] = ui + vi;
                re[b] = ur - vr;
                im[b] = ui - vi;
                const float nr = cur_r * wr - cur_i * wi;
                cur_i = cur_r * wi + cur_i * wr;
                cur_r = nr;
            }
        }
    }
}

void magnitude_spectrum(const std::vector<float>& samples, std::vector<float>& mags) {
    mags.clear();
    const size_t n = floor_pow2(samples.size());
    if (n < 2) return;

    std::vector<float> re(n), im(n, 0.f);
    for (size_t i = 0; i < n; ++i) {
        // Hann window.
        const float w = 0.5f * (1.f - std::cos(2.f * 3.14159265358979323846f * static_cast<float>(i) /
                                               static_cast<float>(n - 1)));
        re[i] = samples[i] * w;
    }
    fft(re, im);
    mags.resize(n / 2);
    for (size_t i = 0; i < n / 2; ++i) {
        mags[i] = std::sqrt(re[i] * re[i] + im[i] * im[i]) / static_cast<float>(n);
    }
}

float dominant_freq(const std::vector<float>& samples, int sample_rate, float min_hz, float max_hz) {
    if (sample_rate <= 0) return 0.f;
    std::vector<float> mags;
    magnitude_spectrum(samples, mags);
    if (mags.size() < 3) return 0.f;
    const size_t n = mags.size() * 2;
    const float bin_hz = static_cast<float>(sample_rate) / static_cast<float>(n);

    size_t lo = static_cast<size_t>(min_hz / bin_hz);
    size_t hi = static_cast<size_t>(max_hz / bin_hz);
    if (lo < 1) lo = 1;
    if (hi >= mags.size() - 1) hi = mags.size() - 2;
    if (hi <= lo) return 0.f;

    size_t peak = lo;
    float peak_v = 0.f;
    for (size_t i = lo; i <= hi; ++i) {
        if (mags[i] > peak_v) {
            peak_v = mags[i];
            peak = i;
        }
    }
    // Noise gate.
    if (peak_v < 1e-4f) return 0.f;

    // Parabolic interpolation around the peak.
    const float a = mags[peak - 1];
    const float b = mags[peak];
    const float c = mags[peak + 1];
    const float denom = (a - 2.f * b + c);
    float delta = 0.f;
    if (std::fabs(denom) > 1e-12f) delta = 0.5f * (a - c) / denom;
    return (static_cast<float>(peak) + delta) * bin_hz;
}

} // namespace tools::dsp
