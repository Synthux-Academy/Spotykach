#include "reverb.h"
#include "common.h"
#include "hw/sdram_alloc.h"

using namespace spotykach;
using namespace daisysp;

void Reverb::init(const float sample_rate)
{
    _reverb = Verb(infrasonic::SDRAM::allocate<ReverbSc>());
    _reverb->Init(sample_rate);
    _reverb->SetLpFreq(4000.f);
}

void Reverb::set_feedback_norm(const float norm)
{
    _reverb->SetFeedback(fclamp(norm, 0.f, 1.f));
}

void Reverb::process(float& inout0, float& inout1)
{
    float out0, out1;
    _reverb->Process(inout0, inout1, &out0, &out1);
    inout0 = out0;
    inout1 = out1;
}
