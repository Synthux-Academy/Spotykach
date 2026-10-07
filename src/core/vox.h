#pragma once

#include <array>
#include <functional>
#include <random>

#include "window.h"
#include "buffer.h"  
#include "adenv.h"
#include "xfade.h"
#include "config.h"
#include "nocopy.h"

namespace spotykach {

enum class SpeedMode: uint8_t {
  Tape, //speed and pitch at once
  Digital //speed and pitch independent
};

class Vox {
public:
  enum class Mode: uint8_t {
    Linear,
    Spread
  };

  Vox();
  ~Vox() = default;

  int ref;

  uint8_t idx() { return _vox_idx; }

  void init(Buffer* buffer, const uint8_t vox_idx);

  void set_mode(const Mode mode);

  void trigger();

  void stop();

  void process(float& out0, float& out1);

  bool is_playing() const { return _state != State::idle; }

  bool is_suspended() const { return _is_suspended; }

  float playhead() const;

  float envelope() const;

  void set_speed_mode(const SpeedMode mode) { _speed_mode = mode; }

  void set_playhead_increment(const float);

  void set_playhead_shift(const float shift) { _playhead_shift = shift; }

  void set_envelope_increment(const float value) { _envelope_increment = value; }

  void set_reverse(const bool);

  void set_start(const float abs) { _start = abs; }
  
  void set_size(const int32_t);

  void set_full_size(const int32_t abs) { _full_size = abs; }

  // Only for spread mode
  void set_spread(const int32_t abs) { _spread = abs; }

  void set_shape(const float);

  void set_win_size(const float);

  void set_is_wide(const bool val) { _is_wide = val; }

private:
    NOCOPY(Vox)

    void _check_window(Window&);

    void _seed();

    void _activate(Phasor playhead, const bool is_first = false);

    void _do_trigger();

    void _set_decay_start();

    void _decay();

    void _stop();

    float _rnd();

    enum class State {
      idle,
      attack,
      sustain,
      decay
    };

    enum class Direction {
      none,
      fwd,
      rev
    };

    static constexpr auto kSlopeKof = 1.f / kSliceSlope;

    ADEnvelope _env;
    XFade _xfade;
    Buffer* _buffer;
    std::array<Window, 6> _wins;

    std::default_random_engine _rand;
    std::normal_distribution<float> _dice;

    size_t _next_inetrval = 240;
    size_t _interval_count = 0;
    size_t _window_size;
    float _att;
    float _playhead_shift;
    float _playhead_increment;
    float _envelope_increment;
    float _start;
    int32_t _size;
    int32_t _spread; //Spread mode only
    int32_t _full_size;
    int32_t _iterator;
    float _decay_start;
    int32_t _slope_counter;
    Mode  _mode;
    SpeedMode _speed_mode;
    State _state;
    uint8_t _vox_idx;
    uint8_t _max_win_count;
    uint8_t _win_count;
    bool _is_reverse;
    bool _is_pending;
    bool _is_suspended;
    bool _is_wide;
};

};
