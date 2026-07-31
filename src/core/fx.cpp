#include "fx.h"
#include "../common.h"

using namespace spotykach;
using namespace infrasonic;
using namespace daisysp;

Fx::Fx():
_flux_on        { false },
_flux_lock      { false },
_grit_on        { false },
_grit_lock      { false }
{}

void Fx::init(const Params p)
{
    _flux_switch.init(p.sample_rate);
    _flux.init(p.sample_rate, p.delay_buf);

    _grit_switch.init(p.sample_rate);
    _grit.init(p.sample_rate);
}

void Fx::set_grit_on(const bool on)
{
    _grit_on = on;
    if (_grit_lock) return;
    _grit_switch.set_on(_grit_on);
}
void Fx::toggle_grit_lock()
{
    _grit_lock = !_grit_lock;
    _grit_switch.set_on(_grit_on || _grit_lock);
}

void Fx::set_flux_on(const bool on)
{
    _flux_on = on;
    if (_flux_lock) return;
    _flux_switch.set_on(_flux_on);
}
void Fx::toggle_flux_lock()
{
    _flux_lock = !_flux_lock;
    _flux_switch.set_on(_flux_on || _flux_lock);
}

void Fx::process(float& inout0, float& inout1)
{
    float out[2] = { inout0, inout1 };

    auto grit_kof = _grit_switch.process();
    if (!_grit_switch.is_idle()) {
        float grit[2] = { inout0, inout1 };
        _grit.process(grit[0], grit[1]);
        out[0] = grit[0] * grit_kof + out[0] * (1.f - grit_kof);
        out[1] = grit[1] * grit_kof + out[1] * (1.f - grit_kof);
    }

    auto send = _flux_switch.process();
    _flux.process(out[0], out[1], send);

    inout0 = out[0];
    inout1 = out[1];
}
