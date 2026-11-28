#include "vad.h"
#include "config.h"

// ===== CÁC BIẾN NỘI BỘ ===== 
static VadState currentState = VadState::Idle;
static uint32_t processedFrames = 0;

// =====
void vadReset()
{
    currentState = VadState::Idle;
    processedFrames = 0;
}

// =====
void vadStartListening()
{
    currentState = VadState::Listening;
    processedFrames = 0;
}

VadEvent vadProcessFrame(const int16_t* samples, size_t count)
{
    if (samples == nullptr || count != audio::FRAME_SAMPLES)
        return VadEvent::InvalidFrame;

    if (currentState == VadState::Idle)
        return VadEvent::None;

    processedFrames++;

    /*=========*/

    return VadEvent::None;
    
}

VadStatus vadGetStatus()
{
    VadStatus result = { currentState, processedFrames };
    return result;
}