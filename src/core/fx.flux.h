#pragma once

#include "../nocopy.h"
#include "echo.h"

namespace spotykach {

class Flux {
public:
    static constexpr size_t kFeedbackDelayBufferLength  { 10000 };
    static constexpr uint8_t kDelayMaxSeconds           { 5 };
    static constexpr size_t kEchoDelayBufferLength      { 48000 * kDelayMaxSeconds };

    Flux();
    ~Flux() = default;

    void init(const float sample_rate, float** delay_buf);
    void process(float& inout0, float& inout1, const float send);

    float intensity() const { return _int; };
    void set_intensity(const float norm);

    float mix() const { return _mix_norm; };
    void set_mix(const float norm);

    float fb() const { return _fb; };
    void set_fb(const float norm);

private:
    NOCOPY(Flux)

    void _apply_fb();
    void _apply_mix();
    void _apply_int(const bool hard = false);

    infrasonic::EchoDelay<kEchoDelayBufferLength> _echo_delay[2];

    float _int;
    float _mix;
    float _mix_norm;
    float _fb;
};

};
