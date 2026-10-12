#pragma once

#include <stdint.h>
#include "hw/hardware.h"
#include "core/core.h"
#include "ui/core.ui.h"
#include "memory/storage.h"
#include "service.h"
#include "nocopy.h"

namespace spotykach {

/*
USB transfer mode: switches the rear USB port from MIDI to CDC when the host
asks for it over SysEx, runs the transfer session and switches back.
See docs/usb-transfer.md.
*/
class USBTransfer {
  public:
    USBTransfer(Hardware&, Core&, CoreUI&, Storage&);
    ~USBTransfer() = default;

    void init();

    /* While active, audio is silent and the main loop runs only process(). */
    bool is_active() const { return _is_active; }

    /* Main loop, normal mode: enters transfer mode if the host asked for it. */
    void check_request();

    /* Main loop, transfer mode: serves the host and leaves when done. */
    void process();

  private:
    NOCOPY(USBTransfer)

    void _enter();
    void _exit();

    Hardware& _hw;
    Core&     _core;
    CoreUI&   _ui;
    Storage&  _storage;

    transfer::Service _service;
    volatile bool     _is_active { false };
    uint32_t          _pads_ms { 0 };
};

};
