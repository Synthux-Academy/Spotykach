#pragma once

#include <string.h>
#include <stdint.h>
#include <bitset>
#include <array>

namespace spotykach {

class Config
{
public:
    enum CueSizeMode: uint8_t {
        ignore  = 0,
        snap    = 1,
        free    = 2
    };

    struct Values {
        std::array<uint8_t, 2> midi_channel = { 0, 1 }; // Actual 1 , 2
        std::bitset<2> midi_play_stop = 0;
        std::bitset<2> is_slice_mono = { 0 };
        std::array<uint8_t, 2> cue_size_mode = { snap, snap };
        bool is_preload_on = true;
    };

    uint8_t midi_channel(const uint8_t idx) const { return _vals.midi_channel[idx]; }
    uint8_t midi_play_stop(const uint8_t idx) const { return _vals.midi_play_stop[idx]; }
    bool is_preload_on() const { return _vals.is_preload_on; }
    bool is_slice_mono(const int idx) const { return _vals.is_slice_mono.test(idx); }
    CueSizeMode cue_size_mode(const int idx) const { return static_cast<CueSizeMode>(_vals.cue_size_mode[idx]); }

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
static constexpr uint8_t kStartOffsetMaxInterval = 8;

// LFO ..........................................
static constexpr float kLFOFreqMin = .01f;
static constexpr float kLFOFreqRange = 11.99f;

// Overdub ......................................
static constexpr float kDefaultFeedback = 0.95f; //-3db at -60...0dB scale

// Cue points ...................................
static constexpr uint8_t kMaxSlicePointCount = 32;

// MIDI ..........................................
enum CC: uint8_t {
    CrossFade   = 3,
    RecExt      = 14,
    RecInt      = 15,
    Start       = 20,
    Size        = 21,
    Env         = 22,
    Pitch       = 23,
    IOMix       = 24,
    DeckFB      = 25,
    EnvSize     = 26,
    WinSize     = 27,
    Fwd         = 85,
    Rev         = 86,
    ModCycle    = 89,
    ModGlow     = 90,
    GritOn      = 102,
    GritIntens  = 103,
    GritMix     = 104,
    FluxOn      = 105,
    FluxIntes   = 106,
    FluxFB      = 107,
    FluxMix     = 108   
};

};
