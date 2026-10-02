#pragma once
#include "common.h"
#include "nocopy.h"
#include <array>

class Expose {
public:
    static Expose& values() 
    {
        static Expose instance;
        return instance;
    }

    void print()
    {
        using namespace infrasonic;

        if (p1 > kOff) {
            Log::PrintLine("P1 %d", p1);
            p1 = kOff;
        }

        if (p2 > kOff) {
            Log::PrintLine("P2 %d", p2);
            p2 = kOff;
        }

        if (p3 > kOff) {
            Log::Print("P3 ");
            Log::PrintLine(FLT_FMT(7), FLT_VAR(7, p3));
            p3 = kOff;
        }

        if (p4 > kOff) {
            Log::Print("P4 ");
            Log::PrintLine(FLT_FMT(7), FLT_VAR(7, p4));
            p4 = kOff;
        }

        if (p5 > kOff) {
            Log::Print("P5 ");
            Log::PrintLine(FLT_FMT(7), FLT_VAR(7, p5));
            p5 = kOff;
        }
    }

    std::array<uint8_t, 8> vox;
    std::array<uint8_t, 4> layer;

    int32_t p1 = kOff;
    int32_t p2 = kOff;
    float p3 = kOff;
    float p4 = kOff;
    float p5 = kOff;

private:
Expose() = default;
NOCOPY(Expose)
static constexpr auto kOff = -INT32_MAX;

};

#ifndef IP1
#define IP1(a) Expose::values().p1 = a;
#endif

#ifndef IP2
#define IP2(a) Expose::values().p2 = a;
#endif

#ifndef FP3
#define FP3(a) Expose::values().p3 = a;
#endif

#ifndef FP4
#define FP4(a) Expose::values().p4 = a;
#endif

#ifndef FP5
#define FP5(a) Expose::values().p5 = a;
#endif
