#pragma once
#include "nocopy.h"

namespace spotykach {

class MValue {
public:
  static constexpr auto kDefaultTreshold = .02f;

  MValue();
  ~MValue() {}

int id() const { return _id; }

float process(const float value, const bool active, int* id = nullptr, const float thresh = kDefaultTreshold);

bool is_tracking() const { return _is_tracking; }

float in_value() const { return _in_value; }

bool apply() const { return _apply; }
void set_apply() { _apply = true; }

float value() { 
  _apply = false;
  return _value; 
}

void set(const float value, const bool apply = true) {
  _is_tracking = false;
  _is_active = false;
  _value = value;
  _apply = apply;
}

private:
  NOCOPY(MValue)

  bool _set_active(const bool active);

  int _id;

  float _in_value;
  float _value;

  bool _is_active;
  bool _is_tracking;
  bool _apply;
};

};
