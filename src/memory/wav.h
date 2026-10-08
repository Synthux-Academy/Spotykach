#pragma once

#include <stddef.h>
#include <stdint.h>

//https://en.wikipedia.org/wiki/WAV

struct WavHeader {
  // Master RIFF chunk
  uint8_t FileTypeBlocID[4] = {'R', 'I', 'F', 'F'};
  size_t size;
  uint8_t FileFormatID[4] = {'W', 'A', 'V', 'E'};
  
  // Chunk describing the data format
  uint8_t FormatBlocID[4] = {'f', 'm', 't', ' '};
  uint32_t BlocSize = 16; // Fixed
  uint16_t AudioFormat;   // 1 = PCM integer. 3=IEEE754 float.
  uint16_t NbrChannels;
  uint32_t SampleRate;
  uint32_t BytePerSec;
  uint16_t BytePerBloc;
  uint16_t BitsPerSample;
  // Chunk containing the sampled data
  uint8_t DataBlocID[4] = {'d', 'a', 't', 'a'};
  uint32_t DataSize;
};

WavHeader wav_header(const size_t size);

bool wav_header(
    uint8_t* in_bytes,
    size_t* out_cue_points, 
    uint32_t size,
    WavHeader& header,
    size_t& header_size,
    uint16_t* out_cue_count,
    uint32_t cue_limit);

void find_cue_points(
    uint8_t* in_bytes, 
    size_t* out_cue_points, 
    uint16_t* out_cue_count,
    const uint32_t cue_limit,
    const uint32_t size);

struct WavInfo {
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t data_offset;   // first byte of audio
    uint32_t data_size;     // as declared by the "data" chunk
    uint16_t cue_count;     // cue points found before "data"
    uint8_t  name_length;   // LIST/INFO/INAM, without trailing NULs
    char     name[64];
};

/*
Parses chunks up to and including the "data" header.
Returns false if it's not a RIFF/WAVE or "fmt "/"data" are missing in the bytes.
*/
bool wav_info(const uint8_t* in_bytes, const uint32_t size, WavInfo& out_info);

/* Number of cue points in the "cue " chunk found among the chunks in the bytes, 0 if none. */
uint16_t wav_cue_count(const uint8_t* in_bytes, const uint32_t size);
