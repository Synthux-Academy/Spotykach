// The firmware's settings parser (Config::fill) on a file, printed in config.txt
// terms as JSON, so the tests can check what the device reads from what the web
// page writes.
//
// usage: config_dump <config.txt>
#include <cstdio>
#include <cmath>
#include <vector>
#include "core/config.h"

using namespace spotykach;

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    auto file = fopen(argv[1], "rb");
    if (!file) return 2;
    std::vector<uint8_t> data;
    uint8_t buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), file)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(file);

    auto& config = Config::dynamic();
    config.fill(data.data(), data.size());
    printf("{\"mid_ch_a\":%d,\"mid_ch_b\":%d,\"mid_ps_a\":%d,\"mid_ps_b\":%d,\"pre_load\":%d,"
           "\"slc_mn_a\":%d,\"slc_mn_b\":%d,\"cue_sm_a\":%d,\"cue_sm_b\":%d,\"grt_mx_a\":%d,\"grt_mx_b\":%d}\n",
        config.midi_channel(0) + 1, config.midi_channel(1) + 1,
        config.midi_play_stop(0), config.midi_play_stop(1),
        config.is_preload_on() ? 1 : 0,
        config.is_slice_mono(0) ? 1 : 0, config.is_slice_mono(1) ? 1 : 0,
        config.cue_size_mode(0), config.cue_size_mode(1),
        (int)std::lround(config.grit_mix(0) * 10), (int)std::lround(config.grit_mix(1) * 10));
    return 0;
}
