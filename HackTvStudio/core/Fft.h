#ifndef FFT_H
#define FFT_H

#include <complex>
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

/*
 * Iterative in-place radix-2 FFT with precomputed twiddles, bit-reversal
 * table and Hann window. Replaces the recursive allocate-per-level version
 * from HackTvGui/constants.h (same output, ~10x faster for 2048 points).
 *
 * compute() returns the spectrum in dBFS, DC in the centre, and the total
 * signal power in dBFS (window-loss corrected) for the S-meter.
 */
class SpectrumAnalyzer
{
public:
    explicit SpectrumAnalyzer(int size = 2048) { setSize(size); }

    int size() const { return m_n; }

    void setSize(int n)
    {
        // Round down to a power of two, minimum 64
        int p = 64;
        while (p * 2 <= n) p *= 2;
        m_n = p;

        const float twoPi = 6.28318530717958647692f;

        m_window.resize(m_n);
        m_windowPower = 0.0f;
        for (int i = 0; i < m_n; ++i) {
            m_window[i] = 0.5f * (1.0f - std::cos(twoPi * i / (m_n - 1)));
            m_windowPower += m_window[i] * m_window[i];
        }
        m_windowPower /= m_n;  // mean(w^2), ~0.375 for Hann

        m_twiddle.resize(m_n / 2);
        for (int k = 0; k < m_n / 2; ++k)
            m_twiddle[k] = std::polar(1.0f, -twoPi * k / m_n);

        int bits = 0;
        while ((1 << bits) < m_n) ++bits;
        m_bitrev.resize(m_n);
        for (int i = 0; i < m_n; ++i) {
            uint32_t r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1u << (bits - 1 - b);
            m_bitrev[i] = r;
        }

        m_work.resize(m_n);
    }

    /* iq must hold at least size() samples; out receives size() dB values. */
    float compute(const std::complex<float>* iq, float* out)
    {
        const int n = m_n;

        // Windowed bit-reversed copy
        for (int i = 0; i < n; ++i)
            m_work[m_bitrev[i]] = iq[i] * m_window[i];

        // Butterflies
        for (int len = 2; len <= n; len <<= 1) {
            const int half = len >> 1;
            const int step = n / len;
            for (int i = 0; i < n; i += len) {
                for (int j = 0; j < half; ++j) {
                    const std::complex<float> w = m_twiddle[j * step];
                    const std::complex<float> u = m_work[i + j];
                    const std::complex<float> v = m_work[i + j + half] * w;
                    m_work[i + j]        = u + v;
                    m_work[i + j + half] = u - v;
                }
            }
        }

        // Power -> dBFS (full-scale sine = 0 dBFS), DC shifted to the centre
        const float ref = static_cast<float>(n) * static_cast<float>(n);
        const float floorPower = 1e-20f;
        double total = 0.0;
        const int halfN = n / 2;
        for (int i = 0; i < n; ++i) {
            const float p = std::norm(m_work[i]);
            total += p;
            const int dst = (i + halfN) & (n - 1);
            out[dst] = 10.0f * std::log10(std::max(p, floorPower) / ref);
        }

        // Parseval: mean power of the windowed input = sum|X|^2 / n^2
        const double meanPower = total / ref / m_windowPower;
        return static_cast<float>(10.0 * std::log10(std::max(meanPower, 1e-20)));
    }

private:
    int m_n = 0;
    float m_windowPower = 1.0f;
    std::vector<float> m_window;
    std::vector<std::complex<float>> m_twiddle;
    std::vector<uint32_t> m_bitrev;
    std::vector<std::complex<float>> m_work;
};

#endif // FFT_H
