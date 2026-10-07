#pragma once

#include <array>
#include <stdint.h>
#include "util/scopedirqblocker.h"
#include "nocopy.h"

namespace spotykach {

/*
Fixed size FIFO that can be pushed to from any context
(main loop, any ISR) and popped from any context.
Every access is done with interrupts disabled, which keeps
the critical sections a handful of instructions long.
*/
template<typename T, uint8_t capacity>
class IrqQueue {
public:
    IrqQueue(): _head { 0 }, _tail { 0 }, _count { 0 } {}
    ~IrqQueue() = default;

    /* Returns false and drops the item if the queue is full. */
    bool push(const T& item) {
        daisy::ScopedIrqBlocker block;
        if (_count == capacity) return false;
        _items[_head] = item;
        _head = (_head + 1) % capacity;
        _count++;
        return true;
    }

    /* Returns false if the queue is empty. */
    bool pop(T& item) {
        daisy::ScopedIrqBlocker block;
        if (_count == 0) return false;
        item = _items[_tail];
        _tail = (_tail + 1) % capacity;
        _count--;
        return true;
    }

private:
    NOCOPY(IrqQueue)

    std::array<T, capacity> _items;
    uint8_t _head;
    uint8_t _tail;
    uint8_t _count;
};

};
