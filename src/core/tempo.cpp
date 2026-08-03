#include "tempo.h"
#include "daisy_seed.h"
#include "expose.h"

using namespace spotykach;
using namespace daisy;

Tempo::Tempo():
_prev_time  { 0 },
_bpm        { 120.f },
_pointer    { 0 },
_full       { false }
{};

void Tempo::tap() 
{
    auto time = System::GetNow();
    if (_prev_time != 0) {
        auto diff = time - _prev_time;
        if (_avg > 0 && (diff < .6f * _avg || diff > 1.4f * _avg)) {
            _avg = 0;
            _pointer = 0;
            _full = false;
            _prev_time = time;
            return;
        }                              

        _times[_pointer] = diff;
        _pointer ++;
        if (_pointer == _times.size()) {
            _full = true;
            _pointer = 0;
        }

        auto size = _full ? _times.size() : _pointer;
        if (size >= 2) {
            size_t summ = 0;
            for (uint8_t i = 0; i < size; i++) {
                summ += _times[i];
            }
            _avg = static_cast<float>(summ) / static_cast<float>(size);
            _bpm = std::clamp(60000.f / _avg, kMin, kMax);
        }
    }
    _prev_time = time;
};
