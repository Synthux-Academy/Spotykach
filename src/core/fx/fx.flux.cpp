#include "fx.flux.h"
#include "common.h"

using namespace spotykach;
using namespace infrasonic;
using namespace daisysp;

Flux::Flux():
_time_norm  { .5f },
_mix_norm   { .7f },
_fb_norm    { .5f },
_tempo_bpm  { 120.f },
_mode { Mode::FreeDelay }
{}

void Flux::init(const float sample_rate, float** delay_buf)
{
    for (auto i = 0; i < 2; i++) {
        _echo_delay[i].Init(sample_rate, delay_buf[i]);
        _echo_delay[i].SetLagTime(0.5f);
    }
    _reverb.init(sample_rate);

    _apply_time(true);
    _apply_mix();
    _apply_fb();
}

void Flux::set_time_norm(const float norm)
{
    _time_norm = fclamp(norm, 0.f, 1.f);
    _apply_time();
}
static constexpr std::array<float, 9> clocked_dly_kof = {
    .125f,  // 1/32
    .25f,   // 1/16,
    .333f,  // 1/8T
    .5f,    // 1/8,
    .5f,    // 1/8,
    .667f,  // 1/4T
    1.f,    // 1/4,
    2.f,    // 1/2
    4.f     // 4/4
};
void Flux::_apply_time(const bool hard)
{
    auto time_s = .01f;
    switch (_mode) {
        case Mode::FreeDelay: {
            time_s = fmap(_time_norm, 0.01f, 2.f); 
            break;
        }
        case Mode::ClockedDelay: {
            auto idx = std::round(_time_norm * (clocked_dly_kof.size() - 1));
            auto kof = clocked_dly_kof[idx];
            time_s = kof * (60.f / _tempo_bpm);
            break;
        }
        case Mode::Reverb: {
            _reverb.set_feedback_norm(_time_norm * .9f);
            return;
        }
    }

    for (auto& d: _echo_delay) d.SetDelayTime(time_s, hard);
}
void Flux::set_fb_norm(const float norm)
{
    _fb_norm = fclamp(norm, 0.f, 1.f);
    _apply_fb();
}
void Flux::_apply_fb()
{
    for (auto& d: _echo_delay) d.SetFeedback(_fb_norm);
}
void Flux::set_mix_norm(const float norm)
{
    _mix_norm = fclamp(norm, 0.0f, 1.0f);
    _apply_mix();
}
void Flux::_apply_mix()
{
    _mix = dbfs2lin(fmap(_mix_norm, -40.f, 0.f));
}

void Flux::switch_mode()
{
    auto mode_count = 3;
    Mode modes[mode_count] = { Mode::FreeDelay, Mode::ClockedDelay, Mode::Reverb };
    for (auto i = 0; i < mode_count; i++) {
        if (_mode == modes[i]) {
            _set_mode(modes[(i + 1) % mode_count]);
            return;
        }
    }
}

void Flux::_set_mode(const Mode mode)
{
    _mode = mode;
    switch (mode) {
        case Mode::FreeDelay: {
            for (auto& d: _echo_delay) d.SetLagTime(.5f);
            _apply_time();
            break;
        }
        case Mode::ClockedDelay: {
            for (auto& d: _echo_delay) d.SetLagTime(.01f);
            _apply_time();
            break;
        }
        case Mode::Reverb: {
            _apply_time();
            break;
        }
    }
}

void Flux::set_tempo_bpm(const float bpm)
{
    if (std::abs(bpm - _tempo_bpm) < 0.002f) return;
    _tempo_bpm = bpm;
    if (_mode == Mode::ClockedDelay) _apply_time();
}

void Flux::process(float& inout0, float& inout1, const float send)
{
    if (_mode == Mode::Reverb) {
        float wet0 = inout0 * send;
        float wet1 = inout1 * send;
        _reverb.process(wet0, wet1);
        inout0 += wet0 * _mix;
        inout1 += wet1 * _mix;
        return;
    }

    inout0 += _echo_delay[0].Process(inout0 * send) * _mix;
    inout1 += _echo_delay[1].Process(inout1 * send) * _mix;
}
