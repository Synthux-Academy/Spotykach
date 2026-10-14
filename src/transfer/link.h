#pragma once

#include <stddef.h>
#include <stdint.h>
#include "protocol.h"
#include "nocopy.h"

namespace spotykach::transfer {

/*
Framing over USB CDC on the external (rear) port.
The host waits for a response before sending the next request,
so a single frame buffer is enough on both sides.
*/
class Link {
  public:
    struct Frame {
      uint8_t type;
      uint8_t seq;
      const uint8_t* payload;
      size_t size;
    };

    enum class Poll: uint8_t {
      none,
      frame,
      error
    };

    Link() = default;
    ~Link() = default;

    void start();
    void stop();

    /* Checks the receive buffer for a complete frame. After Poll::frame
    the frame stays valid until the next call to send(). */
    Poll poll(Frame& out);

    /* Response payload is written by the caller into payload()
    (status byte first) before calling send(). */
    uint8_t* payload();
    bool send(const uint8_t type, const uint8_t seq, const size_t size);
    bool send_status(const uint8_t type, const uint8_t seq, const Status);

    uint32_t last_rx_ms() const;
    /* Why the last Poll::error happened */
    FrameError last_error() const { return _last_error; }

  private:
    NOCOPY(Link)

    void _drop(const size_t count);
    Poll _frame_error(const FrameError);

    FrameError _last_error = FrameError::unknown;
    bool _wait_tx_idle(const uint32_t timeout_ms);
};

};
