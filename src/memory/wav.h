#pragma once

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
    uint8_t* out_cue_count,
    uint32_t cue_limit);

void find_cue_points(
    uint8_t* in_bytes, 
    size_t* out_cue_points, 
    uint8_t* out_cue_count,
    const uint32_t cue_limit,
    const uint32_t size);
