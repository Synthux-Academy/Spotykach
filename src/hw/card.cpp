#include "card.h"
#include "../memory/wav.h"
#include "../core/config.h"

using namespace spotykach;
using namespace daisy;

Card::Card():
_state { State::unmounted },
_is_file_open { false }
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

    char audio_path[16]; // /SK/G/1.wav
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
            _data_size = hdr.DataSize + (hdr.DataSize % 2); // Chunks are word-aligned
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
        /* Chunks after "data" (cue points) start at the end of the whole
        data chunk, which can be past the part that fit into the buffer. */
        if (f_lseek(&_sdfile, _hdr_size + _data_size) == FR_OK 
         && f_read(&_sdfile, _buffer, kChunk, (UINT* )&bytesread) == FR_OK) {
            find_cue_points(
                _buffer,
                _slices,
                _slice_count,
                _size_read_audio,
                bytesread
            );
        }
        auto count = *_slice_count;
        if (count > 0 && count < kMaxSlicePointCount && _slices[count - 1] < _size_read_audio) {
            _slices[count] = _size_read_audio;
            *_slice_count += 1;
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

    char tape_dir_path[8]; // SK/G
    sprintf(tape_dir_path, "%s/%s", data.root_dir, data.tape_dir);
    res = f_mkdir(tape_dir_path);
    if (res != FR_OK && res != FR_EXIST) {
        _state = State::failed;
        return;
    }

    char audio_path[16]; // SK/G/1.wav
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
// Transfer ////////////////////////////////////////
bool Card::begin_transfer()
{
    if (_state != State::idle) return false;
    _is_file_open = false;
    _state = State::transfer;
    return true;
}

void Card::end_transfer()
{
    if (_state != State::transfer) return;
    close();
    _state = State::idle;
}

bool Card::stat(const char* path, uint32_t& out_size)
{
    if (_state != State::transfer) return false;
    FILINFO fno;
    if (f_stat(path, &fno) != FR_OK) return false;
    out_size = fno.fsize;
    return true;
}

bool Card::open_read(const char* path, uint32_t& out_size)
{
    if (_state != State::transfer) return false;
    close();
    if (f_open(&_sdfile, path, FA_OPEN_EXISTING | FA_READ) != FR_OK) return false;
    _is_file_open = true;
    out_size = f_size(&_sdfile);
    return true;
}

bool Card::open_write(const char* path)
{
    if (_state != State::transfer) return false;
    close();
    if (f_open(&_sdfile, path, FA_CREATE_ALWAYS | FA_WRITE) != FR_OK) return false;
    _is_file_open = true;
    return true;
}

bool Card::read(uint8_t* out_data, const size_t size, size_t& out_read)
{
    if (!_is_file_open) return false;
    UINT bytesread = 0;
    auto res = f_read(&_sdfile, out_data, size, &bytesread);
    out_read = bytesread;
    return res == FR_OK;
}

bool Card::write(const uint8_t* in_data, const size_t size)
{
    if (!_is_file_open) return false;
    UINT byteswritten = 0;
    return f_write(&_sdfile, in_data, size, &byteswritten) == FR_OK && byteswritten == size;
}

bool Card::seek(const uint32_t offset)
{
    if (!_is_file_open) return false;
    return f_lseek(&_sdfile, offset) == FR_OK;
}

uint32_t Card::tell()
{
    return _is_file_open ? f_tell(&_sdfile) : 0;
}

bool Card::close()
{
    if (!_is_file_open) return true;
    _is_file_open = false;
    return f_close(&_sdfile) == FR_OK;
}

bool Card::remove(const char* path)
{
    if (_state != State::transfer) return false;
    auto res = f_unlink(path);
    return res == FR_OK || res == FR_NO_FILE;
}

bool Card::rename(const char* from, const char* to)
{
    if (_state != State::transfer) return false;
    return f_rename(from, to) == FR_OK;
}

bool Card::make_dir(const char* path)
{
    if (_state != State::transfer) return false;
    auto res = f_mkdir(path);
    return res == FR_OK || res == FR_EXIST;
}

bool Card::space_kib(uint32_t& out_total, uint32_t& out_free)
{
    if (_state != State::transfer) return false;
    FATFS* fs;
    DWORD free_clusters;
    if (f_getfree(_fsi.GetSDPath(), &free_clusters, &fs) != FR_OK) return false;
    auto sectors_per_cluster = (uint64_t)fs->csize;
    // _MIN_SS == _MAX_SS == 512, two sectors make a KiB
    out_total = (uint32_t)(((uint64_t)(fs->n_fatent - 2) * sectors_per_cluster) / 2);
    out_free = (uint32_t)(((uint64_t)free_clusters * sectors_per_cluster) / 2);
    return true;
}
