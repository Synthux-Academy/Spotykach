// SYNTHUX ACADEMY /////////////////////////////////////////
// SPOTYKACH ///////////////////////////////////////////////
#pragma once

#include <array>
#include <random>

#include "nocopy.h"
#include "config.h"

#include "driver.h"
#include "modulator.h"
#include "xfade.h"
#include "panner.h"
#include "smooth.h"

namespace spotykach {

enum class Route: uint8_t {
  DoubleMono = 1,
  Stereo,
  GenerativeStereo
};

enum class ModType: uint8_t {
  Follow,
  LFO
};

class Core {
public:
  Core();
  ~Core() {}
  
  void init(const float sample_rate, const float buffer_size);

  Driver& driver() { return _driver; }
  Panner& panner() { return _panner; }
  Deck& deck(const Deck::Ref ref) { return _decks[ref]; }
  Modulator& mod(const Deck::Ref ref) { return _mod[ref]; }

  Deck::Source source(const Deck::Ref deck) const { return _source[deck]; }
  void set_source(const Deck::Source source, const Deck::Ref deck) { _source[deck] = source; }
  void infer_panner_mode();

  float mix() const { return _xfade.Stage(); }
  void set_mix(const float value) { _mix = value; }
  void mix_mod_in(const float value) { 
    if (value < 0.01) _mix_mod = 0.f;
    else _mix_mod = value; 
  }

  void set_route(const Route val);
  Route route() const { return _route; }

  void set_click_mix(const float value) { _click_mix = value; }
  
  void prepare();
  void process(const float* const* in, float** out, size_t size);

private:
  NOCOPY(Core)
  
  void _resolve_mix();

  std::array<Deck, Deck::Count> _decks;
  std::array<Modulator, Deck::Count> _mod;
  Driver  _driver;
  XFade   _xfade;
  Click   _click;
  Panner  _panner;
  OnePoleSmoother _mix_smooth;

  std::array<float, Deck::Count> _pos_mod_amnt;
  std::array<Deck::Source, Deck::Count> _source;

  Route _route;

  std::array<float, 2> _bus;

  float _click_mix;
  float _mix;
  float _mix_mod;
};

};
