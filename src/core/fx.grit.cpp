#include "fx.grit.h"
#include "../common.h"

using namespace spotykach;
using namespace daisysp;

void Grit::init(const float sample_rate)
{
    _drive.init(sample_rate);
    _reduce.init(sample_rate);
    set_intensity(_drive.intensity());
    set_mix(_drive.mix());
}

void Grit::switch_mode()
{
    if (_mode == Mode::Drive) {
        _mode = Mode::Reduce;
    }
    else {
        _mode = Mode::Drive;
    }
}

float Grit::intensity()
{
    switch (_mode) {
        case Mode::Drive: return _drive.intensity();
        case Mode::Reduce: return _reduce.intensity();
        default: return 0.0f;
    }
}
void Grit::set_intensity(const float norm)
{
    auto clamp = fclamp(norm, 0.0f, 1.0f);
    switch (_mode) {
        case Mode::Drive: _drive.set_intensity(clamp); break;
        case Mode::Reduce: _reduce.set_intensity(clamp); break;
    }
}
float Grit::mix()
{
    switch (_mode) {
        case Mode::Drive: return _drive.mix();
        case Mode::Reduce: return _reduce.mix();
        default: return 0.f;
    }
}
void Grit::set_mix(const float norm)
{
    auto clamp = fclamp(norm, 0.0f, 1.0f);
    switch (_mode) {
        case Mode::Drive: _drive.set_mix(clamp); break;
        case Mode::Reduce: _reduce.set_mix(clamp); break;
    }
}

void Grit::process(float& inout0, float& inout1)
{
    switch (_mode) {
        case Mode::Drive: _drive.process(inout0, inout1); break;
        case Mode::Reduce: _reduce.process(inout0, inout1); break;
    }
}
