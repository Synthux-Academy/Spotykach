// Copyright 2026 bleeptools
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

#include <cmath>
#include <array>
#include <functional>

#include "nocopy.h"

namespace bleeptools {

class SynClock {
public:
    SynClock();
    ~SynClock() {}

    void init(const uint32_t update_interval_mks, const uint32_t ppqn_out);

    uint32_t ppqn_in() const { return _ppqn_in; }
    void set_ppqn_in(const uint32_t);

    /*
    Called by internal interrupt timer
    */
    void tick(const bool external) {
      if (_external_clock && external) _external_clock_tick();
      else if (_is_running) _emit_ticks(); 
    }

    void set_on_tick(std::function<void(const bool)> on_tick) { _on_tick = on_tick; }
    
    float tempo() { return 60000000.f / _tempo_mks; }

    /*
    Setting tempo from internal control. 
    Has no effect in case of syncing to extrnal clock.
    */
    void set_tempo(const float norm_value);

    /*
    In case of external clock sync this
    method only schedules start. Actual ticking
    starts on the first tick of the external clock.
    see clock_in_tick() below.
    */
    void run() {
      reset();
      if (_external_clock) _is_about_to_run = true; else _is_running = true;
    }

    void stop() {
      _is_running = false;
    }

    void reset();

    bool is_running() const { return _is_running; }

    void set_external_clock(const bool on) { _external_clock = on; }
    bool external_clock() const { return _external_clock; }

private:
    NOCOPY(SynClock)
    /*
    External clock received
    This method starts playback on first received clock
    after playback was scheduled. After that, calls sync 
    for every tick received.
    */
    void _external_clock_tick();

    /*
    See cpp file for details
    */
    void _emit_ticks(const bool on_external_tick = false, const bool kick_off = false);
    
    uint32_t _get_tempo_mks(const float tempo) { return static_cast<uint32_t>(60.f * 1e6 / tempo); }

    std::function<void(const bool)> _on_tick;

    static constexpr float kExtClockOffset = .05f;
    static constexpr int32_t kOverflowThresh = 1073741823; //INT32_MAX / 2;

    float _manual_tempo;

    int32_t _ppqn_in;
    int32_t _ppqn_out;
    int32_t _tr_time;
    int32_t _ticks_per_clock;
    int32_t _ticks;
    int32_t _fticks;
    int32_t _ticks_at_last_clock;
    int32_t _tempo_ticks;
    int32_t _tempo_mks;
    bool _hold;
    bool _is_running;
    bool _is_about_to_run;
    bool _last_state;
    bool _external_clock;
};

};
