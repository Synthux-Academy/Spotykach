#include "wav.h"

#include <stdint.h>
#include <cstring>
#include <algorithm>

template <typename T>
T read_val(const uint8_t* data, size_t offset) 
{
    T value;
    std::memcpy(&value, data + offset, sizeof(T));
    return value;
}
bool check_id(const uint8_t* data, size_t offset, const char* id) 
{
  return std::memcmp(data + offset, id, 4) == 0;
}

void read_cue_points(uint8_t* in_bytes, size_t* out_cue_points, uint8_t* out_cue_count, const uint32_t cue_limit, uint32_t cursor)
{
    uint32_t numPoints = read_val<uint32_t>(in_bytes, cursor);
    auto count = std::min(numPoints, static_cast<uint32_t>(32));
    volatile auto added_points = 0;
    for (uint32_t i = 0; i < count; ++i) {
        // Each cue point is 24 bytes. Sample Offset is at offset 20 within the point.
        // cursor + 4 (to skip NumPoints) + (i * 24) + 20
        auto point = read_val<size_t>(in_bytes, cursor + 24 + (i * 24));
        if (point < cue_limit) {
            out_cue_points[i] = point;
            added_points ++;
        }
    }
    *out_cue_count = added_points;
}

void find_cue_points(
    uint8_t* in_bytes, 
    size_t* out_cue_points, 
    uint8_t* out_cue_count,
    const uint32_t cue_limit,
    const uint32_t size)
{
    uint32_t cursor = 0;
    while (cursor <= size) {
        char chunkID[4];
        std::memcpy(chunkID, in_bytes + cursor, 4);
        uint32_t chunkSize = read_val<uint32_t>(in_bytes, cursor + 4);
        cursor += 8;

        if (std::memcmp(chunkID, "cue ", 4) == 0 && out_cue_points) {
            read_cue_points(in_bytes, out_cue_points, out_cue_count, cue_limit, cursor);
        }

        cursor += chunkSize + (chunkSize % 2); // Chunks are word-aligned
    }
};

bool wav_header(
    uint8_t* in_bytes, 
    size_t* out_cue_points, 
    uint32_t size, 
    WavHeader& header, 
    size_t& header_size,
    uint8_t* out_cue_count,
    uint32_t cue_limit)
{
    uint32_t cursor = 0;
    
    if (size < 12 || !check_id(in_bytes, cursor, "RIFF")) return false;
    
    std::memcpy(header.FileTypeBlocID, in_bytes + cursor, 4);
    header.size = read_val<uint32_t>(in_bytes, cursor + 4); 
    cursor += 8;

    if (!check_id(in_bytes, cursor, "WAVE")) return false;
    std::memcpy(header.FileFormatID, in_bytes + cursor, 4);
    cursor += 4;
    
    bool foundFmt = false, foundData = false;

    while (cursor <= size && !(foundFmt && foundData)) {
        char chunkID[4];
        std::memcpy(chunkID, in_bytes + cursor, 4);
        uint32_t chunkSize = read_val<uint32_t>(in_bytes, cursor + 4);
        cursor += 8;

        if (std::memcmp(chunkID, "fmt ", 4) == 0 && chunkSize >= 16) {
            std::memcpy(header.FormatBlocID, chunkID, 4);
            header.BlocSize = chunkSize;
            header.AudioFormat   = read_val<uint16_t>(in_bytes, cursor + 0);
            header.NbrChannels   = read_val<uint16_t>(in_bytes, cursor + 2);
            header.SampleRate    = read_val<uint32_t>(in_bytes, cursor + 4);
            header.BytePerSec    = read_val<uint32_t>(in_bytes, cursor + 8);
            header.BytePerBloc   = read_val<uint16_t>(in_bytes, cursor + 12);
            header.BitsPerSample = read_val<uint16_t>(in_bytes, cursor + 14);
            foundFmt = true;
        }
        else if (std::memcmp(chunkID, "data", 4) == 0) {
            std::memcpy(header.DataBlocID, chunkID, 4);
            header.DataSize = chunkSize;
            header_size = cursor;
            cue_limit = chunkSize / header.BytePerBloc;
            foundData = true;
        }
        else if (std::memcmp(chunkID, "cue ", 4) == 0 && out_cue_points) {
            read_cue_points(in_bytes, out_cue_points, out_cue_count, cue_limit, cursor);
        }
        cursor += chunkSize + (chunkSize % 2); // Chunks are word-aligned
    }

    return foundFmt && foundData;
};

WavHeader wav_header(const size_t size) {
    WavHeader header;

    header.AudioFormat = 3;
    header.NbrChannels = 2;
    header.SampleRate = 48000;
    header.BytePerBloc = sizeof(float) * header.NbrChannels;
    header.BytePerSec = header.SampleRate * header.BytePerBloc;
    header.BitsPerSample = 32;

    header.DataSize = size;
    header.size = header.DataSize + sizeof(header) - 8;

    return header;
};
