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
_norm_size         { 1.f },
_norm_size_offset  { 0.f },
_is_auto_cue       { false },
_snap_to_cue       { false },
_increment         { 1.f },
_target_increment  { 1.f },
_speed             { 1.f },
_trig_speed_mod_mult { 1.f },
_speed_mode       { SpeedMode::Tape },
_reverse          { false }
{};

void Generator::init(Buffer* buffer, size_t* cue_points) 
{
  _buffer = buffer;
  _cue_points = cue_points;
  uint8_t cnt = 0;
  for (auto& v: _voxs) {
    v.init(buffer, cnt);
    cnt ++;
  }
  reset_start_offset();
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
void Generator::set_start_offset_interval(const float norm)
{
  auto offset_interval = static_cast<uint8_t>(std::round(norm * kStartOffsetMaxInterval));
  if (_offset_interval && !offset_interval) reset_start_offset();
  _offset_interval = offset_interval;
}
void Generator::reset_start_offset()
{
  _offset_count = -1;
  _offset = 0;
}

float Generator::norm_size() const {
    if (_buffer->is_empty()) return 0.f;
    return _abs_size / _buffer->rec_size();
}
void Generator::set_size(float norm, const bool alt) 
{
  if (!_is_auto_cue && !_cue_points_count) norm *= norm;
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
void Generator::set_env_size(const float val)
{
  _env_norm_size = val;
}

void Generator::apply_dimensions()
{
  auto abs_start = 0.f; 
  auto norm_start = _norm_start + _norm_start_offset;
  while (norm_start > 1.f) norm_start -= 1.f;
  while (norm_start < 0.f) norm_start += 1.f;
  
  volatile auto norm_size = std::clamp((_norm_size + _norm_size_offset) * 1.05f, 0.f, 1.f);
  auto buffer_size = _buffer->rec_size();
  volatile auto abs_size = 0.f;

  using VM = Vox::Mode;
  switch (_vox_mode) {
    case VM::Linear: abs_size = norm_size * buffer_size; break;
    case VM::Spread: abs_size = norm_size * std::min(buffer_size, kMaxSpread); break;
  }
  
  
  using CSM = Config::CueSizeMode;
  auto mode = CSM::ignore;//Config::dynamic().cue_size_mode(ref);
  if (mode != CSM::ignore && _cue_points_count > 1) { /* pre-sliced */
    auto last_idx = size_t(_cue_points_count - 1);
    auto start_idx = static_cast<size_t>(std::round(norm_start * (last_idx - 1)));
    start_idx += _offset;
    start_idx %= last_idx;
    abs_start = _cue_points[start_idx];

    if (_vox_mode != VM::Spread && ((_alt_size && mode == CSM::free) || (!_alt_size && mode == CSM::snap))) {
      _cue_size_delta = static_cast<size_t>(std::round(norm_size * (last_idx - 1))) + 1;
      auto end_idx = start_idx + _cue_size_delta;
      if (end_idx > last_idx) end_idx -= last_idx;
      auto abs_end = _cue_points[end_idx];
      if (abs_end < abs_start) abs_end += buffer_size;
      if (end_idx != start_idx) abs_size = abs_end - abs_start;
    }
  }
  else if (_snap_to_cue) { /* slice mode */
    _cue_size_delta = static_cast<uint8_t>(std::max(abs_size / _slice_size, 1.f));
    auto start_idx = static_cast<uint32_t>(std::round(norm_start * _auto_cue_max_idx) + _offset);
    start_idx %= (_auto_cue_max_idx + 1);
    abs_start = _slice_size * start_idx;
  }
  else { /* reel & drift */
    abs_start = norm_start * buffer_size;
  }

  _abs_start = abs_start;
  switch (_vox_mode) {
    case VM::Linear: 
      _abs_size = std::max((size_t)abs_size, kSliceMinSize); 
      break;

    case VM::Spread: 
      _abs_spread = std::min((size_t)abs_size, kMaxSpread); 
      _abs_size = std::max((size_t)(_env_norm_size * buffer_size), kSliceMinSize); 
      break;
  }

  for (auto& v: _voxs) {
    if (_cont_start_mod) v.set_start(abs_start);
    v.set_size(_abs_size); 
    if (_vox_mode == VM::Spread) {
      v.set_spread(_abs_spread);
      v.set_full_size(buffer_size); 
    }    
  }
}

void Generator::add_cue() 
{
  if (_cue_points_count < kMaxSlicePointCount) {
    auto p = _cue_points + _cue_points_count;
    *p = _buffer->read_head();
    _cue_points_count ++;
  }
  _is_auto_cue = false;
}
void Generator::auto_cue(const size_t slice_size, const size_t slice_count)
{
  _slice_size = slice_size;
  _auto_cue_max_idx = slice_count - 1;
  _is_auto_cue = true;
}
void Generator::clear_cue()
{
  std::memset(_cue_points, 0, sizeof(size_t) * kMaxSlicePointCount);
  _cue_points_count = 0;
  _is_auto_cue = true;
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

  if ((_cue_points_count || _snap_to_cue) && _offset_interval) {
    if (++_offset_count >= _offset_interval) {
      _offset_count = 0;
      _offset += _cue_size_delta;
      apply_dimensions();
    }
  }

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
