#pragma once

#include <assert.h>
#include "dev/sdram.h"
#include "daisy_seed.h"
#include "nocopy.h"
#include "../core/track.h"
#include "../core/detector.h"
#include "../core/fx.h"
#include "../core/buffer.h"
#include "card.h"

namespace spotykach {

static constexpr size_t kSampleRate            { 48000 };
static constexpr uint8_t kDecksCount           { 2 };
static constexpr uint8_t kBuffersPerDeckCount  { 1 };
static constexpr uint8_t kChannelsCount        { 2 };
static constexpr uint8_t kSourceMaxSeconds     { 42 };

class SDRAMBuffer {
public:
    static SDRAMBuffer& pool() {
        static SDRAMBuffer instance;
        return instance;
    }

    Buffer::Frame* sourceBuffer();
    size_t sourceBufferSize();

    float* delayBuffer();

    float* detectorBuffer();

    Event* track_buffer_a() const;
    Event* track_buffer_b() const;

    size_t* slices_a() const;
    size_t* slices_b() const;

    uint8_t* card_buffer() const;

private:
    NOCOPY(SDRAMBuffer)

    SDRAMBuffer();
    ~SDRAMBuffer() = default;

    size_t _providedSourceBufCount      { 0 };
    size_t _providedDetectorBufCount    { 0 };
    size_t _providedDelayBufCount       { 0 };
};
};
