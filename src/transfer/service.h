#pragma once

#include <stddef.h>
#include <stdint.h>
#include "hw/card.h"
#include "link.h"
#include "nocopy.h"

namespace spotykach::transfer {

/*
Transfer mode session: answers host requests over the Link
and performs the file operations on the card.
The card must be mounted and in Card::State::transfer.
*/
class Service {
  public:
    Service() = default;
    ~Service() = default;

    void init(Card* card, const uint32_t max_frames);

    void begin();
    void end();
    void process();

    bool should_exit() const { return _should_exit; }
    bool is_busy() const { return _session != Session::idle; }
    float progress() const;

  private:
    NOCOPY(Service)

    enum class Session: uint8_t {
      idle,
      uploading,
      downloading
    };

    void _handle(const Link::Frame&);
    void _reset_session();
    void _remove_temp_files();

    void _hello(const Link::Frame&);
    void _list(const Link::Frame&);
    void _upload_begin(const Link::Frame&);
    void _upload_data(const Link::Frame&);
    void _upload_commit(const Link::Frame&);
    void _download_open(const Link::Frame&);
    void _download_read(const Link::Frame&);
    void _remove(const Link::Frame&);

    size_t _list_entry(const uint8_t tape, const uint8_t slot, uint8_t* out);
    Status _validate_upload();
    void _reply(const Link::Frame&, const Status);
    bool _slot_args(const Link::Frame&, uint8_t& tape, uint8_t& slot);

    Card*    _card;
    Link     _link;
    uint32_t _max_frames;

    Session  _session;
    bool     _should_exit;
    uint32_t _last_frame_ms;
    bool     _is_connected;

    uint8_t  _tape;
    uint8_t  _slot;
    uint32_t _size;
    uint32_t _offset;
    uint32_t _crc;
    uint32_t _expected_crc;
};

};
