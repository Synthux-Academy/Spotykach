#pragma once

#include <algorithm>
#include <bitset>
#include "hann.h"
#include "config.h"
#include "nocopy.h"
#include "buffer.h"
#include "xfade.h"
#include "skip.write.head.h"

namespace spotykach {

class Window {
public:
  enum class Interpolation: uint8_t {
    linear,
    cubic
  };

  enum class Position: uint8_t {
    absolute,
    relative
  };

  Window():
    _start        { 0.f },
    _playhead     { 0.f },
    _increment    { 1.f },
    _loop_start   { 0.f },
    _loop_length  { 0 },
    _iterator     { 0 },
    _interp       { Interpolation::cubic },
    _position     { Position::absolute },
    _is_active    { false },
    _is_reverse   { false },
    _is_init      { false }
    {
      _pan.SetStage(.5f);
    }

    struct Params {
      float start;
      size_t size;
      float loop_start; 
      float loop_length;
      float increment;
      float pan;
      Interpolation interp;
      Position pos;
    };

    void activate(const Params p) 
    {
        _size = p.size;
        _slope_out_start = _size - kWindowSlope;

        _loop_start = p.loop_start;
        _loop_length = p.loop_length;
        
        _steady_playhead = p.start;
        _iterator = 0;

        _playhead = p.start;
        _increment = p.increment;
        
        _interp = p.interp;
        _position = p.pos;
        
        _pan.SetStage(p.pan);
        _is_active = true;
        _is_init = true;
    }

    bool is_active() const 
    { 
      return _is_active; 
    }

    void deactivate() 
    {
      _is_active = false;
      _iterator = 0;
    }

    void set_reverse(const bool reverse)
    {
      if (reverse != _is_reverse) {
        auto base = _position == Position::absolute ? _loop_length : _size;
        _playhead = base - _playhead;
        _steady_playhead = base - _steady_playhead;
    
        _is_reverse = reverse;
      }
    }

    void set_is_first(const bool val) {
      _is_first = val;
    }

    bool is_done() const {  
      return _iterator == _slope_out_start;  
    }

    float play_head() const 
    { 
      return _playhead; 
    }

    float readhead() const 
    {
      return _readhead;
    }
    
    float steady_playhead() 
    {
        return _steady_playhead;
    }

    void set_increment(const float val) 
    {
      _increment = val;
    }

    void process(Buffer* buf, float& out0, float& out1) 
    {
        //Reverse playhead if needed
        auto rev_base = _position == Position::absolute ? _loop_length : _size;
        _readhead = _is_reverse ? rev_base - _playhead : _playhead;
        _readhead += _loop_start;
      
        if (_is_init) {
          _is_init = false;
          _skip_write_head_if_needed(buf, rev_base);
        }

        buf->read_linear(_readhead, out0, out1);

        // Window envelope
        auto att = _attenuation();
        out0 *= att;
        out1 *= att;
        if (_pan.Stage() != .5f) {
          _pan.Process(out0, 0, 0, out1, out0, out1);
        }

        // Advance playhead
        _playhead += _increment;
        _steady_playhead ++;
        if (++_iterator == _size) _is_active = false;
    }
  
private:
    NOCOPY(Window)

    float _attenuation() {
      if (_iterator < kWindowSlope) {
        return _is_first ? 1.f :  bleeptools::Hann::win().point(_iterator * kSlopeKof); //use fade-in of the slice
      }
      
      if (_iterator > _slope_out_start) {
        return bleeptools::Hann::win().point((_size - _iterator - 1) * kSlopeKof);
      }
      
      return 1.f;
    }

    void _skip_write_head_if_needed(Buffer* buf, const int32_t rev_base)
    {
      if (!buf->is_overdubbing()) return;

      SkipRequest r;
      r.ph = &_playhead;
      r.rh = &_readhead;
      r.ws = &_size;
      r.ls = _loop_start;
      r.ll = _loop_length;
      r.rb = rev_base;
      r.inc = _increment;
      r.wh = static_cast<int32_t>(buf->write_head());
      r.rs = static_cast<int32_t>(buf->rec_size());
      r.is_rev = _is_reverse;

      skip_write_head(r);
      _slope_out_start = _size - kWindowSlope;
    }

    XFade _pan;

    static constexpr auto kSlopeKof = 1.f / kWindowSlope;
    
    float _start;
    float _playhead;
    float _readhead;
    float _increment;
    float _loop_start;
    float _loop_length;
    size_t _size;
    size_t _slope_out_start;
    int32_t _steady_playhead;
    size_t _iterator;
    Interpolation _interp;
    Position _position;
    bool _is_first;
    bool _is_active;
    bool _is_reverse;
    bool _is_init;
};
};
