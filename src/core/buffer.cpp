#include "buffer.h"
#include <cstring>
#include "hann.h"
#include "expose.h"
#include "daisysp.h"
#include "common.h"

using namespace spotykach;

Buffer::Buffer():
_buffer         { nullptr },
_buffer_size    { 0 },
_feedback       { 0.95 }, //-3dB
_size           { 0 },
_write_head     { 0 },
_read_head      { 0 },
_fade_counter   { 0 },
_state          { State::idle }
{};

void Buffer::init(Frame* buf, size_t length) 
{
    _buffer = buf;
    _buffer_size = length;
    _buffer_size_kof = 1.f / length;
    _cut_switch.init(48000);
};

void Buffer::set_recording(const bool is_rec_on) 
{
    switch (_state) {
        case State::idle: 
            if (is_rec_on) {
                _write_head = _read_head - std::min(size_t(1), _read_head);
                _state = State::fadein;
                _fade_counter = 0;
            }
            break;
        
        case State::fadeout: break;
        
        default:
            if (!is_rec_on) {
                _state = State::fadeout;
                //wrap around and fade out
                if (!_cut_switch.is_on()) _write_head = 0;
            }
    }
};

void Buffer::set_feedback(const float val) 
{ 
    auto dbfs = 60.f * (val - 1.f);
    _feedback = daisysp::pow10f(dbfs * 0.05f);
};

void Buffer::set_rec_size(const size_t value)
{
    _size = value;
    cut();
};

void Buffer::cut()
{
    if (_cut_switch.is_on()) return;
    _cut_switch.set_on(true);
    _did_cut = true;
    _write_head = 0;
};

bool Buffer::read_reset_did_cut()
{
    if (!_did_cut) return false;
    _did_cut = false;
    return true;
};

void Buffer::clear() 
{
    auto size = sizeof(Frame) * _buffer_size;
    std::memset(_buffer, 0, size);
    _write_head = 0;
    _read_head = 0;
    _size = 0;
    _cut_switch.set_on(false, true);
    _state = State::idle;
};

void Buffer::read_linear(const Phasor& frame, float& out0, float& out1) 
{
    if (_size == 0) {
        out0 = out1 = 0.f;
        return;
    }

    // Wrap integer part into [0, size)
    const auto size = static_cast<int32_t>(_size);
    auto int_fr = frame.integral() % size;
    if (int_fr < 0) int_fr += size;

    auto next_fr = int_fr + 1;
    if (next_fr == size) next_fr = 0;

    // Fractional part has constant precision regardless of the frame position
    auto frac_fr = frame.fraction();

    auto a = _buffer[int_fr];
    auto n = _buffer[next_fr];

    out0 = a.l + frac_fr * (n.l - a.l);
    out1 = a.r + frac_fr * (n.r - a.r);

    _read_head = next_fr;
};

void Buffer::write(const float in0, const float in1) 
{
    auto fade = 1.f;
    switch (_state) {
        case State::idle: return;
        case State::sustain: break;
        case State::fadein:
            fade = bleeptools::Hann::win().point(_fade_counter * kFadeCurveKof);
            if (++_fade_counter == kRecordFade - 1) _state = State::sustain;
            break;

        case State::fadeout:
            fade = bleeptools::Hann::win().point(_fade_counter * kFadeCurveKof);
            if (--_fade_counter == 0) {
                cut();
                _state = State::idle;
                return;
            }
    }

    auto fb = infrasonic::map(_cut_switch.process(), 0.f, 1.f, 1.f, _feedback);
    auto fb_fade = std::clamp(1.f - fade * (1.f - fb), 0.f, 1.f);

    // Write buffer
    auto f = _buffer[_write_head];
    f.l = in0 * fade + f.l * fb_fade;
    f.r = in1 * fade + f.r * fb_fade;
    _buffer[_write_head] = f;

    // Advance write head
    _write_head ++;
    if (_cut_switch.is_on()) {
        if (_write_head >= _size) _write_head = 0;
    } 
    else {
        if (_write_head >= _buffer_size) {
            _size = _buffer_size;
            cut();
        }
        else {
            _size = std::max(_size, _write_head);
        }
    }
};
