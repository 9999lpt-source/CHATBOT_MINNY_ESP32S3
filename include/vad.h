#ifndef VAD_H
#define VAD_H

#include <stdint.h>
#include <stddef.h>

// Trạng thái Vad
enum class VadState : uint8_t
{
    Idle,       // Chờ nhấn nút
    Listening,  // Chờ nói
    Candidate,  // Có tín hiệu, xác minh đang nói
    Speaking    // Đang nói
};

// Sự kiện vad
enum class VadEvent : uint8_t
{
    None,           // Không có sự kiện
    SpeechStart,    // Bắt dầu nói
    SPeechEnd,      // Kết thúc lời nói hợp lệ
    NoSpeech,       // Chờ nhưng không nói
    MaxDuration,    // Đạt giới hạn nói tối đa
    Rejected,       // Tiếng nói không đạt điều kiện
    InvalidFrame,   // Khung âm thanh không đúng
};

// Cấu trúc vad
struct VadStatus
{
    VadState state;
    uint32_t framesProcessed;
};

void vadReset();                                                // Reset vad
void vadStartListening();                                       // Bắt đầu phiên nói mới
VadEvent vadProcessFrame(const int16_t* samples, size_t count);  // Xử lý 1 khung, trả về sự kiện
VadStatus vadGetStatus();                                       // Lấy bản sao trạng thái hiện tại

#endif // VAD_H