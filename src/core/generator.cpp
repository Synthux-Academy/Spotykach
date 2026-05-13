#include "generator.h"
#include "config.h"
#include "expose.h"

using namespace spotykach;

float unified_value(const float value) 
{
  auto abs = std::abs(value);
  if (abs < 1.03 && abs > 0.97) {
    return 1.f;
  }
  return value;
}

Generator::Generator():
_norm_start        { 0.f },
_norm_start_offset { 0.f },
_norm_size         { kSliceMinSize },
_norm_size_offset  { 0.f },
_is_auto_slice     { false },
_snap_to_slice     { false },
_increment         { 1.f },
_target_increment  { 1.f },
_speed             { 1.f },
_trig_speed_mod_mult { 1.f },
_speed_mode       { SpeedMode::Tape },
_reverse          { false }
{};

void Generator::init(Buffer* buffer, size_t* slice_points) 
{
  _buffer = buffer;
  _slice_points = slice_points;
  uint8_t cnt = 0;
  for (auto& v: _voxs) {
    v.init(buffer, cnt);
    cnt ++;
  }
};

void Generator::set_mode(const Vox::Mode value)
{
  _vox_mode = value;
  for (auto& v: _voxs) v.set_mode(value);
}

float Generator::norm_start() const 
{ 
    if (_buffer->is_empty()) return 0.f;
    return _abs_start / _buffer->rec_size();
};
void Generator::set_start(float norm) 
{
  _norm_start = norm;
};
void Generator::set_start_mod_on(const bool on)
{
    _is_start_mod_on = on;
};
void Generator::set_start_mod(const float val)
{
    auto norm_start_offset = 0.f;
    if (_is_start_mod_on) norm_start_offset = std::abs(val) < 0.01 ? 0 : val;
    _norm_start_offset = norm_start_offset;
}

float Generator::norm_size() const {
    if (_buffer->is_empty()) return 0.f;
    return _abs_size / _buffer->rec_size();
}
void Generator::set_size(float norm, const bool alt) 
{
  if (!_is_auto_slice && !_slice_points_count) norm *= norm;
  _norm_size = norm;
  _alt_size = alt;
}
void Generator::set_size_mod_on(const bool on) 
{ 
    _is_size_mod_on = on;
};
void Generator::set_size_mod(const float val) 
{
    auto norm_size_offset = 0.f;
    if (_is_size_mod_on) norm_size_offset = std::abs(val) < 0.01 ? 0 : val;
    _norm_size_offset = norm_size_offset;
};

void Generator::apply_dimensions()
{
  auto abs_start = 0.f; 
  auto norm_start = _norm_start + _norm_start_offset;
  while (norm_start > 1.f) norm_start -= 1.f;
  while (norm_start < 0.f) norm_start += 1.f;
  
  auto norm_size = std::clamp((_norm_size + _norm_size_offset) * 1.05f, 0.f, 1.f);
  auto buffer_size = _buffer->rec_size();
  auto abs_size = 0.f;
  switch (_vox_mode) {
    case Vox::Mode::Linear:
      abs_size = norm_size * buffer_size;
      break;

    case Vox::Mode::Spread:
      abs_size = norm_size * std::min(buffer_size, kMaxSpread);
      break;
  }
  
  if (_slice_points_count > 0) { /* pre-sliced */
    auto last_point_idx = _slice_points_count - 1;
    auto start_idx = static_cast<size_t>(std::round(norm_start * last_point_idx));
    abs_start = _slice_points[start_idx];

    auto mode = Config::dynamic().cue_size_mode(ref);
    using CSM = Config::CueSizeMode;
    if ((_alt_size && mode == CSM::free) || (!_alt_size && mode == CSM::snap)) {
      auto end_idx = static_cast<size_t>(std::round((norm_start + norm_size) * last_point_idx));
      if (end_idx == start_idx) end_idx += 1;
      if (end_idx >= _slice_points_count) end_idx -= _slice_points_count;
      auto abs_end = _slice_points[end_idx];
      if (abs_end < abs_start) abs_end += buffer_size;
      abs_size = abs_end - _abs_start;
    }
  }
  else if (_snap_to_slice) { /* slice mode */
    abs_start = _slice_size * std::round(norm_start * _auto_slice_max_idx);
  }
  else { /* reel & drift */
    abs_start = norm_start * buffer_size;
  }

  _abs_start = abs_start;
  switch (_vox_mode) {
    case Vox::Mode::Linear:
      _abs_size = std::max((size_t)abs_size, kSliceMinSize);
      break;

    case Vox::Mode::Spread:
      _abs_spread = std::min((size_t)abs_size, kMaxSpread);
      break;
  }

  for (auto& v: _voxs) {
    if (_cont_start_mod) v.set_start(abs_start);
    switch (_vox_mode) {
      case Vox::Mode::Linear: 
        v.set_size(_abs_size);
        break;

      case Vox::Mode::Spread: 
        v.set_spread(_abs_spread); 
        v.set_full_size(buffer_size);
        break;
    }    
  }
}

void Generator::slice() 
{
  if (_slice_points_count < kMaxSlicePointCount) {
    auto p = _slice_points + _slice_points_count;
    *p = _buffer->read_head();
    _slice_points_count ++;
  }
  _is_auto_slice = false;
}
void Generator::auto_slice(const size_t slice_size, const size_t slice_count)
{
  _slice_size = slice_size;
  _auto_slice_max_idx = slice_count - 1;
  _is_auto_slice = true;
}
void Generator::clear_slices()
{
  std::memset(_slice_points, 0, sizeof(size_t) * kMaxSlicePointCount);
  _is_auto_slice = true;
}

void Generator::set_speed_mode(const SpeedMode mode) 
{
  _speed_mode = mode;
  for (auto& v: _voxs) v.set_speed_mode(mode);
}
float mapped_speed(const float val) 
{
    return val < .5f ? 2.f * val : 1.f + (val - .5f) * 6.f;
}
// For tape mode it's both speed and pitch,
// for digital -> time stretching
bool Generator::set_speed(float speed) 
{
  switch (_speed_mode) {
    case SpeedMode::Tape: {
      _norm_pitch_speed = speed;
      _target_increment = mapped_speed(speed);
      for (auto& v: _voxs) { v.set_playhead_shift(0.f); }
      return std::abs(_target_increment - 1.f) < .002f;
    }

    case SpeedMode::Digital: {
        auto shift = 0.f;
        if (speed < 0.02) {
          shift = kWindowSlope - kDefaultWindowSize;
        }  
        else {
          shift = speed * (kDefaultWindowSize - kWindowSlope) - kDefaultWindowSize + kWindowSlope;
        }
        for (auto& v: _voxs) {
          v.set_playhead_shift(shift);
          v.set_envelope_increment(speed);
        }
        return 1.f;
    }

    default: 
        return 1.f;
  }
};
// Only for digital mode, when speed and pitch are detached
void Generator::set_pitch(const float pitch) 
{
  _norm_pitch_speed = pitch;
  if (_speed_mode == SpeedMode::Digital) {
    _target_increment = mapped_speed(pitch);
  }
}
void Generator::pitch_speed_mod_in(const float value) { 
  _speed_mod_mult = unified_value(value);
  if (_cont_speed_mod) {
    for (auto& v: _voxs) v.set_playhead_increment(_increment * _speed_mod_mult * _trig_speed_mod_mult);
  }
}

void Generator::set_shape(const float norm) 
{
  _norm_shape = norm;
  for (auto& v: _voxs) v.set_shape(norm);
}
void Generator::set_win_size(const float norm)
{
  for (auto& v: _voxs) v.set_win_size(norm);
}
float Generator::norm_spread() const { 
  if (_buffer->is_empty()) return 0.f;
  return _abs_spread / std::min(_buffer->rec_size(), kMaxSpread); 
}

void Generator::set_is_wide(const bool val)
{
  for (auto& v: _voxs) v.set_is_wide(val);
}

void Generator::set_reverse(const bool value) 
{
  _reverse = value;
  for (auto& v: _voxs) {
    v.set_reverse(value);
  }
};

void Generator::trigger(const uint8_t vox_idx, const Event* event) 
{
  auto& v = _voxs[vox_idx];

  if (!_cont_start_mod) v.set_start(_abs_start);

  /* gate in / midi / track */ 
  if (event->p3_on && (event->discont || !_cont_speed_mod)) {
    _trig_speed_mod_mult = unified_value(event->p3);
  }
  /* v/oct in slice mode */
  else if (!_cont_speed_mod) {
    _trig_speed_mod_mult = _speed_mod_mult;
  }     
  else {
    _trig_speed_mod_mult = 1.f;
  }
  _increment = _target_increment;
  v.set_playhead_increment(_increment * _trig_speed_mod_mult);
  if (_speed_mode == SpeedMode::Tape) {
    v.set_envelope_increment(_increment);
  }
  
  v.trigger();
  _is_triggered = true;
};
void Generator::stop(uint8_t vox_idx) 
{
  auto& v = _voxs[vox_idx];
  if (v.is_playing()) v.stop();
}

void Generator::process(float& out0, float& out1) 
{
  out0 = 0;
  out1 = 0;

  auto set_increment = false;
  if (std::fabs(_target_increment - _increment) > 0.002) {
    _increment += (_target_increment - _increment) * 0.0002083333333f; //100ms
    set_increment = true;
  }
  else {
    _increment = _target_increment;
  }
  
  if (_buffer->is_empty()) return;

  auto s_out0 = 0.f;
  auto s_out1 = 0.f;
  auto speed_mod = _trig_speed_mod_mult;
  if (_cont_speed_mod) speed_mod *= _speed_mod_mult;
  for (auto& v: _voxs) {
    if (set_increment) {
      v.set_playhead_increment(_increment * speed_mod);
      if (_speed_mode == SpeedMode::Tape) {
        v.set_envelope_increment(_increment);
      }
    }

    _is_active.set(v.idx(), v.is_playing());
    if (v.is_playing()) {
      v.process(s_out0, s_out1);
      if (!v.is_playing()) {
        _is_active.reset(v.idx());
        _on_vox_stop(v.idx());
      }
      out0 += s_out0;
      out1 += s_out1;
    }
  }
  if (!is_generating()) _trig_speed_mod_mult = 1.f;
}
