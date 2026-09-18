#pragma once

#include "nocopy.h"
#include "daisysp.h"
#include "../xfade.h"

namespace spotykach {

class Reduce {
public:

    Reduce();
    ~Reduce() = default;

    void init(const float sample_rate);

    void set_intensity(const float norm);
    float intensity() const { return _intensity; }

    void set_mix(const float norm);
    float mix() const { return _mix.Stage(); }

    void process(float& inout0, float& inout1);

private:
    void _apply();

    daisysp::Decimator          _decimator;
    daisysp::SampleRateReducer  _reducer;
    XFade                       _mix;

    float _intensity;
};

};
