#include "led.ring.h"
#include "expose.h"

using namespace spotykach;
using namespace infrasonic;

inline float smoothstep_frac(const float frac)
{
    return frac * frac * (3.f - 2.f * frac);
}
inline void extrapolate(
    const float p, 
    const float b,
    int32_t& p1, 
    int32_t& p2, 
    float& b1, 
    float& b2, 
    const bool reverse = false)
{
    p1 = static_cast<int32_t>(p);
    if (reverse) p2 = std::max(p1 - 1, 0l);
    else p2 = p1 + 1;    
    auto frac = smoothstep_frac(p - p1);
    b1 = (1.f - frac) * b;
    b2 = frac * b;
    if (reverse) std::swap(b1, b2);
}

LEDRing::LEDRing():
_segment_color      { Color(0xff0000) },
_point_color        { Color(0xffffff) },
_segment_brightness { 1.f }
{}

void LEDRing::set_hex_color(const uint32_t hex)
{
    _segment_color = Color(hex);
}

void LEDRing::set_brightness(const float norm)
{
    _segment_brightness = norm;
}

void LEDRing::fill_brightness(const float brightness)
{
    _brights.fill(brightness);
}

void LEDRing::set_point_hex_color(const uint32_t hex)
{
    _point_color = Color(hex);
}

void LEDRing::set_segment(float norm_start, float norm_end, const bool sharp)
{
    int32_t sp1, sp2;
    float sb1, sb2;
    while (norm_start < 0) { norm_start += 1.f; }
    if (sharp) {
        sp1 = sp2 = std::round(norm_start * kLEDCount);
        sb1 = sb2 = _segment_brightness;
    }
    else {
        extrapolate(norm_start * kUpperBound, _segment_brightness, sp1, sp2, sb1, sb2, true);
    }

    int32_t ep1, ep2;
    float eb1, eb2;
    while (norm_end < 0) { norm_end += 1.f; }
    if (sharp) {
        ep1 = ep2 = std::round(norm_end * kLEDCount);
        eb1 = eb2 = _segment_brightness;
    }
    else {
        extrapolate(norm_end * kUpperBound, _segment_brightness, ep1, ep2, eb1, eb2);
    }

    if (sp1 == ep1) {
        _set(sp1, _segment_color, sb1);
    }
    else {
        _set(sp1, _segment_color, sb1);
        _set(sp2, _segment_color, sb2);

        auto s = sp2;
        auto e = ep1;
        while (s > kUpperBound) { s -= kUpperBound; }
        auto l = e < s ? kUpperBound - s + e : e - s;

        for (int8_t idx = 0; idx < l; idx ++) {
            auto p = idx + s + 1;
            if (p >= kLEDCount) p -= kLEDCount;
            _set(p, _segment_color, _segment_brightness);
        }

        _set(ep1, _segment_color, _segment_brightness);
        _set(ep2, _segment_color, sharp ? _segment_brightness : eb2);
    }
}

void LEDRing::add_point(float norm_position, const float brightness, const bool sharp, const bool over)
{
    int32_t p1, p2;
    float b1, b2;      
    while (norm_position < 0) {
        norm_position += 1.f;
    }
    if (sharp) {
        _set(std::round(norm_position * kLEDCount), _point_color, brightness, over);
    } 
    else {
        extrapolate(norm_position * kUpperBound, brightness, p1, p2, b1, b2);
        _set(p1, _point_color, b1, over);
        _set(p2, _point_color, b2, over);
    }
}

void LEDRing::set_point(uint8_t idx, const float brightness)
{
    _set(idx, _point_color, brightness, false);
}

void LEDRing::_set(int8_t idx, const infrasonic::Color color, const float brightness, const bool overlay)
{
    while (idx < 0) idx += kLEDCount;
    while (idx >= kLEDCount) idx -= kLEDCount;
    if (overlay) {
        _colors[idx] += color * (0.85 * brightness + 0.15);
    }
    else {
        _colors[idx] = color;
    }
    
    _brights[idx] = (overlay ? _segment_brightness : brightness) * kBrightnessMult;
}

void set_led(Hardware& hw, const uint16_t ring, const uint16_t idx, const int color, const float brightness)
{
    uint16_t ledidx = ring;
    static const auto kHalf = 16;
    if(idx <= kHalf)
    {
        ledidx += kHalf - idx;
    }
    else
    {
        ledidx += Hardware::kNumLedsPerRing - 1;
        ledidx -= idx - kHalf - 1;
    }

    hw.leds.Set(ledidx, color, brightness);
}
void LEDRing::apply(Hardware& hw, const Hardware::LedId ref)
{
    for (uint8_t i = 0; i < _brights.size(); i++) {
        if (_is_updated) {
            _colors_cache[i] = _colors[i];
            _brights_cache[i] = _brights[i];
        }
        set_led(hw, ref, i, _colors_cache[i].Hex(), _brights_cache[i]);
    }
    _is_updated = false;
}

void LEDRing::clear()
{
    infrasonic::Color color;
    _colors.fill(color);
    _brights.fill(0);
}
