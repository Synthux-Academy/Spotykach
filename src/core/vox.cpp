#include "vox.h"
#include "expose.h"

using namespace spotykach;

Vox::Vox():
    _buffer             { nullptr },
    _next_inetrval      { 240 },
    _interval_count     { 0 },
    _window_size        { kDefaultWindowSize },
    _att                { 1.f },
    _playhead_shift     { 0.f },
    _playhead_increment { 1.f },
    _envelope_increment { 1.f },
    _start              { 0.f },
    _size               { kSliceMinSize },
    _spread             { kSliceMinSize },
    _full_size          { 2 * kWindowSlope },
    _iterator           { 0 },
    _decay_start        { 0 },
    _slope_counter      { 0 },
    _mode               { Mode::Linear },
    _speed_mode         { SpeedMode::Tape },
    _state              { State::idle },
    _max_win_count      { 2 },
    _win_count          { 0 },
    _is_reverse         { false },
    _is_pending         { false },
    _is_suspended       { false }
    {};

void Vox::init(Buffer* buffer, const uint8_t vox_idx) {
    _buffer = buffer;
    _vox_idx = vox_idx;
    _xfade.SetStage(.5f);
}

void Vox::set_mode(const Mode mode)
{
    if (mode == _mode) return;
    _mode = mode;
    
    switch (_mode) {
        case Mode::Linear:
        _max_win_count = 2;
        _window_size = kDefaultWindowSize;
        break;

        case Mode::Spread:
        _max_win_count = _wins.size();
        _win_count = 0;
        _env.reset();
        _iterator = 0;
        break;
    }
}

void Vox::trigger() {
    _win_count = 0;
    if (_state == State::idle) {
        _do_trigger();
    }
    else {
        _is_pending = true;
        _decay();
    }
}

void Vox::stop() {
    switch (_state) {
        case State::idle:
        case State::decay: return;
        default: _decay();
    }
}

void Vox::_decay() {
    if (_state != State::decay && _state != State::idle) {
        _state = State::decay;
    }
}

void Vox::process(float& out0, float& out1) {
    out0 = 0.f;
    out1 = 0.f;
    _att = bleeptools::Hann::win().point(_slope_counter * kSlopeKof);
    switch (_state) {
    case State::idle: return;
    case State::attack:
        if (++_slope_counter == kSliceSlope - 1) {
            _state = State::sustain;
        }
        break;

    case State::decay:
        if (--_slope_counter <= 0) {
        _stop();
        return;
        }
        break;

    case State::sustain:
        for (auto& w: _wins) {
            _check_window(w);
        }
        break;
    }

    if (_mode == Mode::Spread) {
        _seed();
    }

    auto w_out0 = 0.f;
    auto w_out1 = 0.f;
    if (_playhead_increment > 0) {
        for (auto& w: _wins) {
            if (!w.is_active()) continue;
            w_out0 = 0.f;
            w_out1 = 0.f;
            w.process(_buffer, w_out0, w_out1);
            out0 += w_out0;
            out1 += w_out1;
        }
        if (!_is_suspended) {
            _att *= _env.process(_mode == Mode::Linear ? _envelope_increment : 1.f);
            _iterator ++;
        }
        if (_mode == Mode::Spread) _att *= 0.5f;
        out0 *= _att;
        out1 *= _att;
    }   
}

void Vox::_check_window(Window& w)
{
    if (!w.is_active()) return;
    float slice_playhead = 0;
    float win_playhead = 0;
    switch (_mode) {
    case Mode::Linear: {
            win_playhead = 
            slice_playhead = _speed_mode == SpeedMode::Tape ? w.play_head() : w.steady_playhead();
        }
        break;
    case Mode::Spread: {
            win_playhead = 0;
            slice_playhead = _iterator;
        }
        break;
    }
    if (!_is_suspended && slice_playhead >= _decay_start) {
        _decay();
    }
    if (w.is_done()) {
        if (_win_count > 0) _win_count --;
        if (_mode == Mode::Linear && _win_count == 0) _activate(win_playhead);
    }
}

void Vox::_seed()
{
    if (is_playing() && _win_count < _max_win_count && _interval_count >= _next_inetrval) {
        _activate(0);
        _next_inetrval = static_cast<size_t>(_window_size * _rnd() * .1f + 96);
        _interval_count = 0;
    }
    else {
        _interval_count ++;
    }
}

void Vox::_activate(float playhead, const bool is_first) 
{
    for (auto& w: _wins) {
        if (!w.is_active()) {
            w.set_is_first(is_first);

            // Upon start the first window starts with zero...
            if (_playhead_shift != 0 && playhead != 0) {
                //... afterwards every window gets shifted by delta
                if (_is_reverse) playhead -= _playhead_shift;
                else playhead += _playhead_shift;
            }

            Window::Params p;
            p.start = playhead;
            p.size = _window_size;
            p.increment = _playhead_increment;
            p.loop_length = _size;
            p.pan = _is_wide && _mode == Mode::Spread ? _rnd() : .5f;
            switch (_mode) {
                case Mode::Spread: {
                    p.loop_start = _start + _dice(_rand) * _spread * .5f;
                    if (p.loop_start < 0) p.loop_start += _full_size;
                    p.interp = Window::Interpolation::linear;
                    p.pos = Window::Position::relative;
                    break;
                }

                default:
                    p.loop_start = _start;
                    p.interp = Window::Interpolation::cubic;
                    p.pos = Window::Position::absolute;
            }
            w.activate(p);
            _win_count ++;
            return;
        }
    }
}

float Vox::playhead() const
{ 
    for (auto& w : _wins) {
        if (w.is_active()) return w.readhead();
    } 
    return 0.f;
}

float Vox::envelope() const
{
    return is_playing() ? _att : -1.f;
}

void Vox::set_playhead_increment(const float value) 
{ 
    auto increment = std::clamp(value, 0.f, 8.f); // 8 = 2^36/12
    for (auto& w: _wins) w.set_increment(increment);
    _playhead_increment = increment; 
    _set_decay_start();
}

void Vox::set_reverse(const bool value) 
{ 
    if (value != _is_reverse) {
        _is_reverse = value;
        for (auto& w: _wins) w.set_reverse(_is_reverse);

        if (_is_suspended) return;

        if (_mode == Mode::Linear) {
            if (!value && _state == State::decay) {
                _state = State::sustain;
            }
            _env.reverse();
        }
    }
}

void Vox::set_size(const int32_t size) 
{
    _size = size;
    _env.set_length(size);
    _set_decay_start();
}

float Vox::_rnd() 
{
    return std::clamp(_dice(_rand) * .5f + .5f, 0.f, 1.f);
}

void Vox::set_shape(const float norm) 
{
    _env.set_shape(norm);
    auto is_suspended = _mode == Mode::Spread && norm < 0.05;
    if (_is_suspended && !is_suspended) {
        _iterator = 0;
        _env.reset();
    }
    _is_suspended = is_suspended;
}

void Vox::set_win_size(const float norm) 
{
    if (_mode == Mode::Spread) {
        _window_size = 2880 + static_cast<size_t>(norm * 21120); //max 500ms @48K 1x
    }
}

void Vox::_do_trigger() 
{
    _iterator = 0;
    _slope_counter = 0;
    _activate(0, true);
    _env.trigger();
    _state = State::attack;
}

void Vox::_set_decay_start() {
    auto decay_length = kSliceSlope * _playhead_increment;
    if (decay_length < _size) {
        _decay_start = _size - decay_length;
    }
}

void Vox::_stop() 
{
    _state = State::idle;
    _env.reset();
    _xfade.SetStage(.5f);
    for (auto& w: _wins) w.deactivate();
    _win_count = 0;
    _interval_count = 0;
    if (_is_pending) {
        _do_trigger();
        _is_pending = false;
    }
}
