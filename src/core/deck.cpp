#include "deck.h"
#include "config.h"
#include <functional>

#include "expose.h"

using namespace spotykach;

// Setup ////////////////////////////////////////
Deck::Deck():
_pattern_divider     { Divider(kPPQNIntern, Every::_32th) },
_tempo               { 0.43f },
_record_tempo        { 0.f },
_start_step_kof      { 1.f },
_in_out_mix          { .5f },
_in_out_mix_offset   { 0.f },
_feedback            { kDefaultFeedback },
_loop_tick_count     { 0 },
_max_loop_ticks      { 0 },
_loop_ticks          { 0 },
_through_loop_ticks  { 0 },
_active_slices_count { 0 },
_mode                { Mode::None },
_pending_mode        { Mode::None },
_is_armed            { false },
_is_play_queued      { false },
_is_record_queued    { false },
_is_playing          { false },
_adjust_count        { false }
{};
void Deck::init(const Params p) 
{
    _start_step_kof = 30.f * p.sample_rate; // 1/8

    _mix_smooth.init(p.sample_rate);

    _buffer.init(p.main_buf, p.main_buf_size);
    _detector.init(p.detect_buf);

    _generator.ref = ref;
    _generator.init(&_buffer, p.slice_buf);
    
    _track.init(p.track_buf);

    Fx::Params fxp;
    fxp.sample_rate = p.sample_rate;
    fxp.delay_buf = p.delay_buf;
    _fx.init(fxp);

    using namespace std::placeholders;

    auto on_vox_will_stop = std::bind(&Deck::_on_vox_stop, this, _1);
    _generator.set_on_vox_stop(on_vox_will_stop);

    auto on_event_on = std::bind(&Deck::_on_dispatcher_event_on, this, _1, _2);
    _dispatcher.set_on_event_on(on_event_on);

    auto on_event_off = std::bind(&Deck::_on_dispatcher_event_off, this, _1);
    _dispatcher.set_on_event_off(on_event_off);
};
void Deck::set_mode(const Mode new_mode)
{
    if (new_mode == _mode || new_mode == _pending_mode) return;
    if (_generator.is_generating()
        && _mode == Mode::Slice 
        && new_mode == Mode::Drift 
        && _pending_mode == Mode::None) {
        _pending_mode = new_mode;
        _dispatcher.all_off();
        return;
    }
    _set_mode(new_mode);
};
void Deck::_set_mode(const Mode new_mode)
{
    _mode = new_mode;
    _pending_mode = Mode::None;

    auto& g = _generator;

    switch (_mode) {
        case Mode::Reel:
            _dispatcher.set_mode(DispatcherMode::Mono);
            g.set_mode(Vox::Mode::Linear);
            g.set_speed_mode(SpeedMode::Tape);
            g.apply_speed();
            g.apply_shape();
            g.set_snap_to_cue(false);
            g.set_start_mod_cont(true);
            g.set_pitch_mod_cont(true);
            if (_needs_kickstart()) _dispatch();
            break;

        case Mode::Slice:
            _dispatcher.set_mode(DispatcherMode::Poly);
            g.set_mode(Vox::Mode::Linear);
            g.set_speed_mode(SpeedMode::Digital);
            g.apply_speed();
            g.apply_pitch();
            g.apply_shape();
            g.set_snap_to_cue(true);
            g.set_start_mod_cont(false);
            g.set_pitch_mod_cont(false);
            make_grid();
            _resolve_playhead();
            break;
        
        case Mode::Drift:
            _dispatcher.set_mode(DispatcherMode::Mono);
            g.set_mode(Vox::Mode::Spread);
            g.set_speed_mode(SpeedMode::Tape);
            g.apply_speed();
            g.apply_shape();
            g.set_snap_to_cue(false);
            g.set_start_mod_cont(true);
            g.set_pitch_mod_cont(true);
            break;

        case Mode::None: break;
    }
}

// Record /////////////////////////////////////
void Deck::toggle_recording() 
{
    if (is_overdubbing()) { // playing + recording -> stop recording
        _set_buf_armed(false);
    }
    else if (is_playing()) { // playing, not recording -> go to overdub
        _is_armed = true;
        switch (_mode) {
            case Mode::Slice: _is_record_queued = true; break;
            default: _start_recording();
        }
    }
    else if (is_recording()) { //recording, not playing -> cut and go to overdub
        switch (_mode) {
            case Mode::Slice: _is_record_queued = true; break; // on next quarter, see _clock_recording
            default: _is_cut_queued = true;
        }
    }
    else if (!is_armed()) { //not playing, not recording, not armed -> arm for recording
        _set_buf_armed(true);
    }
}
void Deck::set_feedback(const float value) { 
    auto val = std::clamp(value * 1.05f, 0.f, 1.f);
    if (val < 0.02f) val = 0.f;
    _buffer.set_feedback(val);
}
void Deck::_set_buf_armed(const bool on) 
{
    _is_armed = on;
    if (on) {
        _buffer.clear();
        _generator.clear_cue();
        switch (_mode) {
            case Mode::Reel: _detector.set_armed(true); break;
            case Mode::Slice: _is_record_queued = true; break;
            case Mode::Drift: _detector.set_armed(true); break;
            case Mode::None: break;
        }
    }
    else if (_mode == Mode::Reel || _mode == Mode::Drift) {
        _stop_recording();
    }  
};
void Deck::_clock_recording() 
{
    if (is_recording()) {
        if (!is_armed()) {
            _stop_recording();
        }
        else if (_is_record_queued) {
            _is_record_queued = false;
            _is_cut_queued = true;
            make_grid();
        }    
    }
    else if (_is_record_queued) {
        _is_record_queued = false;
        _buffer.set_recording(true);
    }
};
void Deck::_start_recording() 
{
    _buffer.set_recording(true);
}
void Deck::_stop_recording() 
{
    if (_mode == Mode::Slice) make_grid();
    _detector.set_armed(false);
    _buffer.set_recording(false);
};

// Clock /////////////////////////////////////////
void Deck::set_tempo(const float value) 
{
    _tempo = value;
    if (_generator.is_speed_mode(SpeedMode::Digital) && _record_tempo > 0) {
        _generator.set_speed(value / _record_tempo);
    }
};
float Deck::tempo_to_fit(const float frac)
{
    if (_buffer.is_empty()) return _tempo;
    auto bpm = 2880000 * (1 + round(frac * 15)) / _buffer.rec_size();
    _tempo = bpm;
    _record_tempo = bpm;
    make_grid();
    return bpm;
}
void Deck::tick(const bool common_tick, const bool is_key) 
{
    // This method is called at the PPQN of the clock (48 at the moment of writing)
    // common_tick denotes the one that common upsteram divider produces - it's effectively 1/16th.
    if (common_tick && is_key) {
        switch (_mode) {
            case Mode::Slice: {
                _clock_recording();
                if (_is_play_queued) play();
            }
            break;

            default:
                if (!_track.is_empty() && _is_play_queued) play();
        }
        
    }

    auto track_tick = false;
    if (_pattern_divider.tick() && ((_is_playing && !_track.is_empty()) || _track.is_recording() || _track.is_armed())) {
        _track.tick(is_key);
        track_tick = true;
    }
    auto e = _internal_event(common_tick, track_tick);
    if (e != nullptr && e->on && (!_generator.is_suspended() || !_generator.is_generating())) {
        auto hold = true;
        if (_mode == Mode::Slice) {
            hold = Config::dynamic().is_slice_mono(ref) || (_track.is_empty() && common_tick);
        }
        _dispatcher.event_on(e, hold);
    }
}

// Size /////////////////////////////////////////
void Deck::make_grid()
{   
    _record_tempo = _tempo;
    if (is_overdubbing()) return;
    auto ticks = _buffer.rec_size() * _tempo / 720000.f; //4PPQN
    _max_loop_ticks = _mode == Mode::Slice ? std::round(ticks) : ticks;
    _loop_tick_count = -1;
    _through_loop_ticks = -1;
    _generator.auto_cue(_start_step_kof / _record_tempo, _max_loop_ticks * .5f); //_max_loop_ticks are in 16ths
}
void Deck::_quantize_loop(const float norm_size) 
{
    if (_max_loop_ticks <= 0) return;
    auto loop_ticks = static_cast<int16_t>(std::round(norm_size * _max_loop_ticks));
    loop_ticks = std::min(_max_loop_ticks, loop_ticks);
    if (!is_recording() && loop_ticks == _max_loop_ticks && _loop_ticks < _max_loop_ticks) {
        _adjust_count = true;
    }
    _loop_ticks = loop_ticks;
}

// Play /////////////////////////////////////////
void Deck::toggle_play() 
{   
    if (_is_playing) {
        stop();
        return;
    }

    // Several times I've noticed I accidentally tap play to end sequence recording
    // this is to accomodate for such case - it's going to stop recording.
    if (_track.is_armed()) _track.disarm(!_track.is_recording());

    if (_buffer.is_empty()) return;

    if (!_track.is_empty() || _mode == Mode::Slice) {
        _is_play_queued = true;
        return;
    }

    play();
};
void Deck::play() {
    _is_play_queued = false;
    _is_playing = true;

    if (!_track.is_empty()) return; // going to be triggered from tick method
    switch (_mode) {
        case Mode::Reel:
            _dispatch(); 
            break;

        case Mode::Drift: 
            if (!_generator.is_generating()) {
                _dispatch(); 
            }
            break;

        case Mode::Slice: // going to be triggered from tick method
        case Mode::None: break;
    }
};
void Deck::stop()
{
    _is_playing = false;
    _loop_tick_count = -1;
    _through_loop_ticks = -1;
    _generator.reset_start_offset();
    _dispatcher.all_off();
    _track.rewind();
}
void Deck::set_reverse(const bool val) {
    if (val == _generator.is_reverse()) return;
    _generator.set_reverse(val);
    if (_mode == Mode::Slice) {
        _loop_tick_count = _loop_ticks - _loop_tick_count;
    }
}
float Deck::norm_playhead_at(const uint8_t idx) const
{
    if (_buffer.is_empty()) return 0.f;
    return _generator.playhead_at(idx) / _buffer.rec_size();
}
void Deck::_resolve_playhead() 
{
    auto count = _max_loop_ticks * static_cast<float>(_buffer.read_head()) / static_cast<float>(_buffer.rec_size());
    _loop_tick_count = count;
    _through_loop_ticks = count;
}

// Render ///////////////////////////////////////////
void Deck::prepare()
{
    _generator.apply_dimensions();
    if (_mode == Mode::Slice) _quantize_loop(_generator.norm_size());
}
void Deck::process_out(const float in0, const float in1, float& out0, float& out1) 
{
    float bus[2] = { 0.f, 0.f};

    _generator.process(bus[0], bus[1]);

    _resolve_in_out_mix();
    _inout_mix.Process(in0, in1, bus[0], bus[1], out0, out1);

    _fx.process(out0, out1);
};
void Deck::process_in(const float in0, const float in1)
{
    auto src0 = in0;
    auto src1 = in1;
    if (_detector.is_active()) {
        _detector.process(in0, in1, src0, src1);
        if (_detector.is_open()) {
            _start_recording();
        }
    }

    _buffer.write(src0, src1);

    if (_is_cut_queued) {
        _is_cut_queued = false;
        _buffer.cut();
    }

    if (_buffer.read_reset_did_cut() && _buffer.is_recording()) {
        _is_cut_queued = false;
        switch (_mode) {
            case Mode::Slice: make_grid(); _is_play_queued = true; break;
            default: play(); break;
        }
    }
}
void Deck::set_inout_mix(const float val) {
    _in_out_mix = std::clamp(val * 1.03f, 0.f, 1.f);
}
void Deck::inout_mix_mod_in(const float val) { 
    if (std::abs(val) < 0.01) _in_out_mix_offset = 0;
    else _in_out_mix_offset = val;
}
void Deck::_resolve_in_out_mix() {
    auto mix = _mix_smooth.process(_in_out_mix + _in_out_mix_offset);
    _inout_mix.SetStage(std::clamp(mix, 0.f, 1.f));
}

// Trigger //////////////////////////////////////////
void Deck::trigger(Event* event) 
{
    if (is_empty() 
    || (is_recording() && !is_overdubbing()) 
    || _generator.is_suspended()
    || _pending_mode != Mode::None) return;

    /* Record to track */
    if (_track.is_armed() || _track.is_recording()) {
        if (_mode == Mode::Slice && _track.is_empty()) {
            _dispatcher.all_off();
        }
        _track.add_event(event);
    }
    auto is_mono = Config::dynamic().is_slice_mono(ref);
    if (_mode == Mode::Slice && is_mono) {
        _loop_tick_count = 0;
    }   
    _dispatcher.event_on(event, _mode != Mode::Slice || is_mono);
};
void Deck::clear_sequence() {
    _track.disarm(true);
    _track.clear();
    _dispatcher.all_off();
    if (_is_playing) _dispatch();
}
void Deck::_on_vox_stop(const uint8_t vox_idx) 
{   
    if (_mode == Mode::Slice) {
        /* if switching to Drift is pending. */
        if (!_generator.is_generating() && _pending_mode != Mode::None) {
            _set_mode(_pending_mode);
            _dispatch();
        }
    }
    else if (_needs_kickstart()) {
        /* that's where the looping in Reel and Drift is happening */
        _dispatch();
    }
};
void Deck::_dispatch()
{
    auto e = make_event();
    _dispatcher.event_on(&e, true);
}
void Deck::_on_dispatcher_event_on(const uint8_t vox_idx, const Event* event) 
{
    _generator.trigger(vox_idx, event);
};
void Deck::_on_dispatcher_event_off(const uint8_t vox_idx) 
{
    _generator.stop(vox_idx);
};
const Event* Deck::_internal_event(const bool common_tick, const bool track_tick) 
{
    if (!_is_playing) return nullptr;

    // Sequenced ////////////////////////////////////
    Event* event = nullptr;
    if (track_tick) event = _track.event();

    // Clocked //////////////////////////////////////
    if (_mode == Mode::Slice && common_tick) {
        if (++_through_loop_ticks >= _max_loop_ticks) {
            _through_loop_ticks = 0;
        }
        /* that's where the looping in Slice is happening */
        auto kickstart = _loop_tick_count < 0 || _needs_kickstart();
        if (_adjust_count) {
            if (!_generator.start_offset_interval()) {
                _loop_tick_count = _through_loop_ticks;
            }
            _adjust_count = false;
        }
        else {
            _loop_tick_count++;
        }
        if (_loop_tick_count >= _loop_ticks || kickstart) {
            _loop_tick_count = 0;
            if (_track.is_empty()) {
                static Event e = make_event();
                event = &e;
            }
        }
    }
    return event;
    ////////////////////////////////////////////////////
};
bool Deck::_needs_kickstart() const
{
    /* 
    Case 1: 
    Normal looping. 
    
    Case 2: 
    Due to the scheduling, particularly in slice mode,
    there might be a situation when the loop size
    and number of loop counts deviate the way that
    causes silence.

    Case 3: 
    A rare edge case while switching Slice -> Reel.
    */
    return _is_playing && !_generator.is_generating();
}
