#include "link.h"

#include <cstring>
#include <daisy_seed.h>
#include "usbd_cdc.h"

using namespace spotykach::transfer;
using namespace daisy;

extern "C" {
    extern USBD_HandleTypeDef hUsbDeviceHS;
}

static constexpr uint32_t kStaleFrameMs = 500;
static constexpr uint32_t kTxTimeoutMs = 200;

static uint8_t DSY_SDRAM_BSS __attribute__((aligned(32))) _rx_buf[kMaxFrame];
static uint8_t DSY_SDRAM_BSS __attribute__((aligned(32))) _tx_buf[kMaxFrame];

static volatile size_t   _rx_len = 0;
static volatile bool     _rx_overflow = false;
static volatile uint32_t _rx_last_ms = 0;
static size_t            _frame_size = 0;

// Called from the USB interrupt
static void rx_callback(uint8_t* buf, uint32_t* len)
{
    auto size = *len;
    if (_rx_len + size > sizeof(_rx_buf)) {
        _rx_overflow = true;
        return;
    }
    std::memcpy(&_rx_buf[_rx_len], buf, size);
    _rx_len = _rx_len + size;
    _rx_last_ms = System::GetNow();
}

uint32_t spotykach::transfer::crc32_update(uint32_t crc, const uint8_t* data, size_t size)
{
    static uint32_t table[256];
    static bool is_table_ready = false;
    if (!is_table_ready) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (uint8_t k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        is_table_ready = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < size; i++) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
    return ~crc;
}

void Link::start()
{
    __disable_irq();
    _rx_len = 0;
    _rx_overflow = false;
    __enable_irq();
    _frame_size = 0;
    _rx_last_ms = System::GetNow();

    UsbHandle usb;
    usbd_mode = USBD_MODE_CDC;
    usb.Init(UsbHandle::FS_EXTERNAL);
    System::Delay(10);
    usb.SetReceiveCallback(rx_callback, UsbHandle::FS_EXTERNAL);
}

void Link::stop()
{
    _wait_tx_idle(kTxTimeoutMs);
    UsbHandle usb;
    usb.DeInit(UsbHandle::FS_EXTERNAL);
}

uint32_t Link::last_rx_ms() const
{
    return _rx_last_ms;
}

void Link::_drop(const size_t count)
{
    __disable_irq();
    auto len = _rx_len;
    if (count >= len) {
        _rx_len = 0;
    }
    else {
        std::memmove(_rx_buf, &_rx_buf[count], len - count);
        _rx_len = len - count;
    }
    __enable_irq();
}

Link::Poll Link::poll(Frame& out)
{
    if (_frame_size > 0) {
        // Previous frame was not answered, discard it
        _drop(_frame_size);
        _frame_size = 0;
    }

    if (_rx_overflow) {
        __disable_irq();
        _rx_len = 0;
        _rx_overflow = false;
        __enable_irq();
        return Poll::error;
    }

    size_t len = _rx_len;
    if (len == 0) return Poll::none;

    // Resync: anything before the magic is discarded (e.g. modem probing)
    if (_rx_buf[0] != kMagic[0] || (len > 1 && _rx_buf[1] != kMagic[1])) {
        size_t idx = 1;
        while (idx < len && !(_rx_buf[idx] == kMagic[0] && (idx + 1 == len || _rx_buf[idx + 1] == kMagic[1]))) idx++;
        _drop(idx);
        return Poll::none;
    }

    auto is_stale = System::GetNow() - _rx_last_ms > kStaleFrameMs;
    if (len < kHeaderSize) {
        if (is_stale) {
            _drop(len);
            return Poll::error;
        }
        return Poll::none;
    }

    auto payload_size = get_u32(&_rx_buf[4]);
    if (payload_size > kMaxPayload) {
        _drop(len);
        return Poll::error;
    }

    if (len < kHeaderSize + payload_size) {
        if (is_stale) {
            _drop(len);
            return Poll::error;
        }
        return Poll::none;
    }

    auto crc = crc32_update(crc32(&_rx_buf[2], 6), &_rx_buf[kHeaderSize], payload_size);
    if (crc != get_u32(&_rx_buf[8])) {
        _drop(len);
        return Poll::error;
    }

    out.type = _rx_buf[2];
    out.seq = _rx_buf[3];
    out.payload = &_rx_buf[kHeaderSize];
    out.size = payload_size;
    _frame_size = kHeaderSize + payload_size;
    return Poll::frame;
}

bool Link::_wait_tx_idle(const uint32_t timeout_ms)
{
    auto start = System::GetNow();
    while (true) {
        auto hcdc = static_cast<USBD_CDC_HandleTypeDef*>(hUsbDeviceHS.pClassData);
        if (hcdc == nullptr || hcdc->TxState == 0) return true;
        if (System::GetNow() - start > timeout_ms) return false;
    }
}

uint8_t* Link::payload()
{
    // The TX buffer is handed to the USB stack as is, it must not
    // be overwritten while the previous response is still going out.
    _wait_tx_idle(kTxTimeoutMs);
    return &_tx_buf[kHeaderSize];
}

bool Link::send(const uint8_t type, const uint8_t seq, const size_t size)
{
    if (_frame_size > 0) {
        _drop(_frame_size);
        _frame_size = 0;
    }

    _tx_buf[0] = kMagic[0];
    _tx_buf[1] = kMagic[1];
    _tx_buf[2] = type;
    _tx_buf[3] = seq;
    put_u32(&_tx_buf[4], size);
    put_u32(&_tx_buf[8], crc32_update(crc32(&_tx_buf[2], 6), &_tx_buf[kHeaderSize], size));

    UsbHandle usb;
    auto start = System::GetNow();
    while (usb.TransmitExternal(_tx_buf, kHeaderSize + size) != UsbHandle::Result::OK) {
        if (System::GetNow() - start > kTxTimeoutMs) return false;
    }
    return true;
}

bool Link::send_status(const uint8_t type, const uint8_t seq, const Status status)
{
    payload()[0] = static_cast<uint8_t>(status);
    return send(type, seq, 1);
}
