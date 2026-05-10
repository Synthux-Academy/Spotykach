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

void Generator::set_start(float norm) 
{
  _norm_start = std::clamp(norm, 0.f, 1.f);
  _apply_start();
};
void Generator::set_start_offset(const float value)
{
  _norm_start_offset = value;
  if (_cont_start_mod) _apply_start();
};
size_t Generator::_get_abs_start() 
{
  auto start = _norm_start + _norm_start_offset;
  while (start > 1.f) start -= 1.f;
  while (start < 0.f) start += 1.f;
  auto buffer_size = _buffer->rec_size();
  auto abs_start = 0;
  if (_snap_to_slice || _slice_points_count > 0) {
    abs_start = _snap(start);
    _input_start = buffer_size > 0 ? static_cast<float>(abs_start) / buffer_size : 0;  
    if (_slice_points_count > 0) {
      _apply_size();
    }
  }
  else {
    abs_start = start * buffer_size;
    _input_start = start;
  }
  return abs_start;
}
void Generator::_apply_start()
{
  auto abs_start = _get_abs_start();
  for (auto& v: _voxs) v.set_start(abs_start);
}

void Generator::set_size(float norm) 
{
  if (!_snap_to_slice && _slice_points_count == 0) norm *= norm;
  _norm_size = std::clamp(norm, 0.f, 1.f);

  auto abs_size = _get_abs_size();
  auto full_size = _buffer->rec_size();
  for (auto& v: _voxs) {
    v.set_size(abs_size);
    v.set_full_size(full_size);
  }
  _input_size = std::max(abs_size, kSliceMinSize);
}
void Generator::set_size_offset(const float offset) 
{
  _norm_size_offset = offset;
  switch (_vox_mode) {
    case Vox::Mode::Spread: _apply_spread(); break;
    case Vox::Mode::Linear: _apply_size(); break;
  }
}
size_t Generator::_get_abs_size()
{
  volatile auto size = static_cast<int32_t>(_buffer->rec_size());
  volatile auto min_size = static_cast<int32_t>(kSliceMinSize);
  auto norm_size = std::clamp((_norm_size + _norm_size_offset) * 1.05f, 0.f, 1.f);

  if (_slice_points_count > 0) {
    auto base = _slice_points_count - 1;
    volatile auto start_idx = static_cast<size_t>(_input_start * base);
    volatile auto end_idx = static_cast<size_t>(std::round((norm_size + _input_start) * base));
    if (end_idx == start_idx) end_idx += 1;
    if (end_idx >= _slice_points_count) end_idx -= _slice_points_count;
    volatile auto start = _slice_points[start_idx];
    volatile auto end = _slice_points[end_idx];
    if (end < start) end += size;
    size = end - start;
  }
  else {
    size = static_cast<int32_t>(norm_size * size);
  }
  return std::max(size, min_size);
}
void Generator::_apply_size()
{
  _input_size = _get_abs_size();
  for (auto& v: _voxs) v.set_size(_input_size);
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
size_t Generator::_snap(const float norm_value) 
{
  if (_slice_points_count > 0) {
    auto idx = static_cast<size_t>(std::round(norm_value * (_slice_points_count - 1)));
    return _slice_points[idx];
  }
  else {
    auto idx = static_cast<size_t>(std::round(_auto_slice_max_idx * norm_value));
    return _slice_size * idx;
  }
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
void Generator::set_win_spread(const float norm)
{
  _norm_spread = norm;
  _apply_spread();
}
size_t Generator::_abs_spread() 
{
  _input_spread = std::clamp(_norm_spread + _norm_size_offset, 0.f, 1.f);
  return static_cast<int32_t>(_input_spread * std::min(_buffer->rec_size(), (size_t)144000)); //3 seconds max
}
void Generator::_apply_spread()
{
  auto abs_spread = _abs_spread();
  for (auto& v: _voxs) v.set_spread(abs_spread);
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

  // TODO: define override order, something like knob-over-cv-over-MIDI-over-event.
  if (event->p1_on) { _norm_start = event->p1; }
  if (!_cont_start_mod) v.set_start(_get_abs_start());

  if (event->p2_on) { 
    _norm_size = event->p2;
    v.set_size(_get_abs_size());
  }

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
