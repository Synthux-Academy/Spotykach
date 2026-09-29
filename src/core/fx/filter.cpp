#include "filter.h"
#include "common.h"

using namespace spotykach;
using namespace infrasonic;
using namespace daisysp;

static constexpr float kLpfMinHz { 60.f };     // full cut
static constexpr float kLpfMaxHz { 20000.f };  // full open
static constexpr float kHpfMinHz { 20.f };     // full open
static constexpr float kHpfMaxHz { 4000.f };   // full cut

static constexpr float kFlatQ         { .70710678f }; // default, non-resonant response
static constexpr float kMaxQ          { 10.f };
static constexpr float kMaxResonance  { .9f };

Filter::Filter():
_cutoff_norm    { .5f },
_q_norm         { 0.f }
{}

void Filter::init(const float sample_rate)
{
    _lpf.Init(sample_rate);
    _hpf.Init(sample_rate);
    _mix.SetStage(1.f);
    _apply();
}

void Filter::set_cutoff(const float norm)
{
    _cutoff_norm = fclamp(norm, 0.f, 1.f);
    _apply();
}

void Filter::set_q(const float norm)
{
    _q_norm = fclamp(norm, 0.f, kMaxResonance);
    _apply();
}

void Filter::_apply()
{
    auto q = fmap(_q_norm, kFlatQ, kMaxQ, Mapping::EXP);
    _lpf.SetQ(q);
    _hpf.SetQ(q);

    if (_cutoff_norm < .5f) {
        _lpf.SetCutoff(fmap(_cutoff_norm * 2.f, kLpfMinHz, kLpfMaxHz, Mapping::LOG));
    }
    else {
        _hpf.SetCutoff(fmap((_cutoff_norm - .5f) * 2.f, kHpfMinHz, kHpfMaxHz, Mapping::LOG));
    }
}

void Filter::set_mix(const float norm)
{
    _mix.SetStage(norm);
}

void Filter::process(float& inout0, float& inout1)
{
    float wet0 = inout0;
    float wet1 = inout1;

    if (_cutoff_norm < .5f) {
        _lpf.ProcessStereo(wet0, wet1);
    }
    else {
        _hpf.ProcessStereo(wet0, wet1);
    }

    _mix.Process(inout0, inout1, wet0, wet1, inout0, inout1);
    inout0 = SoftLimit(inout0);
    inout1 = SoftLimit(inout1);
}
