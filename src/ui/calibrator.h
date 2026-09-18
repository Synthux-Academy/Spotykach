#pragma once

#include "hw/hardware.h"
#include "memory/settings.h"
#include "nocopy.h"

namespace spotykach {

class Calibrator {
public:
    enum class Phase: uint8_t {
        idle,
        collecting_offset,
        calibrating,
        storing,
        error_storing
    };

    enum class Step: uint8_t {
        neg3v,
        neg1v,
        pos1v,
        pos3v
    };

    enum class State: uint8_t {
        waiting, //yellow
        collecting, //blue
        ok, //green
        deviation //red
    };

    Calibrator(Hardware&, Settings&);
    ~Calibrator() = default;

    void init(const bool calibrate);

    void next();
    void previous();

    void collect();

    void apply_and_exit();

    float correct(Hardware::CvInputId id, const float input);
    float correctVOctA(const float value) { return _voct_a.Process(value); }
    float correctVOctB(const float value) { return _voct_b.Process(value); }

    Step step() const { return _step; }
    Phase phase() const { return _phase; } 
    State state_a() const { return _state[0]; }
    State state_b() const { return _state[1]; }

    bool is_done() {
        return _phase == Phase::calibrating && _step == Step::pos3v && _state[0] == State::ok && _state[1] == State::ok;
    }

private:
    NOCOPY(Calibrator)

    void _calibrate();
    void _collect_offset();
    void _collect(Hardware::CvInputId);
    void _wait();

    static constexpr uint8_t kMaxAvgCount = 50;
    static constexpr float kDeviationTolerance = 0.02f; //120cents 
    
    Hardware& _hw;
    Settings& _settings;

    infrasonic::CalibratedVOct _voct_a;
    infrasonic::CalibratedVOct _voct_b;

    Settings::Data  _defaults;
    Phase           _phase;
    Step            _step;
    State           _state[2]; //Common/A and B
};
};
