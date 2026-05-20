#include "core.ui.h"
#include "Utility/dsp.h"
#include "config.h"

using namespace spotykach;
using namespace infrasonic;
using namespace daisy;
using namespace daisysp;

static constexpr uint32_t kWhite    = 0xffffff;
static constexpr uint32_t kPink     = 0xff00ff;
static constexpr uint32_t kRed      = 0xff0000;
static constexpr uint32_t kBlue     = 0x0000ff;
static constexpr uint32_t kGreen    = 0x00ff00;
static constexpr uint32_t kTurq     = 0x00FFEF;
static constexpr uint32_t kReelColor     = 0xf7941d;
static constexpr uint32_t kSliceColor    = 0x0064ff;
static constexpr uint32_t kDriftColor    = 0xc850ff;
static constexpr uint32_t kDelayColor    = 0xFF6565;
static constexpr uint32_t kSoftFxColor   = 0xFFD524;
static constexpr uint32_t kHarshFxColor  = 0xFF9A24;

static constexpr std::array<uint32_t, kStorageTapeCount> kTapeColor = {
    // Lexicographically ordered colors, 
    // so the folders on the card going 
    // to be in the same order when sorted 
    // by name
    kBlue,    //B lue
    kGreen,   //G reen
    kPink,    //P ink
    kRed,     //R ed
    kTurq,    //T urquoise
    0xffDE21  //Y ellow
};
static uint32_t clock_source_color(Driver& d) 
{
    switch (d.source()) {
        case Driver::Source::internal: return kGreen;
        case Driver::Source::ts4: return kPink;
        case Driver::Source::midi: return kTurq;
        default: return kWhite;
    }
}
static uint32_t clock_color(Driver& d, const bool is_key)
{
    uint32_t src_color = clock_source_color(d);
    if (is_key) { 
        if (d.is_key_at_quarter()) return d.is_external_sync() ? src_color : kWhite; 
        return d.is_key_sub_quarter() ? src_color : kWhite;
    }
    else { 
        return src_color; 
    }
}
static uint32_t count_in_color(const bool is_key)
{
    return is_key ? kWhite : kGreen;
}
static uint32_t mode_color(const spotykach::Mode mode)
{
    switch (mode) {
        case spotykach::Mode::Slice:    return kSliceColor;
        case spotykach::Mode::Drift:   return kDriftColor;
        default:                       return kReelColor;
    };
}
static uint32_t grit_color(const Fx::GritMode mode)
{
      switch (mode) {
        case Fx::GritMode::Reduce: return kHarshFxColor;
        default: return kSoftFxColor;
    }
}

void CoreUI::render_leds() 
{
    static auto led_count = 0;
    if (_clock_led_on) {
        led_count ++;
        if (led_count == 11) {
            _clock_led_on = false;
            led_count = 0;
        }
    }

    _hw.leds.Clear();
    _draw_leds();
    _hw.leds.Show();
}
void CoreUI::_draw_leds() 
{
    if (_state == State::launching) {
        _draw_launching();
        return;
    }

    _breathe_led();
    
    auto& deck_a = _core.deck(Deck::A);
    auto& deck_b = _core.deck(Deck::B);

    auto mode_color_a = mode_color(deck_a.mode()); 
    
    _led[Hardware::LED_GRIT_A].set(_hw);
    _led[Hardware::LED_FLUX_A].set(_hw);
    _hw.leds.Set(Hardware::LED_FADER_A, kWhite, 1.f - _core.mix());

    auto clck_src_color = clock_source_color(_core.driver());

    auto cycle_a_color = kWhite;
    if (_core.mod(Deck::A).type() == Modulator::Type::Follow) cycle_a_color = mode_color_a;
    else if (_core.mod(Deck::A).is_synced()) cycle_a_color = clck_src_color;
    _hw.leds.Set(Hardware::LED_CYCLE_A, cycle_a_color, _lfo_a);

    auto mode_color_b = mode_color(deck_b.mode());
    _led[Hardware::LED_GRIT_B].set(_hw);
    _led[Hardware::LED_FLUX_B].set(_hw);
    _hw.leds.Set(Hardware::LED_FADER_B, kWhite, _core.mix());

    auto cycle_b_color = kWhite;
    if (_core.mod(Deck::B).type() == Modulator::Type::Follow) cycle_b_color = mode_color_b;
    else if (_core.mod(Deck::B).is_synced()) cycle_b_color = clck_src_color;
    _hw.leds.Set(Hardware::LED_CYCLE_B, cycle_b_color, _lfo_b);

    switch (_core.route()) {
        case Route::DoubleMono: _hw.leds.Set(Hardware::LED_MODE_LEFT, kWhite, .8f); break;
        case Route::Stereo: _hw.leds.Set(Hardware::LED_MODE_CENTER, kWhite, .8f); break;
        case Route::GenerativeStereo: _hw.leds.Set(Hardware::LED_MODE_RIGHT, kWhite, .8f); break;
    }

    if (_clock_led_on || _clock_source_changed) {
        auto color = _clock_source_changed ? clck_src_color : clock_color(_core.driver(), _show_key_quarter);
        _hw.leds.Set(Hardware::LED_CLOCK_IN, color, 1.f);
    }

    auto gate_in_a_bright = .25f;
    if (_gate_in_led_cnt[Deck::A] > 0) {
        gate_in_a_bright = 1.f;
        _gate_in_led_cnt[Deck::A] --;
    }
    _hw.leds.Set(Hardware::LED_GATE_IN_A, mode_color_a, gate_in_a_bright);

    auto gate_in_b_bright = .25f;
    if (_gate_in_led_cnt[Deck::B] > 0) {
        gate_in_b_bright = 1.f;
        _gate_in_led_cnt[Deck::B] --;
    }
    _hw.leds.Set(Hardware::LED_GATE_IN_B, mode_color_b, gate_in_b_bright);

    // RINGS ////////////////////////////////////////////////
    _ring[Deck::A].apply(_hw, Hardware::LED_RING_A);
    _ring[Deck::B].apply(_hw, Hardware::LED_RING_B);
    
    _led[Hardware::LED_PLAY_A].set(_hw);
    _led[Hardware::LED_REV_A].set(_hw);
    _led[Hardware::LED_PLAY_B].set(_hw);
    _led[Hardware::LED_REV_B].set(_hw);

    _led[Hardware::LED_ALT_A].set(_hw);
    _led[Hardware::LED_ALT_B].set(_hw);
}
void CoreUI::_draw_launching()
{
    static float t = 0.0f;
    t += 0.0036f;
    const size_t num = 14;
    const float totalFadeInTime = 0.05f * (num - 1) + 0.1f;

    const uint16_t leds[num] = {
        Hardware::LED_FLUX_A, Hardware::LED_FLUX_B,
        Hardware::LED_GRIT_A, Hardware::LED_GRIT_B,
        Hardware::LED_REV_A, Hardware::LED_REV_B,
        Hardware::LED_PLAY_A, Hardware::LED_PLAY_B,
        Hardware::LED_ALT_A, Hardware::LED_ALT_B,
        Hardware::LED_CYCLE_A, Hardware::LED_CYCLE_B,
        Hardware::LED_GATE_IN_A, Hardware::LED_GATE_IN_B,
    };
    for (size_t i = 0; i < num; i += 2) {
        float progress = (t - i * 0.05f) / 0.25f;
        if (t > totalFadeInTime) progress = 1.0f - (t - totalFadeInTime) / 1.5f;
        
        float brightness = std::fmax(0.f, std::fmin(1.f, progress));
        uint32_t color = kWhite;
        if (leds[i] == Hardware::LED_FLUX_A || leds[i] == Hardware::LED_GRIT_A) {
            if (Config::dynamic().is_loaded()) color = kGreen;
        }

        _hw.leds.Set(leds[i], kWhite, brightness);
        _hw.leds.Set(leds[i + 1], color, brightness);
    };

    if (t < totalFadeInTime) return;
    
    auto phase = .25f * (t - totalFadeInTime) / totalFadeInTime;
    auto brightness = .7f * LUT_Sin_Value_At(phase * kLUTSinSize);
    for (auto ref: { Deck::A, Deck::B }) {
        _ring[ref].set_brightness(brightness);
        _ring[ref].set_hex_color(mode_color(_core.deck(ref).mode()));
        _ring[ref].set_segment(0.f, 0.999f);
        _ring[ref].set_updated();
        _ring[ref].apply(_hw, ref == Deck::A ? Hardware::LED_RING_A : Hardware::LED_RING_B);
    }

    if (t < 2 * totalFadeInTime) return;

    _led_breathe_phase = phase;
    _state = State::init_values;
}

// Called from main /////////////////////
void CoreUI::_draw_fx(const Deck::Ref ref)
{
    auto& fx = _core.deck(ref).fx();
    auto grit_id = ref == Deck::A ? Hardware::LED_GRIT_A : Hardware::LED_GRIT_B;
    auto flux_id = ref == Deck::A ? Hardware::LED_FLUX_A : Hardware::LED_FLUX_B;
    _led[grit_id].on(grit_color(fx.grit_mode()), fx.is_grit_on() ? 1.f : 0.5f);
    _led[flux_id].on(kDelayColor, fx.is_flux_on() ? 1.f : 0.5f);
}
void CoreUI::_draw_play(const Deck::Ref ref, const bool blink)
{
    auto& deck = _core.deck(ref);
    auto color = mode_color(deck.mode());
    auto playId = ref == Deck::A ? Hardware::LED_PLAY_A : Hardware::LED_PLAY_B;
    auto revId = ref == Deck::A ? Hardware::LED_REV_A : Hardware::LED_REV_B;
    _led[playId].off();
    _led[revId].off();

    auto& storage = _storage.of(ref);
    if (storage.is_selecting()) {
        if (_touched.test(Alt)) {
            if (storage.can_load()) _led[revId].on(kTapeColor[storage.selected_tape_idx()]);
            _led[playId].on(kWhite);
        }
        else if (storage.can_load()) {
            _led[playId].on(kTapeColor[storage.selected_tape_idx()]);
        }
        return;
    }
    if (deck.is_playing() || (deck.is_play_queued() && _clock_led_on)) { 
        auto ledId = deck.is_reverse() ? revId : playId;
        _led[ledId].on(color);
    }
    if (deck.is_armed() || deck.is_recording()) {
        auto inout = deck.is_armed() != deck.is_recording();
        auto rec = !inout;
        static bool led_on[Deck::Count] = { false, false };
        if (deck.mode() == Mode::Slice) {
            led_on[ref] = _clock_led_on;
        }
        else if (blink) {
            led_on[ref] = !led_on[ref];
        }
        if (rec || (led_on[ref] && inout)) {
            auto led_id = _core.source(ref) == Deck::Source::external ? playId : revId;
            _led[led_id].on(kRed, .8f);
        }
    }
    if (_hold_clear[ref].inidcate()) {
        if (_blink_timer.HasPassedMs(50)) {
            _blink_led_on = !_blink_led_on;
            _blink_timer.Restart();
        }
        _led[playId].on(kWhite, _blink_led_on);
    }
}
void  CoreUI::_draw_alt(const Deck::Ref ref)
{
    auto ledId = ref == Deck::A ? Hardware::LED_ALT_A : Hardware::LED_ALT_B;
    _led[ledId].off();
    if (_alt_blink_count[ref] > 0) {
        if (_blink_timer.HasPassedMs(80)) {
            _alt_blink_count[ref]--;            
            _blink_timer.Restart();
        }
        _led[ledId].on(kWhite, _alt_blink_count[ref] % 2);
    }
    // Track recording
    auto& track = _core.deck(ref).track();
    auto is_armed = track.is_armed();
    auto is_recording = track.is_recording();
    if (is_armed || is_recording) {
        uint32_t color = 0;
        if (is_armed && !is_recording && _clock_led_on) {
            color = count_in_color(_show_key_quarter);
        }
        else if (is_armed && is_recording) {
            color = kRed;
        }
        else if (is_recording && !is_armed && _clock_led_on) {
            color = kRed;
        }
        if (color != 0) {
            _led[ledId].on(color, .8f); 
        }
    }

    if (_touched.test(Alt)) {
        _led[ledId].on(kWhite);
    }
}

// RING // Called from main ////////////////
void CoreUI::_draw_ring(const Deck::Ref ref)
{
    auto& ring = _ring[ref];
    if (ring.is_updated()) return;

    auto& deck = _core.deck(ref);
    auto default_color = mode_color(deck.mode());

    ring.clear();
    ring.set_hex_color(default_color);
    ring.set_brightness(.5f);

    auto storage_state = _storage.of(ref).state();
    if (storage_state != DeckStorage::State::idle) {
        uint32_t color = kWhite;
        switch (storage_state) {
            case DeckStorage::State::selecting: _show_slots(ref); ring.set_updated(); return;
            case DeckStorage::State::saving: color = kWhite; break;
            case DeckStorage::State::loading: color = kTapeColor[_storage.of(ref).selected_tape_idx()]; break;
            case DeckStorage::State::error: /*_error_blink_count[ref] = 5;*/ break;
            default: {}
        }
        ring.set_hex_color(color);
        ring.set_segment(0.f, _storage.of(ref).progress());
        ring.set_updated();
        return;
    }
    // else if (_error_blink_count[ref] > 0) {
    //     _show_error();
    //     _error_blink_count[ref]--;
    //     return;
    // }
    else if (_is_changing(_key_interval) && ref == Deck::A) {
        _show_key_intervals();
    }
    else if (_is_changing(_size_quarters[ref])) {
        _show_size_quarters(ref, default_color);
    }
    else if (_touched.test(ref == Deck::A ? GritA : GritB)) {
        auto fx_color = grit_color(deck.fx().grit_mode());
        _show_value(_grit_intens[ref], ring, fx_color, ValueDisplay::Always);
        _show_value(_grit_mix[ref], ring, fx_color);
    }
    else if (_touched.test(ref == Deck::A ? FluxA : FluxB)) {
        _show_value(_flux_intens[ref], ring, kDelayColor, ValueDisplay::Always);
        _show_value(_flux_mix[ref], ring, kDelayColor);
        _show_value(_flux_fb[ref], ring, kDelayColor);
    } 
    else if (deck.is_empty() && !deck.is_armed()) { 
        ring.set_hex_color(default_color);
        ring.set_brightness(_led_breathe_brightness * .5f);
        ring.set_segment(0.0f, 0.999f);
    }
    else if (deck.is_recording() && !deck.is_overdubbing()) {
        auto& buff = deck.buffer();
        auto size = buff.norm_rec_size();
        /* show recorded segment */
        ring.set_segment(0.f, size);
        /* show write head */
        ring.set_point_hex_color(kRed);
        ring.add_point(size, .9f, true, false);
    }  
    else if (!deck.is_empty()) {
        // BUFFER SEGMENT ///////////////////////////////////////////
        auto segment_start = deck.norm_start();
        auto segment_size = 0.f;
        if (deck.mode() == Mode::Drift) {
            segment_size = deck.voxs().win_spread() * .95f;
            segment_start -= segment_size * .5f;
        }
        else {
            segment_size = deck.norm_size(true);  
        }
        ring.set_segment(segment_start, segment_start + segment_size);
        // PLAYHEADS /////////////////////////////////////////////////
        ring.set_point_hex_color(kWhite);
        if (deck.is_generating()) {
            for (auto i = 0; i < Generator::kVoxCount; i++) {
                if (deck.envelope_at(i) > 0) {
                    auto ph = deck.norm_playhead_at(i);
                    ring.add_point(ph, deck.envelope_at(i));
                }
            }
        }
        // POSITION & SIZE
        _show_value(_pos[ref], ring, kWhite, ValueDisplay::OnMoveDiffOnly);
        if (_is_changing(_size[ref]) && !_size[ref].is_tracking()) {
            if (deck.mode() == Mode::Drift) {
                // DRAW SPREAD
                float red_value = std::max(_size[ref].in_value(), _size[ref].value()) * .95f;
                // RED
                ring.set_brightness(.6f);
                ring.set_hex_color(kRed);
                segment_start = deck.norm_start() - red_value * .5f;
                ring.set_segment(segment_start, segment_start + red_value, true);
                if (_size[ref].in_value() > _size[ref].value()) {
                    ring.add_point(segment_start, .8f, true);
                    ring.add_point(segment_start + red_value, .8f, true);
                }
                // WHITE
                float white_value = std::min(_size[ref].in_value(), _size[ref].value()) * .95f;
                ring.set_brightness(.8f);
                ring.set_hex_color(kWhite);
                segment_start = deck.norm_start() - white_value * .5f;
                ring.set_segment(segment_start, segment_start + white_value, true);
            }
            else {
                ring.set_brightness(.6f); 
                ring.set_hex_color(kRed);
                auto current_val = segment_start + segment_size;
                auto new_val = segment_start + _size[ref].in_value();
                auto start = std::min(current_val, new_val);
                auto end = std::max(current_val, new_val);
                while (end >= 1.f) end -= 1.f; //WORKAROUND. Needs to be fixed in _ring[ref].set_segment().
                ring.set_segment(start, end);
                ring.add_point(segment_start + _size[ref].in_value(), .95f);
            }
        }
        
        if (deck.is_overdubbing()) {
            auto& buff = deck.buffer();
            ring.set_point_hex_color(kRed);
            auto wh = static_cast<float>(buff.write_head()) / buff.rec_size();
            ring.add_point(wh, .9f, true, false);
        }
    }
    
    switch (ref) {
        case Deck::A: {
            _show_value(_mod_amp[Deck::A], ring);
            _show_value(_mod_speed[Deck::A], ring);
            _show_value(_click_mix, ring);
            _show_value(_tempo, ring);
        }
        break;
        case Deck::B: {
            _show_value(_mod_amp[Deck::B], ring);
            _show_value(_mod_speed[Deck::B], ring);
            _show_value(_pan_speed, ring);
            _show_value(_pan_range, ring);
        }
        break;
        default:break;
    }
    _show_value(_mix[ref], ring);
    _show_value(_feedback[ref], ring);
    _show_value(_win[ref], ring);
    _show_value(_env[ref], ring);
    _show_value(_env_size[ref], ring);
    _show_pitch(ref);

    if (_is_changing(_poly_slice[ref])) 
    {
        ring.clear();
        auto start = 0.f;
        if (!_poly_slice[ref].is_tracking()) {
            start = _poly_slice[ref].in_value() < .5f ? 0.f : .5f;    
            ring.set_brightness(.6f);
            ring.set_hex_color(kRed);
            ring.set_segment(start, start + .495f);
        }
        ring.set_brightness(.6f);
        ring.set_hex_color(kWhite);
        if (_poly_slice[ref].value() < .5f) {
            ring.set_segment(.2f, .3f, true);
        }
        else {
            ring.set_segment(.5f, .535f, true);
            ring.set_segment(.65f, .7f, true);
            ring.set_segment(.825f, .855f, true);
            ring.set_segment(.97f, 1.f, true);
        }
    }
    
    ring.set_updated();
}
void CoreUI::_show_value(const MValue& val, LEDRing& ring, const uint32_t def_color, const ValueDisplay display)
{
    if (display != ValueDisplay::Always && !_is_changing(val)) return;

    // VALUE
    if (display != ValueDisplay::OnMoveDiffOnly) {
        ring.clear();
        ring.set_brightness(.6f);
        ring.set_hex_color(def_color);
        ring.set_segment(0.f, std::clamp(val.value(), 0.f, .999f));
    }

    // DEVIATION
    if (val.is_tracking()) return;

    ring.set_brightness(_led_breathe_brightness * .6f); 
    ring.set_hex_color(kRed);
    auto start = std::min(val.value(), val.in_value());
    auto end = std::max(val.value(), val.in_value());
    ring.set_segment(start, end);
    ring.add_point(val.in_value(), .95f);
}
void CoreUI::_show_pitch(const Deck::Ref ref)
{
    if (!_is_changing(_speed[ref])) return;
    auto& ring = _ring[ref];

    ring.set_hex_color(kWhite);
    ring.set_segment(0.f, 0.998f);
    ring.fill_brightness(0.2f);

    if (_touched.test(Alt)) {
        ring.set_brightness(.5f);
        
        auto steps = kSpeedSteps.size() - 1;
        auto norm_step = std::round(steps * _speed[ref].value()) / steps;
        auto spread = 0.05f;
        if (norm_step == 0.f) ring.set_segment(0.f, 2.f * spread, true);
        else if (norm_step == 1.f) ring.set_segment(1.f - 2.f * spread, 1.f, true);
        else ring.set_segment(norm_step - spread, norm_step + spread, true);
    }
    else {
        ring.set_point_hex_color(kWhite);
        ring.add_point(_speed[ref].value(), 1.0f, true);
    }
    _show_value(_speed[ref], ring, kWhite, ValueDisplay::OnMoveDiffOnly);
}
void CoreUI::_show_slots(const Deck::Ref ref)
{   
    auto& s = _storage.of(ref);
    auto idx = 3;
    for (uint8_t i = 0; i < kStorageSlotCount; i++) {
        auto slot = s.slot_at(i);
        auto bright = .8f;
        if (i == s.selected_slot_idx()) bright = _led_breathe_brightness * .8f;
        else bright = slot.is_empty ? .3f : .85f;
        _ring[ref].set_point_hex_color(i == s.selected_slot_idx() ? kWhite : kTapeColor[s.selected_tape_idx()]);
        for (uint8_t i = 0; i < 2; i++) _ring[ref].set_point(idx + i, bright);    
        idx += 5; //2 segment + 3 gap
    }
}
void CoreUI::_show_key_intervals() 
{
    if (!_key_interval.is_tracking()) {
        _ring[Deck::A].set_brightness(_led_breathe_brightness * .6f); 
        _ring[Deck::A].set_hex_color(kRed);
        auto start = std::min(_key_interval.value(), _key_interval.in_value());
        auto end = std::max(_key_interval.value(), _key_interval.in_value());
        _ring[Deck::A].set_segment(start, end);
    }

    auto interval = _core.driver().key_interval();
    uint8_t step;
    uint8_t steps;
    using KI = kKeyInterval;
    switch (interval) {
        case KI::k1_16: step = 1; steps = 32;   break;
        case KI::k1_4:  step = 1; steps = 1;    break;
        default:        step = 2; steps = interval / 4;
    }
    uint32_t color;
    for (uint8_t i = 0; i < steps; i++) {
        if (i == 0) color = _core.driver().is_key_sub_quarter() ? clock_source_color(_core.driver()) : kWhite;
        else color = clock_source_color(_core.driver());
        _ring[Deck::A].set_point_hex_color(color);
        _ring[Deck::A].set_point(i * step + 4, i % 4 && interval != KI::k1_16 ? .25f : .7f);
    }
}
void CoreUI::_show_size_quarters(const Deck::Ref ref, const uint32_t color)
{
    auto steps = 1 + round(_size_quarters[ref].value() * 15);
    for (uint8_t i = 0; i < steps; i++) {
        _ring[ref].set_point_hex_color(i % 4 ? color : kWhite);
        _ring[ref].set_point(i * 2 + 4, .7f);
    }
}
void CoreUI::_show_error(const Deck::Ref ref)
{
    if (_blink_led_on) {
        _ring[ref].set_hex_color(kRed);
        _ring[ref].set_brightness(.6f);
        _ring[ref].set_segment(0.f, 0.98f);
    }
}

void CoreUI::_breathe_led()
{
    _led_breathe_phase += .0005f; 
    if (_led_breathe_phase >= 1.f) {
        _led_breathe_phase = 0.f;
    }
    _led_breathe_brightness = .7f + (LUT_Sin_Value_At(_led_breathe_phase * kLUTSinSize) + 1.f) * .15f;
}

void CoreUI::_show_empty(const Deck::Ref ref)
{
    _alt_blink_count[ref] = 8;
}

void CoreUI::_show_gate_in(const Deck::Ref ref)
{
    _gate_in_led_cnt[ref] = 10;
}
