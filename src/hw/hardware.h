#pragma once

#include "common.h"
#include <functional>
#include "daisy_seed.h"
#include "sr_165.h"
#include "ws2812.h"
#include "ui/voct.h"

namespace spotykach
{

static constexpr size_t kDacBufSize = 48;
static uint16_t DMA_BUFFER_MEM_SECTION dac_buf[2][kDacBufSize];

class Hardware
{
  public:
    // LED indexes in order of chain
    static constexpr uint16_t kNumLedsPerRing = 32;

    enum LedId : uint16_t {
        LED_SPOTY_PAD,
        LED_ALT_A,
        LED_PLAY_A,
        LED_REV_A,

        // Reserved 32 LEDs starting at top of ring,
        // clockwise order - add desired offset (0 - 31)
        LED_RING_A,
        LED_RING_A_LAST = LED_RING_A + kNumLedsPerRing - 1,

        LED_GRIT_A,
        LED_FLUX_A,
        LED_GATE_IN_A,
        LED_CYCLE_A,

        LED_MODE_CENTER,
        LED_MODE_LEFT,
        LED_CLOCK_IN,
        LED_MODE_RIGHT,

        LED_CYCLE_B,
        LED_GATE_IN_B,
        LED_FLUX_B,
        LED_GRIT_B,

        LED_RING_B,
        LED_RING_B_LAST = LED_RING_B + kNumLedsPerRing - 1,

        LED_REV_B,
        LED_PLAY_B,
        LED_FADER_B,
        LED_FADER_A,
        LED_ALT_B,

        LED_LAST
    };

    static constexpr size_t kNumLeds = LED_LAST;

    // Pots/sliders - these are on muxes
    enum AnalogControlId : uint16_t {
        CTRL_SOS_A,
        CTRL_MODFREQ_A,
        CTRL_MOD_AMT_A,
        CTRL_SIZE_A,
        CTRL_PITCH_A,
        CTRL_POS_A,
        CTRL_ENV_A,

        CTRL_SOS_B,
        CTRL_MODFREQ_B,
        CTRL_MOD_AMT_B,
        CTRL_SIZE_B,
        CTRL_PITCH_B,
        CTRL_POS_B,
        CTRL_ENV_B,

        CTRL_CROSSFADE,

        CTRL_LAST
    };

    static constexpr size_t kNumAnalogControls = CTRL_LAST;

    enum class Pad: uint8_t {
        PlayA   = 0,
        RevA    = 1,
        GritA   = 2,
        FluxA   = 3,
        SeqA    = 4,
        PlayB   = 9,
        RevB    = 8,
        GritB   = 7,
        FluxB   = 6,
        SeqB    = 5,
        Spot    = 10,
        Alt     = 11
    };

    // These are in order as they are labeled on the schematic
    enum CvInputId : uint16_t
    {
        CV_SIZE_POS_A,
        CV_MIX_A,
        CV_V_OCT_A,

        CV_CROSSFADE,

        CV_SIZE_POS_B,
        CV_MIX_B,
        CV_V_OCT_B,

        CV_LAST
    };

    static constexpr size_t kNumCVInputs = CV_LAST;

    Hardware()  = default;
    ~Hardware() = default;

    void Init(float sr, size_t blocksize);
    void StartAdcs();

    // Process the analog controls - do this in audiocallback,
    // it is not blocking
    void ProcessAnalogControls();

    // Process the shift register etc.
    // This is a blocking call, don't do it in audiocallback
    void ProcessDigitalControls();

    // Process the pads.
    // This is a blocking call, don't do it in audiocallback
    void ProcessPads();

    inline void SetOnTouch(std::function<void(Pad)> on_touch) { _on_touch = on_touch; };
    inline void SetOnRelease(std::function<void(Pad)> on_release) { _on_release = on_release; };

    // Unipolar 0.0 - 1.0
    float GetAnalogControlValue(AnalogControlId id);

    // Bipolar -1.0 - 1.0 (nominally around 0.0)
    float GetControlVoltageValue(CvInputId id);

    // Adapter for direct use as PotMonitor Backend
    inline float GetPotValue(uint16_t pot_id)
    {
        return GetAnalogControlValue((AnalogControlId)pot_id);
    }

    bool GetClockInputState();
    bool GetGateInputAState();
    bool GetGateInputBState();

    inline void SetGateOutA(bool state) { gate_out_a_.Write(state); }
    inline void SetGateOutB(bool state) { gate_out_b_.Write(state); }

    uint32_t GetBootButtonHeldTime() const;

    // TODO: I'd recommend abstracting this with more readable enums
    //       and abstracted bit testing for switches - not just reading raw bytes
    //       from the shift register (this is just for quick hardware testing code)
    inline uint8_t GetShiftRegState(uint8_t idx) const
    {
        if(idx > 1)
            return 0;
        return shiftreg_.Read(idx);
    }

    inline void StartDAC(daisy::DacHandle::DacCallback&& callback) {
        seed.dac.Start(dac_buf[0], dac_buf[1], kDacBufSize, callback);
    }

    inline uint16_t GetMpr121TouchStates() { return mpr121_.Touched(); }

    // Left public for easy direct handle access
    // but all core peripherals are initialized by this class
    daisy::DaisySeed seed;

    // Using device class directly, maybe easier
    // to wrap inside a helper for color/index management etc
    infrasonic::Ws2812 leds;

    daisy::MidiUartHandler midi_uart;
    daisy::MidiUsbHandler midi_usb;

  private:
    infrasonic::ShiftRegister165 shiftreg_;

    daisy::Mpr121I2C mpr121_;
    uint16_t mpr121_state_;
    std::function<void(Pad pad)> _on_touch;
    std::function<void(Pad pad)> _on_release;

    daisy::GPIO clock_in_;
    daisy::GPIO gate_in_a_;
    daisy::GPIO gate_in_b_;
    daisy::GPIO gate_out_a_;
    daisy::GPIO gate_out_b_;

    daisy::Switch boot_btn_;

    daisy::AnalogControl controls_[kNumAnalogControls];
    daisy::AnalogControl cvinputs_[kNumCVInputs];

    Hardware(const Hardware& a)            = delete;
    Hardware& operator=(const Hardware& a) = delete;
};

} // namespace spotykach
