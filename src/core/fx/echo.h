#pragma once

#include "biquad.h"
#include "deline.h"
#include "common.h"

namespace infrasonic {

/**
 * @brief
 * Derived from implementation by Nick Donaldson (Infrasonic Audio)
 * 
 * Tape-ish echo delay.
 *   - Feedback is unbounded, but signal is soft-clipped
 *   - Output is full-wet, should be mixed with dry signal externally
 *
 * @tparam MaxLength Max length of delay in samples
 */
template<size_t max_size>
class EchoDelay {

    public:

        EchoDelay() {}
        ~EchoDelay() {}

        void Init(float sample_rate, float* buf)
        {
            sample_rate_ = sample_rate;
            delayLine_.Init(buf);
            bpf_.Init(sample_rate);
            bpf_.SetParams(800.0f, 0.f);
        }

        /**
         * @brief Set the approximate lag time (smoothing) for delay time changes, in seconds
         */
        void SetLagTime(const float time_s)
        {
            delay_smooth_coef_ = onepole_coef(time_s, sample_rate_);
        }

        /**
         * @brief Set the Delay Time in seconds
         *
         * @param time_s Delay time in seconds. Will be truncated to MaxLength.
         * @param immediately If true, sets delay time immediately with no smoothing.
         */
        void SetDelayTime(const float time_s, bool immediately = false)
        {
            delay_time_target_ = time_s;
            if (immediately) delay_time_current_ = time_s;
        }

        /**
         * @brief
         * Set the feedback amount (linear multiplier).
         * This can be >1 in magnitude for saturated swells, or negative.
         *
         * NOTE: This is not internally smoothed. Use external smoothing if desired.
         *
         * @param feedback
         */
        void SetFeedback(const float feedback)
        {
            feedback_ = feedback;
        }

        float Feedback() const { return feedback_; }

        inline float Process(const float in)
        {
            float out;
            daisysp::fonepole(delay_time_current_, delay_time_target_, delay_smooth_coef_);
            delayLine_.SetDelay(delay_time_current_ * sample_rate_);
            out = delayLine_.Read();
            out = bpf_.Process(out);
            out = daisysp::SoftClip(out);
            delayLine_.Write(out * feedback_ + in);
            return out;
        }

    private:

        EchoDelay(const EchoDelay &other) = delete;
        EchoDelay(EchoDelay &&other) = delete;
        EchoDelay& operator=(const EchoDelay &other) = delete;
        EchoDelay& operator=(EchoDelay &&other) = delete;

        float sample_rate_;
        float delay_time_current_;
        float delay_time_target_;
        float delay_smooth_coef_;

        float feedback_;

        DeLine<float, max_size> delayLine_;
        BPF12 bpf_;
};

}
