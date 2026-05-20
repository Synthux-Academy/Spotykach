#pragma once

#include <array>
#include <functional>
#include "hw/card.h"
#include "hw/buffer.sdram.h"
#include "core/deck.h"
#include "config.h"
#include "nocopy.h"

namespace spotykach {

static const std::string kConfig = "SK/config.txt";
static const std::string kMemory = "SK/MEM";

class Storage;

static constexpr uint8_t kStorageTapeCount = 6;
static constexpr uint8_t kStorageSlotCount = 6;

class DeckStorage {

friend class Storage;

public:
    struct Slot {
        bool is_empty;
    };

    enum class State: uint8_t {
        idle,
        selecting,
        loading,
        saving,
        error
    };

    DeckStorage();
    ~DeckStorage() = default;

    void init(Card*, Deck*);

    bool is_idle() const { return _state == State::idle; }
    bool is_selecting() const { return _state == State::selecting; }
    bool is_processing() const { return _state == State::loading || _state == State::saving; }
    bool is_released() const { return !is_selecting() && !is_processing(); }
    bool is_preloading() const { return _is_preloading; }
    State state() const { return _state; }
    Deck::Ref deck_ref() const { return _deck->ref; }
    float progress() const { return _state != State::idle ? _card->progress() : 0; }

    void set_on_save_audio(std::function<void(const Deck::Ref)>);

    void next_tape();
    void previous_tape();
    uint8_t selected_tape_idx() const { return _tape_idx; }
    
    Slot slot_at(const uint8_t idx) const { return _slots[idx]; }
    bool can_load() const { return _slot_idx != kNone && !_slots[_slot_idx].is_empty; }
    uint8_t selected_slot_idx() const { return _slot_idx; }
    void select_slot_at(const uint8_t idx) { _slot_idx = idx; }
    void reset_recent_slot() { _recent_slot_idx = kNone; }

protected:
    void mark_error()
    {
        _state = State::error;
    }
    void activate();
    void deactivate();
    void save();
    void load();
    void preload();
    void cancel();
    void process();

private:
    NOCOPY(DeckStorage)

    void _read_slots();
    bool _read_preload_source(uint8_t& tape, uint8_t& slot);
    void _save_preload_source();
    void _clear_preload_source();

    static constexpr uint8_t kNone = 0xff;

    Card* _card;
    Deck* _deck;
    State _state;

    std::string _deck_dir;
    uint8_t _tape_idx;
    uint8_t _slot_idx;
    uint8_t _recent_tape_idx;
    uint8_t _recent_slot_idx;
    std::array<Slot, kStorageSlotCount> _slots;
    std::function<void(const Deck::Ref)> _on_audio_saved;
    bool _is_preloading;
};

class Storage {
    public:
        enum class PreloadStep: uint8_t {
            settings,
            init_deck_a,
            deck_a,
            init_deck_b,
            deck_b,
            done
        };

        Storage(): 
        _opening_deck { Deck::None }
        {};
        ~Storage() = default;

        void init(Deck& deck_a, Deck& deck_b)
        {
            _card.init(SDRAMBuffer::pool().card_buffer());
            
            _deck_storage[Deck::A].init(&_card, &deck_a);
            _deck_storage[Deck::B].init(&_card, &deck_b);

            using namespace std::placeholders;
            auto on_save = std::bind(&Storage::_on_audio_saved, this, _1);
            _deck_storage[Deck::A].set_on_save_audio(on_save);
            _deck_storage[Deck::B].set_on_save_audio(on_save);

            _timer.Init();

            _deck_storage[Deck::B].preload();
        }

        Card::State card_state() const
        {
            return _card.state();
        }

        void read_settigs()
        {
            _preload_step = PreloadStep::settings;
            _counter = 0;
            _timer.Restart();
            _card.recognize();
        }

        void activate(const Deck::Ref ref)
        {
            if (_opening_deck != Deck::None) return;
            _opening_deck = ref;
            _counter = 0;
            _timer.Restart();            
        } 

        void deactivate(const Deck::Ref ref) 
        {
            _deck_storage[ref].deactivate();
        }

        void load(const Deck::Ref ref) 
        {
            if (!of(ref).is_selecting() || card_state() != Card::State::idle) return;
            _deck_storage[ref].load();
        }

        void save(const Deck::Ref ref) 
        {
            if (!of(ref).is_selecting() || card_state() != Card::State::idle) return;
            _deck_storage[ref].save();
        }

        void cancel(const Deck::Ref ref) 
        {
            of(ref).cancel();
            if (_preload_step == PreloadStep::deck_a) {
                _preload_step = PreloadStep::init_deck_b;
            }
            else {
                _preload_step = PreloadStep::done;
            }
        }

        void process()
        {
            switch (_preload_step) {
                case PreloadStep::done: break;
                case PreloadStep::settings: {
                    if (_timer.HasPassedMs(100)) {
                        if (_card.mount()) {
                            _do_read_settings();
                            _preload_or_skip();
                        }
                        else if (_counter >= 10) {
                            _preload_or_skip();
                        }
                        _counter ++;
                        _timer.Restart();
                    }
                    return;
                }

                case PreloadStep::init_deck_a: {
                    of(Deck::A).preload();
                    _preload_step = PreloadStep::deck_a;
                    break;
                }

                case PreloadStep::deck_a: {
                    if (of(Deck::A).is_released()) {
                        _preload_step = PreloadStep::init_deck_b;
                    }
                    break;
                }

                case PreloadStep::init_deck_b: {
                    of(Deck::B).preload();
                    _preload_step = PreloadStep::deck_b;
                    break;
                }

                case PreloadStep::deck_b: {
                    if (of(Deck::B).is_released()) {
                        _preload_step = PreloadStep::done;
                    }
                    break;
                }
            }

            if (_opening_deck != Deck::None && _timer.HasPassedMs(200)) {
                if (_counter < 5) {
                    if (_counter % 2 == 0) {
                        _card.recognize();
                    }
                    else {
                        if (_card.mount()) { 
                            _deck_storage[_opening_deck].activate();
                            _opening_deck = Deck::None;
                            return;
                        }
                        _card.unmount();
                    }
                    _counter ++;
                }
                else {
                    _opening_deck = Deck::None;
                }
                return;
            }

            _deck_storage[Deck::A].process();
            _deck_storage[Deck::B].process();
            if (_can_unmount()) _card.unmount();
        }

        DeckStorage& of(Deck::Ref ref) { return _deck_storage[ref]; }

    private:
        NOCOPY(Storage)

        void _do_read_settings()
        {
            uint8_t* data = nullptr;
            size_t size = 0;
            _card.read_file((char*)kConfig.c_str(), data, &size);
            Config::dynamic().fill(data, size);
        }

        void _preload_or_skip()
        {
            if (Config::dynamic().is_preload_on()) {
                _preload_step = PreloadStep::init_deck_a;
            }
            else {
                _preload_step = PreloadStep::done;
            }
        }

        bool _can_unmount() const
        {
            return _preload_step == PreloadStep::done
                    && (_card.state() == Card::State::idle || _card.state() == Card::State::failed)
                    && _deck_storage[Deck::A].is_released()
                    && _deck_storage[Deck::B].is_released();
        }

        void _on_audio_saved(const Deck::Ref ref)
        {
            auto other_ref = 1 - ref;
            if (_deck_storage[other_ref].is_selecting()) {
                _deck_storage[other_ref]._read_slots();
            }
        }

        Card _card;
        std::array<DeckStorage, Deck::Ref::Count> _deck_storage;
        daisy::StopwatchTimer _timer;
        uint8_t _counter;
        Deck::Ref _opening_deck;
        PreloadStep _preload_step;
};

};
