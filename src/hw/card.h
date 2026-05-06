#pragma once

#include <daisy_seed.h>
#include "nocopy.h"

namespace spotykach {

class Card
{
  public:
    static constexpr size_t kChunk = 32768; //32KB

    enum class State: uint8_t {
      unmounted,
      mounting,
      idle,
      read_file,
      write_file,
      write_audio,
      read_audio,
      failed
    };

    struct AudioData {
      uint8_t* header;
      size_t header_size;
      uint8_t* body;
      size_t body_size;
      char* root_dir;
      char* deck_dir;
      char* tape_dir;
      char* file_name;
      size_t* cue_points;
      uint8_t* cue_count;
    };

    Card();
    ~Card() = default;

    State state() const { return _state; };
    float progress() const { 
      if (_audio_size == 0) return 0.f;
      return std::clamp(static_cast<float>(_offset) / _audio_size, 0.f, 1.f); 
    }

    void init(uint8_t* buffer);
    void recognize();
    bool mount();
    void unmount();
    

    bool file_exists(const char* path);

    void init_read_audio(AudioData data);
    void read_audio();
    size_t size_audio() const { return _size_read_audio; }

    void init_write_audio(AudioData data);
    void write_audio();

    bool write_file(const char* path, const uint8_t* in_data, const size_t in_size);
    bool read_file(const char* path, uint8_t*& out_data, size_t* out_size);

    bool notify_finish_processing();

    void cancel();

  private:
    NOCOPY(Card)

    void _close_file();

    daisy::SdmmcHandler   _sd;
    daisy::FatFSInterface _fsi;
    FIL   _sdfile;
    State _state;    

    uint8_t*  _buffer;
    uint8_t*  _bytes;
    size_t*   _slices;
    size_t    _audio_size;
    size_t    _hdr_size;
    size_t    _offset;
    size_t    _size_read_audio;
    uint8_t*  _slice_count;

    bool _notify_finish_processing;
};
};
