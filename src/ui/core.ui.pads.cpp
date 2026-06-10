#include "core.ui.h"

using namespace spotykach;

void CoreUI::_on_pad_touch(Hardware::Pad pad) 
{
    auto& deck_a = _core.deck(Deck::A);
    auto& deck_b = _core.deck(Deck::B);
    auto is_alt_touched = _touched.test(Alt);
    _reset_changing_value_id();

    using P = Hardware::Pad;
    switch (pad) {
        case P::PlayA: _on_play_touch(Deck::A, false); break;
            
        case P::RevA: _on_play_touch(Deck::A, true); break;

        case P::FluxA: 
            _touched.set(FluxA);
            if (is_alt_touched) {
                deck_a.fx().toggle_flux_lock();
            } else {
                deck_a.fx().set_flux_on(true);
            }
            break;

        case P::GritA: 
            _touched.set(GritA);
            if (is_alt_touched) {
                deck_a.fx().toggle_grit_lock();
            }  
            else {
                deck_a.fx().set_grit_on(true);
            }
            break;
            
        case P::PlayB: _on_play_touch(Deck::B, false); break;

        case P::RevB: _on_play_touch(Deck::B, true); break;
        
        case P::FluxB: 
            _touched.set(FluxB);
             if (is_alt_touched) {
                deck_b.fx().toggle_flux_lock();
            } else {
                deck_b.fx().set_flux_on(true);
            }
            break;

        case P::GritB:
            _touched.set(GritB);
            if (is_alt_touched) {
                deck_b.fx().toggle_grit_lock();
            } 
            else {
                deck_b.fx().set_grit_on(true);
            }
            break;

        case P::SeqA: {
            if (_storage.of(Deck::A).is_selecting()) {
                if (is_alt_touched) _storage.of(Deck::A).previous_tape();
                else _storage.of(Deck::A).next_tape();
            }
            else if (_storage.of(Deck::A).is_idle()) {
                if (_tap_hold.passed() && _core.driver().is_external_sync()) {
                    _core.driver().reset();
                } 
                else if (is_alt_touched) {
                    auto& t = deck_a.track();
                    if (t.is_armed()) t.disarm(); else t.arm(!_core.driver().is_key_sub_quarter());
                    _hold_clear[Deck::A].begin();
                }
                else {
                    auto e = make_event();
                    deck_a.trigger(&e);
                }
            }
            break;
        }
        case P::SeqB: {
            if (_storage.of(Deck::B).is_selecting()) {
                if (is_alt_touched) _storage.of(Deck::B).previous_tape();
                else _storage.of(Deck::B).next_tape();
            }
            else if (_storage.of(Deck::B).is_idle()) {
                if (is_alt_touched) {
                    auto& t = deck_b.track();
                    if (t.is_armed()) t.disarm(); else t.arm(!_core.driver().is_key_sub_quarter());
                    _hold_clear[Deck::B].begin();
                } 
                else {
                    auto e = make_event();
                    deck_b.trigger(&e);    
                }
            }
            break;
        }
        case P::Alt: 
            _on_alt_touch();
            break;

        case P::Spot:
            if (_calibrator.phase() == Calibrator::Phase::calibrating) {
                _calibrator.collect();
            }
            break;

        default: break;
    }
};

void CoreUI::_on_pad_release(Hardware::Pad pad) 
{   
    using P = Hardware::Pad;
    switch (pad) {
        case P::SeqA: 
            _hold_clear[Deck::A].end();
            break;

        case P::SeqB:
            _hold_clear[Deck::B].end();
            break;

        case P::FluxA:
            _touched.reset(FluxA);
            _core.deck(Deck::A).fx().set_flux_on(false);
            _changing_value_id[Deck::A] = 0;
            break;

        case P::GritA:
            _touched.reset(GritA);
            _core.deck(Deck::A).fx().set_grit_on(false);
            _changing_value_id[Deck::A] = 0;
            break;

        case P::FluxB:
            _touched.reset(FluxB);
            _core.deck(Deck::B).fx().set_flux_on(false);
            _changing_value_id[Deck::B] = 0;
            break;

        case P::GritB:
            _touched.reset(GritB);
            _core.deck(Deck::B).fx().set_grit_on(false);
            _changing_value_id[Deck::B] = 0;
            break;

        case P::Alt: 
            _touched.reset(Alt);
            _reset_changing_value_id();
            break;
        
        default: break;
    }
};

void CoreUI::_on_play_touch(const Deck::Ref ref, const bool reverse)
{
    auto& deck = _core.deck(ref);

    if (_tap_hold.passed()) {
        if (deck.is_generating()) deck.stop();
        
        auto& s = _storage.of(ref);
        
        if (s.is_processing()) {
            bool is_loading = s.state() == DeckStorage::State::loading;
            _storage.cancel(ref);
            if (is_loading) deck.buffer().clear();
        }
        else if (s.is_idle()) _storage.activate(ref);
        else if (s.is_selecting()) _storage.deactivate(ref);
        return;
    }

    if (_storage.of(ref).is_selecting()) {
        if (_touched.test(Alt)) {
            if (reverse) _storage.load(ref);
            else _storage.save(ref);
        }
        else if (!reverse) {
            _storage.load(ref);
        }
        return;
    }
    
    if (_touched.test(Alt)) _toggle_record(ref, reverse);
    else _toggle_play(ref, reverse);
}

void CoreUI::_on_alt_touch() 
{
    _touched.set(Alt);
    
    auto& deck_a = _core.deck(Deck::A);
    if (deck_a.track().is_armed()) deck_a.track().disarm();
    if (_touched.test(GritA)) deck_a.fx().toggle_grit_lock();
    if (_touched.test(FluxA)) deck_a.fx().toggle_flux_lock();

    auto& deck_b = _core.deck(Deck::B); 
    if (deck_b.track().is_armed()) deck_b.track().disarm();
    if (_touched.test(GritB)) deck_b.fx().toggle_grit_lock();
    if (_touched.test(FluxB)) deck_b.fx().toggle_flux_lock();

    if (_tap_hold.passed()) {
        _core.driver().toggle_source();
        _clock_source_changed = true;
        _value_display_timeout.start();
    }
}
