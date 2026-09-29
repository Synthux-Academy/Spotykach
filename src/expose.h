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

        if (p1 != 0) {
            Log::PrintLine("P1 %d", p1);
            p1 = 0;
        }

        if (p2 != 0) {
            Log::PrintLine("P2 %d", p2);
            p2 = 0;
        }

        if (p3 > -2) {
            Log::Print("P3 ");
            Log::PrintLine(FLT_FMT(5), FLT_VAR(5, p3));
            p3 = -2;
        }

        if (p4 > -2) {
            Log::Print("P4 ");
            Log::PrintLine(FLT_FMT(5), FLT_VAR(5, p4));
            p4 = -2;
        }

        if (p5 > -2) {
            Log::Print("P5 ");
            Log::PrintLine(FLT_FMT(5), FLT_VAR(5, p5));
            p5 = -2;
        }
    }

    std::array<uint8_t, 8> vox;
    std::array<uint8_t, 4> layer;

    int p1 = 0;
    int p2 = 0;
    float p3 = -2;
    float p4 = -2;
    float p5 = -2;

private:
Expose() = default;
NOCOPY(Expose)

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
