#pragma once

#include <stddef.h>
#include <stdint.h>
#include "hw/card.h"
#include "usb_transfer/server.h"
#include "protocol.h"
#include "nocopy.h"

namespace spotykach::transfer {

/*
Transfer mode session: Spotykach's commands on top of usb_transfer::Server
(lib/usb-transfer), which runs the link on the external (rear) port and
answers HELLO's framing, PING, EXIT and frame errors.
The card must be mounted and in Card::State::transfer.
*/
class Service: public usb_transfer::Handler {
  public:
    using Frame = usb_transfer::Link::Frame;

    Service() = default;
    ~Service() = default;

    void init(Card* card, const uint32_t max_frames);

    void begin();
    void end();
    void process();

    bool should_exit() const { return _server.should_exit(); }
    bool is_busy() const { return _session != Session::idle; }
    float progress() const;

    // usb_transfer::Handler
    void hello(usb_transfer::Server&, const Frame&) override;
    bool handle(usb_transfer::Server&, const Frame&) override;
    void reset_session() override { _reset_session(); }

  private:
    NOCOPY(Service)

    enum class Session: uint8_t {
      idle,
      uploading,
      downloading
    };

    void _reset_session();
    void _remove_temp_files();

    void _list(const Frame&);
    void _upload_begin(const Frame&);
    void _upload_data(const Frame&);
    void _upload_commit(const Frame&);
    void _download_open(const Frame&);
    void _download_read(const Frame&);
    void _remove(const Frame&);
    void _config_read(const Frame&);
    void _config_write(const Frame&);
    void _file_read(const Frame&);
    void _file_write(const Frame&);

    size_t _list_entry(const uint8_t tape, const uint8_t slot, uint8_t* out);
    Status _validate_upload();
    void _reply(const Frame&, const Status);
    void _reply_upload(const Frame&, const Status);
    bool _slot_args(const Frame&, uint8_t& tape, uint8_t& slot);

    Card*    _card;
    uint32_t _max_frames;
    usb_transfer::Server _server;

    Session  _session;

    uint8_t  _tape;
    uint8_t  _slot;
    uint32_t _size;
    uint32_t _offset;
    uint32_t _crc;
    uint32_t _expected_crc;
};

};
