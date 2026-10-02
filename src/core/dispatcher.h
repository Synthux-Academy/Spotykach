#pragma once
#include <array>
#include <functional>
#include <stdint.h>
#include "event.h"
#include "nocopy.h"
#include "expose.h"

namespace spotykach {

enum class DispatcherMode: uint8_t {
    Mono,
    Poly
};

template<uint8_t max_vox_count>
class Dispatcher {
public:
    Dispatcher():
    _mode           { DispatcherMode::Mono },
    _note_on_count  { 0 },
    _holding_index  { kNone }
    {
        for (uint8_t i = 0; i < max_vox_count; i++) {
            _queue[i] = i;
            _active[i] = false;
        }
    }
    ~Dispatcher() = default;

    uint8_t event_on(const Event* event, const bool hold) {
        auto vox = _vox_for_event(event, hold);
        _events[vox.index] = event;
        _active[vox.index] = true;
        _note_on_count++;
        _on_event_on(vox.index, event);
        return vox.index;
    }

    void event_off(const Event* event) {
        _release_event(event);
    }

    void vox_off(const uint8_t vox) {
        _release_event_at(vox);
    }

    void release_holding() {
        if (_holding_index != kNone) _release_event_at(_holding_index);
    }

    void set_mode(const DispatcherMode mode) {
        _mode = mode;
    }

    bool is_note_on(const uint8_t note) {
        for (auto i = 0; i < max_vox_count; i++) {
            if (_events[i] == note && _active[i]) return true;
        }
        return false;
    }

    uint8_t has_events() {
        return _note_on_count > 0;
    }

    void all_off() {
        for (auto i = 0; i < max_vox_count; i++) {
            if (_active[i]) _release_event_at(i);
        }
        _holding_index = kNone;
    }

    void set_on_event_on(std::function<void(const uint8_t, const Event*)> on_event_on) {
        _on_event_on = on_event_on;
    }

    void set_on_event_off(std::function<void(const uint8_t)> on_event_off) {
        _on_event_off = on_event_off;
    }

private:
    NOCOPY(Dispatcher)

    struct Voice {
        uint8_t index;
        bool retrigger;
    };

    Voice _vox_for_event(const Event* event, const bool hold) {
        Voice vox;
        uint8_t queue_idx = kNone;
        uint8_t i;

        if (hold) release_holding();

        // Find free voice enumerating in the queue order
        for (i = 0; i < max_vox_count; i++) {
            if (!_active[_queue[i]]) {
                queue_idx = i;
                vox.retrigger = true;
                break;
            }
        }

        // If there's no voice found in previous
        // step, take the first in the queue
        if (queue_idx == kNone) {
            queue_idx = 0;
            vox.retrigger = true;
        }

        // If the voice should be kept on,
        // take the next one from the queue.
        // Bounded to one pass over the queue so that a corrupted queue
        // (all entries equal to the holding index) can't hang the caller.
        auto vox_idx = _queue[queue_idx];
        for (uint8_t n = 0; vox_idx == _holding_index && n < max_vox_count; n++) {
            queue_idx ++;
            if (queue_idx == _queue.size()) {
                queue_idx = 0;
            }
            vox_idx = _queue[queue_idx];
        }

        //Hold the voice if needed
        if (hold) {
            _holding_index = vox_idx;
        }

        //
        vox.index = vox_idx;

        // Move the taken voice to the end of the queue.
        for (i = queue_idx; i < max_vox_count - 1; i++) {
            _queue[i] = _queue[i+1];
        }
        _queue[i] = vox.index;

        //
        return vox;
    }

    void _release_event(const Event* event) {
        uint8_t i;
        for (i = 0; i < max_vox_count; i++) {
            if (_events[i] == event) {
                _release_event_at(i);
                break;
            }
        }
    }

    void _release_event_at(const uint8_t idx) {
        _active[idx] = false;
        if (_note_on_count > 0) {
            _note_on_count --;
        }
        _on_event_off(idx);
    }

    static constexpr uint8_t kNone = 0xff;

    std::function<void(const uint8_t, const Event*)> _on_event_on;
    std::function<void(const uint8_t)> _on_event_off;

    std::array<bool, max_vox_count> _active;
    std::array<const Event*, max_vox_count> _events;
    std::array<uint8_t, max_vox_count> _queue;

    DispatcherMode _mode;

    uint8_t _note_on_count;
    uint8_t _holding_index;
    };
};
