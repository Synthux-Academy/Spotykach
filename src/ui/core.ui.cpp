#include "core.ui.h"
#include "Utility/dsp.h"
#include "expose.h"

using namespace spotykach;
using namespace infrasonic;
using namespace daisy;

static float snapped_speed(const float speed)
{
    auto s = static_cast<float>(kSpeedSteps.size() - 1);
    auto idx = static_cast<int>(std::clamp(std::round(speed * s), 0.f, s));
    return kSpeedSteps[idx];
}

CoreUI::CoreUI(Hardware& hw, Core& core, Settings& settings, Storage& storage):
_hw                 { hw },
_core               { core },
_midi               { CoreMIDI(hw, core) },
_settings           { settings },
_storage            { storage },
_calibrator         { Calibrator(hw, settings) },
_state              { State::launching },
_show_key_quarter   { false },
_clock_led_on       { false },
_tap_was_tapped     { false }
{};

void CoreUI::init() 
{
    _hw.StartAdcs();

    _blink_timer.Init();
    _arm_blink_timer.Init();

    _gate_out_timer[Deck::A].Init();
    _gate_out_timer[Deck::B].Init();

    _pot_monitor.Init(_ui_queue, _hw, 500, 0.005f, 0.002f);

    using namespace std::placeholders;
    auto on_touch = std::bind(&CoreUI::_on_pad_touch, this, _1);
    auto on_release = std::bind(&CoreUI::_on_pad_release, this, _1);
    _hw.SetOnTouch(on_touch);
    _hw.SetOnRelease(on_release);

    auto on_quarter = std::bind(&CoreUI::_on_quarter, this, _1);
    _core.driver().set_on_quarter(on_quarter);

    auto on_clock_out = std::bind(&CoreUI::_process_clock_out, this);
    _core.driver().set_on_clock_out(on_clock_out);

    auto on_play = std::bind(&CoreUI::_toggle_play, this, _1, _2);
    auto on_record = std::bind(&CoreUI::_toggle_record, this, _1, _2);
    auto on_note = std::bind(&CoreUI::_on_midi_note_on, this, _1, _2);
    auto on_cc = std::bind(&CoreUI::_on_midi_cc, this, _1, _2, _3);
    _midi.set_on_play(on_play);
    _midi.set_on_record(on_record);
    _midi.set_on_note_on(on_note);
    _midi.set_on_cc(on_cc);

    _speed_map.init();

    for (int i = 0; i < Hardware::LED_LAST; i++) _led[i].init(i);
};
void CoreUI::_init_values()
{
    for (auto ref: { Deck::A, Deck::B }) {
        
        auto& deck = _core.deck(ref);
        _pos[ref].set(0.f);
        _pos_offset[ref].set(0.f);
        _size[ref].set(1.f);
        _speed[ref].set(.5f);
        _mix[ref].set(.5f);
        _feedback[ref].set(kDefaultFeedback);
        _env[ref].set(0.f);
        _env_size[ref].set(1.f);
        _win[ref].set(.2f);

        _mod_speed[ref].set(.3f);
        _mod_amp[ref].set(0.f);

        _hold_clear[ref].init();

        auto& fx = deck.fx();
        _grit_mix[ref].set(fx.grit_mix());
        _grit_intens[ref].set(fx.grit_intensity());
        _flux_mix[ref].set(fx.flux_mix());
        _flux_intens[ref].set(fx.flux_intensity());
        _flux_fb[ref].set(fx.flux_fb());
    }

    _pan_range.set(.6f);
    _pan_speed.set(1.f);

    _click_mix.set(0.f);
    _key_interval.set(.0588f); //Corresponds to 1/4th
    _tempo.set(Tempo::abs_to_norm(120.f));
}

void CoreUI::process() 
{
    if (_state == State::launching) return;

    _hw.ProcessDigitalControls();
    _pot_monitor.Process();
    _hw.ProcessPads();
    _process_ui_queue();
    _process_switches();
    _process_gate_out(Deck::A);
    _process_gate_out(Deck::B);

    if (_state == State::init_values) {
        _init_values(); // effectively override the first read of the pots
        _reset_changing_value_id();
        _state = State::ready;
    }

    _tap_hold.process();

    auto blink = _arm_blink_timer.HasPassedMs(250);
    if (blink) _arm_blink_timer.Restart();

    for (auto ref: { Deck::A, Deck::B }) {
        auto& deck = _core.deck(ref);
        
        if (_pos[ref].apply()) deck.voxs().set_start(_pos[ref].value());
        if (_pos_offset[ref].apply()) deck.voxs().set_start_offset_interval(_pos_offset[ref].value());
        if (_size[ref].apply()) deck.voxs().set_size(_size[ref].value(), _touched.test(Alt));
        if (_env[ref].apply()) deck.voxs().set_shape(_env[ref].value());
        if (_speed[ref].apply()) {
            auto speed = _speed[ref].value();
            if (_pitch_quantized.test(ref)) {
                speed = snapped_speed(speed);
            }
            if (deck.mode() == Mode::Slice) deck.voxs().set_pitch(speed);
            else deck.voxs().set_speed(speed);
            _pitch_knob_val[ref] = speed;
        }

        if (_env_size[ref].apply()) deck.voxs().set_env_size(_env_size[ref].value());
        if (_win[ref].apply()) deck.voxs().set_win_size(_win[ref].value());

        if (_mix[ref].apply()) deck.set_inout_mix(_mix[ref].value());
        if (_feedback[ref].apply()) deck.set_feedback(_feedback[ref].value());

        if (_flux_fb[ref].apply()) deck.fx().set_flux_fb(_flux_fb[ref].value());
        if (_flux_intens[ref].apply()) deck.fx().set_flux_intensity(_flux_intens[ref].value());
        if (_grit_intens[ref].apply()) deck.fx().set_grit_intensity(_grit_intens[ref].value());
        if (_flux_mix[ref].apply()) deck.fx().set_flux_mix(_flux_mix[ref].value());
        if (_grit_mix[ref].apply()) deck.fx().set_grit_mix(_grit_mix[ref].value());

        if (_mod_speed[ref].apply()) _core.mod(ref).set_speed_norm(_mod_speed[ref].value(), _touched.test(Alt));
        if (_mod_amp[ref].apply()) _core.mod(ref).set_amp_norm(_mod_amp[ref].value());

        if (_hold_clear[ref].process()) {
            _hold_clear[ref].end();
            deck.clear_sequence();
        }

        // LEDs /////////
        _draw_ring(ref);
        _draw_fx(ref);
        _draw_alt(ref);    
        _draw_play(ref, blink);
    }
    if (_tempo.apply()) _core.driver().set_tempo_norm(_tempo.value());
    if (_pan_speed.apply()) _core.panner().set_speed(_pan_speed.value());
    if (_pan_range.apply()) _core.panner().set_range(_pan_range.value());
    if (_click_mix.apply()) _core.set_click_mix(_click_mix.value());
    if (_key_interval.apply()) _core.driver().set_key_tick_interval_norm(_key_interval.value());

    //Don't forget to reset flags
    if ((!_tap_hold.passed() && !_touched.test(Alt)) && _value_display_timeout.is_passed()) {
        _reset_changing_value_id();

        // Re-apply quantized pitch knob value
        for (auto d: { Deck::A, Deck::B }) {
            if (_pitch_quantized.test(d)) {
                _speed[d].set(_pitch_knob_val[d]);
                _pitch_quantized.reset(d);
            }
        }
    }
};

// Inputs ........................................
void CoreUI::read_cv() {
    auto& deck_a = _core.deck(Deck::A);

    auto mix_mod_a = _hw.GetControlVoltageValue(Hardware::CV_MIX_A);
    auto cor_mix_mod_a = _calibrator.correct(Hardware::CV_MIX_A, mix_mod_a);
    deck_a.inout_mix_mod_in(cor_mix_mod_a);

    auto pos_size_mod_a = _hw.GetControlVoltageValue(Hardware::CV_SIZE_POS_A);
    auto cor_pos_size_mod_a = _calibrator.correct(Hardware::CV_SIZE_POS_A, pos_size_mod_a);
    cor_pos_size_mod_a = std::round(cor_pos_size_mod_a * 1000.f) / 1000.f;

    deck_a.voxs().set_start_mod(cor_pos_size_mod_a);
    deck_a.voxs().set_size_mod(cor_pos_size_mod_a);
    

    auto raw_cv_a = _hw.GetControlVoltageValue(Hardware::CV_V_OCT_A);
    auto voct_a = _calibrator.correctVOctA(raw_cv_a);
    _speed_mult[Deck::A] = _speed_map.bipolar_pitch2speed(voct_a);
    deck_a.voxs().pitch_speed_mod_in(_speed_mult[Deck::A]);

    auto& deck_b = _core.deck(Deck::B);

    auto mix_mod_b = _hw.GetControlVoltageValue(Hardware::CV_MIX_B);
    auto cor_mix_mod_b = _calibrator.correct(Hardware::CV_MIX_B, mix_mod_b);
    deck_b.inout_mix_mod_in(cor_mix_mod_b);

    auto pos_size_mod_b = _hw.GetControlVoltageValue(Hardware::CV_SIZE_POS_B);
    auto cor_pos_size_mod_b = _calibrator.correct(Hardware::CV_SIZE_POS_B, pos_size_mod_b);
    cor_pos_size_mod_b = std::round(cor_pos_size_mod_b * 1000.f) / 1000.f;
    deck_b.voxs().set_start_mod(cor_pos_size_mod_b);
    deck_b.voxs().set_size_mod(cor_pos_size_mod_b);

    auto raw_cv_b = _hw.GetControlVoltageValue(Hardware::CV_V_OCT_B);
    auto voct_b = _calibrator.correctVOctB(raw_cv_b);
    _speed_mult[Deck::B] = _speed_map.bipolar_pitch2speed(voct_b);
    deck_b.voxs().pitch_speed_mod_in(_speed_mult[Deck::B]);

    auto mix_mod = _hw.GetControlVoltageValue(Hardware::CV_CROSSFADE);
    auto cor_mix_mod = _calibrator.correct(Hardware::CV_CROSSFADE, mix_mod);
    _core.mix_mod_in(cor_mix_mod);
}
void CoreUI::process_gate_in()
{ 
    if (_storage.of(Deck::A).is_idle()) {
        auto a_high = _hw.GetGateInputAState();
        if (a_high && !_gate_in.test(0)) {
            _gate_a_latency.start();
        }
        _gate_in.set(0, a_high);
        if (_gate_a_latency.is_passed()) {
            _trigger(Deck::A, _speed_mult[Deck::A]);
        }
    }
    
    if (_storage.of(Deck::B).is_idle()) {
        auto b_high = _hw.GetGateInputBState();
        if (b_high && !_gate_in.test(1)) {
            _gate_b_latency.start();
        }
        _gate_in.set(1, b_high);
        if (_gate_b_latency.is_passed()) {
            _trigger(Deck::B, _speed_mult[Deck::B]);
        }
    }
}
void CoreUI::_process_gate_out(const Deck::Ref ref)
{
    if (_core.deck(ref).voxs().read_reset_is_triggered()) {
        _gate_out_timer[ref].Restart();
        _gate_out_high[ref] = true;
    }
    if (_gate_out_high[ref] && _gate_out_timer[ref].HasPassedMs(7)) {
        _gate_out_high[ref] = false;
    }
    if (ref == Deck::A) {
        _hw.SetGateOutA(_gate_out_high[ref]);
    }
    else {
        _hw.SetGateOutB(_gate_out_high[ref]);
    }
}

// Knobs & Switches ..............................
void CoreUI::_process_ui_queue()
{
    auto& deck_a = _core.deck(Deck::A); 
    auto& deck_b = _core.deck(Deck::B);

    auto is_alt_touched = _touched.test(Alt);
    auto fx_a_touched = _touched.test(FluxA) || _touched.test(GritA);
    auto fx_b_touched = _touched.test(FluxB) || _touched.test(GritB);

    int* changing_id_a = &(_changing_value_id[Deck::A]);
    int* changing_id_b = &(_changing_value_id[Deck::B]);

    while(!_ui_queue.IsQueueEmpty())
    {
        auto event = _ui_queue.GetAndRemoveNextEvent();
        if (event.type == UiEventQueue::Event::EventType::potMoved) {
            auto val = event.asPotMoved.newPosition;
            switch (event.asPotMoved.id) {
                // DECK A //////////////////////////////////////
                case Hardware::CTRL_SOS_A: {
                    _flux_mix[Deck::A].process(val, _touched.test(FluxA), changing_id_a);
                    _grit_mix[Deck::A].process(val, _touched.test(GritA), changing_id_a);
                    _key_interval.process(val, !fx_a_touched && !is_alt_touched && _tap_hold.is_holding(), changing_id_a);
                    _mix[Deck::A].process(val, !fx_a_touched && !is_alt_touched && !_tap_hold.is_holding(), changing_id_a);
                    _feedback[Deck::A].process(val, !fx_a_touched && is_alt_touched && !_tap_hold.is_holding(), changing_id_a);
                    break;
                }

                case Hardware::CTRL_SIZE_A: 
                    switch (deck_a.mode()) {
                        case Mode::Reel: {
                            _size[Deck::A].process(val, true, changing_id_a); 
                        }
                        break;
                        case Mode::Slice: {
                            if (_tap_hold.passed() && !deck_a.is_empty()) {
                                _size_quarters[Deck::A].process(val, true, changing_id_a);
                                _set_tempo_by_size(Deck::A, val);
                            }
                            else {
                                _size[Deck::A].process(val, true, changing_id_a);
                                _size_quarters[Deck::A].set(val);
                            }
                        }
                        break;
                        case Mode::Drift: {
                            _size[Deck::A].process(val, !is_alt_touched, changing_id_a);
                            _win[Deck::A].process(val, is_alt_touched, changing_id_a);
                            
                        }
                        break;
                        case Mode::None: break;
                    }
                    break;

                case Hardware::CTRL_POS_A:
                    _pos[Deck::A].process(val, !fx_a_touched && !is_alt_touched, changing_id_a);
                    if (deck_a.voxs().has_cue()) {
                        _pos_offset[Deck::A].process(val, !fx_a_touched && is_alt_touched, changing_id_a);
                    }
                    _flux_fb[Deck::A].process(val, _touched.test(FluxA), changing_id_a);
                    break;

                case Hardware::CTRL_ENV_A: 
                    if (deck_a.mode() == Mode::Drift) {
                        _env[Deck::A].process(val, !is_alt_touched, changing_id_a);
                        _env_size[Deck::A].process(val, is_alt_touched, changing_id_a);
                    }
                    else {
                        _env[Deck::A].process(val, true, changing_id_a);
                    }
                    break;

                case Hardware::CTRL_PITCH_A: {
                    if (_storage.of(Deck::A).state() == DeckStorage::State::selecting) {
                        _storage.of(Deck::A).select_slot_at(std::round(val * (kStorageSlotCount - 1)));
                        break;
                    }

                    _flux_intens[Deck::A].process(val, _touched.test(FluxA), changing_id_a);
                    _grit_intens[Deck::A].process(val, _touched.test(GritA), changing_id_a);
                    auto thresh = is_alt_touched ? .1f : MValue::kDefaultTreshold;
                    _speed[Deck::A].process(val, !fx_a_touched, changing_id_a, thresh);
                    if (!fx_a_touched) {
                        _pitch_quantized.set(Deck::A, _touched.test(Alt));
                    }
                    break;
                }

                case Hardware::CTRL_MODFREQ_A: 
                    _tempo.process(val, _tap_hold.passed(), changing_id_a);
                    _mod_speed[Deck::A].process(val, !_tap_hold.passed(), changing_id_a);
                    break;

                case Hardware::CTRL_MOD_AMT_A: 
                    _click_mix.process(val, _tap_hold.passed(), changing_id_a);
                    _mod_amp[Deck::A].process(val, !_tap_hold.passed(), changing_id_a);
                    break;

                // DECK B //////////////////////////////////////
                case Hardware::CTRL_SOS_B: {
                    _flux_mix[Deck::B].process(val, _touched.test(FluxB), changing_id_b);
                    _grit_mix[Deck::B].process(val, _touched.test(GritB), changing_id_b);
                    _mix[Deck::B].process(val, !fx_b_touched && !is_alt_touched, changing_id_b);
                    _feedback[Deck::B].process(val, !fx_b_touched && is_alt_touched, changing_id_b);
                    break;
                }

                case Hardware::CTRL_SIZE_B:
                    switch (deck_b.mode()) {
                        case Mode::Reel: {
                            _size[Deck::B].process(val, true, changing_id_b);
                        }
                        break;
                        case Mode::Slice: {
                            if (_tap_hold.passed() && !deck_b.is_empty()) {
                                _size_quarters[Deck::B].process(val, true, changing_id_b);
                                _set_tempo_by_size(Deck::B, val);
                            }
                            else {
                                _size[Deck::B].process(val, true, changing_id_b);
                                _size_quarters[Deck::B].set(val);
                            }
                        }
                        break;
                        case Mode::Drift: {
                            _size[Deck::B].process(val, !is_alt_touched, changing_id_b);
                            _win[Deck::B].process(val, is_alt_touched, changing_id_b);
                        }
                        break;
                        case Mode::None: break;
                    }
                    break;

                case Hardware::CTRL_POS_B:
                    _pos[Deck::B].process(val, !fx_b_touched, changing_id_b);
                    if (deck_b.voxs().has_cue()) {
                        _pos_offset[Deck::B].process(val, !fx_b_touched && is_alt_touched, changing_id_b);
                    }
                    _flux_fb[Deck::B].process(val, _touched.test(FluxB), changing_id_b);
                    break;

                case Hardware::CTRL_ENV_B: 
                    if (deck_b.mode() == Mode::Drift) {
                        _env[Deck::B].process(val, !is_alt_touched, changing_id_b);
                        _env_size[Deck::B].process(val, is_alt_touched, changing_id_b);
                    }
                    else {
                        _env[Deck::B].process(val, true, changing_id_b);
                    }
                    break;
                
                case Hardware::CTRL_PITCH_B: {                    
                    if (_storage.of(Deck::B).state() == DeckStorage::State::selecting) {
                        _storage.of(Deck::B).select_slot_at(std::round(val * (kStorageSlotCount - 1)));
                        break;
                    }

                    _flux_intens[Deck::B].process(val, _touched.test(FluxB), changing_id_b);
                    _grit_intens[Deck::B].process(val, _touched.test(GritB), changing_id_b);

                    auto thresh = is_alt_touched ? .1f : MValue::kDefaultTreshold;
                    _speed[Deck::B].process(val, !fx_b_touched, changing_id_b, thresh);
                    if (!fx_b_touched) {
                        _pitch_quantized.set(Deck::B, _touched.test(Alt));
                    }
                    break;
                }

                case Hardware::CTRL_MODFREQ_B: 
                    _pan_speed.process(val, _tap_hold.passed(), changing_id_b);
                    _mod_speed[Deck::B].process(val, !_tap_hold.passed(), changing_id_b);
                    break;

                case Hardware::CTRL_MOD_AMT_B:
                    _pan_range.process(val, _tap_hold.passed(), changing_id_b);
                    _mod_amp[Deck::B].process(val, !_tap_hold.passed(), changing_id_b);
                    break;

                case Hardware::CTRL_CROSSFADE:
                    _core.set_mix(val);
                    break;
            }

            _value_display_timeout.start();
        }
    }
}
void CoreUI::_process_switches() 
{
    auto& deck_a = _core.deck(Deck::A);
    auto& deck_b = _core.deck(Deck::B);

    // construct into 8-bit set from inverted bitmask state
    // (all inputs are inverted due to pullups)
    std::bitset<8> sr1 = ~_hw.GetShiftRegState(0);
    std::bitset<8> sr2 = ~_hw.GetShiftRegState(1);

    // Mode A/B/C switch (mutex)
    if(sr1.test(2)) _core.set_route(Route::GenerativeStereo);
    else if(sr1.test(3)) _core.set_route(Route::DoubleMono);
    else _core.set_route(Route::Stereo);

    // Mod A Type switch
    auto& mod_a = _core.mod(Deck::A);
    if(sr1.test(1)) {
        mod_a.set_type(Modulator::Type::Follow);
    }
    else {
        mod_a.set_type(Modulator::Type::LFO);
        if(sr1.test(0)) mod_a.set_lfo_type(LFO::Type::square);
        else mod_a.set_lfo_type(LFO::Type::random);
    }

    // Mode A switch
    Mode ma = deck_a.mode();
    Mode nma = ma;
    if(sr1.test(6))      nma = Mode::Drift;
    else if(sr1.test(7)) nma = Mode::Reel;
    else                 nma = Mode::Slice;
    if (nma != ma) {
        deck_a.set_mode(nma);
        _core.infer_panner_mode();
        _size[Deck::A].set_apply();
    }

    // Size/Pos A switch
    deck_a.voxs().set_start_mod_on(sr1.test(5) || !sr1.test(4));
    deck_a.voxs().set_size_mod_on(sr1.test(4) || !sr1.test(5));

    // Mod B Type switch
    auto& mod_b = _core.mod(Deck::B);
    if(sr2.test(5)) {
        mod_b.set_type(Modulator::Type::Follow);
    }
    else {
        mod_b.set_type(Modulator::Type::LFO);
        if(sr2.test(4)) mod_b.set_lfo_type(LFO::Type::saw);
        else mod_b.set_lfo_type(LFO::Type::sine);
    }

    // Mode B switch
    Mode mb = deck_b.mode();
    Mode nmb = mb;
    if(sr2.test(2))      nmb = Mode::Drift;
    else if(sr2.test(3)) nmb = Mode::Reel;
    else                 nmb = Mode::Slice;
    if (nmb != mb) {
        deck_b.set_mode(nmb);
        _core.infer_panner_mode();
        _size[Deck::B].set_apply();
    }

    // Size/Pos B switch
    deck_b.voxs().set_start_mod_on(sr2.test(1) || !sr2.test(0));
    deck_b.voxs().set_size_mod_on(sr2.test(0) || !sr2.test(1));

    // Manual tempo tap switch
    // Update no faster than 500Hz
    static auto is_tap_tapped = false;
    static uint32_t last_tap_update = 0;
    uint32_t now = System::GetNow();
    if(now - last_tap_update >= 2)
    {
        last_tap_update = now;
        is_tap_tapped = sr2.test(6);
    }
    if (is_tap_tapped) {
        if (_tap_was_tapped) return;
        _tap_was_tapped = true;
        
        if (_touched.test(Alt)) {
            _core.driver().toggle_source();
            _clock_source_changed = true;
            _value_display_timeout.start();
        } 
        else if (_touched.test(GritA)) {
            deck_a.fx().switch_grit_mode();
            _grit_intens[Deck::A].set(deck_a.fx().grit_intensity());
            _grit_mix[Deck::A].set(deck_a.fx().grit_mix());
        } 
        else if (_touched.test(GritB)) {
            deck_b.fx().switch_grit_mode();
            _grit_intens[Deck::B].set(deck_b.fx().grit_intensity());
            _grit_mix[Deck::B].set(deck_b.fx().grit_mix());
        }
        else {
            auto& d = _core.driver();
            if (!d.is_external_sync()) {
                d.tap_tempo();
                _tempo.set(Tempo::abs_to_norm(d.tempo()), false);
            }
            if (!_tap_hold.is_holding()) {
                _tap_hold.begin();
            }
        }
    }
    else if (_tap_was_tapped) {
        _reset_changing_value_id();
        _tap_was_tapped = false;
        _tap_hold.end();
    }
}

// Clock ..........................................
static bool clock_state = false;
void CoreUI::tick()
{
    auto&d = _core.driver();
    auto new_state = false;
    auto midi_state = _midi.process();
    switch (d.source()) {
        case Driver::Source::ts4: new_state = _hw.GetClockInputState(); break;
        case Driver::Source::midi: new_state = midi_state; break;
        default: break;
    }
    d.tick(new_state && !clock_state);
    clock_state = new_state;
}
void CoreUI::_on_quarter(const bool is_key_quarter) 
{
    _clock_led_on = true;
    _show_key_quarter = is_key_quarter;
}
void CoreUI::_set_tempo_by_size(const Deck::Ref ref, const float fraction)
{
    auto bpm = _core.deck(ref).tempo_to_fit(fraction);
    auto norm = Tempo::abs_to_norm(bpm); 
    _tempo.set(norm);
    _core.driver().set_tempo_norm(norm);
}
void CoreUI::_process_clock_out()
{
    _midi.send_clock();
}

// Calibration .....................................
void CoreUI::calibrate(const bool recalibrate) {
    _calibrator.init(recalibrate);
}

// Track value .....................................
bool CoreUI::_is_changing(const MValue& value) const
{
    return _changing_value_id[Deck::A] == value.id() || _changing_value_id[Deck::B] == value.id();
}
void CoreUI::_reset_changing_value_id()
{
   _changing_value_id.fill(0);
   _clock_source_changed = false;
}

// Play & Record ...................................
void CoreUI::_toggle_play(const Deck::Ref ref, const bool reverse)
{
    if (!_storage.of(ref).is_idle()) return;

    auto& deck = _core.deck(ref);
    deck.disarm();
    if (deck.is_empty()) _show_empty(ref);
    if (!deck.is_overdubbing() && (!deck.is_playing() || deck.is_reverse() == reverse)) {
        _core.driver().toggle_play(ref);
    }
    deck.set_reverse(reverse);
}
void CoreUI::_toggle_record(const Deck::Ref ref, const bool internal)
{
    if (!_storage.of(ref).is_idle()) return;

    auto& deck = _core.deck(ref);
    auto src = internal ? Deck::Source::internal : Deck::Source::external;
    _core.set_source(src, ref);
    deck.toggle_recording();
    _storage.of(ref).reset_recent_slot();
}
void CoreUI::_trigger(const Deck::Ref ref, const float speed, const bool discont) 
{
    auto e = make_event();
    e.discont = discont;
    e.p3 = speed;
    e.p3_on = true;
    _core.deck(ref).trigger(&e);
    _show_gate_in(ref);
}

// MIDI ............................................
void CoreUI::_on_midi_note_on(const Deck::Ref ref, const uint8_t num)
{
    _trigger(ref, _speed_map.bipolar_pitch2speed(num - 60), true);
}
void CoreUI::_on_midi_cc(const Deck::Ref ref, const CC cc, const float val)
{
    switch (cc) {
        case CC::CrossFade:  _core.set_mix(val);         break;
        case CC::Start:      _pos[ref].set(val);         break;
        case CC::Offset:     _pos_offset[ref].set(val);  break;
        case CC::Size:       _size[ref].set(val);        break;
        case CC::Env:        _env[ref].set(val);         break;
        case CC::Pitch:      _speed[ref].set(val);       break;
        case CC::IOMix:      _mix[ref].set(val);         break;
        case CC::DeckFB:     _feedback[ref].set(val);    break;
        case CC::EnvSize:    _env_size[ref].set(val);    break;
        case CC::WinSize:    _win[ref].set(val);         break;
        case CC::ModCycle:   _mod_speed[ref].set(val);   break;
        case CC::ModGlow:    _mod_amp[ref].set(val);     break;
        case CC::GritOn:     _core.deck(ref).fx().set_grit_on(val > 0); break;
        case CC::GritIntens: _grit_intens[ref].set(val); break;
        case CC::GritMix:    _grit_mix[ref].set(val);    break;
        case CC::FluxOn:     _core.deck(ref).fx().set_flux_on(val > 0); break;
        case CC::FluxIntes:  _flux_intens[ref].set(val); break;
        case CC::FluxFB:     _flux_fb[ref].set(val);     break;
        case CC::FluxMix:    _flux_mix[ref].set(val);    break;
        default: break;
    }
}
