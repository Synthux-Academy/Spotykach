#include "synclock.h"
#include <stdint.h>

using namespace bleeptools;

SynClock::SynClock():
_on_tick             { nullptr },
_manual_tempo        { 120.f },
_ppqn_in             { 4 },
_ppqn_out            { 48 },
_tr_time             { 0 },
_ticks_per_clock     { 1 },
_ticks               { 0 },
_fticks              { 0 },
_ticks_at_last_clock { 0 },
_tempo_ticks         { 0 },
_tempo_mks           { 500000 },
_hold                { false },
_is_running          { false },
_is_about_to_run     { false },
_last_state          { false }
{};

void SynClock::init(const uint32_t update_interval_mks, const uint32_t ppqn_out) {
    _tr_time = ppqn_out * update_interval_mks;
    _ppqn_out = ppqn_out;
};

void SynClock::set_ppqn_in(const uint32_t value)
{
    _ticks_per_clock = _ppqn_out / value; 
    _ppqn_in = value;
} 

void SynClock::set_tempo(const float tempo) {
    // Check for change larger than 2nd decimal digit
    if (fabsf(tempo - _manual_tempo) < .01f) return;
    //Below 0.05 -> external clock: (val - 0.05) / (1 - 0.05)
    //TODO can fold and optimize a bit
    _manual_tempo = tempo;
    _tempo_mks = _get_tempo_mks(_manual_tempo);
    if (_external_clock) {
        if (_is_running) {
            _is_running = false;
            _is_about_to_run = true;
        }
        else if (_is_about_to_run) {
            _is_running = true; 
            _is_about_to_run = false;
        }
        reset();
    }
};

void SynClock::_external_clock_tick() {
    if (!_is_running && !_is_about_to_run) return;
    auto kick_off = _is_about_to_run;
    if (_is_about_to_run) {
        _is_about_to_run = false;
        _is_running = true;
    }
    else {
         _hold = false;
    }
    _emit_ticks(true, kick_off);
};

/*
Derived from Maximum MIDI Programmer's ToolKit Copyright ©1993-1998 by Paul Messick and modified 
to maintain precision while running at slower rates. Namely _tempo_mks calculation was changed.

This method generates internal ticks and also synchronises to the external clock. So it's called both from internal 
interrupt timer and upon external clock tick reception.

nticks - integer ticks
_fticks - fractional ticks
_tempo_ticks - integer ticks count since last external clock
_tempo_mks - tempo in microseconds / beat (quarter note)
_hold - flag to stop advancing internal timeline if the number of internal ticks exceeded expected count of internal ticks per extrnal tick
_tr_time - internal resolution (_ppqn_out) multiplied by interrupt interval.
*/
void SynClock::_emit_ticks(const bool on_external_tick, const bool kick_off) {
    if (_ticks >= kOverflowThresh 
    || _fticks >= kOverflowThresh 
    || _tempo_ticks >= kOverflowThresh) reset();

    int32_t nticks = 0;

    //If we generated more internal ticks per extrnal tick as expected,
    //we don't advance internal "timeline", but only accumulate _tempo_ticks
    //in order to calculate and correct the tempo.
    //This flag is set to false upon reception of the external tick.
    if (_hold) {
        nticks = (_fticks + _tr_time) / _tempo_mks;
        _fticks += _tr_time - (nticks * _tempo_mks);
        _tempo_ticks += nticks;
        return;
    }

    //Once a tick of the extrnal clock is received,
    //we do resync, i.e. align inernal timeline with the external one
    //and adjust tempo.
    if (on_external_tick && !kick_off) {
         nticks = _ticks_per_clock - _ticks + _ticks_at_last_clock;
        _ticks_at_last_clock = _ticks + nticks;
        _tempo_mks += (_tempo_ticks * _tempo_mks + _fticks + _tr_time) / _ppqn_out - _tempo_mks / _ppqn_in;
        _tempo_ticks = 0;
        _fticks = 0;
    }
    //Regular mode. We generate internal ticks.
    else {
        nticks = (_fticks + _tr_time) / _tempo_mks;
        _fticks += _tr_time - nticks * _tempo_mks;
        if (_external_clock) {
            _tempo_ticks += nticks;
            //If there are more internal ticks per external tick than
            //expected, we set _hold to true effectively stopping advancing timeline
            //until next external tick
            if (_tempo_ticks >= _ticks_per_clock - 1) {
                nticks = _ticks_per_clock - 1 - (_ticks - _ticks_at_last_clock);
                _hold = true;
            }
        }
    }

    //Accumulate ticks
    _ticks += nticks;
    
    //Advance timeline
    if (_on_tick != nullptr && (nticks > 0 || kick_off)) {
        for (int32_t i = 0; i < nticks - 1; i++) _on_tick(false);
        _on_tick(on_external_tick);
    }
};

void SynClock::reset() {
    _fticks = 0;
    _ticks = 0;
    _ticks_at_last_clock = 0;
    _tempo_ticks = 0;
    _hold = false;
};
