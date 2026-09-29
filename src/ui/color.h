// Copyright 2024 Infrasonic Audio LLC
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
#pragma once

#include "common.h"

namespace infrasonic
{
/** Class for handling simple colors, based on daisy::Color but heavily modified  */
class Color
{
  public:
    /// Defaults to black (aka "off" for LEDs)
    constexpr Color() : red_(0.0f), green_(0.0f), blue_(0.0f) {}

    ~Color() = default;

    // Construct from float values
    constexpr Color(float red, float green, float blue)
    : red_(unitclamp(red)), green_(unitclamp(green)), blue_(unitclamp(blue))
    {
    }

    /// Construct from 8-bit values
    constexpr Color(uint8_t red, uint8_t green, uint8_t blue)
    : red_(red / 255.0f), green_(green / 255.0f), blue_(blue / 255.0f)
    {
    }

    /// Construct from 32-bit hex (RGB order, MSB ignored)
    constexpr Color(uint32_t hex)
    : Color(uint8_t((hex >> 16) & 0x00FF),
            uint8_t((hex >> 8) & 0x00FF),
            uint8_t(hex & 0x00FF))
    {
    }

    static Color FromHSV(uint8_t h, uint8_t s, uint8_t v);

    /** Returns the 0-1 value for Red */
    constexpr float Red() const { return red_; }

    /** Returns the 0-1 value for Green */
    constexpr float Green() const { return green_; }

    /** Returns the 0-1 value for Blue */
    constexpr float Blue() const { return blue_; }

    constexpr uint8_t Red8() const { return red_ * 255; }
    constexpr uint8_t Green8() const { return green_ * 255; }
    constexpr uint8_t Blue8() const { return blue_ * 255; }

    constexpr uint32_t Hex() const
    {
        uint32_t hex = 0;
        hex |= Red8() << 16;
        hex |= Green8() << 8;
        hex |= Blue8();
        return hex;
    }

    constexpr Color Inverted() const { return Color(~Hex()); }

    inline Color Normalized() const
    {
        float max = red_;
        if(green_ > max)
            max = green_;
        if(blue_ > max)
            max = blue_;
        if(max == 0.0f)
            return *this;
        return operator*(1.0f / max);
    }


    // uint32_t GammaCorrectedHex() const;

    /// Returns a copy of the Color offseted by a float (all channels)
    Color operator+(const float offset) const;

    /// Returns a copy of the Color by adding another color (channel wise)
    Color operator+(const Color& other) const;

    /// Returns a copy of the Color offseted by a float (all channels)
    Color operator-(const float offset) const;

    /// Returns a copy of the Color by subtracting another color (channel wise)
    Color operator-(const Color& other) const;

    /// Returns a copy of the Color scaled (all channels multiplied) by a float
    Color operator*(const float scale) const;

    /// Returns a copy of the Color multiplied by another color (channel-wise)
    Color operator*(const Color& other) const;

    Color& operator+=(const float offset);
    Color& operator+=(const Color& other);
    Color& operator-=(const float offset);
    Color& operator-=(const Color& other);
    Color& operator*=(const float scale);
    Color& operator*=(const Color& other);

  private:
    float red_, green_, blue_;
};
} // namespace infrasonic
