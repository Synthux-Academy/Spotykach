#pragma once

#include <stddef.h>
#include <stdint.h>
#include "usb_transfer/protocol.h"
#include "usb_transfer/sysex.h"

/*
USB sample transfer protocol, see docs/usb-transfer.md. The frames, CRC, frame
errors and HELLO/PING/EXIT are the shared part in lib/usb-transfer; this is
what Spotykach adds. All multi-byte values are little-endian.
*/
namespace spotykach::transfer {

static constexpr uint8_t kProtoMajor = 1;
static constexpr uint8_t kProtoMinor = 2; // 1.1: config_read, config_write; 1.2: file_read, file_write

// SysEx trigger: F0 7D 53 4B <cmd> F7, see usb_transfer/sysex.h
static constexpr auto kSysExSignature = usb_transfer::sysex::signature('S', 'K');

enum class EnterStatus: uint8_t {
    ok      = 0,
    busy    = 1,
    no_card = 2
};

// Frame ///////////////////////////////////////////
static constexpr uint8_t  kMagic[2] = { 0x53, 0x4B }; // "SK"
static constexpr size_t   kMaxChunk = 32768;
static constexpr size_t   kMaxPayload = kMaxChunk + 16;
static constexpr size_t   kMaxFrame = usb_transfer::frame_size(kMaxPayload);
static constexpr uint32_t kMaxNameLength = 64;
// FILE_READ / FILE_WRITE: the path field that starts the payload, and the largest file
static constexpr size_t   kFileNameSize = 32;
static constexpr size_t   kMaxFileSize = kMaxChunk - kFileNameSize;

using usb_transfer::kHeaderSize;
using usb_transfer::kResponseFlag;
using usb_transfer::kFrameErrorType;
using usb_transfer::FrameError;

// hello, ping and exit are answered by usb_transfer::Server
enum class Command: uint8_t {
    hello          = 0x01,
    ping           = 0x02,
    list           = 0x03,
    upload_begin   = 0x10,
    upload_data    = 0x11,
    upload_commit  = 0x12,
    upload_abort   = 0x13,
    download_open  = 0x20,
    download_read  = 0x21,
    download_close = 0x22,
    remove         = 0x30,
    config_read    = 0x40,
    config_write   = 0x41,
    file_read      = 0x42,
    file_write     = 0x43,
    exit           = 0x7F
};

// 0–4 are the shared codes (usb_transfer::Status)
enum class Status: uint8_t {
    ok          = 0,
    bad_frame   = 1,
    unknown_cmd = 2,
    bad_state   = 3,
    bad_arg     = 4,
    no_card     = 5,
    io          = 6,
    not_found   = 7,
    no_space    = 8,
    too_large   = 9,
    bad_offset  = 10,
    bad_crc     = 11,
    bad_format  = 12
};
static_assert(static_cast<uint8_t>(Status::no_card) == usb_transfer::kFirstDeviceStatus, "device codes follow the shared ones");

// List entry flags
static constexpr uint8_t kSlotOccupied = 1 << 0;
static constexpr uint8_t kSlotLoadable = 1 << 1;
static constexpr uint8_t kSlotParsed   = 1 << 2;

using usb_transfer::crc32_update;
using usb_transfer::crc32;
using usb_transfer::get_u16;
using usb_transfer::get_u32;
using usb_transfer::put_u16;
using usb_transfer::put_u32;

};
