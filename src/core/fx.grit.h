#pragma once

#include "../nocopy.h"
#include "fx.drive.h"
#include "fx.reduce.h"

namespace spotykach {

class Grit {
public:
    enum class Mode: uint8_t {
        Drive,
        Reduce
    };

    Grit() = default;
    ~Grit() = default;

    void init(const float sample_rate);
    void process(float& inout0, float& inout1);

    Mode mode() const { return _mode; }
    void switch_mode();

    float intensity();
    void set_intensity(const float norm);

    float mix();
    void set_mix(const float norm);

private:
    NOCOPY(Grit)

    Drive _drive;
    Reduce _reduce;

    Mode _mode { Mode::Drive };
};

};
