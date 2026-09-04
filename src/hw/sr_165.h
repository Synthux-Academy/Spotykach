/*
MIT License

Copyright (c) 2020 Electrosmith, Corp.
Copyright (c) 2024 Infrasonic Audio LLC

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE
*/
#pragma once

#include "common.h"

namespace infrasonic
{

/** Maximum Number of chained devices Connect device's QH' pin to the next chips serial input*/
const size_t kMaxSr165DaisyChain = 16;

/** @addtogroup shiftregister
    @{
    */

/**
   @brief Device Driver for 8-bit shift register. \n
   CD74HC165 - 8-bit  parallel to serial input shift
   @author Nick Donaldson
   @date Apr 2024
   @note Adapted from shensley sr_595 driver
*/
class ShiftRegister165
{
  public:
    struct Config
    {
        daisy::Pin data;
        daisy::Pin clk;
        daisy::Pin load;
    };

    ShiftRegister165() {}
    ~ShiftRegister165() {}

    void Init(const Config& config, size_t num_daisy_chained = 1);

    void Update();

    inline uint8_t Read(size_t chain_idx = 0) const
    {
        chain_idx = (chain_idx > kMaxSr165DaisyChain - 1) ? kMaxSr165DaisyChain
                                                          : chain_idx;
        return state_[chain_idx];
    }

  private:
    daisy::GPIO data_, clk_, load_;
    uint8_t     state_[kMaxSr165DaisyChain];
    size_t      num_devices_;
};

} // namespace infrasonic
/** @} */
