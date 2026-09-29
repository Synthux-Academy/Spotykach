#pragma once

#include <array>
#include "color.h"
#include "../hw/hardware.h"

namespace spotykach
{
    class LEDRing
    {
    public:
        LEDRing();
        ~LEDRing() = default;
        
        void set_hex_color(const uint32_t);
        void set_brightness(const float);
        void fill_brightness(const float brightness);

        void set_segment(float norm_start, float norm_size, const bool sharp = false);

        void set_point_hex_color(const uint32_t);

        void set_point(uint8_t idx, const float brightness);
        void add_point(float norm_position, const float brightness, const bool sharp = false, const bool over = true);

        bool is_updated() const { return _is_updated; }
        void set_updated() { _is_updated = true; }
        void apply(Hardware&, const Hardware::LedId);

        void clear();

    private:
        void _set(const int8_t idx, const infrasonic::Color color, const float brightness, const bool overlay = false);

        static constexpr float kBrightnessMult = .8f;
        static constexpr int8_t kLEDCount = 32;
        static constexpr int8_t kUpperBound = kLEDCount - 1;

        std::array<infrasonic::Color, kLEDCount> _colors;
        std::array<float, kLEDCount> _brights;

        std::array<infrasonic::Color, kLEDCount> _colors_cache;
        std::array<float, kLEDCount> _brights_cache;

        infrasonic::Color _segment_color;
        infrasonic::Color _point_color;
        float _segment_brightness;

        bool _is_updated;
    };
}
