#include "driver.h"
#include <functional>

#include "config.h"
#include "mode.h"

using namespace spotykach;

Driver::Driver(
    Deck& deck_a, 
    Deck& deck_b, 
    Click& click, 
    Panner& panner, 
    Modulator* mod
):
_deck_a             { deck_a },
_deck_b             { deck_b },
_divider            { Divider(kPPQNIntern) },
_click              { click },
_panner             { panner },
_mod                { mod },
_source             { Source::internal },
_key_tick_count     { 0 },
_key_tick_interval  { 4 },
_tap                { false }
{};

void Driver::init(const float sample_rate, const float buffer_size) 
{
    _reset_timer.Init();

    _clock.init(2000, kPPQNIntern); //2000 mks => 500Hz (see app.cpp)
    _clock.set_ppqn_in(Source::ts4);

    using namespace std::placeholders;

    auto on_clock = std::bind(&Driver::_on_clock_tick, this, _1);
    _clock.set_tempo(_tempo.bpm());
    _clock.set_on_tick(on_clock);
    _clock.run();
};

void Driver::toggle_source() 
{
    _clock.stop();
    _divider_reset_counts(true);
    static const auto src_cnt = 3;
    static Source src[src_cnt] = { Source::internal, Source::ts4, Source::midi };    
    for (uint8_t i = 0; i < src_cnt; i++) {
        if (src[i] == _source) {
            auto n = i + 1;
            _source = src[n < src_cnt ? n : 0];
            _clock.set_ppqn_in(_source);
            _clock.set_external_clock(_source != Source::internal);
            break;
        }
    }
    _clock.run();
    _reset_timer.Restart();
}

void Driver::tick(const bool external_tick) 
{
    if (external_tick) {
        if (!_clock.is_running()) {
            _clock.run();
        }
        _reset_timer.Restart();
        _clock.tick(true);
    }
    else {
        // 1 second is chosen because the maximum expected interval between 
        // ticks at 4PPQN = 60000 / 20bpm / 4PPQN = 750ms. If more - we consider
        // there's no clock coming in.
        if (_clock.external_clock() && _reset_timer.HasPassedMs(1000)) {
            _clock.stop();
            _divider_reset_counts(true);
        }
        else {
            _clock.tick(false);
            _clock.set_tempo(_tempo.bpm());
        }
    }
};

void Driver::toggle_play(Deck::Ref deck) {    
    (deck == Deck::A ? _deck_a : _deck_b).toggle_play();
};

void Driver::_on_clock_tick(const bool external_tick) {
    if (external_tick) {
        _reset_timer.Restart();
        _send_clock = true;
    }

    // Given the clock resolution is 48PPQN, sending every second tick.
    if (_send_clock) _on_clock_out();
    _send_clock = !_send_clock;

    auto is_quarter = false;
    auto tick = _divider.tick();
    _is_key = false;
    if (tick) {
        if (--_key_tick_count <= 0) _make_key();

        if (--_quarter_tick_count <= 0) {
            _quarter_tick_count = static_cast<int8_t>(_divider.resolution());
            // Changing from something les then quarter to quarter and more I need to make sure that
            // key counter is in syc with quarters counter. Otherwise key counts can fall between quarters.
            if (_key_tick_interval >= _quarter_tick_count && (_key_tick_count % _quarter_tick_count) != 0) {
                _make_key();
            }
            is_quarter = true;
        }
        _panner.tick();
    }

    _deck_a.set_tempo(_clock.tempo());
    _deck_b.set_tempo(_clock.tempo());

    _send_tick(tick, is_quarter, _clock.tempo());
};

void Driver::set_key_tick_interval_norm(const float norm)
{
    _key_tick_interval = kKeyIntervals[std::round(norm * (kKeyIntervals.size() - 1))];
}

void Driver::reset()
{
    if (_source == Source::internal) return;
    _divider_reset_counts();
}

void Driver::_divider_reset_counts(const bool force)
{
    if (_reset_timer.HasPassedMs(4) || force) { 
        //The clock is stopped or we're long after the tick, 
        // so the next one will be the key
        _key_tick_count = 0;
        _quarter_tick_count = 0;
        _divider.reset();
        _deck_a.reset_track_divider();
        _deck_b.reset_track_divider();
        if (_clock.is_running()) {
            _clock.stop();
            _clock.run();
        }
    }
    else if (!_is_key) {
        // There's recent tick and it's not the key one.
        // Then we simulate the key.
        _quarter_tick_count = static_cast<int8_t>(_divider.resolution());
        _make_key();
        _indicate_quarter();
        _send_tick(true, _clock.tempo(), true);
    }
}

void Driver::_make_key()
{
    _key_tick_count = _key_tick_interval;
    _is_key = true;
}

void Driver::_indicate_quarter()
{
    _click.trigger(_is_key && !is_key_sub_quarter());
    _on_quarter(_is_key);
}

void Driver::_send_tick(const bool is_common_tick, const bool is_quarter, const float tempo)
{
    _deck_a.tick(is_common_tick, _is_key);
    _deck_b.tick(is_common_tick, _is_key);
    if (is_common_tick) {
        (_mod+Deck::A)->tick(tempo, is_quarter);
        (_mod+Deck::B)->tick(tempo, is_quarter);
    }
    if (is_quarter) _indicate_quarter();
}


