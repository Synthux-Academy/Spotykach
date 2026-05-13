#include "core.ui.h"
#include "daisy.h"

using namespace spotykach;
using namespace daisy;

// CLOCK ///////////////////////////////////
static bool clock_state = false;
void CoreUI::tick()
{
    auto&d = _core.driver();
    auto new_state = false;
    auto midi_state = _process_midi();
    switch (d.source()) {
        case Driver::Source::ts4: new_state = _hw.GetClockInputState(); break;
        case Driver::Source::midi: new_state = midi_state; break;
        default: break;
    }
    d.tick(new_state && !clock_state);
    clock_state = new_state;

    // Modified libDaisy MIDI handlers require explicit call to transmit
    // enqueued messages instead of blocking every time a message is sent
    _hw.midi_uart.TransmitEnqueuedMessages();
}

// MIDI /////////////////////////////////////
bool CoreUI::_process_midi()
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

            default: break;
        }
    }
    return has_clock;
}
void CoreUI::_process_note_on(daisy::NoteOnEvent& note_on)
{
    auto ref = Deck::Count;
    auto& c = Config::dynamic();
    if (note_on.channel == c.midi_channel(Deck::A)) ref = Deck::A;
    else if (note_on.channel == c.midi_channel(Deck::B)) ref = Deck::B;
    if (ref != Deck::Count) {
        _trigger(ref, _speed_map.bipolar_pitch2speed(note_on.note - 60), true);
    }
}
void CoreUI::_trigger(const Deck::Ref ref, const float speed, const bool discont) 
{
    auto e = make_event();
    e.discont = discont;
    e.p3 = speed;
    e.p3_on = true;
    _core.deck(ref).trigger(&e);
    _show_gate_in(ref);
}
bool CoreUI::_process_realtime(daisy::MidiEvent& event)
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

void CoreUI::_process_clock_out()
{
    _hw.midi_uart.EnqueueMessage(MidiTxMessage::SystemRealtimeClock());
}
