#include "vad.h"
#include "config.h"
#include <math.h>

// ===== CÁC BIẾN NỘI BỘ ===== 
static VadState currentState = VadState::Idle;
static uint32_t processedFrames = 0;
static uint16_t currentRms = 0;

// ===== RESET VAD =====
void vadReset()
{
    currentState = VadState::Idle;
    processedFrames = 0;
    currentRms = 0;
}

// ===== BẮT ĐẦU NGHE (NHẤN NÚT, CHỜ NÓI) =====
void vadStartListening()
{
    currentState = VadState::Listening;
    processedFrames = 0;
    currentRms = 0; 
}

// ===== TÍNH RMS =====
static void calculateRmsValue(const int16_t* samples, size_t count)
{
    int64_t sumSq = 0;
    for (size_t i = 0; i < count; i++)
    {
        int32_t sample = samples[i];
        sumSq += (int64_t)sample * sample;
    }

    float meanSq = (float)sumSq / (float)count;
    currentRms = (uint16_t)sqrtf(meanSq);
}

// ===== XỬ LÝ VAD (TÍNH RMS) =====
VadEvent vadProcessFrame(const int16_t* samples, size_t count)
{
    if (samples == nullptr || count != audio::FRAME_SAMPLES)
        return VadEvent::InvalidFrame;

    if (currentState == VadState::Idle)
        return VadEvent::None;

    calculateRmsValue(samples, count);

    processedFrames++;

    return VadEvent::None;
}

VadStatus vadGetStatus()
{
    VadStatus result = { currentState, processedFrames, currentRms};
    return result;
}