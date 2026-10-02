#pragma once

#include <cmath>
#include <cstdint>

namespace spotykach {

/**
 * Fixed-point phase accumulator (signed Q31.32).
 *
 * Integer frame index lives in the upper 32 bits, fraction in the lower 32 bits.
 * Unlike float, the fractional resolution (2^-32 frame) does not depend on the
 * magnitude of the position, and add/sub are exact integer operations, so
 * repeated increments never drift or jitter.
 */
class Phasor {
public:
    constexpr Phasor(): _v { 0 } {}

    constexpr explicit Phasor(const int32_t frame): _v { static_cast<int64_t>(frame) * kOne } {}

    explicit Phasor(const float frame): _v { _from_float(frame) } {}

    /** Integer part, rounded towards negative infinity */
    int32_t integral() const { return static_cast<int32_t>(_v >> kFracBits); }

    /** Fractional part in [0, 1). Top 24 bits are used, so the conversion is exact and never rounds up to 1 */
    float fraction() const { return static_cast<float>(static_cast<uint32_t>(_v) >> 8) * kFracKof; }

    float to_float() const { return static_cast<float>(integral()) + fraction(); }

    Phasor& operator+=(const Phasor& o) { _v += o._v; return *this; }
    Phasor& operator-=(const Phasor& o) { _v -= o._v; return *this; }

    friend Phasor operator+(Phasor a, const Phasor& b) { return a += b; }
    friend Phasor operator-(Phasor a, const Phasor& b) { return a -= b; }

    friend bool operator==(const Phasor& a, const Phasor& b) { return a._v == b._v; }
    friend bool operator!=(const Phasor& a, const Phasor& b) { return a._v != b._v; }
    friend bool operator< (const Phasor& a, const Phasor& b) { return a._v <  b._v; }
    friend bool operator> (const Phasor& a, const Phasor& b) { return a._v >  b._v; }
    friend bool operator<=(const Phasor& a, const Phasor& b) { return a._v <= b._v; }
    friend bool operator>=(const Phasor& a, const Phasor& b) { return a._v >= b._v; }

private:
    static constexpr int kFracBits = 32;
    static constexpr int64_t kOne = int64_t(1) << kFracBits;
    static constexpr float kFracKof = 1.f / 16777216.f; // 2^-24

    static int64_t _from_float(const float f)
    {
        // Split first so both conversions stay 32 bit (hardware VCVT, no libcall)
        auto i = std::floor(f);
        auto frac = f - i; // exact, in [0, 1)
        return static_cast<int64_t>(static_cast<int32_t>(i)) * kOne
             + static_cast<int64_t>(static_cast<uint32_t>(frac * 4294967296.f)); // 2^32, i.e. uint32_t max
    }

    int64_t _v;
};

};
