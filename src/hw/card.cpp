#include "card.h"
#include "../memory/wav.h"

using namespace spotykach;
using namespace daisy;

Card::Card():
_state { State::unmounted }
{}

void Card::init(uint8_t* buffer) {
    _buffer = buffer;
}

bool Card::file_exists(const char* path)
{
    FILINFO fno;
    return f_stat(path, &fno) == FR_OK && fno.fsize > 0; 
}

void Card::init_read_audio(AudioData data) 
{   
    if (_state != State::idle) return;

    _size_read_audio = 0;

    char audio_path[11];
    sprintf(audio_path, "/%s/%s/%s", data.root_dir, data.tape_dir, data.file_name); // /SK/G/1.WAV

    WavHeader hdr;
    size_t hdr_size = 0;
    size_t bytesread = 0;
    
    if (f_open(&_sdfile, audio_path, FA_OPEN_EXISTING | FA_READ) != FR_OK) {
        _state = State::failed;
        return;
    }
    
    if (f_read(&_sdfile, _buffer, kChunk, (UINT*)&bytesread) != FR_OK
    || !wav_header(_buffer, data.cue_points, kChunk, hdr, hdr_size, data.cue_count, data.body_size)) {
        _state = State::failed;
        _close_file();
        return;
    }
    
    #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    if (hdr.NbrChannels == 2 
        && hdr.BitsPerSample == 32
        && hdr.SampleRate == 48000
        && hdr.AudioFormat == 3
        && hdr.DataSize > 0) {
            _offset = 0;
            _bytes = data.body;
            _slices = data.cue_points;
            _slice_count = data.cue_count;
            _hdr_size = hdr_size;
            _audio_size = std::min(data.body_size, (size_t)hdr.DataSize);
            _state = State::read_audio;        
    }
    else {
        _state = State::failed;
        _close_file();      
    }
    if (f_lseek(&_sdfile, hdr_size) != FR_OK) {
        _state = State::failed;
        _close_file();
    }
}

void Card::read_audio()
{
    if (_state != State::read_audio) {
        return;
    }
    
    size_t bytesread;
    if (f_read(&_sdfile, _buffer, kChunk, (UINT* )&bytesread) != FR_OK) {
        _state = State::failed;
        _close_file();
        return;
    }

    auto buf_len = std::min(_audio_size - _offset, bytesread);
    std::memcpy(&_bytes[_offset], _buffer, buf_len);
    
    _offset += buf_len;
    _size_read_audio = _offset * .125f; // 1 / (2 channels * 4 bytes)

    if (bytesread < kChunk || buf_len < bytesread) {
        if (f_lseek(&_sdfile, _hdr_size + _audio_size) == FR_OK 
         && f_read(&_sdfile, _buffer, kChunk, (UINT* )&bytesread) == FR_OK) {
            find_cue_points(
                _buffer,
                _slices,
                _slice_count,
                _audio_size / 8,
                bytesread
            );
            auto end_idx = (int32_t)*_slice_count - 1;
            if (end_idx >= 0 && end_idx < 31 && _slices[end_idx] < _audio_size) {
                _slices[end_idx + 1] = _size_read_audio;
                *_slice_count += 1;
            }
        }
        _notify_finish_processing = true;
        _state = State::idle;
        _close_file();
        return;
    }
}

void Card::init_write_audio(const AudioData data)
{
    if (_state != State::idle) return;

    auto res = f_mkdir(data.root_dir);
    if (res != FR_OK && res != FR_EXIST) {
        _state = State::failed;
        return;
    }

    char tape_dir_path[4];
    sprintf(tape_dir_path, "%s/%s", data.root_dir, data.tape_dir);
    res = f_mkdir(tape_dir_path);
    if (res != FR_OK && res != FR_EXIST) {
        _state = State::failed;
        return;
    }

    char audio_path[11]; // /SK/G/1.wav
    sprintf(audio_path, "%s/%s", tape_dir_path, data.file_name);
    if (f_open(&_sdfile, audio_path, (FA_CREATE_ALWAYS) | (FA_WRITE)) != FR_OK) {
        _state = State::failed;
        return;
    }

    uint32_t byteswritten;
    if (f_write(&_sdfile, data.header, data.header_size, (UINT*)&byteswritten) == FR_OK) {
        _bytes = (uint8_t*)data.body;
        _audio_size = data.body_size;
        _offset = 0;
        _state = State::write_audio;
    }
    else {
        _state = State::failed;
        _close_file();
    }
}

void Card::write_audio() 
{
    if (_state != State::write_audio) {
        return;
    }
    if (_offset >= _audio_size) {
        _notify_finish_processing = true;
        _state = State::idle;
        _close_file();
        return;
    }
    
    uint32_t byteswritten;
    auto write_len = std::min(_audio_size - _offset, kChunk);
    if (f_write(&_sdfile, &_bytes[_offset], write_len, (UINT*)&byteswritten) != FR_OK) {
        _state = State::failed;
        _close_file();
        return;
    }
    _offset += byteswritten;
}

bool Card::read_file(const char* path, uint8_t*& out_data, size_t* out_size)
{
    if (_state != State::idle) return false;

    _state = State::read_file;
    if (f_open(&_sdfile, path, FA_OPEN_EXISTING | FA_READ) != FR_OK) {
        _state = State::idle;
        return false;
    }
    
    if (f_read(&_sdfile, _buffer, kChunk, (UINT*)out_size) != FR_OK) {
        _state = State::idle;
        _close_file();
        return false;
    }
    
    _close_file();
    _state = State::idle;

    out_data = _buffer;
    
    return true;
}

bool Card::write_file(const char* path, const uint8_t* in_data, const size_t in_size)
{
    if (_state != State::idle) return false;

    _state = State::write_file;
    if (f_open(&_sdfile, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) {
        _state = State::idle;
        return false;
    }
    
    uint32_t byteswritten;
    if (f_write(&_sdfile, in_data, in_size, (UINT*)&byteswritten) != FR_OK) {
        _close_file();
        _state = State::idle;
        return false;
    }

    _close_file();
    _state = State::idle;
    
    return true;
}

void Card::_close_file()
{
    f_close(&_sdfile);
}

void Card::recognize()
{
    /* Init handler */
    SdmmcHandler::Config sd_cfg;
    sd_cfg.Defaults();
    sd_cfg.speed = SdmmcHandler::Speed::MEDIUM_SLOW;
    sd_cfg.width = SdmmcHandler::BusWidth::BITS_1;
    _sd.Init(sd_cfg);

    /* Links libdaisy i/o to fatfs driver. */
    _fsi.Init(FatFSInterface::Config::MEDIA_SD);

    _state = State::mounting;
}

bool Card::mount()
{
    /* Mount the card */
    auto path = _fsi.GetSDPath();
    if (f_mount(&_fsi.GetSDFileSystem(), path, 1) != FR_OK) {
        return false;
    }
    _state = State::idle;
    return true;
}

void Card::unmount()
{
    auto path = _fsi.GetSDPath();
    f_mount(NULL, path, 0);
    _fsi.DeInit();
    _state = State::unmounted;
}

bool Card::notify_finish_processing()
{
    auto did_finish = _notify_finish_processing;
    _notify_finish_processing = false;
    return did_finish;
}

void Card::cancel()
{
    _close_file();
    _state = State::idle;
}