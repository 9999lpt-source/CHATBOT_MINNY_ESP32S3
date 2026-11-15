#include "micCapture.h"
#include "config.h"
#include <Arduino.h>
#include <math.h>
#include "driver/i2s.h"

// ===== THÔNG SỐ STATIC =====
static const i2s_port_t MIC_PORT = I2S_NUM_1;

static volatile uint16_t levelRms = 0;
static volatile uint16_t levelPeak = 0;

//===== CẤU HÌNH I2S =====
static bool installI2s()
{
    i2s_config_t cfg = {};
    cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    cfg.sample_rate = audio::SAMPLE_RATE;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    cfg.channel_format = audio::MIC_USE_LEFT ? I2S_CHANNEL_FMT_ONLY_LEFT : I2S_CHANNEL_FMT_ONLY_RIGHT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    cfg.dma_buf_count = audio::MIC_DMA_COUNT;
    cfg.dma_buf_len = audio::FRAME_SAMPLES;
    cfg.use_apll = false;

    if (i2s_driver_install(MIC_PORT, &cfg, 0, NULL) != ESP_OK)
    {
        Serial.println("[MIC] Loi cai I2S");
        return false;
    }

    i2s_pin_config_t pins = {};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = pin::MIC_SCK;
    pins.ws_io_num = pin::MIC_WS;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = pin::MIC_SD;

    if (i2s_set_pin(MIC_PORT, &pins) != ESP_OK)
    {
        i2s_driver_uninstall(MIC_PORT);
        Serial.println("[MIC] Loi cai dat chan I2S");
        return false;
    }

    return true;
}

// ===== MIC TASK =====
static void micTask(void *pvParameters)
{
    int32_t raw[audio::FRAME_SAMPLES];
    int32_t dcAcc = 0;
    int64_t sumSq = 0;
    uint32_t count = 0;
    int32_t peak = 0;
    uint8_t frames = 0;

    for (;;)
    {
        size_t byteRead = 0;
        esp_err_t err = i2s_read(MIC_PORT, raw, sizeof(raw), &byteRead, pdMS_TO_TICKS(audio::MIC_READ_TIMEOUT_MS));

        if (err != ESP_OK || byteRead == 0)
        {
            Serial.println("[MIC] Loi doc du lieu tu mic");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t n = byteRead / sizeof(int32_t);
        for (size_t i =  0; i < n; i++)
        {
            int32_t x = raw[i] >> audio::MIC_SHIIFT;
            dcAcc += x - (dcAcc >> 8);
            int32_t y = x - (dcAcc >> 8); // bỏ mức nền trước khi cắt về 16-bit
            if (y > 32767)
                y = 32767;
            else if (y < -32768)
                y = -32768;

            int32_t a = (y < 0) ? -y : y;
            if (a > peak) 
                peak = a;
            sumSq += (int64_t)y * y;
            count++;
        }

        frames++;
        if (frames >= audio::LEVEL_FREAME)
        {
            levelRms = (uint16_t)sqrtf((float)(sumSq / (int64_t)count));
            levelPeak = (uint16_t)peak;
            sumSq = 0;
            count = 0;
            peak = 0;
            frames = 0;
        }
    }
}

void initMic()
{
    if (!installI2s())
    {
        Serial.println("[MIC] Cai I2S that bai");
        return;
    }
    xTaskCreatePinnedToCore(micTask, task::MIC.name, task::MIC.stack,
                            NULL, task::MIC.prio, NULL, task::MIC.core);
}

uint16_t micGetLevel() { return levelRms; }
uint16_t micGetPeak() { return levelPeak; }