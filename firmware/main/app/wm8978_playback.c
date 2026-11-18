/**
 * @file    wm8978_playback.c
 * @brief   WM8978 播放通路：ESP32-S3 内部生成 1 kHz 正弦波，48 kHz / 16 bit /
 *          立体声循环送入 I2S TX，经 LOUT1/ROUT1 输出到耳机。
 *
 * @details 执行顺序（不能颠倒）：
 *          1) `wm8978_i2c_init()`      借用共享 I2C 总线并挂载 0x1A；
 *          2) `wm8978_apply_init_seq()` 写寄存器，配好时钟/接口/DAC/输出通路；
 *          3) `wm8978_i2s_tx_open()`   打开 I2S TX 主机通道（MCLK 12.288 MHz）；
 *          4) 建任务循环 `i2s_channel_write()`。
 *
 * @warning **必须先配置 codec、后开 I2S**。WM8978 在未完成寄存器初始化时
 *          收到 MCLK 会进入异常状态，控制口随即静默（社区实测案例）。本文件
 *          的调用顺序即为此约束而设，勿为「先出波形」而把 I2S 提前。
 *
 * @note    引脚（按 codec 侧命名，已实板确认）：
 *          MCLK=IO17、BCLK=IO6、WS/LRCLK=IO7、DOUT->DACDAT=IO16。
 *          ADCDAT=IO15 本版不接收（TX-only），驱动不会配置该脚。
 *          MODE 已在硬件接低（2-wire），本文件不驱动 MODE。
 *
 * @copyright (c) 2026 BYAI. All rights reserved.
 */

#include "wm8978_playback.h"

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board_i2c.h"
#include "sysdef.h"

static const char *TAG = "wm8978";

/* ------------------------------------------------------------------------- */
/* 参数                                                                       */
/* ------------------------------------------------------------------------- */

#define WM8978_I2C_ADDR         0x1A        /**< MODE=0 的 7 位地址 */
#define WM8978_I2C_FREQ_HZ      100000
#define WM8978_I2C_TIMEOUT_MS   500
#define WM8978_WRITE_RETRY      3

#define AUDIO_SAMPLE_RATE_HZ    48000
#define AUDIO_MCLK_MULTIPLE     256         /**< 48k × 256 = 12.288 MHz */

/** 1 kHz 在 48 kHz 下正好 48 个采样点一个周期 —— 整数关系，无累积误差 */
#define TONE_FREQ_HZ            1000
#define TONE_PERIOD_SAMPLES     (AUDIO_SAMPLE_RATE_HZ / TONE_FREQ_HZ)   /* 48 */
/** 每次写 20 个周期（20 ms），给日志输出留出余量，避免欠载 */
#define TONE_PERIODS_PER_WRITE  20
#define TONE_BUF_SAMPLES        (TONE_PERIOD_SAMPLES * 2 * TONE_PERIODS_PER_WRITE)
#define TONE_BUF_BYTES          (TONE_BUF_SAMPLES * sizeof(int16_t))

/* I2S 引脚：MCLK / BCLK / WS 由 ESP32-S3 输出，DOUT 送 codec 的 DACDAT */
#define PIN_I2S_MCLK            GPIO_NUM_17
#define PIN_I2S_BCLK            GPIO_NUM_6
#define PIN_I2S_WS              GPIO_NUM_7
#define PIN_I2S_DOUT            GPIO_NUM_16

/* ------------------------------------------------------------------------- */
/* WM8978 寄存器                                                              */
/* ------------------------------------------------------------------------- */

/* 寄存器地址（7 位） */
#define R00_RESET               0x00
#define R01_POWER_MGMT_1        0x01
#define R02_POWER_MGMT_2        0x02
#define R03_POWER_MGMT_3        0x03
#define R04_AUDIO_INTERFACE     0x04
#define R06_CLOCKING            0x06
#define R07_ADDITIONAL          0x07
#define R0A_DAC_CONTROL         0x0A
#define R0B_LDAC_VOLUME         0x0B
#define R0C_RDAC_VOLUME         0x0C
#define R0E_ADC_CONTROL         0x0E
#define R0F_LADC_VOLUME         0x0F
#define R10_RADC_VOLUME         0x10
#define R2B_BEEP_CONTROL        0x2B
#define R2C_INPUT_CONTROL       0x2C
#define R2D_LEFT_INP_PGA        0x2D
#define R2E_RIGHT_INP_PGA       0x2E
#define R2F_LEFT_ADC_BOOST      0x2F
#define R30_RIGHT_ADC_BOOST     0x30
#define R31_OUTPUT_CONTROL      0x31
#define R32_LEFT_MIXER          0x32
#define R33_RIGHT_MIXER         0x33
#define R34_LOUT1_HP            0x34
#define R35_ROUT1_HP            0x35
#define R36_LOUT2_SPK           0x36
#define R37_ROUT2_SPK           0x37

/** 音量/增益寄存器的 update bit(bit8)：写入时须置位才会锁存 */
#define REG_UPDATE_BIT          0x100
/** R1：VMIDSEL[1:0]=11 取 5 kΩ，BIASEN / BUFIOEN / MICBEN 使能 */
#define PM1_VMID_5K_BIAS_BUF_MIC    0x01F
/** R4：FMT=I2S、WL=16 bit，极性默认 */
#define AIF_I2S_16BIT           0x010
/** R7：SR[3:1] 的 000 档 = 48 kHz（数据手册无 44.1 kHz 专用档） */
#define ADDCTL_SR_48K           0x000
/** R2D/R2E：INPPGAVOL=0x10 即 0 dB，bit6 是静音位必须为 0 */
#define PGA_GAIN_0DB_UNMUTED    0x010

typedef struct {
    uint8_t     reg;    /**< 7 位寄存器地址 */
    uint16_t    val;    /**< 9 位寄存器值 */
    const char *desc;   /**< 这一步在做什么，仅描述代码行为 */
} wm8978_reg_t;

/**
 * @brief 播放路径的上电配置序列，顺序即写入顺序（先复位、再偏置、最后开输出）。
 * @note  只走耳机通路（LOUT1/ROUT1）；扬声器输出级不使能（板上功放另行处理）。
 *        取值为本板既有参数，未做爆音抑制优化。
 */
static const wm8978_reg_t wm8978_init_seq[] = {
    { R00_RESET,            0x000,                          "软复位，恢复默认值" },
    { R01_POWER_MGMT_1,     PM1_VMID_5K_BIAS_BUF_MIC,       "VMID 5k + 偏置/BUFIO/麦克风偏置使能" },
    { R02_POWER_MGMT_2,     0x1BF,                          "LOUT1EN/ROUT1EN 耳机输出 + ADC/PGA/Boost" },
    { R03_POWER_MGMT_3,     0x00F,                          "双 DAC + 左右输出混合器；扬声器输出级关闭" },
    { R04_AUDIO_INTERFACE,  AIF_I2S_16BIT,                  "I2S Philips、16 bit、时钟极性默认" },
    { R06_CLOCKING,         0x000,                          "MCLK 直供不分频、codec 作从机" },
    { R07_ADDITIONAL,       ADDCTL_SR_48K,                  "R7 SR 取 48 kHz 档" },
    { R0A_DAC_CONTROL,      0x008,                          "DAC 128x 过采样，取消软静音" },
    { R0E_ADC_CONTROL,      0x108,                          "ADC 128x 过采样 + HPFEN" },
    { R2B_BEEP_CONTROL,     0x000,                          "不启用 INVROUT2（只影响 ROUT2/BTL）" },
    { R2C_INPUT_CONTROL,    0x033,                          "左右声道差分输入（沿用既有取值）" },
    { R2D_LEFT_INP_PGA,     REG_UPDATE_BIT | PGA_GAIN_0DB_UNMUTED, "左输入 PGA 0 dB、非静音并锁存" },
    { R2E_RIGHT_INP_PGA,    REG_UPDATE_BIT | PGA_GAIN_0DB_UNMUTED, "右输入 PGA 0 dB、非静音并锁存" },
    { R2F_LEFT_ADC_BOOST,   0x100,                          "左 PGA +20 dB（默认值）" },
    { R30_RIGHT_ADC_BOOST,  0x100,                          "右 PGA +20 dB（默认值）" },
    { R31_OUTPUT_CONTROL,   0x002,                          "TSDEN 过热保护，SPKBOOST 关闭" },
    { R32_LEFT_MIXER,       0x001,                          "左 DAC 接入输出混合器" },
    { R33_RIGHT_MIXER,      0x001,                          "右 DAC 接入输出混合器" },
    { R0B_LDAC_VOLUME,      REG_UPDATE_BIT | 0x0FF,         "左 DAC 数字音量 0 dB" },
    { R0C_RDAC_VOLUME,      REG_UPDATE_BIT | 0x0FF,         "右 DAC 数字音量 0 dB" },
    { R0F_LADC_VOLUME,      REG_UPDATE_BIT | 0x0FF,         "左 ADC 数字音量 0 dB" },
    { R10_RADC_VOLUME,      REG_UPDATE_BIT | 0x0FF,         "右 ADC 数字音量 0 dB" },
    { R34_LOUT1_HP,         REG_UPDATE_BIT | 0x039,         "左耳机音量（0x39 = 0 dB）" },
    { R35_ROUT1_HP,         REG_UPDATE_BIT | 0x039,         "右耳机音量" },
    { R36_LOUT2_SPK,        REG_UPDATE_BIT | 0x039,         "左扬声器音量（输出级关闭，仅留配置）" },
    { R37_ROUT2_SPK,        REG_UPDATE_BIT | 0x039,         "右扬声器音量（输出级关闭，仅留配置）" },
};

/* ------------------------------------------------------------------------- */
/* 正弦波数据                                                                 */
/* ------------------------------------------------------------------------- */

/** 一个周期的 48 点正弦表，幅度 16384（约 -6 dBFS），避免削波 */
static const int16_t s_sine_period[TONE_PERIOD_SAMPLES] = {
        0,  2139,  4240,  6270,  8192,  9974, 11585, 12998,
    14189, 15137, 15826, 16244, 16384, 16244, 15826, 15137,
    14189, 12998, 11585,  9974,  8192,  6270,  4240,  2139,
        0, -2139, -4240, -6270, -8192, -9974, -11585, -12998,
   -14189, -15137, -15826, -16244, -16384, -16244, -15826, -15137,
   -14189, -12998, -11585, -9974, -8192, -6270, -4240, -2139,
};

/** 送 DMA 的缓冲：整数个周期，左右声道交织（L,R,L,R...） */
static int16_t s_tone_buf[TONE_BUF_SAMPLES];

/* ------------------------------------------------------------------------- */
/* 静态句柄                                                                   */
/* ------------------------------------------------------------------------- */

static i2c_master_bus_handle_t s_i2c_bus;
static i2c_master_dev_handle_t s_wm8978_dev;
static i2s_chan_handle_t       s_tx_chan;

/* ------------------------------------------------------------------------- */
/* I2C 与寄存器写入                                                           */
/* ------------------------------------------------------------------------- */

esp_err_t wm8978_i2c_init(void)
{
    esp_err_t err = board_i2c_get_bus(&s_i2c_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "借用板级 I2C 总线失败: 0x%x (%s)", err, esp_err_to_name(err));
        return err;
    }

    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = WM8978_I2C_ADDR,
        .scl_speed_hz = WM8978_I2C_FREQ_HZ,
    };
    err = i2c_master_bus_add_device(s_i2c_bus, &dev_cfg, &s_wm8978_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WM8978 设备挂载失败 (7 位地址 0x%02X): 0x%x (%s)",
                 WM8978_I2C_ADDR, err, esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

/**
 * @brief 写入一个 9 位寄存器值，失败时做有限重试。
 * @note  WM8978 只支持写、不支持回读，返回值只表示 I2C 传输是否被 ACK。
 */
static esp_err_t wm8978_write_reg(uint8_t reg, uint16_t value)
{
    /* 帧格式：首字节 = 7 位寄存器地址 + 数据最高位，次字节 = 数据低 8 位 */
    const uint8_t frame[2] = {
        (uint8_t)((reg << 1) | ((value >> 8) & 0x01U)),
        (uint8_t)(value & 0xFFU),
    };

    esp_err_t err = ESP_OK;
    for (int attempt = 1; attempt <= WM8978_WRITE_RETRY; ++attempt) {
        err = i2c_master_transmit(s_wm8978_dev, frame, sizeof(frame), WM8978_I2C_TIMEOUT_MS);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        ESP_LOGW(TAG, "R%02X 写入第 %d/%d 次失败: 0x%x",
                 reg, attempt, WM8978_WRITE_RETRY, err);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return err;
}

static esp_err_t wm8978_apply_init_seq(void)
{
    const size_t total = sizeof(wm8978_init_seq) / sizeof(wm8978_init_seq[0]);
    for (size_t i = 0; i < total; ++i) {
        const wm8978_reg_t *item = &wm8978_init_seq[i];
        const esp_err_t err = wm8978_write_reg(item->reg, item->val);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "R%02X 写入失败: 0x%x (%s) —— %s",
                     item->reg, err, esp_err_to_name(err), item->desc);
            return err;
        }
        /* 软复位后等内部状态机稳定；VMID 偏置建立后等参考电压稳定 */
        if (item->reg == R00_RESET) {
            vTaskDelay(pdMS_TO_TICKS(20));
        } else if (item->reg == R01_POWER_MGMT_1) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    ESP_LOGI(TAG, "寄存器写入完成，共 %u 项（控制口 ACK 正常）", (unsigned)total);
    return ESP_OK;
}

/* ------------------------------------------------------------------------- */
/* I2S TX                                                                     */
/* ------------------------------------------------------------------------- */

static esp_err_t wm8978_i2s_tx_open(void)
{
    const i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_chan, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S 通道创建失败: 0x%x (%s)", err, esp_err_to_name(err));
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE_HZ),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                       I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = PIN_I2S_MCLK,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .dout = PIN_I2S_DOUT,
            .din  = I2S_GPIO_UNUSED,   /* 本版只发送，不接 ADCDAT */
        },
    };
    /* 48k × 256 = 12.288 MHz；MCLK/BCLK = 12.288M/1.536M = 8，整数分频 */
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

    err = i2s_channel_init_std_mode(s_tx_chan, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S TX 初始化失败: 0x%x (%s)", err, esp_err_to_name(err));
        return err;
    }
    err = i2s_channel_enable(s_tx_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S TX 使能失败: 0x%x (%s)", err, esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

/** @brief 按周期展开正弦表；每次写整周期倍数的数据，相位不会随写入次数漂移。 */
static void wm8978_tone_fill(void)
{
    for (int p = 0; p < TONE_PERIODS_PER_WRITE; ++p) {
        for (int i = 0; i < TONE_PERIOD_SAMPLES; ++i) {
            const int idx = (p * TONE_PERIOD_SAMPLES + i) * 2;
            s_tone_buf[idx]     = s_sine_period[i];   /* 左声道 */
            s_tone_buf[idx + 1] = s_sine_period[i];   /* 右声道 */
        }
    }
}

/** @brief 循环写入 I2S TX；写满 DMA 后阻塞等待，天然与采样率同步。 */
static void wm8978_tone_task(void *arg)
{
    (void)arg;
    uint32_t writes = 0;

    ESP_LOGI(TAG, "开始循环播放：%d Hz 正弦，%d Hz / 16 bit / 立体声，每次 %u ms",
             TONE_FREQ_HZ, AUDIO_SAMPLE_RATE_HZ,
             (unsigned)(TONE_PERIODS_PER_WRITE * 1000 / TONE_FREQ_HZ));

    while (true) {
        size_t written = 0;
        const esp_err_t err = i2s_channel_write(s_tx_chan, s_tone_buf, TONE_BUF_BYTES,
                                                &written, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "I2S 写入失败: 0x%x (%s)，已写入 %u 字节，停止播放",
                     err, esp_err_to_name(err), (unsigned)written);
            break;
        }
        if (written != TONE_BUF_BYTES) {
            ESP_LOGW(TAG, "部分写入: %u/%u 字节", (unsigned)written, (unsigned)TONE_BUF_BYTES);
        }
        if ((++writes % 1000) == 0) {
            ESP_LOGI(TAG, "已持续播放约 %u 秒", (unsigned)(writes * TONE_PERIODS_PER_WRITE / 1000));
        }
    }

    i2s_channel_disable(s_tx_chan);
    vTaskDelete(NULL);
}

/* ------------------------------------------------------------------------- */
/* 对外接口                                                                   */
/* ------------------------------------------------------------------------- */

esp_err_t wm8978_playback_start(void)
{
    if (s_wm8978_dev == NULL) {
        ESP_LOGE(TAG, "未初始化：请先调用 wm8978_i2c_init()");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_tx_chan != NULL) {
        ESP_LOGW(TAG, "播放已启动，忽略重复调用");
        return ESP_OK;
    }

    /* 先配 codec、后开 I2S：WM8978 未初始化时收到 MCLK 会进入异常状态 */
    esp_err_t err = wm8978_apply_init_seq();
    if (err != ESP_OK) {
        return err;
    }

    err = wm8978_i2s_tx_open();
    if (err != ESP_OK) {
        return err;
    }

    wm8978_tone_fill();
    ESP_LOGI(TAG, "MCLK=%u Hz, BCLK=%u Hz, 缓冲 %u 字节",
             (unsigned)(AUDIO_SAMPLE_RATE_HZ * AUDIO_MCLK_MULTIPLE),
             (unsigned)(AUDIO_SAMPLE_RATE_HZ * 2 * 16),
             (unsigned)TONE_BUF_BYTES);

    if (xTaskCreate(wm8978_tone_task, "audio_tone", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "音频任务创建失败");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
