#include "app.h"

#include <functional>

#include "common.h"
#include "memory/settings.h"
#include "hw/hardware.h"
#include "ui/core.ui.h"
#include "core/core.h"
#include "memory/storage.h"
#include "transfer/service.h"
#include "expose.h"

// #define METER
#ifdef METER
#include "meter.h"
#endif

using namespace daisy;
using namespace infrasonic;

namespace spotykach {

class AppImpl {
  public:
    AppImpl():
    _ui     { CoreUI(_hw, _core, _settings, _storage) }
    {}

    ~AppImpl() = default;

    void Init();
    void Loop();
    void mod_src(float& out0, float& out1) {
        _core.mod(Deck::A).process(out0);
        _core.mod(Deck::B).process(out1);

        _ui.set_lfo(out0, out1);
    }
    Core& core() { return _core; }
    CoreUI& ui() { return _ui; }
    bool is_transfer() const { return _is_transfer; }

    void ProcessAudio(AudioHandle::InputBuffer  in,
                      AudioHandle::OutputBuffer out,
                      size_t                    size);

  private:
    NOCOPY(AppImpl)

    void _enter_transfer();
    void _exit_transfer();
    void _process_transfer();

    #if DEBUG
    StopwatchTimer _log_timer;
    void logDebugInfo();
    #endif
    bool _log_enabled;

    Core        _core;
    CoreUI      _ui;
    Hardware    _hw;
    Settings    _settings;
    Storage     _storage;

    transfer::Service _transfer;
    volatile bool     _is_transfer { false };
    uint32_t          _transfer_pads_ms { 0 };
};
};

using namespace spotykach;

static AppImpl impl;

static int8_t leds_update_counter = 0;
void T5Callback(void* data) 
{
    if (!impl.is_transfer()) {
        impl.ui().process_gate_in();
        impl.core().prepare();
    }
    if (leds_update_counter++ == 3) {
        leds_update_counter = 0;
        impl.ui().render_leds();
    }
};

//According to GetPClk2Freq docs, timers run at the frequency twice faster
//as their peripheral frequency. So call_freq_hz should be twise smaller 
//then synclock period.
TimerHandle tim5_handle;
void StartT5Callback(TimerHandle::PeriodElapsedCallback cb, uint32_t call_freq_hz) {
    TimerHandle::Config timcfg;
    timcfg.periph = TimerHandle::Config::Peripheral::TIM_5;
    timcfg.dir = TimerHandle::Config::CounterDir::UP;
    timcfg.period = System::GetPClk2Freq() / call_freq_hz;
    timcfg.enable_irq = true;
    tim5_handle.Init(timcfg);
    tim5_handle.SetCallback(cb);
    tim5_handle.Start();
};

void DACCallback(uint16_t **out, size_t size) 
{
    for (size_t i = 0; i < size; i++) {
        float out_a = 0;
        float out_b = 0;
        impl.mod_src(out_a, out_b);
        uint16_t a = __USAT(out_a * (1 << 12), 12);
        uint16_t b = __USAT(out_b * (1 << 12), 12);

        out[0][i] = a;
        out[1][i] = b;
    }
};

static void AudioCallback(AudioHandle::InputBuffer  in,
                          AudioHandle::OutputBuffer out,
                          size_t                    size)
{
    impl.ProcessAudio(in, out, size);
};

void AppImpl::Init() 
{
    auto sample_rate = 48000;
    auto block_size = 96;
    _hw.Init(sample_rate, block_size);
    _core.init(sample_rate, block_size);
    _ui.init();
    
    _storage.init(_core.deck(Deck::A), _core.deck(Deck::B));
    _storage.read_settigs();
    _transfer.init(&_storage.card(), SDRAMBuffer::pool().sourceBufferSize());

    Log::StartLog(false);
#if DEBUG
    _log_timer.Init();
#endif

    StartT5Callback(T5Callback, 250);

    
    _hw.StartDAC(DACCallback);

    auto& audio = _hw.seed.audio_handle;
    audio.SetSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);
    audio.SetBlockSize(block_size);
    audio.Start(AudioCallback);

    _settings.init(_hw);
    _settings.read();

    _ui.calibrate(false);

    #ifdef METER
    Meter::cpu().load.Init(sample_rate, block_size);
    #endif
}

void AppImpl::Loop()
{
    while(true) {
        // If boot button held for 3s, reset into bootloader mode for update
        if (_hw.GetBootButtonHeldTime() >= 3000)
        {
            System::ResetToBootloader(System::BootloaderMode::DAISY_INFINITE_TIMEOUT);
        }

        if (_is_transfer) {
            _process_transfer();
            continue;
        }

        _ui.process();
        _storage.process();

        #ifndef DEBUG
        if (_ui.midi().take_transfer_request()) _enter_transfer();
        #endif
        
        #if DEBUG
        if(_log_timer.HasPassedMs(250))
        {
            logDebugInfo();
            _log_timer.Restart();

            #ifdef METER
            auto& loadMeter = Meter::cpu().load;
            const float avgLoad = loadMeter.GetAvgCpuLoad();
            const float maxLoad = loadMeter.GetMaxCpuLoad();
            const float minLoad = loadMeter.GetMinCpuLoad();
            Log::PrintLine("Processing Load %:");
            Log::PrintLine("Max: " FLT_FMT3, FLT_VAR3(maxLoad * 100.0f));
            Log::PrintLine("Avg: " FLT_FMT3, FLT_VAR3(avgLoad * 100.0f));
            Log::PrintLine("Min: " FLT_FMT3, FLT_VAR3(minLoad * 100.0f));
            #endif
        }
        #endif
    }
}

void AppImpl::ProcessAudio(AudioHandle::InputBuffer  in,
                           AudioHandle::OutputBuffer out,
                           size_t                    size)
{
    if (_is_transfer) {
        auto channels = _hw.seed.audio_handle.GetChannels();
        for (size_t ch = 0; ch < channels; ch++) std::fill(out[ch], out[ch] + size, 0.f);
        return;
    }

    #ifdef METER
    Meter::cpu().load.OnBlockStart();
    #endif
    
    _hw.ProcessAnalogControls();
    _ui.tick();
    _ui.read_cv();
    _core.process(in, out, size);

    #ifdef METER
    Meter::cpu().load.OnBlockEnd();
    #endif
}

// USB transfer mode //////////////////////////////
void AppImpl::_enter_transfer()
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
    _is_transfer = true;
    System::Delay(5);
    _ui.set_transfer(true);

    _hw.StopUsbMidi();
    System::Delay(200); // so the host notices the disconnect
    _transfer.begin();
}

void AppImpl::_exit_transfer()
{
    _transfer.end();
    _storage.end_transfer();
    System::Delay(200); // so the host notices the disconnect
    _hw.StartUsbMidi();
    _ui.set_transfer(false);
    _is_transfer = false;
}

void AppImpl::_process_transfer()
{
    _transfer.process();
    _ui.set_transfer_progress(_transfer.is_busy(), _transfer.progress());

    auto exit = _transfer.should_exit();
    auto now = System::GetNow();
    if (now - _transfer_pads_ms >= 20) {
        _transfer_pads_ms = now;
        exit = _ui.process_transfer() || exit;
    }
    if (exit) _exit_transfer();
}

#if DEBUG
void AppImpl::logDebugInfo()
{
    Expose::values().print();

    // float val = hw.GetAnalogControlValue(Hardware::CTRL_PITCH_A);
    // float val = hw.GetControlVoltageValue(Hardware::CV_V_OCT_A);
    // Log::PrintLine(FLT_FMT(5), FLT_VAR(5, val));
    // uint16_t touch = hw.GetMpr121TouchStates();
}
#endif

void Application::Init() 
{
    impl.Init();
}

void Application::Loop() 
{
    impl.Loop();
}
