#include "core.h"
#include "daisysp.h"
#include <functional>
#include "hw/buffer.sdram.h"
#include "mode.h"
#include "expose.h"

using namespace spotykach;
using namespace daisysp;

Core::Core():
_driver { Driver(_decks[Deck::A], _decks[Deck::B], _click, _panner, _mod.data()) }
{
    _source.fill(Deck::Source::external);
    _bus.fill(0);
    _xfade.SetStage(.5f);
};
  
void Core::init(const float sample_rate, const float callback_buffer_size) {
    _driver.init(sample_rate, callback_buffer_size);
    _panner.init(sample_rate);
    _click.init(sample_rate);
    _mix_smooth.init(sample_rate);

    auto& pool = SDRAMBuffer::pool();
    auto size = pool.sourceBufferSize(); 

    static Buffer::Frame* main_buf[Deck::Count] = { pool.sourceBuffer(), pool.sourceBuffer() };

    static float* detect_buf[Deck::Count][2] = {
        { pool.detectorBuffer(), pool.detectorBuffer() },
        { pool.detectorBuffer(), pool.detectorBuffer() }
    };
    static float* delay_buf[Deck::Count][2] = {
        { pool.delayBuffer(), pool.delayBuffer() },
        { pool.delayBuffer(), pool.delayBuffer() }
    };
    static size_t* slice_buf[Deck::Count] = { 
        pool.slices_a(), pool.slices_b() 
    }; 
    static Event* track_buf[Deck::Count] = { 
        pool.track_buffer_a(), pool.track_buffer_b() 
    };

    for (auto d = 0; d < Deck::Count; d++) {
        auto ref = (Deck::Ref)d;
        deck(ref).ref = ref;

        Deck::Params p;
        p.sample_rate = sample_rate;
        p.main_buf_size = size;
        p.main_buf = main_buf[d];
        p.detect_buf = detect_buf[d];
        p.delay_buf = delay_buf[d];
        p.slice_buf = slice_buf[d];
        p.track_buf = track_buf[d];
        deck(ref).init(p);
        
        _mod[ref].init(sample_rate);
    }  
};

Panner::Mode _panner_mode(const spotykach::Mode mode)
{
    switch (mode) {
        case spotykach::Mode::Reel: return Panner::Mode::smooth;
        case spotykach::Mode::Slice: return Panner::Mode::step;
        default: return Panner::Mode::off;
    }
}
void Core::set_route(const Route val)
{
    if (val == _route) return;
    _route = val;

    infer_panner_mode();
}
void Core::infer_panner_mode()
{
    for (auto ref: { Deck::A, Deck::B }) {
        auto& deck = _decks[ref];
        auto pan_mode = Panner::Mode::off;
        auto wide = false;
        if (_route == Route::GenerativeStereo) {
            wide = deck.mode() == Mode::Drift;
            pan_mode = _panner_mode(deck.mode());
        }
        _panner.set_mode(pan_mode, ref);
        deck.voxs().set_is_wide(wide);
    }
}

void Core::prepare() 
{
    for (auto& d: _decks) d.prepare();
}

void Core::process(const float* const* in, float** out, size_t size) 
{
    float in_a[2] = { 0, 0 };
    float in_b[2] = { 0, 0 };
    float out_a[2] = { 0, 0 };
    float out_b[2] = { 0, 0 };
    auto& deck_a = deck(Deck::A);
    auto& deck_b = deck(Deck::B);

    auto stereo = _route != Route::DoubleMono;

    for (size_t i = 0; i < size; i++) {
        if (stereo) {
            in_a[0] = in_b[0] = in[0][i];
            in_a[1] = in_b[1] = in[1][i];
        }
        else {
            in_a[0] = in_a[1] = in[0][i];
            in_b[0] = in_b[1] = in[1][i];
        }

        deck_a.process_out(in_a[0], in_a[1], out_a[0], out_a[1]);
        deck_b.process_out(in_b[0], in_b[1], out_b[0], out_b[1]);

        switch (_source[Deck::A]) {
            case Deck::Source::internal: deck_a.process_in(out_b[0], out_b[1]); break;
            case Deck::Source::external: deck_a.process_in(in_a[0], in_a[1]); break;
        }
        
        switch (_source[Deck::B]) {
            case Deck::Source::internal: deck_b.process_in(out_a[0], out_a[1]); break;
            case Deck::Source::external: deck_b.process_in(in_b[0], in_b[1]); break;
        }

        _mod[Deck::A].follow(out_a[0]);
        _mod[Deck::B].follow(out_b[0]);

        float* d_out[2] = { out_a, out_b };
        _panner.process(d_out, d_out);

        auto mix = _mix_smooth.process(_mix + _mix_mod);
        _xfade.SetStage(std::clamp(mix, 0.f, 1.f));
        
        if (stereo) {
            _xfade.Process(out_a[0], out_a[1], out_b[0], out_b[1], _bus[0], _bus[1]);
        }
        else {
            auto sum_a = (out_a[0] + out_a[1]) * 0.7079;
            auto sum_b = (out_b[0] + out_b[1]) * 0.7079;
            _xfade.Process(sum_a, 0, 0, sum_b, _bus[0], _bus[1]);
        }
        
        auto click = _click.process() * _click_mix;
        
        out[0][i] = SoftLimit(_bus[0] + click);
        out[1][i] = SoftLimit(_bus[1] + click);
    }
};
