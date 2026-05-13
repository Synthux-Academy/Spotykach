#include "config.h"

using namespace spotykach;

void Config::fill(const uint8_t* data, const size_t size)
{
    if (data == nullptr || size == 0) return;

    auto line_size = 8;
    char prop[line_size];
    size_t cursor = 0;
    while (cursor < size) {
        char line[line_size];
        int8_t line_idx = -1;

        // Read the line
        while (cursor < size && data[cursor] != '\n' && data[cursor] != '\r') {
            if (data[cursor] != ' ') {
                line_idx ++;
                if (line_idx < line_size) line[line_idx] = data[cursor];
            }
            cursor++;
        }

        while (cursor < size && (data[cursor] == '\n' || data[cursor] == '\r')) {
            cursor++;
        }

        if (line_idx < 0) continue;

        auto is_numeric = true;
        for (int8_t j = 0; j <= line_idx; j++) {
            if (j == 0 && line[j] == '-') {
                continue;
            }
            if (line[j] < '0' || line[j] > '9') {
                is_numeric = false;
                break;
            }
        }

        if (!is_numeric) {
            memcpy(prop, line, line_size);
            continue;
        }

        int32_t val = 0;
        int32_t sign = 1;
        size_t start = 0;

        if (line[0] == '-') {
            sign = -1;
            start = 1;
        }

        for (int8_t j = start; j <= line_idx; j++) {
            val = val * 10 + (line[j] - '0');
        }
        val *= sign;

             if (memcmp(prop, "mid_ch_a", line_size) == 0) { _vals.midi_channel_a = val - 1;  _is_loaded = true; }
        else if (memcmp(prop, "mid_ch_b", line_size) == 0) { _vals.midi_channel_b = val - 1;  _is_loaded = true; }
        else if (memcmp(prop, "mid_ps_a", line_size) == 0) { _vals.midi_play_stop_a = val;    _is_loaded = true; }
        else if (memcmp(prop, "mid_ps_b", line_size) == 0) { _vals.midi_play_stop_b = val;    _is_loaded = true; }
        else if (memcmp(prop, "pre_load", line_size) == 0) { _vals.is_preload_on = val;       _is_loaded = true; }
        else if (memcmp(prop, "s_mono_a", line_size) == 0) { _vals.is_slice_mono.set(0, val); _is_loaded = true; }
        else if (memcmp(prop, "s_mono_b", line_size) == 0) { _vals.is_slice_mono.set(1, val); _is_loaded = true; }
    }
}