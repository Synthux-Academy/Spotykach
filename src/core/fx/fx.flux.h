#pragma once

#include "nocopy.h"
#include "echo.h"

namespace spotykach {

class Flux {
public:
    static constexpr size_t kFeedbackDelayBufferLength  { 10000 };
    static constexpr uint8_t kDelayMaxSeconds           { 5 };
    static constexpr size_t kEchoDelayBufferLength      { 48000 * kDelayMaxSeconds };

    enum class Mode: uint8_t {
        FreeDelay,
        ClockedDelay
    };

    Flux();
    ~Flux() = default;

    void init(const float sample_rate, float** delay_buf);
    void process(float& inout0, float& inout1, const float send);

    float time_norm() const { return _time_norm; };
    void set_time_norm(const float norm);

    float mix_norm() const { return _mix_norm; };
    void set_mix_norm(const float norm);

    float fb_norm() const { return _fb_norm; };
    void set_fb_norm(const float norm);

    void switch_mode();
    Mode mode() const { return _mode; }
    void set_tempo_bpm(const float);

private:
    NOCOPY(Flux)

    void _apply_fb();
    void _apply_mix();
    void _apply_time(const bool hard = false);
    void _set_mode(const Mode);
    
    infrasonic::EchoDelay<kEchoDelayBufferLength> _echo_delay[2];

    float _time_norm;
    float _mix;
    float _mix_norm;
    float _fb_norm;
    float _tempo_bpm;

    Mode _mode;
};

};
