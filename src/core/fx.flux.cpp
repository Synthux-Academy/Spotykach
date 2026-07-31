#include "fx.flux.h"
#include "../common.h"

using namespace spotykach;
using namespace infrasonic;
using namespace daisysp;

Flux::Flux():
_int       { .5f },
_mix_norm  { .7f },
_fb        { .5f }
{}

void Flux::init(const float sample_rate, float** delay_buf)
{
    for (auto i = 0; i < 2; i++) {
        _echo_delay[i].Init(sample_rate, delay_buf[i]);
        _echo_delay[i].SetLagTime(0.5f);
    }
    _apply_int(true);
    _apply_mix();
    _apply_fb();
}

void Flux::set_intensity(const float norm)
{
    _int = fclamp(norm, 0.f, 1.f);
    _apply_int();
}
void Flux::_apply_int(const bool hard)
{
    auto map_int = fmap(_int, 0.01f, 2.f);
    for (auto& d: _echo_delay) d.SetDelayTime(map_int, hard);
}
void Flux::set_fb(const float norm)
{
    _fb = fclamp(norm, 0.f, 1.f);
    _apply_fb();
}
void Flux::_apply_fb()
{
    for (auto& d: _echo_delay) d.SetFeedback(_fb);
}
void Flux::set_mix(const float norm)
{
    _mix_norm = fclamp(norm, 0.0f, 1.0f);
    _apply_mix();
}
void Flux::_apply_mix()
{
    _mix = dbfs2lin(fmap(_mix_norm, -40.f, 0.f));
}

void Flux::process(float& inout0, float& inout1, const float send)
{
    inout0 += _echo_delay[0].Process(inout0 * send) * _mix;
    inout1 += _echo_delay[1].Process(inout1 * send) * _mix;
}
