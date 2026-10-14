// Loads WAVs whose audio starts at every alignment (and with a NaN, an Inf and
// 255 cue points) through Card::init_read_audio/read_audio, with fatfs_stub.cpp
// flagging every read the SD DMA couldn't do.
#include <cmath>
#include <string>
#include <vector>
#include <sys/stat.h>
#include "daisy_seed.h"
#include "usbd_cdc.h"
#include "hw/card.h"
#include "core/config.h"

extern std::string g_root;
extern int g_dma_violations;

uint32_t daisy::System::GetNow() { return 0; }
daisy::UsbHandle::Result daisy::UsbHandle::TransmitExternal(uint8_t*, size_t) { return Result::OK; }
void daisy::UsbHandle::SetReceiveCallback(ReceiveCallback, UsbPeriph) {}

static void u32(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back(x >> (8 * i)); }
static void u16(std::vector<uint8_t>& v, uint16_t x) { v.push_back(x); v.push_back(x >> 8); }
static void id(std::vector<uint8_t>& v, const char* s) { v.insert(v.end(), s, s + 4); }

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    g_root = argv[1];
    mkdir((g_root + "/SK").c_str(), 0755);
    mkdir((g_root + "/SK/G").c_str(), 0755);

    static uint8_t __attribute__((aligned(32))) card_buffer[spotykach::Card::kChunk];
    static float body[100000 * 2];
    static size_t cues[spotykach::kMaxSlicePointCount];
    const uint32_t frames = 100000;
    int failures = 0;

    for (int pad = 0; pad < 8; pad += 2) { // a JUNK chunk shifts the audio by 2 bytes per step
        std::vector<uint8_t> f;
        id(f, "RIFF"); u32(f, 0); id(f, "WAVE");
        id(f, "fmt "); u32(f, 16); u16(f, 3); u16(f, 2); u32(f, 48000); u32(f, 384000); u16(f, 8); u16(f, 32);
        id(f, "JUNK"); u32(f, pad); f.insert(f.end(), pad, 0);
        id(f, "data"); u32(f, frames * 8);
        size_t data_at = f.size();
        for (uint32_t i = 0; i < frames * 2; i++) {
            float x = (float)i;
            uint8_t b[4];
            memcpy(b, &x, 4);
            f.insert(f.end(), b, b + 4);
        }
        float nan = NAN, inf = INFINITY;
        memcpy(&f[data_at + 8 * 1000], &nan, 4);
        memcpy(&f[data_at + 8 * 2000 + 4], &inf, 4);
        id(f, "cue "); u32(f, 4 + 255 * 24); u32(f, 255);
        for (int i = 0; i < 255; i++) { u32(f, i + 1); u32(f, i * 300); id(f, "data"); u32(f, 0); u32(f, 0); u32(f, i * 300); }
        uint32_t riff = f.size() - 8;
        memcpy(&f[4], &riff, 4);
        FILE* out = fopen((g_root + "/SK/G/3.wav").c_str(), "wb");
        fwrite(f.data(), 1, f.size(), out);
        fclose(out);

        uint16_t count = 0;
        memset(body, 0xAB, sizeof(body));
        spotykach::Card card;
        card.init(card_buffer);
        card.recognize();
        card.mount();
        spotykach::Card::AudioData ad {};
        ad.body = (uint8_t*)body;
        ad.body_size = sizeof(body);
        ad.root_dir = (char*)"SK";
        ad.tape_dir = (char*)"G";
        ad.file_name = (char*)"3.wav";
        ad.cue_points = cues;
        ad.cue_count = &count;
        g_dma_violations = 0;
        card.init_read_audio(ad);
        while (card.state() == spotykach::Card::State::read_audio) card.read_audio();

        bool samples_ok = true;
        for (uint32_t i = 0; i < frames * 2 && samples_ok; i++) {
            float want = (i == 2000 || i == 4001) ? 0.f : (float)i; // NaN/Inf replaced with silence
            samples_ok = body[i] == want;
        }
        bool ok = samples_ok && count == 256 && cues[254] == 254 * 300 && cues[255] == frames && g_dma_violations == 0;
        printf("%s audio at %zu (mod 4 = %zu): samples %s, %u slices, DMA violations %d\n",
               ok ? "ok  " : "FAIL", data_at, data_at % 4, samples_ok ? "ok" : "wrong", count, g_dma_violations);
        failures += !ok;
    }
    return failures ? 1 : 0;
}
