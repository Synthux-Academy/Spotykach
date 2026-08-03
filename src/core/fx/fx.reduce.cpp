#include "fx.reduce.h"
#include "common.h"

using namespace spotykach;
using namespace infrasonic;
using namespace daisysp;

Reduce::Reduce():
_intensity  { 0.55f }
{}

void Reduce::init(const float sample_rate) 
{
    _decimator.Init();
    _decimator.SetBitsToCrush(16);
    _reducer.Init();
    _reducer.SetFreq(0.6f);
    _mix.SetStage(.5f);
    _apply();
}

void Reduce::set_intensity(const float norm)
{
    _intensity = norm;
    _apply();
}

void Reduce::_apply()
{
    _decimator.SetDownsampleFactor(_intensity);
    _decimator.SetBitcrushFactor(fmap(1.0f - _intensity, .5f, .7f, Mapping::EXP));
}

void Reduce::set_mix(const float norm)
{
    _mix.SetStage(norm);
}

void Reduce::process(float& inout0, float& inout1)
{    
    auto redux0 = _reducer.Process(_decimator.Process(inout0)); 
    auto redux1 = _reducer.Process(_decimator.Process(inout1));
    _mix.Process(inout0, inout1, redux0, redux1, inout0, inout1);
}
