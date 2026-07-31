#include "fx.flux.h"
#include "../common.h"

using namespace spotykach;
using namespace infrasonic;
using namespace daisysp;

Flux::Flux():
_time_norm  { .5f },
_mix_norm   { .7f },
_fb_norm    { .5f },
_tempo_bpm  { 120.f },
_delay_mode { DelayMode::Free }
{}

void Flux::init(const float sample_rate, float** delay_buf)
{
    for (auto i = 0; i < 2; i++) {
        _echo_delay[i].Init(sample_rate, delay_buf[i]);
        _echo_delay[i].SetLagTime(0.5f);
    }
    _apply_time(true);
    _apply_mix();
    _apply_fb();
}

void Flux::set_time_norm(const float norm)
{
    _time_norm = fclamp(norm, 0.f, 1.f);
    _apply_time();
}
void Flux::_apply_time(const bool hard)
{
    auto map_int = fmap(_time_norm, 0.01f, 2.f);
    for (auto& d: _echo_delay) d.SetDelayTime(map_int, hard);
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

void Flux::set_delay_mode(const DelayMode mode)
{

}

void Flux::set_tempo_bpm(const float bpm)
{
    if (std::abs(bpm - _tempo_bpm) < 0.002f) return;
    //TODO: adjust time tempo based
}

void Flux::process(float& inout0, float& inout1, const float send)
{
    inout0 += _echo_delay[0].Process(inout0 * send) * _mix;
    inout1 += _echo_delay[1].Process(inout1 * send) * _mix;
}
