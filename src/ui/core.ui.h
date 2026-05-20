#pragma once

#include <daisy_seed.h>
#include <functional>
#include <cstring>
#include <bitset>
#include <array>

#include "../common.h"
#include "color.h"
#include "hw/hardware.h"
#include "core/core.h"
#include "memory/storage.h"
#include "nocopy.h"
#include "hold.h"
#include "mvalue.h"
#include "speed.map.h"
#include "calibrator.h"
#include "time.iterval.h"
#include "led.ring.h"
#include "../core/lutsinosc.h"

namespace spotykach { 

static constexpr std::array<float, 7> kSpeedSteps = { 
    .125f,          // -24
    .25f,           // -12
    .33371f,        // -7
    .5f,            // 0
    .5830516667f,   // 7 
    .666666667f,    // 12
    1.f             // 24
};

class CoreUI {
public:
    CoreUI(Hardware&, Core&, Settings&, Storage&);
    ~CoreUI() = default;

    void init();
    void process();
    void tick();
    void read_cv();

    void render_leds();

    void set_lfo(const float a, const float b)  {
        _lfo_a = a;
        _lfo_b = b;
    }

    void calibrate(const bool recalibrate);

    void process_gate_in();

private:
    NOCOPY(CoreUI)

    enum class State: uint8_t {
        launching,
        init_values,
        ready
    };

    void _init_values();

    void _process_ui_queue();
    void _process_switches();
    void _process_gate_out(const Deck::Ref);
    bool _process_midi();
    bool _process_realtime(daisy::MidiEvent&);
    void _process_note_on(daisy::NoteOnEvent&);

    void _on_pad_touch(Hardware::Pad pad);
    void _on_pad_release(Hardware::Pad pad);
    void _on_play_touch(const Deck::Ref, const bool reverse);
    void _on_alt_touch();

    void _process_clock_out();
    void _on_quarter(const bool /*is key quarter*/);
    void _set_tempo_by_size(const Deck::Ref, const float fraction);

    void _trigger(const Deck::Ref, const float speed, const bool discont = false);

    // LEDs ////////////////////////////////////////
    void _draw_leds();
    void _draw_launching();

    void _draw_fx(const Deck::Ref);
    void _draw_play(const Deck::Ref, const bool blink);
    void _draw_alt(const Deck::Ref);

    void _draw_ring(const Deck::Ref);
    void _show_pitch(const Deck::Ref);
    void _show_slots(const Deck::Ref);
    void _show_key_intervals();
    void _show_size_quarters(const Deck::Ref, const uint32_t color);
    void _show_error(const Deck::Ref);
    
    void _show_empty(const Deck::Ref);
    void _show_gate_in(const Deck::Ref);

    void _breathe_led();

    struct LED {
        public: 
        void init(int id)
        {
            _id = (Hardware::LedId)id;
        }

        void on(const uint32_t color, const float bright = 1.f) 
        {
            _bright = bright;
            _color = color;
        }

        void off() {
            _bright = 0;
            _color = 0;
        }

        void set(Hardware& hw)
        {
            hw.leds.Set(_id, _color, _bright);
        }

        private:
        Hardware::LedId _id;
        float _bright;
        uint32_t _color;
    };
    std::array<LED, Hardware::LED_LAST> _led;

    //////////////////////////////////////////////////

    enum class ValueDisplay: uint8_t {
        Always,
        OnMove,
        OnMoveDiffOnly
    };
    void _show_value(
        const MValue&, 
        LEDRing&,
        const uint32_t default_color = 0xffffff, 
        const ValueDisplay display = ValueDisplay::OnMove);
    bool _is_changing(const MValue&) const;
    void _reset_changing_value_id();

    Hardware& _hw;
    Core& _core;
    Settings& _settings;
    Storage& _storage;
    Calibrator _calibrator;

    daisy::UiEventQueue _ui_queue;
    daisy::PotMonitor<Hardware, Hardware::kNumAnalogControls> _pot_monitor;
    std::bitset<Hardware::CTRL_LAST> _init;
    std::bitset<Hardware::CTRL_LAST> _apply;

    std::array<int, Deck::Count> _changing_value_id;

    std::array<Hold<1500/*ms*/>, Deck::Count> _hold_clear;
    Hold<100/*ms*/> _tap_hold;

    std::array<MValue, Deck::Count> _flux_mix;
    std::array<MValue, Deck::Count> _grit_mix;
    std::array<MValue, Deck::Count> _flux_intens;
    std::array<MValue, Deck::Count> _grit_intens;
    std::array<MValue, Deck::Count>_flux_fb;
    
    std::array<MValue, Deck::Count> _mix;
    std::array<MValue, Deck::Count> _feedback;
    std::array<MValue, Deck::Count> _speed;
    std::array<MValue, Deck::Count> _pos;
    std::array<MValue, Deck::Count> _size;
    std::array<MValue, Deck::Count> _env;
    std::array<MValue, Deck::Count> _env_size;
    std::array<MValue, Deck::Count> _win;
    std::array<MValue, Deck::Count> _poly_slice;
    std::array<MValue, Deck::Count> _size_quarters;
    std::array<MValue, Deck::Count> _mod_speed;
    std::array<MValue, Deck::Count> _mod_amp;

    MValue _click_mix;
    MValue _tempo;
    MValue _key_interval;

    MValue _pan_speed;
    MValue _pan_range;

    std::array<LEDRing, Deck::Count> _ring;

    float _led_breathe_phase;
    float _led_breathe_brightness;
    bool _blink_led_on;

    float _lfo_a;
    float _lfo_b;
    std::array<float, Deck::Count> _speed_mult;

    daisy::StopwatchTimer _blink_timer;
    daisy::StopwatchTimer _arm_blink_timer;
    daisy::StopwatchTimer _clock_switch_timer;

    std::array<daisy::StopwatchTimer, Deck::Count> _gate_out_timer;
    std::array<bool, Deck::Count> _gate_out_high;
    std::array<int8_t, Deck::Count> _alt_blink_count;
    std::array<int8_t, Deck::Count> _gate_in_led_cnt;

    bool _show_rec_a;
    bool _show_rec_b;
    bool _middle_pitch_a;
    bool _middle_pitch_b;
    float pitch_seg_a;
    float pitch_seg_b;

    std::bitset<2> _gate_in;
    TimeInterval<8/*ms*/> _gate_a_latency;
    TimeInterval<8/*ms*/> _gate_b_latency;

    SpeedMap<60> _speed_map;

    TimeInterval<1000/*ms*/> _value_display_timeout;

    enum Touched: uint8_t {
        Shift,
        Alt,
        FluxA,
        GritA,
        FluxB,
        GritB,
        TouchedOptions
    };
    std::bitset<TouchedOptions> _touched;
    std::bitset<Deck::Ref::Count> _pitch_quantized;

    State _state;
    bool _show_key_quarter;
    bool _clock_led_on;
    bool _clock_source_changed;
    bool _tap_was_tapped;
};

};
