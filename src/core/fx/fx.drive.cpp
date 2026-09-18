#include "fx.drive.h"
#include "common.h"
#include "expose.h"

using namespace spotykach;
using namespace infrasonic;
using namespace daisysp;

static float attenuation_for_intensity(const float intensity)
{
    return dbfs2lin(fmap(intensity, -3.f, -24.f));
}

Drive::Drive():
_intensity      { .2f },
_attenuation    { attenuation_for_intensity(.35f) }
{}

void Drive::init(const float sample_rate) 
{
    _drive.Init();
    _drive.SetDrive(0.0f); 
    _mix.SetStage(0.33f);
    _apply();
}

void Drive::set_intensity(const float norm)
{
    _intensity = norm;
    _apply();
}

void Drive::_apply() 
{
    _attenuation = attenuation_for_intensity(_intensity);
    _drive.SetDrive(dbfs2lin(fmap(_intensity, -6.f, 0.f)));
}

void Drive::set_mix(const float norm)
{
    _mix.SetStage(norm);
}

void Drive::process(float& inout0, float& inout1)
{   
    float driv0 = _drive.Process(inout0) * _attenuation;
    float driv1 = _drive.Process(inout1) * _attenuation;
    _mix.Process(inout0, inout1, driv0, driv1, inout0, inout1);
}
