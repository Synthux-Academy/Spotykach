#pragma once

#include <daisy_seed.h>
#include <stdint.h>
#include <array>

#include "nocopy.h"
#include "synclock.h"
#include "divider.h"
#include "deck.h"
#include "click.h"
#include "tempo.h"
#include "panner.h"
#include "modulator.h"

namespace spotykach {

enum kKeyInterval: int8_t {
    k1_16   = 1,    // 1/16th
    k1_4    = 4,
    k2_4    = 8, 
    k3_4    = 12,
    k4_4    = 16,   // 1 bar
    k5_4    = 20,
    k6_4    = 24,
    k7_4    = 28,
    k8_4    = 32,   // 2 bars
    k9_4    = 36,   
    k10_4   = 40,
    k11_4   = 44,
    k12_4   = 48,   // 3 bars
    k13_4   = 52,
    k14_4   = 56,
    k15_4   = 60,
    k16_4   = 64    // 4 bars
};

static const std::array<kKeyInterval, 17> kKeyIntervals = { 
    kKeyInterval::k1_16,
    kKeyInterval::k1_4, 
    kKeyInterval::k2_4,
    kKeyInterval::k3_4, 
    kKeyInterval::k4_4,
    kKeyInterval::k5_4,
    kKeyInterval::k6_4,
    kKeyInterval::k7_4,
    kKeyInterval::k8_4,
    kKeyInterval::k9_4,
    kKeyInterval::k10_4,
    kKeyInterval::k11_4,
    kKeyInterval::k12_4,
    kKeyInterval::k13_4,
    kKeyInterval::k14_4,
    kKeyInterval::k15_4,
    kKeyInterval::k16_4 
};

class Driver {
public:
    enum Source: uint8_t {
        internal    = 1,
        ts4         = 4,
        midi        = 24
    };
    
    Driver(Deck&, Deck&, Click&, Panner&, Modulator*);
    ~Driver() {};

    void init(const float sample_rate, const float buffer_size);

    void toggle_play(const Deck::Ref deck);
    
    float tempo() { return _clock.tempo(); }
    void set_tempo_norm(const float value) { _tempo.set_norm(value); }
    void tap_tempo() { _tempo.tap(); }

    Source source() const { return _source; }
    void toggle_source();
    bool is_external_sync() const { return _source != Source::internal; };

    void set_key_tick_interval_norm(const float); 
    uint8_t key_interval() const { return _key_tick_interval; }
    bool is_key_sub_quarter() const { return _key_tick_interval < k1_4; }
    bool is_key_at_quarter() const { return _key_tick_interval == k1_4; }

    void tick(const bool external_tick);
    
    void set_on_quarter(std::function<void(const bool)> on_quarter) { _on_quarter = on_quarter; }
    void set_on_clock_out(std::function<void()> on_clock_out) { _on_clock_out = on_clock_out; }

    void reset();

private:
    NOCOPY(Driver)

    void _on_clock_tick(const bool external_tick);
    void _toggle_clock();
    void _indicate_quarter();
    void _send_tick(const bool is_common_tick, const bool is_quarter, const float tempo);
    void _make_key();
    void _divider_reset_counts(const bool force = false);
    
    Deck&       _deck_a;
    Deck&       _deck_b;
    Divider     _divider;
    Tempo       _tempo;
    Click&      _click;
    Panner&     _panner;
    Modulator*  _mod;
    
    bleeptools::SynClock    _clock;
    daisy::StopwatchTimer   _reset_timer;

    std::function<void(const bool /*is key quarter*/)> _on_quarter;
    std::function<void()> _on_clock_out;

    Source _source;
    int8_t _quarter_tick_count;
    int8_t _key_tick_count;
    int8_t _key_tick_interval;
    uint8_t _key_tick_interval_idx;
    bool _is_key;
    bool _tap;
    bool _send_clock;
};

};
