#include "core.midi.h"
#include "core/config.h"
#include "daisy.h"
#include "core/config.h"
#include "expose.h"
#include "transfer/protocol.h"

using namespace spotykach;
using namespace daisy;

CoreMIDI::CoreMIDI(Hardware& hw, Core& core):
_hw     { hw },
_core   { core }
{}

void CoreMIDI::send_clock()
{
    _hw.midi_uart.EnqueueMessage(MidiTxMessage::SystemRealtimeClock());
    _hw.midi_usb.EnqueueMessage(MidiTxMessage::SystemRealtimeClock());
}

bool CoreMIDI::process()
{
    bool has_clock = false;

    _hw.midi_uart.Listen();
    while(_hw.midi_uart.HasEvents()) {
        auto event = _hw.midi_uart.PopEvent();
        has_clock = _process_event(event, false) || has_clock;
    }

    #ifndef DEBUG
    _hw.midi_usb.Listen();
    while(_hw.midi_usb.HasEvents()) {
        auto event = _hw.midi_usb.PopEvent();
        has_clock = _process_event(event, true) || has_clock;
    }

    if (_transfer_reply >= 0) {
        using namespace transfer;
        const uint8_t reply[] = { 
            0xF0, 
            kSysExManufacturer, 
            kSysExSignature[0], 
            kSysExSignature[1], 
            kSysExReply, 
            static_cast<uint8_t>(_transfer_reply), 
            0xF7 
        };
        _hw.midi_usb.SendMessage(reply, sizeof(reply));
        _transfer_reply = -1;
    }
    #endif

    // Modified libDaisy MIDI handlers require explicit call to transmit
    // enqueued messages instead of blocking every time a message is sent
    _hw.midi_uart.TransmitEnqueuedMessages();
    #ifndef DEBUG
    _hw.midi_usb.TransmitEnqueuedMessages();
    #endif
    
    return has_clock;
}
bool CoreMIDI::_process_event(daisy::MidiEvent& event, const bool is_usb)
{
    switch(event.type) {
        case MidiMessageType::SystemCommon: {
            if (is_usb && event.sc_type == SystemCommonType::SystemExclusive) _process_sysex(event);
            return false;
        }

        case MidiMessageType::SystemRealTime: {
            return _process_realtime(event); 
        }
        
        case MidiMessageType::NoteOn: {
            auto e = event.AsNoteOn();
            _process_note_on(e);
            return false;
        }

        case MidiMessageType::ControlChange: {
            auto e = event.AsControlChange();
            _process_cc(e);
            return false;
        }

        default: 
            return false;
    }
}

void CoreMIDI::_process_note_on(daisy::NoteOnEvent& note_on)
{
    auto ref = Deck::Count;
    auto& c = Config::dynamic();
    if (note_on.channel == c.midi_channel(Deck::A)) ref = Deck::A;
    else if (note_on.channel == c.midi_channel(Deck::B)) ref = Deck::B;
    if (ref != Deck::Count && _on_note_on) {
        _on_note_on(ref, note_on.note);
    }
}

void CoreMIDI::_process_sysex(daisy::MidiEvent& event)
{
    using namespace transfer;
    // Data comes without the F0/F7 framing
    auto data = event.sysex_data;
    if (event.sysex_message_len == 4
        && data[0] == kSysExManufacturer
        && data[1] == kSysExSignature[0]
        && data[2] == kSysExSignature[1]
        && data[3] == kSysExEnter) {
        _transfer_requested = true;
    }
}

bool CoreMIDI::_process_realtime(daisy::MidiEvent& event)
{
    auto& c = Config::dynamic();

    switch (event.srt_type) {
        case SystemRealTimeType::TimingClock: return true;
        case SystemRealTimeType::Start:
        case SystemRealTimeType::Continue: {
            _core.driver().reset();
            if (c.midi_play_stop(Deck::A) && !_core.deck(Deck::A).is_empty()) _core.deck(Deck::A).play();
            if (c.midi_play_stop(Deck::B) && !_core.deck(Deck::B).is_empty()) _core.deck(Deck::B).play();    
            break;
        }

        case SystemRealTimeType::Stop: {
            if (c.midi_play_stop(Deck::A)) _core.deck(Deck::A).stop();
            if (c.midi_play_stop(Deck::B)) _core.deck(Deck::B).stop();
            break;
        }

        default: break;
    }
    
    return false;
}

void CoreMIDI::_process_cc(daisy::ControlChangeEvent& event)
{
    auto ref = Deck::Count;
    auto& c = Config::dynamic();
    if (event.channel == c.midi_channel(Deck::A)) ref = Deck::A;
    else if (event.channel == c.midi_channel(Deck::B)) ref = Deck::B;
    switch (event.control_number) {
        case CC::RecExt: {
            if (event.value > 0 && _on_record) _on_record(ref, false); 
            break;
        }
        case CC::RecInt: {
            if (event.value > 0 && _on_record) _on_record(ref, true); 
            break;
        }
        case CC::Fwd: {
            if (event.value > 0 && _on_play) _on_play(ref, false); 
            break;
        }
        case CC::Rev: {
            if (event.value > 0 && _on_play) _on_play(ref, true); 
            break;
        }
        default: 
            if (_on_cc) _on_cc(ref, (CC)event.control_number, std::clamp(event.value / 127.f, 0.f, 1.f));
    }
}
