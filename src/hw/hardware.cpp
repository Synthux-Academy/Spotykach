#include "hardware.h"

using namespace spotykach;
using namespace daisy;

namespace spotykach
{
static constexpr Pin kLEDDataPin = seed::D17;

static constexpr Pin kClockInputPin  = seed::D3;
static constexpr Pin kGateAInputPin  = seed::D1;
static constexpr Pin kGateAOutputPin = seed::D31;
static constexpr Pin kGateBInputPin  = seed::D0;
static constexpr Pin kGateBOutputPin = seed::D32;

static constexpr Pin kSRDataPin  = seed::D27;
static constexpr Pin kSRClockPin = seed::D26;
static constexpr Pin kSRLoadPin  = seed::D7;

static constexpr Pin kI2CSdaPin = seed::D12;
static constexpr Pin kI2CSclPin = seed::D11;

static constexpr Pin kMux1SignalPin = seed::A9;
static constexpr Pin kMux2SignalPin = seed::A10;
static constexpr Pin kMuxAddrAPin   = seed::D8;
static constexpr Pin kMuxAddrBPin   = seed::D9;
static constexpr Pin kMuxAddrCPin   = seed::D10;

static constexpr Pin kCVInput1Pin = seed::A4;
static constexpr Pin kCVInput2Pin = seed::A11;
static constexpr Pin kCVInput3Pin = seed::A5;
static constexpr Pin kCVInput4Pin = seed::A3;
static constexpr Pin kCVInput5Pin = seed::A0;
static constexpr Pin kCVInput6Pin = seed::A6;
static constexpr Pin kCVInput7Pin = seed::A1;

static constexpr Pin kMidiUartRxPin = seed::D14;
static constexpr Pin kMidiUartTxPin = seed::D13;

static constexpr size_t kNumAdcChannels = 9; // 7 CV + 2 Mux

} // namespace spotykach

void Hardware::Init(float sr, size_t blocksize)
{
    const float kProcessRate = sr / blocksize;

    seed.Init(true);

    boot_btn_.Init(seed::D2, 0, Switch::TYPE_MOMENTARY, Switch::POLARITY_INVERTED, GPIO::Pull::NOPULL);

    // --- LEDs ---

    infrasonic::Ws2812::Config led_cfg;
    led_cfg.num_leds    = kNumLeds;
    led_cfg.tim_pin     = kLEDDataPin;
    led_cfg.tim_periph  = TimerHandle::Config::Peripheral::TIM_3;
    led_cfg.tim_channel = infrasonic::Ws2812::Config::CH4;
    leds.Init(led_cfg);
    leds.SetBrightnessLimit(1.f);

    // --- GPIO - gate/clk/etc ---

    GPIO::Config gpio_cfg;
    gpio_cfg.mode = GPIO::Mode::INPUT;
    gpio_cfg.pull = GPIO::Pull::NOPULL;

    gpio_cfg.pin = kClockInputPin;
    clock_in_.Init(gpio_cfg);
    gpio_cfg.pin = kGateAInputPin;
    gate_in_a_.Init(gpio_cfg);
    gpio_cfg.pin = kGateBInputPin;
    gate_in_b_.Init(gpio_cfg);

    gpio_cfg.mode = GPIO::Mode::OUTPUT;
    gpio_cfg.pin  = kGateAOutputPin;
    gate_out_a_.Init(gpio_cfg);
    gpio_cfg.pin = kGateBOutputPin;
    gate_out_b_.Init(gpio_cfg);

    // --- Shift registers (switches) ---

    infrasonic::ShiftRegister165::Config srcfg;
    srcfg.clk  = kSRClockPin;
    srcfg.data = kSRDataPin;
    srcfg.load = kSRLoadPin;
    shiftreg_.Init(srcfg, 2);

    // --- MPR121 (I2C) ---

    // Default device address is fine
    Mpr121I2C::Config mpr_cfg;
    mpr_cfg.transport_config.periph = I2CHandle::Config::Peripheral::I2C_1;
    mpr_cfg.transport_config.mode   = I2CHandle::Config::Mode::I2C_MASTER;
    mpr_cfg.transport_config.scl    = kI2CSclPin;
    mpr_cfg.transport_config.sda    = kI2CSdaPin;
    mpr_cfg.transport_config.speed  = I2CHandle::Config::Speed::I2C_400KHZ;
    mpr121_.Init(mpr_cfg);

    // --- Init ADCs ---
    // (normally I'd write loopable config structs for this but
    //  this is quick and dirty code)

    // Speed and oversampling can usually be reduced from defaults
    // to increase effective ADC sample rate with no major downsides,
    // but possibly more jitter
    const auto kAdcSpeed = AdcChannelConfig::SPEED_2CYCLES_5;
    const auto kAdcOvs   = AdcHandle::OverSampling::OVS_32;

    AdcChannelConfig adc_cfg[kNumAdcChannels];
    adc_cfg[0].InitMux(
        kMux1SignalPin, 8, kMuxAddrAPin, kMuxAddrBPin, kMuxAddrCPin, kAdcSpeed);
    adc_cfg[1].InitMux(
        kMux2SignalPin, 8, kMuxAddrAPin, kMuxAddrBPin, kMuxAddrCPin, kAdcSpeed);
    adc_cfg[2].InitSingle(kCVInput1Pin, kAdcSpeed);
    adc_cfg[3].InitSingle(kCVInput2Pin, kAdcSpeed);
    adc_cfg[4].InitSingle(kCVInput3Pin, kAdcSpeed);
    adc_cfg[5].InitSingle(kCVInput4Pin, kAdcSpeed);
    adc_cfg[6].InitSingle(kCVInput5Pin, kAdcSpeed);
    adc_cfg[7].InitSingle(kCVInput6Pin, kAdcSpeed);
    adc_cfg[8].InitSingle(kCVInput7Pin, kAdcSpeed);

    seed.adc.Init(adc_cfg, kNumAdcChannels, kAdcOvs);

    // --- Analog Controls ---
    // Again this is verbose and clumsy - would normally do a loopable configuration mapping
    constexpr float kPotSmoothTime = 0.02f;

    controls_[CTRL_SOS_A].Init(
        seed.adc.GetMuxPtr(0, 0), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_MODFREQ_A].Init(
        seed.adc.GetMuxPtr(0, 1), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_MOD_AMT_A].Init(
        seed.adc.GetMuxPtr(0, 3), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_SIZE_A].Init(
        seed.adc.GetMuxPtr(0, 5), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_PITCH_A].Init(
        seed.adc.GetMuxPtr(0, 2), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_POS_A].Init(
        seed.adc.GetMuxPtr(0, 6), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_ENV_A].Init(
        seed.adc.GetMuxPtr(0, 4), kProcessRate, false, false, kPotSmoothTime);

    controls_[CTRL_SOS_B].Init(
        seed.adc.GetMuxPtr(1, 0), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_MODFREQ_B].Init(
        seed.adc.GetMuxPtr(1, 4), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_MOD_AMT_B].Init(
        seed.adc.GetMuxPtr(1, 6), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_SIZE_B].Init(
        seed.adc.GetMuxPtr(1, 3), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_PITCH_B].Init(
        seed.adc.GetMuxPtr(1, 2), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_POS_B].Init(
        seed.adc.GetMuxPtr(1, 1), kProcessRate, false, false, kPotSmoothTime);
    controls_[CTRL_ENV_B].Init(
        seed.adc.GetMuxPtr(1, 5), kProcessRate, false, false, kPotSmoothTime);

    controls_[CTRL_CROSSFADE].Init(
        seed.adc.GetMuxPtr(1, 7), kProcessRate, false, false, kPotSmoothTime);

    // --- CV Inputs ---

    // NOTE: being bipolar, these will all benefit from zero-point calibration
    // i.e. capture reading for each with no patch cables plugged in (OV input) and
    // subtract from all future readings on that channel.
    //
    // The V/Oct inputs will also require at least 2-point (1V and 3V) calibration
    // for linear fit to track V/Oct reasonably well
    cvinputs_[CV_SIZE_POS_A].InitBipolarCv(seed.adc.GetPtr(2), kProcessRate);
    cvinputs_[CV_V_OCT_A].InitBipolarCv(seed.adc.GetPtr(4), kProcessRate);
    cvinputs_[CV_MIX_A].InitBipolarCv(seed.adc.GetPtr(3), kProcessRate);
    cvinputs_[CV_CROSSFADE].InitBipolarCv(seed.adc.GetPtr(5), kProcessRate);
    cvinputs_[CV_SIZE_POS_B].InitBipolarCv(seed.adc.GetPtr(6), kProcessRate);
    cvinputs_[CV_V_OCT_B].InitBipolarCv(seed.adc.GetPtr(8), kProcessRate);
    cvinputs_[CV_MIX_B].InitBipolarCv(seed.adc.GetPtr(7), kProcessRate);

    // --- UART MIDI ---
    MidiUartHandler::Config midi_uart_cfg;
    midi_uart_cfg.transport_config.periph = UartHandler::Config::Peripheral::USART_1;
    midi_uart_cfg.transport_config.rx     = kMidiUartRxPin;
    midi_uart_cfg.transport_config.tx     = kMidiUartTxPin;
    midi_uart.Init(midi_uart_cfg);
    midi_uart.StartReceive();

    #ifndef DEBUG
    StartUsbMidi();
    #endif

    // -- DAC --
    DacHandle::Config config;
    config.target_samplerate = 48000;
    config.chn        = DacHandle::Channel::BOTH;
    config.bitdepth   = DacHandle::BitDepth::BITS_12;
    config.mode       = DacHandle::Mode::DMA;
    config.buff_state = DacHandle::BufferState::ENABLED;
    seed.dac.Init(config);
}

void Hardware::StartAdcs()
{
    seed.adc.Start();
}

void Hardware::ProcessAnalogControls()
{
    for(auto& control : controls_)
    {
        control.Process();
    }
    for(auto& cv : cvinputs_)
    {
        cv.Process();
    }
}

static bool was_pressed = false;
void Hardware::ProcessDigitalControls()
{
    boot_btn_.Debounce();
    if (!boot_btn_.Pressed() && was_pressed) {
        HAL_NVIC_SystemReset();
    }
    was_pressed = boot_btn_.Pressed();
    
    shiftreg_.Update();
}

void Hardware::StartUsbMidi()
{
    MidiUsbHandler::Config midi_usb_cfg;
    midi_usb_cfg.transport_config.periph = MidiUsbTransport::Config::Periph::EXTERNAL;
    midi_usb.Init(midi_usb_cfg);
    midi_usb.StartReceive();
}

void Hardware::StopUsbMidi()
{
    UsbHandle usb;
    usb.DeInit(UsbHandle::FS_EXTERNAL);
}

void Hardware::ProcessPads()
{
    uint16_t pad;
    bool is_touched;
    bool was_touched;
    auto state = mpr121_.Touched();
    for (uint16_t i = 0; i < 12; i++) {
        pad = 1 << i;
        is_touched = state & pad;
        was_touched = mpr121_state_ & pad;

        if (_on_touch != nullptr && is_touched && !was_touched) {
            _on_touch(static_cast<Pad>(i));
        }
        else if (_on_release != nullptr && was_touched && !is_touched) {
            _on_release(static_cast<Pad>(i));
        }
    }
    mpr121_state_ = state;
}

float Hardware::GetAnalogControlValue(AnalogControlId id)
{
    // inset scaling for full range
    if(id >= CTRL_LAST)
        return 0.0f;
    float val
        = infrasonic::map(controls_[id].Value(), 0.08f, 0.92f, 0.0f, 1.0f);
    return infrasonic::unitclamp(val);
}

float Hardware::GetControlVoltageValue(CvInputId id)
{
    if (id >= CV_LAST) return 0.0f;
    return cvinputs_[id].Value();
}

// These are all inverted due to transistors
bool Hardware::GetClockInputState()
{
    return !clock_in_.Read();
}
bool Hardware::GetGateInputAState()
{
    return !gate_in_a_.Read();
}
bool Hardware::GetGateInputBState()
{
    return !gate_in_b_.Read();
}

uint32_t Hardware::GetBootButtonHeldTime() const
{
    return boot_btn_.TimeHeldMs();
}
