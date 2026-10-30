#include "gateway_hardware.h"

#include <stdbool.h>
#include <stddef.h>

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "soc/soc_caps.h"
#include "soc/uart_pins.h"
#include "User_Settings.h"

static const char *TAG = "gateway_hardware";

#if CONFIG_USER_MODBUS_RTU

#if !CONFIG_USER_DISPLAY_NONE && !CONFIG_USER_DISPLAY_LORA_GATEWAY
#include "User_Setup.h"
#endif

#if CONFIG_USER_DISPLAY_LVGL_TDISPLAY_S3
#include "board_tdisplay_s3.h"
#endif

#if CONFIG_USER_DISPLAY_LORA_GATEWAY
#include "board_pins.h"
#endif

_Static_assert(CONFIG_USER_MODBUS_UART_PORT < SOC_UART_HP_NUM,
    "Modbus requires an available high-performance UART");
_Static_assert(CONFIG_USER_MSTP_UART_PORT < SOC_UART_HP_NUM,
    "MS/TP requires an available high-performance UART");

typedef struct {
    const char *name;
    int pin;
    bool output;
} serial_pin_t;

typedef struct {
    const char *name;
    int pin;
} reserved_pin_t;

static const reserved_pin_t reserved_pins[] = {
    {"unused", -1},
#ifdef TFT_MOSI
    {"display MOSI", TFT_MOSI},
#endif
#ifdef TFT_MISO
    {"display MISO", TFT_MISO},
#endif
#ifdef TFT_SCLK
    {"display clock", TFT_SCLK},
#endif
#ifdef TFT_CS
    {"display CS", TFT_CS},
#endif
#ifdef TFT_DC
    {"display DC", TFT_DC},
#endif
#ifdef TFT_RST
    {"display reset", TFT_RST},
#endif
#ifdef TFT_BL
    {"display backlight", TFT_BL},
#endif
#ifdef TFT_WR
    {"display write", TFT_WR},
#endif
#ifdef TFT_RD
    {"display read", TFT_RD},
#endif
#ifdef TFT_D0
    {"display D0", TFT_D0}, {"display D1", TFT_D1},
    {"display D2", TFT_D2}, {"display D3", TFT_D3},
    {"display D4", TFT_D4}, {"display D5", TFT_D5},
    {"display D6", TFT_D6}, {"display D7", TFT_D7},
#endif
#if CONFIG_USER_DISPLAY_LVGL_TDISPLAY_S3
    {"display power", BOARD_LCD_POWER_EN_PIN},
    {"boot button", BOARD_BUTTON_BOOT},
    {"user button", BOARD_BUTTON_USER},
    {"battery ADC", BOARD_BAT_ADC_PIN},
#endif
#if CONFIG_USER_TOUCH_CST816
    {"touch SDA", CONFIG_USER_TOUCH_CST816_SDA_GPIO},
    {"touch SCL", CONFIG_USER_TOUCH_CST816_SCL_GPIO},
    {"touch reset", CONFIG_USER_TOUCH_CST816_RST_GPIO},
    {"touch interrupt", CONFIG_USER_TOUCH_CST816_INT_GPIO},
#endif
#if CONFIG_USER_DISPLAY_LORA_GATEWAY
    {"LoRa NSS", LORA_NSS}, {"LoRa clock", LORA_SCK},
    {"LoRa MOSI", LORA_MOSI}, {"LoRa MISO", LORA_MISO},
    {"LoRa reset", LORA_RESET}, {"LoRa busy", LORA_BUSY},
    {"LoRa DIO1", LORA_DIO1}, {"LoRa power", LORA_PA_POWER},
    {"LoRa PA enable", LORA_PA_EN}, {"LoRa TX enable", LORA_PA_TX_EN},
    {"OLED SDA", 17}, {"OLED SCL", 18},
    {"OLED reset", 21}, {"OLED power", 36},
#endif
#if CONFIG_ESP_CONSOLE_UART_DEFAULT
    {"console TX", U0TXD_GPIO_NUM}, {"console RX", U0RXD_GPIO_NUM},
#elif CONFIG_ESP_CONSOLE_UART_CUSTOM
    {"console TX", CONFIG_ESP_CONSOLE_UART_TX_GPIO},
    {"console RX", CONFIG_ESP_CONSOLE_UART_RX_GPIO},
#endif
#if CONFIG_IDF_TARGET_ESP32
    {"flash clock", 6}, {"flash D0", 7}, {"flash D1", 8},
    {"flash D2", 9}, {"flash D3", 10}, {"flash command", 11},
#if CONFIG_SPIRAM
    {"PSRAM CS", 16}, {"PSRAM clock", 17},
#endif
#elif CONFIG_IDF_TARGET_ESP32S3
    {"flash/PSRAM", 26}, {"flash/PSRAM", 27}, {"flash/PSRAM", 28},
    {"flash/PSRAM", 29}, {"flash/PSRAM", 30}, {"flash/PSRAM", 31},
    {"flash/PSRAM", 32},
#if CONFIG_SPIRAM_MODE_OCT || CONFIG_ESPTOOLPY_OCT_FLASH
    {"octal memory", 33}, {"octal memory", 34}, {"octal memory", 35},
    {"octal memory", 36}, {"octal memory", 37},
#endif
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG || CONFIG_ESP_CONSOLE_USB_CDC || \
    CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG
    {"USB D-", 19}, {"USB D+", 20},
#endif
#endif
};

static esp_err_t validate_serial_pins(
    const serial_pin_t *pins, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        int pin = pins[i].pin;
        if (!GPIO_IS_VALID_GPIO(pin) ||
            (pins[i].output && !GPIO_IS_VALID_OUTPUT_GPIO(pin))) {
            ESP_LOGE(TAG, "%s GPIO%d is not a valid %s pin",
                pins[i].name, pin, pins[i].output ? "output" : "input");
            return ESP_ERR_INVALID_ARG;
        }

        for (size_t j = 0; j < i; ++j) {
            if (pin == pins[j].pin) {
                ESP_LOGE(TAG, "%s and %s both use GPIO%d",
                    pins[i].name, pins[j].name, pin);
                return ESP_ERR_INVALID_ARG;
            }
        }
        for (size_t j = 0;
             j < sizeof(reserved_pins) / sizeof(reserved_pins[0]); ++j) {
            if (pin == reserved_pins[j].pin) {
                ESP_LOGE(TAG, "%s GPIO%d conflicts with %s",
                    pins[i].name, pin, reserved_pins[j].name);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    return ESP_OK;
}
#endif

esp_err_t gateway_hardware_validate(void)
{
#if CONFIG_USER_MODBUS_RTU
    if (USER_ENABLE_BACNET_MSTP &&
        CONFIG_USER_MODBUS_UART_PORT == CONFIG_USER_MSTP_UART_PORT) {
        ESP_LOGE(TAG, "Modbus and MS/TP must use different UARTs");
        return ESP_ERR_INVALID_ARG;
    }
#if CONFIG_ESP_CONSOLE_UART
    if (CONFIG_USER_MODBUS_UART_PORT == CONFIG_ESP_CONSOLE_UART_NUM ||
        (USER_ENABLE_BACNET_MSTP &&
         CONFIG_USER_MSTP_UART_PORT == CONFIG_ESP_CONSOLE_UART_NUM)) {
        ESP_LOGE(TAG, "An RS485 UART conflicts with the serial console");
        return ESP_ERR_INVALID_ARG;
    }
#endif

    const serial_pin_t pins[] = {
        {"Modbus TX", CONFIG_USER_MODBUS_UART_TX_GPIO, true},
        {"Modbus RX", CONFIG_USER_MODBUS_UART_RX_GPIO, false},
        {"Modbus DE/RE", CONFIG_USER_MODBUS_UART_DE_GPIO, true},
        {"MS/TP TX", CONFIG_USER_MSTP_UART_TX_GPIO, true},
        {"MS/TP RX", CONFIG_USER_MSTP_UART_RX_GPIO, false},
        {"MS/TP DE/RE", CONFIG_USER_MSTP_UART_DE_GPIO, true},
    };
    size_t count = USER_ENABLE_BACNET_MSTP ? 6U : 3U;
    esp_err_t err = validate_serial_pins(pins, count);
    if (err != ESP_OK) {
        return err;
    }

#if CONFIG_USER_MODBUS_PARITY_EVEN
    const char *parity = "even";
#elif CONFIG_USER_MODBUS_PARITY_ODD
    const char *parity = "odd";
#else
    const char *parity = "none";
#endif
    ESP_LOGI(TAG, "Modbus UART%d TX=%d RX=%d DE/RE=%d, %d baud, 8 data, %s parity, %d stop",
        CONFIG_USER_MODBUS_UART_PORT, CONFIG_USER_MODBUS_UART_TX_GPIO,
        CONFIG_USER_MODBUS_UART_RX_GPIO, CONFIG_USER_MODBUS_UART_DE_GPIO,
        CONFIG_USER_MODBUS_BAUD_RATE, parity, CONFIG_USER_MODBUS_STOP_BITS);
    ESP_LOGI(TAG, "Modbus timeout=%d ms, retries=%d, poll interval=%d ms",
        CONFIG_USER_MODBUS_RESPONSE_TIMEOUT_MS, CONFIG_USER_MODBUS_RETRIES,
        CONFIG_USER_MODBUS_POLL_INTERVAL_MS);
    ESP_LOGW(TAG, "Modbus hardware configuration only; polling is not implemented");
    if (USER_ENABLE_BACNET_MSTP) {
        ESP_LOGI(TAG, "MS/TP UART%d TX=%d RX=%d DE/RE=%d",
            CONFIG_USER_MSTP_UART_PORT, CONFIG_USER_MSTP_UART_TX_GPIO,
            CONFIG_USER_MSTP_UART_RX_GPIO, CONFIG_USER_MSTP_UART_DE_GPIO);
    }
#else
    ESP_LOGI(TAG, "Modbus RTU hardware configuration disabled");
#endif
    return ESP_OK;
}
