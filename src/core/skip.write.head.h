#pragma once

#include <algorithm>
#include "config.h"
#include "phasor.h"


namespace spotykach {

struct SkipRequest {
    Phasor* rh;     //readhead
    Phasor* ph;     //playhead
    size_t* ws;     //win size
    int32_t ls;     //loop start
    int32_t ll;     //loop length
    int32_t rb;     //in reverse rh = rb - ph
    float inc;      //increment
    int32_t wh;     //write head
    int32_t rs;     //record size
    bool is_rev;    //reverse
};


inline static void skip_write_head_fwd(SkipRequest r)
{
    /* wrapped read head */
    auto rh = r.rh->integral() % r.rs;

    /* integer write head */
    auto wh = static_cast<int32_t>(r.wh);

    /* overtaking */
    if (r.inc > 1.f) {
        if (wh < rh) wh += r.rs;
        auto ws = static_cast<size_t>((wh - rh) / (r.inc - 1.f));
        if (ws >= kMinimumWindowSize) {
            *r.ws = std::min(ws, kDefaultWindowSize);
        }
        else {
            wh = r.ls > r.wh ? r.wh + r.rs : r.wh;
            auto ph = wh - r.ls + 1;
            /* prevent escaping from the segment */
            if (ph < r.ll) {
                *r.ph = Phasor(ph);
                *r.rh = Phasor(wh + 1);
            }
        }
    }
    /* lagging */
    else {
        if (wh > rh) wh -= r.rs;
        auto ws = static_cast<size_t>((rh - wh) / (1.f - r.inc));
        if (ws >= kMinimumWindowSize) {
            *r.ws = std::min(ws, kDefaultWindowSize);
        }
        else {
            wh = r.ls > r.wh ? r.wh + r.rs : r.wh;
            *r.rh = Phasor(wh - 1);
            *r.ph = *r.rh - Phasor(r.ls);
        }
    }
}

inline static void skip_write_head_rev(SkipRequest r)
{
    /* wrapped read head */
    auto rh = r.rh->integral() % r.rs;
    while (rh < 0) rh += r.rs;

    /* write head */
    auto wh = static_cast<int32_t>(r.wh);
    auto wh_gt_rh = wh > rh;
    if (wh_gt_rh) wh -= r.rs;

    auto ws = static_cast<size_t>((rh - wh) / (1.f + r.inc));
    if (ws >= kMinimumWindowSize) {
        *r.ws = std::min(ws, *r.ws);
    }
    else {
        auto sh = (*r.rh > Phasor(r.rs)) ? r.rs : 0;
        
        rh = r.wh + sh - 1;
        auto ph = r.ls + r.rb - rh;

        /* prevent escaping from the segment */
        if (ph > 0) {
            *r.rh = Phasor(rh);
            *r.ph = Phasor(ph);
        }
    }
}

inline static void skip_write_head(SkipRequest r)
{
    if (r.is_rev) skip_write_head_rev(r);
    else skip_write_head_fwd(r);
}

};