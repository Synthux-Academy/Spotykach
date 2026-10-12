#include "usb.transfer.h"

#include "hw/buffer.sdram.h"

using namespace spotykach;
using namespace daisy;

USBTransfer::USBTransfer(Hardware& hw, Core& core, CoreUI& ui, Storage& storage):
_hw      { hw },
_core    { core },
_ui      { ui },
_storage { storage }
{}

void USBTransfer::init()
{
    _service.init(&_storage.card(), SDRAMBuffer::pool().sourceBufferSize());
}

void USBTransfer::check_request()
{
    if (_ui.midi().take_transfer_request()) _enter();
}

void USBTransfer::process()
{
    _service.process();
    _ui.set_transfer_progress(_service.is_busy(), _service.progress());

    auto exit = _service.should_exit();
    auto now = System::GetNow();
    if (now - _pads_ms >= 20) {
        _pads_ms = now;
        exit = _ui.process_transfer() || exit;
    }
    if (exit) _exit();
}

void USBTransfer::_enter()
{
    using transfer::EnterStatus;
    auto& midi = _ui.midi();

    if (_core.deck(Deck::A).is_recording() || _core.deck(Deck::B).is_recording()) {
        midi.reply_transfer(static_cast<uint8_t>(EnterStatus::busy));
        return;
    }

    switch (_storage.begin_transfer()) {
        case Storage::TransferCheck::busy:
            midi.reply_transfer(static_cast<uint8_t>(EnterStatus::busy));
            return;
        case Storage::TransferCheck::no_card:
            midi.reply_transfer(static_cast<uint8_t>(EnterStatus::no_card));
            return;
        case Storage::TransferCheck::ok: break;
    }

    // Stop while the audio callback still runs, so the commands get processed
    _core.deck(Deck::A).stop();
    _core.deck(Deck::B).stop();

    // The reply goes out from the audio callback, wait for it to leave
    midi.reply_transfer(static_cast<uint8_t>(EnterStatus::ok));
    auto start = System::GetNow();
    while (midi.is_transfer_reply_pending() && System::GetNow() - start < 100) {}
    System::Delay(20);

    // From here on the audio callback is silent and doesn't touch USB MIDI
    _is_active = true;
    System::Delay(5);
    _ui.set_transfer(true);

    _hw.StopUsbMidi();
    System::Delay(200); // so the host notices the disconnect
    _service.begin();
}

void USBTransfer::_exit()
{
    _service.end();
    _storage.end_transfer();
    System::Delay(200); // so the host notices the disconnect
    _hw.StartUsbMidi();
    _ui.set_transfer(false);
    _is_active = false;
}
