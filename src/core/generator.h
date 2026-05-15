#pragma once

#include <array>
#include <bitset>
#include <functional>
#include "buffer.h"
#include "vox.h"
#include "event.h"
#include "nocopy.h"

namespace spotykach {

class Generator {

friend class Deck;
friend class Drifter;

public:
  enum Param {
    Start,
    Speed,
    Count
  };

  static constexpr uint8_t kVoxCount = 3;
  static constexpr size_t kMaxSpread = 144000; // 3sec @48K

  Generator();
  ~Generator() = default;

  uint8_t ref;

  bool is_generating() const { return _is_active.any(); }

  void init(Buffer*, size_t* slice_points);

  float norm_spread() const;

  // float start() { return static_cast<float>(_start); }
  void pitch_speed_mod_in(const float value);
  
  float norm_start() const;
  void set_start(float);
  void set_start_mod(const float);
  void set_start_mod_on(const bool);
  void set_start_mod_cont(const bool val) { _cont_start_mod = val; }; // If set to false (default), start is applied once at the beginning of the slice
  void set_start_offset_interval(const float norm);
  uint8_t start_offset_interval() const { return _offset_interval; }

  float norm_size() const;
  void set_size(const float, const bool alt);
  void set_size_mod(const float);
  void set_size_mod_on(const bool);
  void set_env_size(const float);

  void set_shape(const float);
  void set_win_size(const float);

  void set_pitch(const float);

  void set_is_wide(const bool);

  bool set_speed(float);
  void apply_speed() { set_speed(_norm_pitch_speed); }

  bool has_cue() const { return _cue_points_count || _snap_to_cue; }
  void add_cue();
  void auto_cue(const size_t slice_size, const size_t slice_count);
  void clear_cue();
  void set_snap_to_cue(const bool value) { _snap_to_cue = value; }
  size_t* cue_points() const { return _cue_points; }
  uint8_t* cue_count() { return &_cue_points_count; }

  float increment() const { return _increment; }

  bool is_speed_mode(const SpeedMode mode) const { return _speed_mode == mode; }
  void set_speed_mode(const SpeedMode);

  void stop(uint8_t slice_idx);
  void set_on_vox_stop(std::function<void(const uint8_t)>&& on_stop) { _on_vox_stop = on_stop; }

  bool read_reset_is_triggered() {
    auto is_triggered = _is_triggered;
    _is_triggered = false;
    return is_triggered;
  }

  void process(float& out0, float& out1);

protected:
  void set_mode(const Vox::Mode);

  void apply_dimensions();
  void reset_start_offset();

  bool is_reverse() const { return _reverse; }
  void set_reverse(const bool);

  void apply_pitch() { set_pitch(_norm_pitch_speed); }
  void set_pitch_mod_cont(const bool val) { _cont_speed_mod = val; }
  
  void trigger(const uint8_t vox_idx, const Event* event);

  void apply_shape() { set_shape(_norm_shape); }

  float playhead_at(const uint8_t idx) const { return _voxs[idx].playhead(); }
  float envelope_at(const uint8_t idx) const { return _voxs[idx].envelope(); }

  bool is_suspended() const { return _voxs[0].is_suspended(); }

private:
  NOCOPY(Generator)

  Buffer* _buffer;
  std::array<Vox, kVoxCount> _voxs;

  std::function<void(const uint8_t)> _on_vox_stop;

  float _abs_start;
  bool _cont_start_mod;
  float _norm_start;
  float _norm_start_offset;

  float _abs_size;
  float _norm_size;
  float _norm_size_offset;
  float _abs_spread;
  float _env_norm_size;

  size_t  _slice_size;
  size_t  _auto_cue_max_idx;
  size_t* _cue_points;
  uint8_t _cue_points_count;
  uint8_t _cue_size_delta;
  bool    _is_auto_cue;
  bool    _snap_to_cue;

  uint8_t _offset;
  int8_t _offset_count;
  uint8_t _offset_interval;

  float _norm_pitch_speed;

  float _increment;
  float _target_increment;

  float _speed;
  float _speed_mod_mult;
  float _trig_speed_mod_mult;
  bool _cont_speed_mod;

  float _norm_shape;

  Vox::Mode _vox_mode;
  SpeedMode _speed_mode;

  std::bitset<kVoxCount> _is_active;

  bool _reverse;
  bool _is_triggered;
  bool _is_start_mod_on;
  bool _is_size_mod_on;
  bool _alt_size;
};
};
