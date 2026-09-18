// Copyright 2024 Infrasonic Audio LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
#pragma once

#include <daisy.h>
#include <daisysp.h>

#define LOG_TAGGED(TAG, FMT, ...) \
    Log::PrintLine("[%s] " FMT, TAG, ##__VA_ARGS__)

#define INFS_MIN(in, mn) (in < mn ? in : mn)
#define INFS_MAX(in, mx) (in > mx ? in : mx)
#define INFS_CLAMP(in, mn, mx) INFS_MIN(INFS_MAX(in, mn), mx)

#ifndef INFS_LOG_TARGET
#define INFS_LOG_TARGET daisy::LOGGER_INTERNAL
#endif

#ifndef INFS_LOG_THROTTLE_INTERVAL
#define INFS_LOG_THROTTLE_INTERVAL 250
#endif

namespace infrasonic
{
#if INFS_LOG
using Log = daisy::Logger<INFS_LOG_TARGET>;
#else
using Log = daisy::Logger<daisy::LOGGER_NONE>;
#endif

inline void FailAssertion(const char* msg = NULL, ...)
{
#if DEBUG
    if(msg != NULL)
    {
        Log::Print("Assertion Failed: ");
        va_list va;
        va_start(va, msg);
        Log::PrintLineV(msg, va);
        va_end(va);
    }
    asm("bkpt 255");
#endif
}

// Math stuff

constexpr float is_in_unit_range(float in)
{
    return in >= 0.0f && in <= 1.0f;
}

constexpr float sgn(float in)
{
    return in > 0 ? 1.0f : -1.0f;
}

constexpr float unitclamp(float in)
{
    return std::clamp(in, 0.0f, 1.0f);
}

inline float lerp(float a, float b, float t)
{
    return (1 - t) * a + t * b;
}

inline float lerp3(float value1, float value2, float value3, float t)
{
    if(t < 0.5)
    {
        return infrasonic::lerp(value1, value2, t * 2.0f);
    }
    else
    {
        return infrasonic::lerp(value2, value3, 2.0f * t - 1.0f);
    }
}

inline float frac_table_lerp(const float* table, int size, float tick)
{
    int   index1 = DSY_CLAMP(static_cast<int>(floorf(tick)), 0, size - 1);
    int   index2 = DSY_CLAMP((index1 + 1), 0, size - 1);
    float mu     = tick - floorf(tick);
    return infrasonic::lerp(table[index1], table[index2], mu);
}

template <typename T>
inline T map(const T& x,
             const T& in_min,
             const T& in_max,
             const T& out_min,
             const T& out_max)
{
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

inline float dbfs2lin(float dbfs)
{
    return daisysp::pow10f(dbfs * 0.05f);
}

inline float dbfs2lin_pwr(float dbfs)
{
    return daisysp::pow10f(dbfs * 0.1f);
}

inline float lin2dbfs(float lin)
{
    return daisysp::fastlog10f(lin) * 20.0f;
}

inline float map_db(const float in, const float max = 0.0f)
{
    if(in == 0.0f)
    {
        return 0.0f;
    }
    else
    {
        return dbfs2lin(daisysp::fmap(in, -60.0f, 0.0f));
    }
}

inline float map_db_split(const float in, const float midpoint, const float max)
{
    if(in < 0.5f)
    {
        float maxlin = dbfs2lin(midpoint);
        return daisysp::fmap(in * 2.0f, 0.0f, maxlin);
    }
    else
    {
        return dbfs2lin(daisysp::fmap((in - 0.5f) * 2.0f, midpoint, max));
    }
}

// Coefficient for one pole smoothing filter based on Tau time constant for `time_s`
inline float onepole_coef(float time_s, float sample_rate)
{
    if (time_s <= 0.0f || sample_rate <= 0.0f) { return 1.0f; }
    return daisysp::fmin(1.0f / (time_s * sample_rate), 1.0f);
}

} // namespace infrasonic
