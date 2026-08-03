#pragma once

#include "nocopy.h"
#include "fx.grit.h"
#include "fx.flux.h"
#include "../softswitch.h"

namespace spotykach {

class Fx {
public:
    struct Params {
        float sample_rate;
        float** delay_buf;
    };

    static constexpr size_t kEchoDelayBufferLength { Flux::kEchoDelayBufferLength };

    Fx();
    ~Fx() = default;

    void init(const Params);
    void process(float& inout0, float& inout1);

    // GRIT ///////////////////////////////////////////
    Grit& grit() { return _grit; }

    bool is_grit_on() const { return _grit_switch.is_on(); }
    void set_grit_on(const bool);
    void toggle_grit_lock();

    // FLUX ///////////////////////////////////////////
    Flux& flux() { return _flux; }

    bool is_flux_on() const { return _flux_switch.is_on(); }
    void set_flux_on(const bool);
    void toggle_flux_lock();

private:
    NOCOPY(Fx)

    Grit _grit;
    Flux _flux;

    SoftSwitch _flux_switch;
    SoftSwitch _grit_switch;

    bool _flux_on;
    bool _flux_lock;

    bool _grit_on;
    bool _grit_lock;
};

};
