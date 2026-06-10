#include "core.midi.h"
#include "config.h"
#include "daisy.h"
#include "expose.h"

using namespace spotykach;
using namespace daisy;

CoreMIDI::CoreMIDI(Hardware& hw, Core& core):
_hw     { hw },
_core   { core }
{}

void CoreMIDI::send_clock()
{
    _hw.midi_uart.EnqueueMessage(MidiTxMessage::SystemRealtimeClock());
}

bool CoreMIDI::process()
{
    _hw.midi_uart.Listen();
    bool has_clock = false;
    while(_hw.midi_uart.HasEvents())
    {
        auto event = _hw.midi_uart.PopEvent();
        switch(event.type)
        {
            case MidiMessageType::SystemRealTime: {
                has_clock = _process_realtime(event) || has_clock; 
            }
            break;
            
            case MidiMessageType::NoteOn: {
                auto e = event.AsNoteOn();
                _process_note_on(e);
            }
            break;

            case MidiMessageType::ControlChange: {
                auto e = event.AsControlChange();
                _process_cc(e);
            }

            default: break;
        }
    }
    // Modified libDaisy MIDI handlers require explicit call to transmit
    // enqueued messages instead of blocking every time a message is sent
    _hw.midi_uart.TransmitEnqueuedMessages();
    return has_clock;
}
void CoreMIDI::_process_note_on(daisy::NoteOnEvent& note_on)
{
    auto ref = Deck::Count;
    auto& c = Config::dynamic();
    if (note_on.channel == c.midi_channel_a()) ref = Deck::A;
    else if (note_on.channel == c.midi_channel_b()) ref = Deck::B;
    if (ref != Deck::Count && _on_note_on) {
        _on_note_on(ref, note_on.note);
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
            if (c.midi_play_stop_a() && !_core.deck(Deck::A).is_empty()) _core.deck(Deck::A).play();
            if (c.midi_play_stop_b() && !_core.deck(Deck::B).is_empty()) _core.deck(Deck::B).play();    
            break;
        }

        case SystemRealTimeType::Stop: {
            if (c.midi_play_stop_a()) _core.deck(Deck::A).stop();
            if (c.midi_play_stop_b()) _core.deck(Deck::B).stop();
            break;
        }

        default: break;
    }
    
    return false;
}

enum CC: uint8_t {
    XFade       = 3,
    RecExt      = 14,
    RecInt      = 15,
    Start       = 20,
    Size        = 21,
    Env         = 22,
    Pitch       = 23,
    IOMix       = 24,
    DeckFB      = 25,
    EnvSize     = 26,
    WinSize     = 27,
    Fwd         = 85,
    Rev         = 86,
    ModCycle    = 89,
    ModGlow     = 90,
    GritOn      = 102,
    GritIntens  = 103,
    GritMix     = 104,
    FluxOn      = 105,
    FluxIntes   = 106,
    FluxFB      = 107,
    FluxMix     = 108   
};

void CoreMIDI::_process_cc(daisy::ControlChangeEvent& event)
{
    auto ref = Deck::Count;
    auto& c = Config::dynamic();
    if (event.channel == c.midi_channel_a()) ref = Deck::A;
    else if (event.channel == c.midi_channel_b()) ref = Deck::B;

    switch (event.control_number) {
        case CC::XFade: break;
        case CC::RecExt: _handle_record(ref, false); break;
        case CC::RecInt: _handle_record(ref, true); break;
        case CC::Start: break;
        case CC::Size: break;
        case CC::Env: break;
        case CC::Pitch: break;
        case CC::IOMix: break;
        case CC::DeckFB: break;
        case CC::EnvSize: break;
        case CC::WinSize: break;
        case CC::Fwd: _handle_play(ref, event.value > 0, false); break;
        case CC::Rev: _handle_play(ref, event.value > 0, true); break;
        case CC::ModCycle: break;
        case CC::ModGlow: break;
        case CC::GritOn: break;
        case CC::GritIntens: break;
        case CC::GritMix: break;
        case CC::FluxOn: break;
        case CC::FluxIntes: break;
        case CC::FluxFB: break;
        case CC::FluxMix: break;
        default: break;
    }

}
void CoreMIDI::_handle_play(const Deck::Ref ref, const bool on, const bool reverse)
{
    auto& deck = _core.deck(ref);
    if (on != deck.is_playing() || reverse != deck.is_reverse()) {
        if (_on_play) _on_play(ref, reverse);
    }
}
 
void CoreMIDI::_handle_record(const Deck::Ref ref, const bool internal)
{
    if (_on_record) _on_record(ref, internal);
}