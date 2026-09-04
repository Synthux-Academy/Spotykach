#pragma once

#include "nocopy.h"
#include "biquad.h"
#include "../xfade.h"

namespace spotykach {

class Filter {
public:
    Filter();
    ~Filter() = default;

    void init(const float sample_rate);

    float cutoff() const { return _cutoff_norm; }
    void set_cutoff(const float norm);

    float q() const { return _q_norm; }
    void set_q(const float norm);

    float mix() const { return _mix.Stage(); }
    void set_mix(const float norm);

    void process(float& inout0, float& inout1);

private:
    NOCOPY(Filter)

    void _apply();

    infrasonic::LPF12   _lpf;
    infrasonic::HPF12   _hpf;
    XFade               _mix;

    float _cutoff_norm;
    float _q_norm;
};

};
