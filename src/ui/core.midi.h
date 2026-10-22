#pragma once

#include <functional>
#include <daisy_seed.h>
#include "core/core.h"
#include "hw/hardware.h"

namespace spotykach {
  
class CoreMIDI {
public:
    CoreMIDI(Hardware&, Core&);
    ~CoreMIDI() = default;

    bool process();
    void send_clock();

    void set_on_note_on(std::function<void(const Deck::Ref, const uint8_t)> on_note_on)
    {
        _on_note_on = on_note_on;
    }
    void set_on_play(std::function<void(const Deck::Ref, const bool reverse)> on_play)
    {
        _on_play = on_play;
    }
    void set_on_record(std::function<void(const Deck::Ref, const bool internal)> on_record)
    {
        _on_record = on_record;
    }
    void set_on_cc(std::function<void(const Deck::Ref, const CC, const float)> on_cc)
    {
        _on_cc = on_cc;
    }

    /* USB transfer mode request, set from the audio callback
    when the "enter" SysEx arrives over USB MIDI. */
    bool take_transfer_request()
    {
        auto is_requested = _transfer_requested;
        _transfer_requested = false;
        return is_requested;
    }
    /* Reply SysEx is sent from the next process() call */
    void reply_transfer(const uint8_t status) { _transfer_reply = status; }
    bool is_transfer_reply_pending() const { return _transfer_reply >= 0; }

private:
    bool _process_event(daisy::MidiEvent&, const bool is_usb);
    void _process_sysex(daisy::MidiEvent&);
    bool _process_realtime(daisy::MidiEvent&);
    void _process_note_on(daisy::NoteOnEvent&);
    void _process_cc(daisy::ControlChangeEvent&);
    void _handle_play(const Deck::Ref, const bool reverse);
    void _handle_record(const Deck::Ref, const bool internal);

    std::function<void(const Deck::Ref, const uint8_t num)> _on_note_on;
    std::function<void(const Deck::Ref, const bool reverse)> _on_play;
    std::function<void(const Deck::Ref, const bool internal)> _on_record;
    std::function<void(const Deck::Ref, const CC, const float)> _on_cc;

    Hardware&   _hw;
    Core&       _core;

    volatile bool    _transfer_requested { false };
    volatile int16_t _transfer_reply { -1 };
};
};
