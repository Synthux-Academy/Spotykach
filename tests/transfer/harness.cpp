// Device side of the transfer tests: the firmware's Service/Link/Card/wav code on
// the host, FatFS on a directory (fatfs_stub.cpp), USB CDC on stdin/stdout.
//
// usage: harness <card root>
// FAULT environment variable, for the retry paths:
//   upload-noreply  drops the reply to the 5th UPLOAD_DATA
//   read-cut        sends half of the 5th DOWNLOAD_READ reply
//   read-drop       drops the 5th DOWNLOAD_READ reply
//   read-burst      cuts every other DOWNLOAD_READ reply larger than 8 KB to 8 KB,
//                   like a macOS tty overflowing
#include <chrono>
#include <string>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include "daisy_seed.h"
#include "usbd_cdc.h"
#include "transfer/service.h"

extern std::string g_root;
extern int g_dma_violations;

extern "C" {
    uint8_t usbd_mode = 0;
    USBD_HandleTypeDef hUsbDeviceHS { nullptr };
}

static daisy::UsbHandle::ReceiveCallback g_rx = nullptr;

uint32_t daisy::System::GetNow()
{
    using namespace std::chrono;
    static auto t0 = steady_clock::now();
    return duration_cast<milliseconds>(steady_clock::now() - t0).count();
}

daisy::UsbHandle::Result daisy::UsbHandle::TransmitExternal(uint8_t* buf, size_t size)
{
    static int uploads = 0, reads = 0, large_reads = 0;
    static const char* fault = getenv("FAULT");
    if (fault) {
        std::string f = fault;
        if (buf[2] == 0x91 && ++uploads == 5 && f == "upload-noreply") return Result::OK;
        if (buf[2] == 0xA1) {
            ++reads;
            if (reads == 5 && f == "read-drop") return Result::OK;
            if (reads == 5 && f == "read-cut") size /= 2;
            if (f == "read-burst" && size > 8192 + 16 && (++large_reads % 2)) size = 8192;
        }
    }
    for (size_t off = 0; off < size;) {
        auto written = write(1, buf + off, size - off);
        if (written <= 0) return Result::ERR;
        off += written;
    }
    return Result::OK;
}

void daisy::UsbHandle::SetReceiveCallback(ReceiveCallback cb, UsbPeriph) { g_rx = cb; }

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    g_root = argv[1];

    spotykach::Card card;
    static uint8_t __attribute__((aligned(32))) card_buffer[spotykach::Card::kChunk];
    card.init(card_buffer);
    card.recognize();
    card.mount();
    if (!card.begin_transfer()) return 2;

    spotykach::transfer::Service service;
    service.init(&card, 42 * 48000);
    service.begin();

    fcntl(0, F_SETFL, O_NONBLOCK);
    uint8_t packet[64]; // USB full speed packets
    while (!service.should_exit()) {
        pollfd pfd { 0, POLLIN, 0 };
        poll(&pfd, 1, 5);
        while (true) {
            auto n = read(0, packet, sizeof(packet));
            if (n == 0) return 0;
            if (n < 0) break;
            uint32_t len = n;
            g_rx(packet, &len);
        }
        service.process();
    }
    service.end();
    card.end_transfer();
    fprintf(stderr, "harness: exit, DMA violations %d\n", g_dma_violations);
    return g_dma_violations ? 3 : 0;
}
