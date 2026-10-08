#include "service.h"

#include <algorithm>
#include <cstring>
#include <daisy_seed.h>
#include "core/config.h"
#include "memory/slots.h"
#include "memory/wav.h"

using namespace spotykach;
using namespace spotykach::transfer;

static constexpr uint32_t kIdleTimeoutMs = 10000;
static constexpr uint32_t kListHeaderRead = 4096;
static constexpr uint32_t kListTailRead = 8192;
static constexpr uint32_t kUploadOverhead = 65536; // header, name and cue chunks

static constexpr const char* kWavExt = ".wav";
static constexpr const char* kTempExt = ".tmp";

static bool is_loadable(const WavInfo& info)
{
    return info.audio_format == 3
        && info.channels == 2
        && info.sample_rate == 48000
        && info.bits_per_sample == 32
        && info.block_align == 8
        && info.data_size > 0;
}

void Service::init(Card* card, const uint32_t max_frames)
{
    _card = card;
    _max_frames = max_frames;
    _session = Session::idle;
    _should_exit = false;
}

void Service::begin()
{
    _should_exit = false;
    _session = Session::idle;
    _remove_temp_files();
    _link.start();
    _last_frame_ms = daisy::System::GetNow();
}

void Service::end()
{
    _reset_session();
    _link.stop();
}

float Service::progress() const
{
    if (_session == Session::idle || _size == 0) return 0.f;
    return std::clamp(static_cast<float>(_offset) / _size, 0.f, 1.f);
}

void Service::process()
{
    if (_should_exit) return;

    Link::Frame frame;
    switch (_link.poll(frame)) {
        case Link::Poll::none: break;
        case Link::Poll::error:
            _link.send_status(kFrameErrorType, 0, Status::bad_frame);
            _reset_session();
            break;
        case Link::Poll::frame:
            _last_frame_ms = daisy::System::GetNow();
            _handle(frame);
            break;
    }

    if (daisy::System::GetNow() - _last_frame_ms > kIdleTimeoutMs) {
        _should_exit = true;
    }
}

void Service::_handle(const Link::Frame& frame)
{
    auto session = _session;
    switch (static_cast<Command>(frame.type)) {
        case Command::hello: _hello(frame); break;
        case Command::ping: _reply(frame, Status::ok); break;

        case Command::list:
            if (session != Session::idle) _reply(frame, Status::bad_state);
            else _list(frame);
            break;

        case Command::upload_begin:
            if (session != Session::idle) _reply(frame, Status::bad_state);
            else _upload_begin(frame);
            break;

        case Command::upload_data:
            if (session != Session::uploading) _reply(frame, Status::bad_state);
            else _upload_data(frame);
            break;

        case Command::upload_commit:
            if (session != Session::uploading) _reply(frame, Status::bad_state);
            else _upload_commit(frame);
            break;

        case Command::upload_abort:
            if (session == Session::uploading) _reset_session();
            _reply(frame, Status::ok);
            break;

        case Command::download_open:
            if (session != Session::idle) _reply(frame, Status::bad_state);
            else _download_open(frame);
            break;

        case Command::download_read:
            if (session != Session::downloading) _reply(frame, Status::bad_state);
            else _download_read(frame);
            break;

        case Command::download_close:
            if (session == Session::downloading) _reset_session();
            _reply(frame, Status::ok);
            break;

        case Command::remove:
            if (session != Session::idle) _reply(frame, Status::bad_state);
            else _remove(frame);
            break;

        case Command::exit:
            _reset_session();
            _reply(frame, Status::ok);
            _should_exit = true;
            break;

        default: _reply(frame, Status::unknown_cmd);
    }
}

void Service::_reply(const Link::Frame& frame, const Status status)
{
    _link.send_status(frame.type | kResponseFlag, frame.seq, status);
}

bool Service::_slot_args(const Link::Frame& frame, uint8_t& tape, uint8_t& slot)
{
    if (frame.size < 4) return false;
    tape = frame.payload[0];
    slot = frame.payload[1];
    return tape < kStorageTapeCount && slot < kStorageSlotCount;
}

void Service::_reset_session()
{
    auto session = _session;
    _session = Session::idle;
    _card->close();
    if (session == Session::uploading) {
        char path[16];
        storage_slot_path(_tape, _slot, kTempExt, path);
        _card->remove(path);
    }
    _size = 0;
    _offset = 0;
}

void Service::_remove_temp_files()
{
    char path[16];
    for (uint8_t tape = 0; tape < kStorageTapeCount; tape++) {
        for (uint8_t slot = 0; slot < kStorageSlotCount; slot++) {
            storage_slot_path(tape, slot, kTempExt, path);
            _card->remove(path);
        }
    }
}

// HELLO ///////////////////////////////////////////
void Service::_hello(const Link::Frame& frame)
{
    _reset_session();

    uint32_t total_kib = 0;
    uint32_t free_kib = 0;
    auto has_space = _card->space_kib(total_kib, free_kib);

    auto p = _link.payload();
    std::memset(p, 0, 35);
    p[0] = static_cast<uint8_t>(has_space ? Status::ok : Status::no_card);
    p[1] = kProtoMajor;
    p[2] = kProtoMinor;
    p[3] = kStorageTapeCount;
    p[4] = kStorageSlotCount;
    for (uint8_t i = 0; i < kStorageTapeCount; i++) p[5 + i] = storage_tape_name(i)[0];
    put_u32(&p[14], kMaxChunk);
    put_u32(&p[18], _max_frames);
    put_u16(&p[22], kMaxFileCuePoints);
    put_u32(&p[26], total_kib);
    put_u32(&p[30], free_kib);
    auto version_length = std::min(strlen(kFirmwareVersion), (size_t)255);
    p[34] = version_length;
    std::memcpy(&p[35], kFirmwareVersion, version_length);
    _link.send(frame.type | kResponseFlag, frame.seq, 35 + version_length);
}

// LIST ////////////////////////////////////////////
size_t Service::_list_entry(const uint8_t tape, const uint8_t slot, uint8_t* out)
{
    static constexpr size_t kEntrySize = 20;
    std::memset(out, 0, kEntrySize);
    out[0] = tape;
    out[1] = slot;

    char path[16];
    storage_slot_path(tape, slot, kWavExt, path);
    uint32_t file_size = 0;
    if (!_card->stat(path, file_size) || file_size == 0) return kEntrySize;

    uint8_t flags = kSlotOccupied;
    put_u32(&out[4], file_size);

    uint32_t size = 0;
    if (!_card->open_read(path, size)) {
        out[2] = flags;
        return kEntrySize;
    }

    auto buf = _card->buffer();
    size_t got = 0;
    WavInfo info;
    auto parsed = _card->read(buf, kListHeaderRead, got) && wav_info(buf, got, info);
    if (!parsed && got == kListHeaderRead) {
        // Large chunks before "data", look further
        parsed = _card->seek(0) && _card->read(buf, Card::kChunk, got) && wav_info(buf, got, info);
    }

    auto name_length = 0;
    if (parsed) {
        flags |= kSlotParsed;
        if (is_loadable(info)) flags |= kSlotLoadable;

        auto cue_count = info.cue_count;
        uint64_t data_end = (uint64_t)info.data_offset + info.data_size + (info.data_size % 2);
        if (data_end < file_size
            && _card->seek(data_end)
            && _card->read(buf, kListTailRead, got)) {
            cue_count = std::max(cue_count, wav_cue_count(buf, got));
        }

        put_u32(&out[8], info.block_align ? info.data_size / info.block_align : 0);
        put_u32(&out[12], info.sample_rate);
        put_u16(&out[16], cue_count);
        out[18] = std::min(info.channels, (uint16_t)255);
        out[19] = std::min(info.bits_per_sample, (uint16_t)255);

        name_length = std::min((uint32_t)info.name_length, kMaxNameLength);
        std::memcpy(&out[kEntrySize], info.name, name_length);
    }
    _card->close();

    out[2] = flags;
    out[3] = name_length;
    return kEntrySize + name_length;
}

void Service::_list(const Link::Frame& frame)
{
    auto p = _link.payload();
    p[0] = static_cast<uint8_t>(Status::ok);
    p[1] = kStorageTapeCount * kStorageSlotCount;
    size_t size = 2;
    for (uint8_t tape = 0; tape < kStorageTapeCount; tape++) {
        for (uint8_t slot = 0; slot < kStorageSlotCount; slot++) {
            size += _list_entry(tape, slot, &p[size]);
        }
    }
    _link.send(frame.type | kResponseFlag, frame.seq, size);
}

// UPLOAD //////////////////////////////////////////
void Service::_upload_begin(const Link::Frame& frame)
{
    uint8_t tape, slot;
    if (frame.size < 12 || !_slot_args(frame, tape, slot)) {
        _reply(frame, Status::bad_arg);
        return;
    }
    auto size = get_u32(&frame.payload[4]);
    auto crc = get_u32(&frame.payload[8]);

    if (size == 0) {
        _reply(frame, Status::bad_arg);
        return;
    }
    if (size > _max_frames * 8 + kUploadOverhead) {
        _reply(frame, Status::too_large);
        return;
    }

    uint32_t total_kib, free_kib;
    if (!_card->space_kib(total_kib, free_kib)) {
        _reply(frame, Status::no_card);
        return;
    }
    if ((uint64_t)free_kib * 1024 < size + 32768) { // + a cluster or so for the directory entries
        _reply(frame, Status::no_space);
        return;
    }

    char path[16];
    storage_root_path(path);
    auto dirs_ok = _card->make_dir(path);
    storage_tape_path(tape, path);
    dirs_ok = dirs_ok && _card->make_dir(path);

    storage_slot_path(tape, slot, kTempExt, path);
    if (!dirs_ok || !_card->open_write(path)) {
        _reply(frame, Status::io);
        return;
    }

    _tape = tape;
    _slot = slot;
    _size = size;
    _offset = 0;
    _crc = 0;
    _expected_crc = crc;
    _session = Session::uploading;
    _reply(frame, Status::ok);
}

void Service::_upload_data(const Link::Frame& frame)
{
    if (frame.size < 4) {
        _reply(frame, Status::bad_arg);
        return;
    }
    auto offset = get_u32(frame.payload);
    auto data = &frame.payload[4];
    auto length = frame.size - 4;

    if (offset != _offset) {
        _reply(frame, Status::bad_offset);
        return;
    }
    if (length == 0 || length > kMaxChunk || (uint64_t)_offset + length > _size) {
        _reply(frame, Status::bad_arg);
        return;
    }
    if (!_card->write(data, length)) {
        _reset_session();
        _reply(frame, Status::io);
        return;
    }
    _crc = crc32_update(_crc, data, length);
    _offset += length;
    _reply(frame, Status::ok);
}

Status Service::_validate_upload()
{
    if (_offset != _size) return Status::bad_offset;
    if (_crc != _expected_crc) return Status::bad_crc;
    if (!_card->close()) return Status::io;

    char path[16];
    storage_slot_path(_tape, _slot, kTempExt, path);
    uint32_t size = 0;
    if (!_card->open_read(path, size)) return Status::io;

    auto buf = _card->buffer();
    size_t got = 0;
    WavInfo info;
    auto is_valid = _card->read(buf, Card::kChunk, got) && wav_info(buf, got, info) && is_loadable(info);
    _card->close();
    return is_valid ? Status::ok : Status::bad_format;
}

void Service::_upload_commit(const Link::Frame& frame)
{
    auto status = _validate_upload();
    if (status != Status::ok) {
        _reset_session();
        _reply(frame, status);
        return;
    }

    char temp_path[16];
    char path[16];
    storage_slot_path(_tape, _slot, kTempExt, temp_path);
    storage_slot_path(_tape, _slot, kWavExt, path);
    if (!_card->remove(path) || !_card->rename(temp_path, path)) {
        _reset_session();
        _reply(frame, Status::io);
        return;
    }

    _session = Session::idle;
    _size = 0;
    _offset = 0;
    _reply(frame, Status::ok);
}

// DOWNLOAD ////////////////////////////////////////
void Service::_download_open(const Link::Frame& frame)
{
    uint8_t tape, slot;
    if (!_slot_args(frame, tape, slot)) {
        _reply(frame, Status::bad_arg);
        return;
    }

    char path[16];
    storage_slot_path(tape, slot, kWavExt, path);
    uint32_t size = 0;
    if (!_card->stat(path, size) || size == 0) {
        _reply(frame, Status::not_found);
        return;
    }
    if (!_card->open_read(path, size)) {
        _reply(frame, Status::io);
        return;
    }

    _tape = tape;
    _slot = slot;
    _size = size;
    _offset = 0;
    _session = Session::downloading;

    auto p = _link.payload();
    p[0] = static_cast<uint8_t>(Status::ok);
    p[1] = p[2] = p[3] = 0;
    put_u32(&p[4], size);
    _link.send(frame.type | kResponseFlag, frame.seq, 8);
}

void Service::_download_read(const Link::Frame& frame)
{
    if (frame.size < 8) {
        _reply(frame, Status::bad_arg);
        return;
    }
    auto offset = get_u32(frame.payload);
    auto length = get_u32(&frame.payload[4]);
    if (length == 0 || length > kMaxChunk) {
        _reply(frame, Status::bad_arg);
        return;
    }
    if ((uint64_t)offset + length > _size) {
        _reply(frame, Status::bad_offset);
        return;
    }

    if (offset != _card->tell() && !_card->seek(offset)) {
        _reset_session();
        _reply(frame, Status::io);
        return;
    }

    auto p = _link.payload();
    size_t got = 0;
    if (!_card->read(&p[4], length, got) || got != length) {
        _reset_session();
        _reply(frame, Status::io);
        return;
    }
    _offset = offset + length;

    p[0] = static_cast<uint8_t>(Status::ok);
    p[1] = p[2] = p[3] = 0;
    _link.send(frame.type | kResponseFlag, frame.seq, 4 + length);
}

// DELETE //////////////////////////////////////////
void Service::_remove(const Link::Frame& frame)
{
    uint8_t tape, slot;
    if (!_slot_args(frame, tape, slot)) {
        _reply(frame, Status::bad_arg);
        return;
    }
    char path[16];
    storage_slot_path(tape, slot, kWavExt, path);
    _reply(frame, _card->remove(path) ? Status::ok : Status::io);
}
