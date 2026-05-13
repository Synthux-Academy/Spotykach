#pragma once

#include <string.h>
#include <stdint.h>
#include <bitset>

namespace spotykach {

class Config
{
public:
    struct Values {
        uint8_t midi_channel_a = 0; // Actual 1
        uint8_t midi_channel_b = 1; // Actual 2
        uint8_t midi_play_stop_a = 0;
        uint8_t midi_play_stop_b = 0;
        bool is_preload_on = true;
        std::bitset<2> is_slice_mono = { 0 };
    };

    uint8_t midi_channel_a() const { return _vals.midi_channel_a; }
    uint8_t midi_channel_b() const { return _vals.midi_channel_b; }
    uint8_t midi_play_stop_a() const { return _vals.midi_play_stop_a; }
    uint8_t midi_play_stop_b() const { return _vals.midi_play_stop_b; }
    bool is_preload_on() const { return _vals.is_preload_on; }
    bool is_slice_mono(const int idx) const { return _vals.is_slice_mono.test(idx); }

    bool is_loaded() const { return _is_loaded; }

    void fill(const uint8_t* data, const size_t size);

    static Config& dynamic()
    {
        static Config instance;
        return instance;
    }
    Config(Config const&)           = delete;
    void operator=(Config const&)   = delete;

private:
    Config() {}
    Values _vals;
    bool _is_loaded;
};


// STATIC CONFIG ///////////////////////////////////////////////
// Clock ........................................
static constexpr uint8_t kPPQNIntern = 48;

// Buffer
static constexpr size_t kRecordFade = 192; // 4ms

// Grain ........................................
static constexpr size_t kWindowSlope = 960; //20ms @ 48K 1x
static constexpr size_t kMinimumWindowSize = 2 * kWindowSlope; //40ms @ 48k 1x
static constexpr size_t kDefaultWindowSize = 2880; //60ms @ 48k 1x

// Slice ........................................
static constexpr size_t kSliceSlope = 192; //4ms
static constexpr size_t kSliceMinSize = 2 * kSliceSlope + 960; //+20ms sustain @ 48K 1x

// LFO ..........................................
static constexpr float kLFOFreqMin = .01f;
static constexpr float kLFOFreqRange = 11.99f;

// Overdub ......................................
static constexpr float kDefaultFeedback = 0.95f; //-3db at -60...0dB scale

// Cue points ...................................
static constexpr uint8_t kMaxSlicePointCount = 32;

}