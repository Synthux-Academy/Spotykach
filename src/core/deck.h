#pragma once

#include "nocopy.h"
#include "detector.h"
#include "buffer.h"
#include "generator.h"
#include "track.h"
#include "mode.h"
#include "divider.h"
#include "dispatcher.h"
#include "xfade.h"
#include "fx.h"
#include "smooth.h"
#include "daisysp.h"

namespace spotykach  {

class Deck {
public:
    enum Ref: uint8_t {
        A,
        B,
        Count,
        None = 0xff
    };

    enum class Source: uint8_t {
        external,
        internal
    };

    Deck();
    ~Deck() {};

    Ref ref;

    struct Params {
        float sample_rate;
        size_t main_buf_size;
        Event* track_buf;
        Buffer::Frame* main_buf;
        float** detect_buf;
        float** delay_buf;
        size_t* slice_buf;
    };

    void init(const Params p);

    void prepare();

    void process_out(const float in0, const float in1, float& out0, float& out1);
    void process_in(const float in0, const float in1);

    bool is_generating() const { return _generator.is_generating(); }

    void toggle_play();
    void play();
    void stop();
    bool is_playing() const { return _is_playing; }
    bool is_play_queued() const { return _is_play_queued; }

    bool is_armed() const { return _is_armed; }
    void toggle_recording();
    void disarm() { _set_buf_armed(false); }
    bool is_recording() const { return _buffer.is_recording(); }
    bool is_overdubbing() const { return is_playing() && is_recording(); }
    bool is_empty() const { return _buffer.is_empty(); }

    void make_grid();

    void clear_sequence();
    
    void tick(const bool common_tick, const bool is_key);
    void trigger(Event *);
    void reset_track_divider() { _pattern_divider.reset(); }

    float norm_playhead_at(const uint8_t idx) const;
    float envelope_at(const uint8_t idx) const { return _generator.envelope_at(idx); };

    bool is_reverse() const { return _generator.is_reverse(); }
    void set_reverse(const bool);

    void set_tempo(const float);
    void set_record_tempo(const float value) { _record_tempo = value; }
    float tempo_to_fit(const float frac);

    Mode mode() const { return _mode; }
    void set_mode(const Mode val);

    void set_inout_mix(const float val);
    void inout_mix_mod_in(const float val);

    void set_feedback(const float value);

    Track& track() { return _track; }
    Buffer& buffer() { return _buffer; }
    Generator& voxs() { return _generator; }
    Fx& fx() { return _fx; }

private:
    NOCOPY(Deck)

    void _set_mode(const Mode);

    void _start_recording();
    void _stop_recording();
    void _clock_recording();
    void _set_buf_armed(const bool val);

    void _resolve_in_out_mix();

    void _quantize_loop(const float);
    
    void _resolve_playhead();
    
    const Event* _internal_event(const bool common_tick, const bool track_tick);
    void _dispatch();
    void _on_dispatcher_event_on(const uint8_t slice_idx, const Event* event);
    void _on_dispatcher_event_off(const uint8_t slice_idx);
    
    void _on_vox_stop(const uint8_t vox_idx);
    bool _needs_kickstart() const;

    Dispatcher<Generator::kVoxCount> _dispatcher;
    Detector        _detector;
    Buffer          _buffer;
    XFade           _inout_mix;
    Generator       _generator;
    Divider         _pattern_divider;
    Track           _track;
    Fx              _fx;
    OnePoleSmoother _mix_smooth;
    
    float _tempo;
    float _record_tempo;
    
    float _start_step_kof;

    float _in_out_mix;
    float _in_out_mix_offset;

    float _feedback;

    int16_t _loop_tick_count;
    int16_t _max_loop_ticks;
    int16_t _loop_ticks;
    int16_t _through_loop_ticks;
    uint8_t _active_slices_count;

    Mode _mode;
    Mode _pending_mode;

    bool _is_armed;  
    bool _is_play_queued;
    bool _is_record_queued;
    bool _is_playing;
    bool _adjust_count;
    bool _is_cut_queued;
};
};
