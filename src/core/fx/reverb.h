#pragma once

#include <memory>
#include "nocopy.h"
#include "daisysp.h"

namespace spotykach {

class Reverb {
public:
    Reverb() = default;
    ~Reverb() = default;

    void init(const float sample_rate);
    void process(float& inout0, float& inout1);

    void set_feedback_norm(const float norm);

private:
    NOCOPY(Reverb)

    using Verb = std::unique_ptr<daisysp::ReverbSc>;
    Verb _reverb;
};

};
