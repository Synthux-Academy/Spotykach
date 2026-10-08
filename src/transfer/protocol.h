#pragma once

#include <stddef.h>
#include <stdint.h>

/*
USB sample transfer protocol, see docs/usb-transfer.md.
All multi-byte values are little-endian.
*/
namespace spotykach::transfer {

static constexpr uint8_t kProtoMajor = 1;
static constexpr uint8_t kProtoMinor = 0;

// SysEx trigger: F0 7D 53 4B <cmd> F7 /////////////
static constexpr uint8_t kSysExManufacturer = 0x7D; // non-commercial
static constexpr uint8_t kSysExSignature[2] = { 0x53, 0x4B }; // "SK"
static constexpr uint8_t kSysExEnter = 0x01;
static constexpr uint8_t kSysExReply = 0x02;

enum class EnterStatus: uint8_t {
    ok      = 0,
    busy    = 1,
    no_card = 2
};

// Frame ///////////////////////////////////////////
static constexpr uint8_t  kMagic[2] = { 0x53, 0x4B }; // "SK"
static constexpr size_t   kHeaderSize = 12;
static constexpr size_t   kMaxChunk = 32768;
static constexpr size_t   kMaxPayload = kMaxChunk + 16;
static constexpr size_t   kMaxFrame = kHeaderSize + kMaxPayload;
static constexpr uint8_t  kResponseFlag = 0x80;
static constexpr uint8_t  kFrameErrorType = 0x80; // response to the reserved command 0x00
static constexpr uint32_t kMaxNameLength = 64;

enum class Command: uint8_t {
    // 0x00 is reserved, its response type marks frame errors
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
    exit           = 0x7F
};

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

// List entry flags
static constexpr uint8_t kSlotOccupied = 1 << 0;
static constexpr uint8_t kSlotLoadable = 1 << 1;
static constexpr uint8_t kSlotParsed   = 1 << 2;

// CRC-32 (IEEE 802.3, same as zlib) ///////////////
uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t size);
inline uint32_t crc32(const uint8_t* data, size_t size) { return crc32_update(0, data, size); }

// Little-endian helpers ///////////////////////////
inline uint16_t get_u16(const uint8_t* p) { return p[0] | (p[1] << 8); }
inline uint32_t get_u32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
inline void put_u16(uint8_t* p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
inline void put_u32(uint8_t* p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

};
