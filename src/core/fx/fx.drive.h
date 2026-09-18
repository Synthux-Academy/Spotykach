#pragma once

#include "nocopy.h"
#include "daisysp.h"
#include "../xfade.h"

namespace spotykach {

class Drive {
public:
    
    Drive();
    ~Drive() = default;

    void init(const float sample_rate);

    float intensity() const { return _intensity; }
    void set_intensity(const float norm);
    
    float mix() const { return _mix.Stage(); }
    void set_mix(const float norm);

    void process(float& inout0, float& inout1);

private:

    void _apply();

    daisysp::Overdrive  _drive;
    XFade               _mix;

    float _intensity;
    float _attenuation;
};

};
