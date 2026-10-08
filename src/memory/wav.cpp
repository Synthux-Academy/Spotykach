#include "wav.h"

#include <stdint.h>
#include <cstring>
#include <algorithm>
#include "core/config.h"

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

static void sort_cue_points(size_t* points, const uint16_t count)
{
    for (uint16_t i = 1; i < count; ++i) {
        auto point = points[i];
        auto j = i;
        while (j > 0 && points[j - 1] > point) {
            points[j] = points[j - 1];
            --j;
        }
        points[j] = point;
    }
}

/*
Reads the "cue " chunk payload starting at cursor (pointing at dwCuePoints).
Points at or beyond cue_limit are skipped, the rest are stored contiguously
and sorted. At most spotykach::kMaxFileCuePoints are kept so the end slice still fits.
*/
static void read_cue_points(
    const uint8_t* in_bytes,
    const uint32_t size,
    size_t* out_cue_points,
    uint16_t* out_cue_count,
    const uint32_t cue_limit,
    const uint32_t cursor)
{
    if (cursor + 4 > size) return;
    auto num_points = read_val<uint32_t>(in_bytes, cursor);
    uint16_t added_points = 0;
    for (uint32_t i = 0; i < num_points && added_points < spotykach::kMaxFileCuePoints; ++i) {
        // Each cue point is 24 bytes, dwSampleOffset is at offset 20 within the point.
        auto point_offset = cursor + 4 + i * 24 + 20;
        if (point_offset + 4 > size) break;
        auto point = read_val<uint32_t>(in_bytes, point_offset);
        if (point < cue_limit) {
            out_cue_points[added_points++] = point;
        }
    }
    sort_cue_points(out_cue_points, added_points);
    *out_cue_count = added_points;
}

void find_cue_points(
    uint8_t* in_bytes, 
    size_t* out_cue_points, 
    uint16_t* out_cue_count,
    const uint32_t cue_limit,
    const uint32_t size)
{
    uint32_t cursor = 0;
    while (cursor + 8 <= size) {
        char chunkID[4];
        std::memcpy(chunkID, in_bytes + cursor, 4);
        uint32_t chunkSize = read_val<uint32_t>(in_bytes, cursor + 4);
        cursor += 8;

        if (std::memcmp(chunkID, "cue ", 4) == 0 && out_cue_points) {
            read_cue_points(in_bytes, size, out_cue_points, out_cue_count, cue_limit, cursor);
        }

        auto next = (uint64_t)cursor + chunkSize + (chunkSize % 2); // Chunks are word-aligned
        if (next > size) break;
        cursor = next;
    }
};

bool wav_header(
    uint8_t* in_bytes, 
    size_t* out_cue_points, 
    uint32_t size, 
    WavHeader& header, 
    size_t& header_size,
    uint16_t* out_cue_count,
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

    while (cursor + 8 <= size && !(foundFmt && foundData)) {
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
            if (foundFmt && header.BytePerBloc) cue_limit = chunkSize / header.BytePerBloc;
            foundData = true;
        }
        else if (std::memcmp(chunkID, "cue ", 4) == 0 && out_cue_points) {
            // Until "data" is found the limit is the destination size in bytes.
            auto limit = !foundData && foundFmt && header.BytePerBloc ? cue_limit / header.BytePerBloc : cue_limit;
            read_cue_points(in_bytes, size, out_cue_points, out_cue_count, limit, cursor);
        }
        auto next = (uint64_t)cursor + chunkSize + (chunkSize % 2); // Chunks are word-aligned
        if (next > size) break;
        cursor = next;
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

static void read_info_name(const uint8_t* in_bytes, uint32_t cursor, const uint32_t end, WavInfo& out_info)
{
    while (cursor + 8 <= end) {
        auto sub_size = read_val<uint32_t>(in_bytes, cursor + 4);
        if (check_id(in_bytes, cursor, "INAM")) {
            auto length = std::min({ sub_size, end - (cursor + 8), (uint32_t)sizeof(out_info.name) });
            std::memcpy(out_info.name, in_bytes + cursor + 8, length);
            while (length > 0 && out_info.name[length - 1] == 0) length--;
            out_info.name_length = length;
            return;
        }
        auto next = (uint64_t)cursor + 8 + sub_size + (sub_size % 2);
        if (next > end) break;
        cursor = next;
    }
}

bool wav_info(const uint8_t* in_bytes, const uint32_t size, WavInfo& out_info)
{
    out_info = WavInfo {};
    if (size < 12 || !check_id(in_bytes, 0, "RIFF") || !check_id(in_bytes, 8, "WAVE")) return false;

    bool found_fmt = false;
    uint32_t cursor = 12;
    while (cursor + 8 <= size) {
        auto chunk_size = read_val<uint32_t>(in_bytes, cursor + 4);
        auto body = cursor + 8;

        if (check_id(in_bytes, cursor, "fmt ") && chunk_size >= 16 && body + 16 <= size) {
            out_info.audio_format    = read_val<uint16_t>(in_bytes, body + 0);
            out_info.channels        = read_val<uint16_t>(in_bytes, body + 2);
            out_info.sample_rate     = read_val<uint32_t>(in_bytes, body + 4);
            out_info.block_align     = read_val<uint16_t>(in_bytes, body + 12);
            out_info.bits_per_sample = read_val<uint16_t>(in_bytes, body + 14);
            found_fmt = true;
        }
        else if (check_id(in_bytes, cursor, "data")) {
            out_info.data_offset = body;
            out_info.data_size = chunk_size;
            return found_fmt;
        }
        else if (check_id(in_bytes, cursor, "LIST") && body + 4 <= size && check_id(in_bytes, body, "INFO")) {
            read_info_name(in_bytes, body + 4, std::min(body + chunk_size, size), out_info);
        }
        else if (check_id(in_bytes, cursor, "cue ") && body + 4 <= size) {
            out_info.cue_count = std::min(read_val<uint32_t>(in_bytes, body), (uint32_t)UINT16_MAX);
        }
        auto next = (uint64_t)body + chunk_size + (chunk_size % 2);
        if (next > size) break;
        cursor = next;
    }
    return false;
}

uint16_t wav_cue_count(const uint8_t* in_bytes, const uint32_t size)
{
    uint32_t cursor = 0;
    while (cursor + 8 <= size) {
        auto chunk_size = read_val<uint32_t>(in_bytes, cursor + 4);
        if (check_id(in_bytes, cursor, "cue ") && cursor + 12 <= size) {
            return std::min(read_val<uint32_t>(in_bytes, cursor + 8), (uint32_t)UINT16_MAX);
        }
        auto next = (uint64_t)cursor + 8 + chunk_size + (chunk_size % 2);
        if (next > size) break;
        cursor = next;
    }
    return 0;
}
