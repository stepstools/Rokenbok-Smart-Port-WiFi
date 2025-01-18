// ASCII Art: https://www.patorjk.com/software/taag/ ("Big" Font)
// OTA Update Code Shamelessly Stolen From: https://github.com/Jeija/esp32-softap-ota/tree/master/main

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"
#include <esp_system.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "driver/gpio.h"
#include "driver/gpio_filter.h"
#include "driver/spi_slave.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

// OTA Firmware Update URL
#define OTA_URL "https://raw.githubusercontent.com/stepstools/Rokenbok-Smart-Port-WiFi/refs/heads/main/firmware/ota_firmware.bin"

// WiFi Defines
#define AP_SSID "Smart-Port-Provisioning"
#define AP_PASSWORD "Smart-Port-Provisioning"
#define MAX_WIFI_SSID_LEN 32
#define MAX_WIFI_PASSWORD_LEN 64
#define WIFI_CONNECT_ATTEMPTS 20

// Smart Port Master Bytes
#define NULL_CMD 0x00
#define BCAST_TPADS 0xc0
#define BCAST_SELECT 0xC1
#define BCAST_END 0xC2
#define EDIT_TPADS 0xC3
#define EDIT_SELECT 0xC4
#define EDIT_END 0xC5
#define PRESYNC 0xC6
#define MASTER_SYNC 0xC7
#define READ_ATTRIB 0xC8
#define MASTER_NO_INS 0xC9
#define MASTER_ASK_INS 0xCA
#define READ_REPLY 0xCB
#define READ_NO_SEL_TIMEOUT 0xCC
#define NO_RADIO_PKT 0xCD
#define HAVE_RADIO_PKT 0xCE

// Smart Port Slave Bytes
#define NULL_CMD 0x00
#define VERIFY_EDIT 0x80
#define SLAVE_SYNC 0x81
#define SLAVE_NO_INS 0x82
#define SLAVE_WAIT_INS 0x83
#define PACKET_INJECT_ATTRIB_BYTE 0x2D // Packet Injection
#define ENABLE_ATTRIB_BYTE 0x0D // No Packet Injection
#define DISABLE_ATTRIB_BYTE 0x00
#define NO_SEL_TIMEOUT 0x00 // 1 = Controller Never Times Out, 0 = Normal V4|V3|V2|V1|P4|P3|P2|P1

// Series Logic Defines
#define NO_SERIES 0
#define SYNC_SERIES 1
#define EDIT_TPADS_SERIES 2
#define EDIT_SELECT_SERIES 3
#define PKT_INJECT_SERIES 4

// GPIO Defines
#define GPIO_RESET 0
#define GPIO_MOSI 11
#define GPIO_MISO 13
#define GPIO_SCLK 12
#define GPIO_FE 9
#define GPIO_SR 21
#define GPIO_WIFI_LED 18
#define GPIO_SP_LED 17

//  __      __     _____  _____          ____  _      ______  _____   //
//  \ \    / /\   |  __ \|_   _|   /\   |  _ \| |    |  ____|/ ____|  //
//   \ \  / /  \  | |__) | | |    /  \  | |_) | |    | |__  | (___    //
//    \ \/ / /\ \ |  _  /  | |   / /\ \ |  _ <| |    |  __|  \___ \   //
//     \  / ____ \| | \ \ _| |_ / ____ \| |_) | |____| |____ ____) |  //
//      \/_/    \_\_|  \_\_____/_/    \_\____/|______|______|_____/   //

// Global Variables
char nvs_admin_password[33] = "NULL";
char nvs_vehicle_names[15][21] = {"No Data", "No Data", "No Data", "No Data", "No Data", "No Data", "No Data", "No Data", "No Data", "No Data", "No Data", "No Data", "No Data", "No Data", "No Data"};
char control_key_user_names[256][17] = {0};

// Rokenbok Control Logic Variables
uint8_t timeouts[12] = {false, false, false, false, false, false, false, false, false, false, false, false}; // V1, V2, V3, V4, P1, P2, P3, P4, D1, D2, D3, D4
uint8_t enable_control[12] = {false, false, false, false, false, false, false, false, false, false, false, false}; // V1, V2, V3, V4, P1, P2, P3, P4, D1, D2, D3, D4 // FALSE = Normal, TRUE = SP Controlled
//uint8_t control_keys[12] = {0x19, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80, 0x00, 0x00, 0x00, 0x00}; // V1, V2, V3, V4, P1, P2, P3, P4, D1, D2, D3, D4
uint8_t control_keys[12] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}; // V1, V2, V3, V4, P1, P2, P3, P4, D1, D2, D3, D4
uint8_t next_control_key = 2; // 0 = Unused, 1 = Physical Controller Plugged In

uint8_t next_dpi_index = 0;

uint8_t selects[12] = {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F}; // V1, V2, V3, V4, P1, P2, P3, P4, D1, D2, D3, D4

uint8_t share_mode = true;
uint8_t is16sel_mode = true;
uint8_t controller_timeout = 10;

uint8_t enabled_controllers = 0b11111111; // V4|V3|V2|V1|P4|P3|P2|P1 // 0 = Enabled, 1 = Disabled
uint8_t sp_a = 0x00;                      // V4|V3|V2|V1|P4|P3|P2|P1
uint8_t sp_b = 0x00;
uint8_t sp_x = 0x00;
uint8_t sp_y = 0x00;
uint8_t sp_up = 0x00;
uint8_t sp_down = 0x00;
uint8_t sp_right = 0x00;
uint8_t sp_left = 0x00;
uint8_t sp_rt = 0x00;
// uint8_t sp_sel_button = 0x00;
// uint8_t sp_lt = 0x00;
// uint8_t sp_share = 0x00;
// uint8_t sp_is16SEL = 0x00;
uint8_t sp_priority_byte = 0x00;

uint8_t dpi_a[4] = {false, false, false, false};
uint8_t dpi_b[4] = {false, false, false, false};
uint8_t dpi_x[4] = {false, false, false, false};
uint8_t dpi_y[4] = {false, false, false, false};
uint8_t dpi_up[4] = {false, false, false, false};
uint8_t dpi_down[4] = {false, false, false, false};
uint8_t dpi_left[4] = {false, false, false, false};
uint8_t dpi_right[4] = {false, false, false, false};
uint8_t dpi_rt[4] = {false, false, false, false};

uint8_t sp_status = 0;

uint8_t spi_current_series = 0;
uint8_t spi_series_count = 0;

// Variables and Structs for WiFi Event Management
EventGroupHandle_t wifi_event_group;
uint8_t WIFI_CONNECTED_BIT = BIT0;
uint8_t connection_attempts = 0;
uint8_t wifi_led_status = false;

// Timer Handle Definitions
esp_timer_handle_t sync_timer;
esp_timer_handle_t timeout_timer;
esp_timer_handle_t post_timeout_timer;
esp_timer_handle_t reset_button_timer;
esp_timer_handle_t wifi_led_timer;

//   _    _ ______ _      _____  ______ _____    //
//  | |  | |  ____| |    |  __ \|  ____|  __ \   //
//  | |__| | |__  | |    | |__) | |__  | |__) |  //
//  |  __  |  __| | |    |  ___/|  __| |  _  /   //
//  | |  | | |____| |____| |    | |____| | \ \   //
//  |_|  |_|______|______|_|    |______|_|  \_\  //

/// @brief Converts all + to spaces and converts percent encoded values within a string.
/// @param string String to process.
void convert_percent_encoded(char *string) {
    char *src = string;
    char *dst = string;
    while (*src) {
        if (*src == '+') {
            *dst++ = ' '; // Convert '+' to space
            src++;
        } else if (src[0] == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
            int value;
            sscanf(src + 1, "%2x", &value); // Convert hex digits to a character
            *dst++ = (char)value;
            src += 3; // Move past "%XX"
        } else {
            *dst++ = *src++; // Copy regular characters
        }
    }
    *dst = '\0'; // Null-terminate the result string
}

/// @brief Helper function to determine if an array contains a value.
/// @param array The array to be checked.
/// @param size The size of the array.
/// @param value The value being looked for.
/// @return true or false boolean value.
bool contains(uint8_t array[], size_t size, uint8_t value)
{
    for (size_t i = 0; i < size; i++)
    {
        if (array[i] == value)
        {
            return true;
        }
    }
    return false;
}

//  __          ________ ____   _____ ______ _______      ________ _____    //
//  \ \        / /  ____|  _ \ / ____|  ____|  __ \ \    / /  ____|  __ \   //
//   \ \  /\  / /| |__  | |_) | (___ | |__  | |__) \ \  / /| |__  | |__) |  //
//    \ \/  \/ / |  __| |  _ < \___ \|  __| |  _  / \ \/ / |  __| |  _  /   //
//     \  /\  /  | |____| |_) |____) | |____| | \ \  \  /  | |____| | \ \   //
//      \/  \/   |______|____/|_____/|______|_|  \_\  \/   |______|_|  \_\  //

/// @brief HTTP Client Event Handler
/// @param evt Event to handle.
/// @return Error Code
esp_err_t http_client_event_handler(esp_http_client_event_t *evt) {
    // switch(evt->event_id) {
    //     case HTTP_EVENT_ERROR:
    //         ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ERROR");
    //         break;
    //     case HTTP_EVENT_ON_CONNECTED:
    //         ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_CONNECTED");
    //         break;
    //     case HTTP_EVENT_HEADER_SENT:
    //         ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_HEADER_SENT");
    //         break;
    //     case HTTP_EVENT_ON_HEADER:
    //         ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
    //         break;
    //     case HTTP_EVENT_ON_DATA:
    //         ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
    //         break;
    //     case HTTP_EVENT_ON_FINISH:
    //         ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_ON_FINISH");
    //         break;
    //     case HTTP_EVENT_DISCONNECTED:
    //         ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_DISCONNECTED");
    //         break;
    //     case HTTP_EVENT_REDIRECT:
    //         ESP_LOGI("HTTP CLIENT", "HTTP_EVENT_REDIRECT");
    //         break;
    // }
    return ESP_OK;
}

/// @brief Interprets received control codes and assigns controllers to control keys.
/// @param control_code Specified Action Code (Press A, Unpress A, etc.)
/// @param control_key Control Key For the Source Gamepad/Keyboard/Mobile
/// @return void
static uint8_t IRAM_ATTR handle_control_bytes(uint8_t control_code, uint8_t control_key, uint8_t requested_vehicle)
{
    uint8_t controller_index = 255;
    for (uint8_t i = 0; i < 12; i++) // Search For Control Key In Virtual, Physical, and Direct Packet Injection (DPI)
    {
        if (control_keys[i] == control_key) // Control Key Found
        {
            controller_index = i;
            // ESP_LOGI("SP", "CONTROL KEY %i ALREADY ASSIGNED TO CONTROLLER %i", control_key, i);
            break;
        }
    }

    if (controller_index == 255) // Control Key Not Found
    {
        for (uint8_t i = 0; i < 12; i++) // Search For Unused Virtual or Physical Controller
        {
            if (control_keys[i] == 0) // Controller Not Currently Used
            {
                controller_index = i;
                control_keys[i] = control_key;
                enable_control[i] = true;
                if (controller_index < 8) // Only Do This Step For Virtual and Physical Controllers
                {
                    enabled_controllers &= ~(0b00000001 << ((i + 4) % 8));
                }
                // ESP_LOGI("SP", "CONTROL KEY %i NOW ASSIGNED TO CONTROLLER %i", control_key, i);
                break;
            }
        }
    }

    if (controller_index == 255) // No Empty Spots Available
    {
        //ESP_LOGI("SP", "NO AVAILABLE CONTROLLERS TO ASSIGN");
        return 0xFF;
    }

    timeouts[controller_index] = true; // Indicate That Controller Was Used During Timeout Cycle

    uint8_t select_changed = false;
    if (selects[controller_index] != requested_vehicle)
    { // If the requested vehicle is different from the stored value...
        select_changed = true;
        if ((share_mode) || (requested_vehicle == 15))
        { // Just give the assignment if share mode is on or no_select is requested.
            selects[controller_index] = requested_vehicle;
        }
        else
        {
            if ((requested_vehicle > selects[controller_index]) || ((requested_vehicle == 0) && (selects[controller_index] == 15)))
            { // Detecting if the value was incremented.
                uint8_t new_vehicle = requested_vehicle;
                while (contains(selects, 8, new_vehicle))
                {
                    if (new_vehicle >= 14)
                    {
                        new_vehicle = 0;
                    }
                    else
                    {
                        new_vehicle++;
                    }
                }
                selects[controller_index] = new_vehicle;
            }
            else if (requested_vehicle < selects[controller_index])
            { // Detecting if the value was decremented.
                uint8_t new_vehicle = requested_vehicle;
                while (contains(selects, 8, new_vehicle))
                {
                    if (new_vehicle == 0)
                    {
                        new_vehicle = 14;
                    }
                    else
                    {
                        new_vehicle--;
                    }
                }
                selects[controller_index] = new_vehicle;
            }
        }
    }

    if (controller_index < 8) // Virtual or Physical Controllers
    {
        uint8_t bitwise_index = (controller_index + 4) % 8; // Convert V1-4, P1-4 Array Index to V4|V3|V2|V1|P4|P3|P2|P1 Bitwise
        switch (control_code)
        {
        // PRESS CONTROL CODES
        case 0x00:
            sp_a |= (0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x01:
            sp_b |= (0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x02:
            sp_x |= (0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x03:
            sp_y |= (0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x04:
            sp_up |= (0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x05:
            sp_down |= (0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x06:
            sp_left |= (0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x07:
            sp_right |= (0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x08:
            sp_rt |= (0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        // UNPRESS CONTROL CODES
        case 0x10:
            sp_a &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x11:
            sp_b &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x12:
            sp_x &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x13:
            sp_y &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x14:
            sp_up &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x15:
            sp_down &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x16:
            sp_left &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x17:
            sp_right &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x18:
            sp_rt &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x19: // Unpress All
            sp_a &= ~(0b00000001 << bitwise_index);
            sp_b &= ~(0b00000001 << bitwise_index);
            sp_x &= ~(0b00000001 << bitwise_index);
            sp_y &= ~(0b00000001 << bitwise_index);
            sp_up &= ~(0b00000001 << bitwise_index);
            sp_down &= ~(0b00000001 << bitwise_index);
            sp_left &= ~(0b00000001 << bitwise_index);
            sp_right &= ~(0b00000001 << bitwise_index);
            sp_rt &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
            break;

        case 0x1A: // Release Controller
            sp_a &= ~(0b00000001 << bitwise_index);
            sp_b &= ~(0b00000001 << bitwise_index);
            sp_x &= ~(0b00000001 << bitwise_index);
            sp_y &= ~(0b00000001 << bitwise_index);
            sp_up &= ~(0b00000001 << bitwise_index);
            sp_down &= ~(0b00000001 << bitwise_index);
            sp_left &= ~(0b00000001 << bitwise_index);
            sp_right &= ~(0b00000001 << bitwise_index);
            sp_rt &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);

            selects[controller_index] = 15;
            select_changed = true;

            timeouts[controller_index] = false;
            control_keys[controller_index] = 0;

            esp_timer_start_once(post_timeout_timer, 0.1 * 1000000); // 0.1 Second Interval
            break;

        // UTILITY CONTROL CODES
        // case 0x40: // Soft Reset
        //     enable_control[0] = false;
        //     enable_control[1] = false;
        //     enable_control[2] = false;
        //     enable_control[3] = false;
        //     enable_control[4] = false;
        //     enable_control[5] = false;
        //     enable_control[6] = false;
        //     enable_control[7] = false;

        //     enabled_controllers = 0xFF;

        //     selects[0] = 0x0F;
        //     selects[1] = 0x0F;
        //     selects[2] = 0x0F;
        //     selects[3] = 0x0F;
        //     selects[4] = 0x0F;
        //     selects[5] = 0x0F;
        //     selects[6] = 0x0F;
        //     selects[7] = 0x0F;

        //     sp_a = 0x00;
        //     sp_b = 0x00;
        //     sp_x = 0x00;
        //     sp_y = 0x00;
        //     sp_up = 0x00;
        //     sp_down = 0x00;
        //     sp_right = 0x00;
        //     sp_left = 0x00;
        //     sp_rt = 0x00;

        //     sp_priority_byte = 0xFF;

        //     vTaskDelay(pdMS_TO_TICKS(1000));
        //     break;

        // case 0x41: // Hard Reset the ESP
        //     esp_restart();
        //     break;
        default:
            break;
        }
    }
    else if (controller_index < 12) // Direct Packet Injection
    {
        uint8_t dpi_index = controller_index - 8;
        switch (control_code)
        {
        // PRESS CONTROL CODES
        case 0x00:
            dpi_a[dpi_index] = true;
            break;

        case 0x01:
            dpi_b[dpi_index] = true;
            break;

        case 0x02:
            dpi_x[dpi_index] = true;
            break;

        case 0x03:
            dpi_y[dpi_index] = true;
            break;

        case 0x04:
            dpi_up[dpi_index] = true;
            break;

        case 0x05:
            dpi_down[dpi_index] = true;
            break;

        case 0x06:
            dpi_left[dpi_index] = true;
            break;

        case 0x07:
            dpi_right[dpi_index] = true;
            break;

        case 0x08:
            dpi_rt[dpi_index] = true;
            break;

        // UNPRESS CONTROL CODES
        case 0x10:
            dpi_a[dpi_index] = false;
            break;

        case 0x11:
            dpi_b[dpi_index] = false;
            break;

        case 0x12:
            dpi_x[dpi_index] = false;
            break;

        case 0x13:
            dpi_y[dpi_index] = false;
            break;

        case 0x14:
            dpi_up[dpi_index] = false;
            break;

        case 0x15:
            dpi_down[dpi_index] = false;
            break;

        case 0x16:
            dpi_left[dpi_index] = false;
            break;

        case 0x17:
            dpi_right[dpi_index] = false;
            break;

        case 0x18:
            dpi_rt[dpi_index] = false;
            break;

        case 0x19: // Unpress All
            dpi_a[dpi_index] = false;
            dpi_b[dpi_index] = false;
            dpi_x[dpi_index] = false;
            dpi_y[dpi_index] = false;
            dpi_up[dpi_index] = false;
            dpi_down[dpi_index] = false;
            dpi_left[dpi_index] = false;
            dpi_right[dpi_index] = false;
            dpi_rt[dpi_index] = false;
            break;

        case 0x1A: // Release Controller
            dpi_a[dpi_index] = false;
            dpi_b[dpi_index] = false;
            dpi_x[dpi_index] = false;
            dpi_y[dpi_index] = false;
            dpi_up[dpi_index] = false;
            dpi_down[dpi_index] = false;
            dpi_left[dpi_index] = false;
            dpi_right[dpi_index] = false;
            dpi_rt[dpi_index] = false;

            selects[controller_index] = 15;
            select_changed = true;

            timeouts[controller_index] = false;
            control_keys[controller_index] = 0;

            esp_timer_start_once(post_timeout_timer, 0.1 * 1000000); // 0.1 Second Interval
            break;
        }
    }
    
    if (select_changed)
    {
        return selects[controller_index];
    }
    else
    {
        return 0xFF;
    }
}

/// @brief Handles Incoming Websocket Data.  This is the core of the internet communication protocol.
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t websocket_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET)
    {
        // ESP_LOGI("WS", "New Websocket Connection Opened");
        return ESP_OK;
    }
    httpd_ws_frame_t ws_pkt;
    uint8_t *buf = NULL;
    memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
    ws_pkt.type = HTTPD_WS_TYPE_TEXT;
    esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0); // Set max_len = 0 to get the frame length
    if (ret != ESP_OK)
    {
        ESP_LOGE("WS", "httpd_ws_recv_frame failed to get frame length with %d!", ret);
        return ret;
    }
    // ESP_LOGI("WS", "Frame length is %d", ws_pkt.len);

    if (ws_pkt.len)
    {
        buf = calloc(1, ws_pkt.len + 1); // ws_pkt.len + 1 is for NULL termination as we are expecting a string.
        if (buf == NULL)
        {
            ESP_LOGE("WS", "Failed to calloc memory for buffer!");
            return ESP_ERR_NO_MEM;
        }
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len); // Set max_len = ws_pkt.len to get the frame payload.
        if (ret != ESP_OK)
        {
            ESP_LOGE("WS", "httpd_ws_recv_frame failed to get frame payload with %d!", ret);
            free(buf);
            return ret;
        }
        // ESP_LOGI("WS", "Got packet with message: %s", ws_pkt.payload);
        // ESP_LOGI("WS", "Packet type: %d", ws_pkt.type);
    }

    if (ws_pkt.type == HTTPD_WS_TYPE_BINARY)
    {
        if (ws_pkt.payload[0] < 0x42 && ws_pkt.len > 2) // Pass Control Codes Over to handle_control_bytes
        {
            uint8_t ret = handle_control_bytes(ws_pkt.payload[0], ws_pkt.payload[1], ws_pkt.payload[2]);
            if (ret != 0xFF)
            { // A non 0xFF value is a new select value.
                static uint8_t send_data[3];
                send_data[0] = 0x20;
                send_data[1] = ws_pkt.payload[1];
                send_data[2] = ret;
                httpd_ws_frame_t send_pkt;
                memset(&send_pkt, 0, sizeof(httpd_ws_frame_t));
                send_pkt.len = 3;
                send_pkt.type = HTTPD_WS_TYPE_BINARY;
                send_pkt.payload = send_data;
                httpd_ws_send_frame(req, &send_pkt);
            }
        }
        else if (ws_pkt.payload[0] == 0xC0) // Request For Status Data
        {
            static uint8_t send_data[26];
            send_data[0] = 0xC0;
            send_data[1] = sp_status;
            send_data[2] = selects[0];
            send_data[3] = selects[1];
            send_data[4] = selects[2];
            send_data[5] = selects[3];
            send_data[6] = selects[4];
            send_data[7] = selects[5];
            send_data[8] = selects[6];
            send_data[9] = selects[7];
            send_data[10] = selects[8];
            send_data[11] = selects[9];
            send_data[12] = selects[10];
            send_data[13] = selects[11];
            send_data[14] = control_keys[0];
            send_data[15] = control_keys[1];
            send_data[16] = control_keys[2];
            send_data[17] = control_keys[3];
            send_data[18] = control_keys[4];
            send_data[19] = control_keys[5];
            send_data[20] = control_keys[6];
            send_data[21] = control_keys[7];
            send_data[22] = control_keys[8];
            send_data[23] = control_keys[9];
            send_data[24] = control_keys[10];
            send_data[25] = control_keys[11];
            httpd_ws_frame_t send_pkt;
            memset(&send_pkt, 0, sizeof(httpd_ws_frame_t));
            send_pkt.len = 26;
            send_pkt.type = HTTPD_WS_TYPE_BINARY;
            send_pkt.payload = send_data;
            httpd_ws_send_frame(req, &send_pkt);
        }
        else if (ws_pkt.payload[0] == 0xC1 && ws_pkt.len > 1) // Request For Control Key
        {
            // ESP_LOGI("WS", "RECEIVED REQUEST FOR NEW CONTROL KEY, ASSIGNING %i", next_control_key);
            static uint8_t send_data[3];
            send_data[0] = 0xC1;
            send_data[1] = ws_pkt.payload[1];
            send_data[2] = next_control_key;
            httpd_ws_frame_t send_pkt;
            memset(&send_pkt, 0, sizeof(httpd_ws_frame_t));
            send_pkt.len = 3;
            send_pkt.type = HTTPD_WS_TYPE_BINARY;
            send_pkt.payload = send_data;
            httpd_ws_send_frame(req, &send_pkt);

            snprintf(control_key_user_names[next_control_key], 17, "ID #%i", next_control_key);

            if (next_control_key == 255) // Handle the rollover from 255 to 1. 0 is used to indicate no control key.
            {
                next_control_key = 2;
            }
            else
            {
                next_control_key++;
            }
        }
        else if (ws_pkt.payload[0] == 0xC2) // Request For Vehicle Names
        {
            // 1 For "0xC2" plus 16 delimiters.
            uint16_t total_length = 1 + strlen(nvs_vehicle_names[0]) + strlen(nvs_vehicle_names[1]) + strlen(nvs_vehicle_names[2]) +
                                    strlen(nvs_vehicle_names[3]) + strlen(nvs_vehicle_names[4]) + strlen(nvs_vehicle_names[5]) +
                                    strlen(nvs_vehicle_names[6]) + strlen(nvs_vehicle_names[7]) + strlen(nvs_vehicle_names[8]) +
                                    strlen(nvs_vehicle_names[9]) + strlen(nvs_vehicle_names[10]) + strlen(nvs_vehicle_names[11]) +
                                    strlen(nvs_vehicle_names[12]) + strlen(nvs_vehicle_names[13]) + strlen(nvs_vehicle_names[14]) + 16;

            char combined_string[total_length + 1];

            snprintf(combined_string, sizeof(combined_string), "X=%s=%s=%s=%s=%s=%s=%s=%s=%s=%s=%s=%s=%s=%s=%s=",
                     nvs_vehicle_names[0], nvs_vehicle_names[1], nvs_vehicle_names[2], nvs_vehicle_names[3], nvs_vehicle_names[4], nvs_vehicle_names[5], nvs_vehicle_names[6], nvs_vehicle_names[7],
                     nvs_vehicle_names[8], nvs_vehicle_names[9], nvs_vehicle_names[10], nvs_vehicle_names[11], nvs_vehicle_names[12], nvs_vehicle_names[13], nvs_vehicle_names[14]);
            combined_string[0] = 0xC2;

            httpd_ws_frame_t send_pkt;
            memset(&send_pkt, 0, sizeof(httpd_ws_frame_t));
            send_pkt.len = sizeof(combined_string);
            send_pkt.type = HTTPD_WS_TYPE_BINARY;
            send_pkt.payload = (uint8_t *)combined_string;
            httpd_ws_send_frame(req, &send_pkt);
        }
        else if (ws_pkt.payload[0] == 0xC3  && ws_pkt.len > 1) // Received User Name
        {
            if (ws_pkt.payload[1] > 1)
            {
                char received_string[17];
                strncpy(received_string, (char *)ws_pkt.payload + 2, ws_pkt.len - 2);
                received_string[ws_pkt.len - 2] = '\0';

                snprintf(control_key_user_names[ws_pkt.payload[1]], 17, received_string);
                // ESP_LOGI("TEST", "Username: %s", control_key_user_names[ws_pkt.payload[1]]);
            }
        }
        else if (ws_pkt.payload[0] == 0xC4) // Request For User Names
        {
            // 1 For "0xC4" plus 13 delimiters.
            uint16_t total_length = 1 + strlen(control_key_user_names[control_keys[0]]) + strlen(control_key_user_names[control_keys[1]]) +
                                    strlen(control_key_user_names[control_keys[2]]) + strlen(control_key_user_names[control_keys[3]]) +
                                    strlen(control_key_user_names[control_keys[4]]) + strlen(control_key_user_names[control_keys[5]]) +
                                    strlen(control_key_user_names[control_keys[6]]) + strlen(control_key_user_names[control_keys[7]]) +
                                    strlen(control_key_user_names[control_keys[8]]) + strlen(control_key_user_names[control_keys[9]]) +
                                    strlen(control_key_user_names[control_keys[10]]) + strlen(control_key_user_names[control_keys[11]]) + 13;

            char combined_string[total_length + 1];

            snprintf(combined_string, sizeof(combined_string), "X=%s=%s=%s=%s=%s=%s=%s=%s=%s=%s=%s=%s=",
                    control_key_user_names[control_keys[0]], control_key_user_names[control_keys[1]], control_key_user_names[control_keys[2]],
                    control_key_user_names[control_keys[3]], control_key_user_names[control_keys[4]], control_key_user_names[control_keys[5]],
                    control_key_user_names[control_keys[6]], control_key_user_names[control_keys[7]], control_key_user_names[control_keys[8]],
                    control_key_user_names[control_keys[9]], control_key_user_names[control_keys[10]], control_key_user_names[control_keys[11]]);
            combined_string[0] = 0xC4;

            httpd_ws_frame_t send_pkt;
            memset(&send_pkt, 0, sizeof(httpd_ws_frame_t));
            send_pkt.len = sizeof(combined_string);
            send_pkt.type = HTTPD_WS_TYPE_BINARY;
            send_pkt.payload = (uint8_t *)combined_string;
            httpd_ws_send_frame(req, &send_pkt);
        }
        else if (ws_pkt.payload[0] == 0xC5) // Request For Admin Page Data
        {
            static uint8_t send_data[4];
            send_data[0] = 0xC5;
            send_data[1] = share_mode;
            send_data[2] = is16sel_mode;
            send_data[3] = controller_timeout;
            httpd_ws_frame_t send_pkt;
            memset(&send_pkt, 0, sizeof(httpd_ws_frame_t));
            send_pkt.len = 4;
            send_pkt.type = HTTPD_WS_TYPE_BINARY;
            send_pkt.payload = send_data;
            httpd_ws_send_frame(req, &send_pkt);
        }
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_CLOSE)
    {
        // ESP_LOGI("WS", "Websocket Closed");
    }
    else if (ws_pkt.type == HTTPD_WS_TYPE_TEXT)
    {
        ESP_LOGI("WS", "Received Text: %s", ws_pkt.payload);
        // httpd_ws_send_frame(req, &ws_pkt); // Echo Text Back
    }
    free(buf);
    return ESP_OK;
}

static const httpd_uri_t websocket_uri = {
    .uri = "/ws",
    .method = HTTP_GET,
    .handler = websocket_handler,
    .user_ctx = NULL,
    .is_websocket = true,
    .handle_ws_control_frames = true};

/// @brief HTML Handler for Favicon Data
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t favicon_handler(httpd_req_t *req)
{
    extern const unsigned char favicon_ico_start[] asm("_binary_favicon_ico_start");
    extern const unsigned char favicon_ico_end[] asm("_binary_favicon_ico_end");
    const size_t favicon_ico_size = (favicon_ico_end - favicon_ico_start);
    httpd_resp_set_type(req, "image/x-icon");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, (const char *)favicon_ico_start, favicon_ico_size);
    return ESP_OK;
}

static const httpd_uri_t favicon_uri = {
    .uri = "/favicon.ico",
    .method = HTTP_GET,
    .handler = favicon_handler,
    .user_ctx = NULL};

/// @brief HTML Handler for Bank Gothic Bold Font Data
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t font_handler(httpd_req_t *req)
{
    extern const unsigned char bankgothicbold_ttf_start[] asm("_binary_bankgothicbold_ttf_start");
    extern const unsigned char bankgothicbold_ttf_end[] asm("_binary_bankgothicbold_ttf_end");
    const size_t bankgothicbold_ttf_size = (bankgothicbold_ttf_end - bankgothicbold_ttf_start);
    httpd_resp_set_type(req, "font/ttf");
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, (const char *)bankgothicbold_ttf_start, bankgothicbold_ttf_size);
    return ESP_OK;
}

static const httpd_uri_t font_uri = {
    .uri = "/bankgothicbold.ttf",
    .method = HTTP_GET,
    .handler = font_handler,
    .user_ctx = NULL};

/// @brief HTML Handler for Main Index Page
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t index_handler(httpd_req_t *req)
{
    extern const unsigned char index_html_start[] asm("_binary_index_html_start");
    extern const unsigned char index_html_end[] asm("_binary_index_html_end");
    const size_t index_html_size = (index_html_end - index_html_start);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, (const char *)index_html_start, index_html_size);
    return ESP_OK;
}

static const httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL};

/// @brief HTML Handler for Mobile Page
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t mobile_handler(httpd_req_t *req)
{
    extern const unsigned char mobile_html_start[] asm("_binary_mobile_html_start");
    extern const unsigned char mobile_html_end[] asm("_binary_mobile_html_end");
    const size_t mobile_html_size = (mobile_html_end - mobile_html_start);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, (const char *)mobile_html_start, mobile_html_size);
    return ESP_OK;
}

static const httpd_uri_t mobile_uri = {
    .uri = "/mobile",
    .method = HTTP_GET,
    .handler = mobile_handler,
    .user_ctx = NULL};

/// @brief HTML Handler for Help Page
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t help_handler(httpd_req_t *req)
{
    extern const unsigned char help_html_start[] asm("_binary_help_html_start");
    extern const unsigned char help_html_end[] asm("_binary_help_html_end");
    const size_t help_html_size = (help_html_end - help_html_start);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, (const char *)help_html_start, help_html_size);
    return ESP_OK;
}

static const httpd_uri_t help_uri = {
    .uri = "/help",
    .method = HTTP_GET,
    .handler = help_handler,
    .user_ctx = NULL};

/// @brief HTML Handler for Admin Page
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t admin_handler(httpd_req_t *req)
{
    extern const unsigned char admin_html_start[] asm("_binary_admin_html_start");
    extern const unsigned char admin_html_end[] asm("_binary_admin_html_end");
    const size_t admin_html_size = (admin_html_end - admin_html_start);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, (const char *)admin_html_start, admin_html_size);
    return ESP_OK;
}

static const httpd_uri_t admin_uri = {
    .uri = "/admin",
    .method = HTTP_GET,
    .handler = admin_handler,
    .user_ctx = NULL};

/// @brief HTML Handler for the Admin Page Form
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t admin_form_handler(httpd_req_t *req)
{
    char buf[1088];
    int ret, remaining = req->content_len;

    while (remaining > 0)
    {
        ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                httpd_resp_send_500(req);
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }

    buf[req->content_len] = '\0';
    //ESP_LOGI("HTML POST", "Received form data: |%s|", buf);

    char rec_admin_password[96 + 1];
    char rec_share_mode[6];
    char rec_is16sel_mode[6];
    char rec_timeout[4];
    char rec_names[15][64 + 1];

    // Tokenize the Input String
    char *token = strtok(buf, "&=");
    while (token != NULL)
    {
        if (strcmp(token, "pw") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_admin_password, token, sizeof(rec_admin_password) - 1);
                rec_admin_password[sizeof(rec_admin_password) - 1] = '\0';
            }
        }
        else if (strcmp(token, "share") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_share_mode, token, sizeof(rec_share_mode) - 1);
                rec_share_mode[sizeof(rec_share_mode) - 1] = '\0';
            }
        }
        else if (strcmp(token, "16sel") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_is16sel_mode, token, sizeof(rec_is16sel_mode) - 1);
                rec_is16sel_mode[sizeof(rec_is16sel_mode) - 1] = '\0';
            }
        }
        else if (strcmp(token, "timeout") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_timeout, token, sizeof(rec_timeout) - 1);
                rec_timeout[sizeof(rec_timeout) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh1") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[0], token, sizeof(rec_names[0]) - 1);
                rec_names[0][sizeof(rec_names[0]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh2") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[1], token, sizeof(rec_names[1]) - 1);
                rec_names[1][sizeof(rec_names[1]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh3") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[2], token, sizeof(rec_names[2]) - 1);
                rec_names[2][sizeof(rec_names[2]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh4") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[3], token, sizeof(rec_names[3]) - 1);
                rec_names[3][sizeof(rec_names[3]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh5") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[4], token, sizeof(rec_names[4]) - 1);
                rec_names[4][sizeof(rec_names[4]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh6") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[5], token, sizeof(rec_names[5]) - 1);
                rec_names[5][sizeof(rec_names[5]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh7") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[6], token, sizeof(rec_names[6]) - 1);
                rec_names[6][sizeof(rec_names[6]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh8") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[7], token, sizeof(rec_names[7]) - 1);
                rec_names[7][sizeof(rec_names[7]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh9") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[8], token, sizeof(rec_names[8]) - 1);
                rec_names[8][sizeof(rec_names[8]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh10") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[9], token, sizeof(rec_names[9]) - 1);
                rec_names[9][sizeof(rec_names[9]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh11") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[10], token, sizeof(rec_names[10]) - 1);
                rec_names[10][sizeof(rec_names[10]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh12") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[11], token, sizeof(rec_names[11]) - 1);
                rec_names[11][sizeof(rec_names[11]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh13") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[12], token, sizeof(rec_names[12]) - 1);
                rec_names[12][sizeof(rec_names[12]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh14") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[13], token, sizeof(rec_names[13]) - 1);
                rec_names[13][sizeof(rec_names[13]) - 1] = '\0';
            }
        }
        else if (strcmp(token, "veh15") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_names[14], token, sizeof(rec_names[14]) - 1);
                rec_names[14][sizeof(rec_names[14]) - 1] = '\0';
            }
        }
        token = strtok(NULL, "&=");
    }

    convert_percent_encoded(rec_admin_password);
    for (uint8_t i = 0; i < 15; i++) {
        convert_percent_encoded(rec_names[i]);
    }

    if (strcmp(rec_admin_password, nvs_admin_password) == 0)
    {
        // Store the entered vehicle names to NVS.
        nvs_handle_t nvs_handle;
        esp_err_t nvs_ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (nvs_ret == ESP_OK)
        {
            if (strcmp(rec_share_mode, "true") == 0)
            {
                nvs_set_u8(nvs_handle, "share_mode", true);
                share_mode = true;
            }
            else
            {
                nvs_set_u8(nvs_handle, "share_mode", false);
                share_mode = false;
            }

            if (strcmp(rec_is16sel_mode, "true") == 0)
            {
                nvs_set_u8(nvs_handle, "16sel_mode", true);
                is16sel_mode = true;
            }
            else
            {
                nvs_set_u8(nvs_handle, "16sel_mode", false);
                is16sel_mode = false;
            }

            controller_timeout = atoi(rec_timeout);
            // Restart Timeout Timer With New Value
            esp_timer_stop(timeout_timer);
            esp_timer_start_periodic(timeout_timer, controller_timeout * 1000000); // controller_timeout Second Interval
            nvs_set_u8(nvs_handle, "cont_timeout", controller_timeout);

            nvs_set_str(nvs_handle, "v1_name", rec_names[0]);
            nvs_set_str(nvs_handle, "v2_name", rec_names[1]);
            nvs_set_str(nvs_handle, "v3_name", rec_names[2]);
            nvs_set_str(nvs_handle, "v4_name", rec_names[3]);
            nvs_set_str(nvs_handle, "v5_name", rec_names[4]);
            nvs_set_str(nvs_handle, "v6_name", rec_names[5]);
            nvs_set_str(nvs_handle, "v7_name", rec_names[6]);
            nvs_set_str(nvs_handle, "v8_name", rec_names[7]);
            nvs_set_str(nvs_handle, "v9_name", rec_names[8]);
            nvs_set_str(nvs_handle, "v10_name", rec_names[9]);
            nvs_set_str(nvs_handle, "v11_name", rec_names[10]);
            nvs_set_str(nvs_handle, "v12_name", rec_names[11]);
            nvs_set_str(nvs_handle, "v13_name", rec_names[12]);
            nvs_set_str(nvs_handle, "v14_name", rec_names[13]);
            nvs_set_str(nvs_handle, "v15_name", rec_names[14]);

            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);

            strcpy(nvs_vehicle_names[0], rec_names[0]);
            strcpy(nvs_vehicle_names[1], rec_names[1]);
            strcpy(nvs_vehicle_names[2], rec_names[2]);
            strcpy(nvs_vehicle_names[3], rec_names[3]);
            strcpy(nvs_vehicle_names[4], rec_names[4]);
            strcpy(nvs_vehicle_names[5], rec_names[5]);
            strcpy(nvs_vehicle_names[6], rec_names[6]);
            strcpy(nvs_vehicle_names[7], rec_names[7]);
            strcpy(nvs_vehicle_names[8], rec_names[8]);
            strcpy(nvs_vehicle_names[9], rec_names[9]);
            strcpy(nvs_vehicle_names[10], rec_names[10]);
            strcpy(nvs_vehicle_names[11], rec_names[11]);
            strcpy(nvs_vehicle_names[12], rec_names[12]);
            strcpy(nvs_vehicle_names[13], rec_names[13]);
            strcpy(nvs_vehicle_names[14], rec_names[14]);

            const char response[] = "Entered admin password was correct and updated settings were stored!\n"
                                    "Click <a href='/'>here</a> to return to the main page.";
            httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
        }
        else
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal Server Error");
        }
    }
    else
    {
        const char response[] = "Entered admin password was not correct!\n"
                                "Click <a href='admin'>here</a> to return to the admin page.";
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

static httpd_uri_t admin_form_uri = {
    .uri = "/updatesettings",
    .method = HTTP_POST,
    .handler = admin_form_handler,
    .user_ctx = NULL};

/// @brief HTML Handler for the Admin Page OTA Update Button
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t ota_firmware_update_handler(httpd_req_t *req)
{
    char buf[128]; // TODO: Could buffer size be reduced?
    int ret, remaining = req->content_len;

    while (remaining > 0)
    {
        ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                httpd_resp_send_500(req);
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }

    buf[req->content_len] = '\0';
    //ESP_LOGI("HTML POST", "Received form data: |%s|", buf);

    char rec_admin_password[96 + 1];

    // Tokenize the Input String
    char *token = strtok(buf, "&=");
    while (token != NULL)
    {
        if (strcmp(token, "pw") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_admin_password, token, sizeof(rec_admin_password) - 1);
                rec_admin_password[sizeof(rec_admin_password) - 1] = '\0';
            }
        }
        token = strtok(NULL, "&=");
    }

    convert_percent_encoded(rec_admin_password);

    if (strcmp(rec_admin_password, nvs_admin_password) == 0)
    {
        esp_err_t err;
        esp_ota_handle_t update_handle = 0 ;
        const esp_partition_t *update_partition = NULL;

        ESP_LOGI("OTA", "Starting OTA Update");

        esp_http_client_config_t config = {
            .url = OTA_URL,
            .event_handler = http_client_event_handler,
            .timeout_ms = 10000,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            ESP_LOGE("HTTP", "Failed to open HTTP connection: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }

        esp_http_client_fetch_headers(client);

        update_partition = esp_ota_get_next_update_partition(NULL);

        err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
        if (err != ESP_OK) {
            ESP_LOGE("OTA", "esp_ota_begin failed, error=%s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }

        int binary_file_len = 0;
        int data_read;
        char ota_write_data[512];
        while ((data_read = esp_http_client_read(client, ota_write_data, sizeof(ota_write_data))) > 0) {
            err = esp_ota_write(update_handle, (const void *)ota_write_data, data_read);
            if (err != ESP_OK) {
                ESP_LOGE("OTA", "Error: esp_ota_write failed! err=0x%x", err);
                esp_http_client_cleanup(client);
                esp_ota_end(update_handle);
                return err;
            }
            binary_file_len += data_read;
            //ESP_LOGI("OTA", "Written image length %d", binary_file_len);
        }

        if (data_read < 0) {
            ESP_LOGE("HTTP", "Error: SSL data read error");
            esp_http_client_cleanup(client);
            esp_ota_end(update_handle);
            return err;
        }

        ESP_LOGI("OTA", "Total Write binary data length: %d", binary_file_len);

        if (esp_ota_end(update_handle) != ESP_OK) {
            ESP_LOGE("OTA", "Error: esp_ota_end failed! err=0x%x", err);
            return err;
        }

        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE("OTA", "esp_ota_set_boot_partition failed! err=0x%x", err);
            return err;
        }

        const char response[] = "The firmware update has completed and the system will now reboot.\n"
                                "Click <a href='admin'>here</a> to return to the admin page.";
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
        vTaskDelay(1000 / portTICK_PERIOD_MS);

        ESP_LOGI("OTA", "OTA update completed. Rebooting...");
        esp_restart();
    }
    else
    {
        const char response[] = "Entered admin password was not correct!\n"
                                "Click <a href='admin'>here</a> to return to the admin page.";
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

static httpd_uri_t ota_firmware_update_uri = {
    .uri = "/otafirmwareupdate",
    .method = HTTP_POST,
    .handler = ota_firmware_update_handler,
    .user_ctx = NULL};

/// @brief HTML Handler for Initialization Index Page
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t init_index_handler(httpd_req_t *req)
{
    extern const unsigned char initindex_html_start[] asm("_binary_initindex_html_start");
    extern const unsigned char initindex_html_end[] asm("_binary_initindex_html_end");
    const size_t initindex_html_size = (initindex_html_end - initindex_html_start);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, (const char *)initindex_html_start, initindex_html_size);
    return ESP_OK;
}

static httpd_uri_t init_index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = init_index_handler,
    .user_ctx = NULL};

/// @brief HTML Handler for the Initialization Index Form
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t init_form_handler(httpd_req_t *req)
{
    char buf[512]; // TODO: Could buffer size be reduced?
    int ret, remaining = req->content_len;

    while (remaining > 0)
    {
        ret = httpd_req_recv(req, buf, MIN(remaining, sizeof(buf)));
        if (ret <= 0)
        {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT)
            {
                httpd_resp_send_500(req);
            }
            return ESP_FAIL;
        }
        remaining -= ret;
    }

    buf[req->content_len] = '\0';
    //ESP_LOGI("HTML POST", "Received form data: |%s|", buf);

    char rec_admin_password[96 + 1];
    char rec_ssid[MAX_WIFI_SSID_LEN*3 + 1]; // Times 3 allows for percent encoding.
    char rec_password[MAX_WIFI_PASSWORD_LEN*3 + 1]; // Times 3 allows for percent encoding.
    uint8_t rec_gateway[4] = {0}; // Array to store each part of the Gateway IP
    uint8_t rec_static[4] = {0};  // Array to store each part of the Static IP
    uint8_t rec_netmask[4] = {0}; // Array to store each part of the Net Mask IP

    rec_admin_password[0] = '\0';
    rec_ssid[0] = '\0';
    rec_password[0] = '\0';

    // Tokenize the Input String
    char *token = strtok(buf, "&=");
    while (token != NULL)
    {
        if (strcmp(token, "adminpassword") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_admin_password, token, sizeof(rec_admin_password) - 1);
                rec_admin_password[sizeof(rec_admin_password) - 1] = '\0';
            }
        }
        else if (strcmp(token, "ssid") == 0)
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_ssid, token, sizeof(rec_ssid) - 1);
                rec_ssid[sizeof(rec_ssid) - 1] = '\0';
            }
        }
        else if (strcmp(token, "password") == 0) // Handle the Password
        {
            token = strtok(NULL, "&=");
            if (token != NULL)
            {
                strncpy(rec_password, token, sizeof(rec_password) - 1);
                rec_password[sizeof(rec_password) - 1] = '\0';
            }
        }
        else if (strncmp(token, "gw", 2) == 0) // Handle the Gateway IP Address Parts
        {
            int part = atoi(token + 2);
            if (part >= 1 && part <= 4)
            {
                token = strtok(NULL, "&=");
                if (token != NULL)
                {
                    rec_gateway[part - 1] = atoi(token);
                }
            }
        }
        else if (strncmp(token, "sip", 3) == 0) // Handle the Static IP Address Parts
        {
            int part = atoi(token + 3);
            if (part >= 1 && part <= 4)
            {
                token = strtok(NULL, "&=");
                if (token != NULL)
                {
                    rec_static[part - 1] = atoi(token);
                }
            }
        }
        else if (strncmp(token, "nm", 2) == 0) // Handle the Net Mask IP Address Parts
        {
            int part = atoi(token + 2);
            if (part >= 1 && part <= 4)
            {
                token = strtok(NULL, "&=");
                if (token != NULL)
                {
                    rec_netmask[part - 1] = atoi(token);
                }
            }
        }
        token = strtok(NULL, "&=");
    }

    convert_percent_encoded(rec_admin_password);
    convert_percent_encoded(rec_ssid);
    convert_percent_encoded(rec_password);

    if (rec_ssid[0] != '\0' && rec_password[0] != '\0' && rec_admin_password[0] != '\0')
    {
        // Store the entered credentials and IP addresses to NVS
        nvs_handle_t nvs_handle;
        esp_err_t nvs_ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
        if (nvs_ret == ESP_OK)
        {
            nvs_set_str(nvs_handle, "admin_password", rec_admin_password);
            nvs_set_str(nvs_handle, "wifi_ssid", rec_ssid);
            nvs_set_str(nvs_handle, "wifi_password", rec_password);
            nvs_set_blob(nvs_handle, "gateway_ip", rec_gateway, sizeof(rec_gateway));
            nvs_set_blob(nvs_handle, "static_ip", rec_static, sizeof(rec_static));
            nvs_set_blob(nvs_handle, "netmask_ip", rec_netmask, sizeof(rec_netmask));
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);

            // Send a response back to the client
            const char response[] = "WiFi configuration received. The system will now reboot and connect!\n"
                                    "Saved credentials and IP addresses will be cleared if the system cannot connect within 30 seconds.";
            httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
        }
        else
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Internal Server Error");
        }
    }
    else
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Bad Request");
    }
    return ESP_OK;
}

static httpd_uri_t init_form_uri = {
    .uri = "/submit",
    .method = HTTP_POST,
    .handler = init_form_handler,
    .user_ctx = NULL};

/// @brief HTML Handler for Initialization Update Page
/// @param req http Request
/// @return ESP Error Returns
static esp_err_t init_update_handler(httpd_req_t *req)
{
    extern const unsigned char initupdate_html_start[] asm("_binary_initupdate_html_start");
    extern const unsigned char initupdate_html_end[] asm("_binary_initupdate_html_end");
    const size_t initupdate_html_size = (initupdate_html_end - initupdate_html_start);
    httpd_resp_set_hdr(req, "Connection", "close");
    httpd_resp_send(req, (const char *)initupdate_html_start, initupdate_html_size);
    return ESP_OK;
}

static httpd_uri_t init_update_uri = {
    .uri = "/update",
    .method = HTTP_GET,
    .handler = init_update_handler,
    .user_ctx = NULL};

/// @brief HTML Handler for Flash POST
/// @param req http Request
/// @return ESP Error Returns
esp_err_t update_post_handler(httpd_req_t *req)
{
    char buffer[512];
    esp_ota_handle_t ota_handle;
    int remaining = req->content_len;

    const esp_partition_t *ota_partition = esp_ota_get_next_update_partition(NULL);
    ESP_ERROR_CHECK(esp_ota_begin(ota_partition, OTA_SIZE_UNKNOWN, &ota_handle));

    while (remaining > 0)
    {
        int recv_len = httpd_req_recv(req, buffer, MIN(remaining, sizeof(buffer)));

        // Timeout Error: Just retry
        if (recv_len == HTTPD_SOCK_ERR_TIMEOUT)
        {
            continue;

        // Serious Error: Abort OTA
        }
        else if (recv_len <= 0)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Protocol Error");
            return ESP_FAIL;
        }

        // Successful Upload: Flash firmware chunk
        if (esp_ota_write(ota_handle, (const void *)buffer, recv_len) != ESP_OK)
        {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash Error");
            return ESP_FAIL;
        }

        remaining -= recv_len;
    }

    // Validate and switch to new OTA image and reboot
    if (esp_ota_end(ota_handle) != ESP_OK || esp_ota_set_boot_partition(ota_partition) != ESP_OK)
    {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Validation / Activation Error");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "Firmware update complete, rebooting now!\n");

    vTaskDelay(500 / portTICK_PERIOD_MS);
    esp_restart();

    return ESP_OK;
}

httpd_uri_t update_post_uri = {
    .uri = "/flash",
    .method = HTTP_POST,
    .handler = update_post_handler,
    .user_ctx = NULL};

/// @brief Start and Configure the HTTP Webserver
/// @param page_version 0 = Normal, 1 = Initialization
/// @return HTTP Server Handle
static httpd_handle_t start_http_webserver(uint8_t page_version)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_open_sockets = 13;
    config.max_uri_handlers = 16;
    config.stack_size = 8192;

    // Start the httpd server
    //ESP_LOGI("WEBSERVER", "Starting server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK)
    {
        if (page_version == 0)
        { // Normal Pages
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &index_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &mobile_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &help_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &admin_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &admin_form_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ota_firmware_update_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &websocket_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &favicon_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &font_uri));
        }
        else if (page_version == 1)
        { // Initialization Pages
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &init_index_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &init_form_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &init_update_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &update_post_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &favicon_uri));
            ESP_ERROR_CHECK(httpd_register_uri_handler(server, &font_uri));
        }
        return server;
    }
    ESP_LOGE("WEBSERVER", "Error starting server!");
    return NULL;
}

//    _____ _____ _____   //
//   / ____|  __ \_   _|  //
//  | (___ | |__) || |    //
//   \___ \|  ___/ | |    //
//   ____) | |    _| |_   //
//  |_____/|_|   |_____|  //

/// @brief Called after a transaction is queued and ready for the next frame.
/// @param trans SPI Transaction (Not Used)
/// @return void
static void IRAM_ATTR post_setup_callback(spi_slave_transaction_t *trans)
{
    gpio_set_level(GPIO_SR, 0); // Sets Slave Ready low.
}

/// @brief Called after transaction is completed.
/// @param trans SPI Transaction (Not Used)
/// @return void
static void IRAM_ATTR post_transaction_callback(spi_slave_transaction_t *trans)
{
    gpio_set_level(GPIO_SR, 1); // Sets Slave Ready high.
}

/// @brief Process the received SPI byte and prepare the next output.  This is the core of the Smart Port communications.
/// @param arg xTask Arguments (Not Used)
/// @return void
static void IRAM_ATTR spi_task(void *arg)
{
    spi_slave_transaction_t t;
    memset(&t, 0, sizeof(t));

    uint8_t send_byte = 0x00;
    uint8_t recv_byte = 0x00;

    t.length = 8; // 8 Bits of Data Per Frame
    t.tx_buffer = &send_byte;
    t.rx_buffer = &recv_byte;

    while (true)
    {
        recv_byte = 0x00;

        spi_slave_transmit(SPI2_HOST, &t, portMAX_DELAY);

        // NO SERIES
        if (spi_current_series == NO_SERIES)
        {
            if (recv_byte == PRESYNC)
            {
                spi_current_series = SYNC_SERIES;
                spi_series_count = 1;
                send_byte = SLAVE_SYNC;
            }
            else if (recv_byte == EDIT_TPADS)
            {
                spi_current_series = EDIT_TPADS_SERIES;
                spi_series_count = 1;
                send_byte = VERIFY_EDIT;

                // RESET SYNC TIMER
                sp_status = 1;
                gpio_set_level(GPIO_SP_LED, 1);
                esp_timer_restart(sync_timer, 0.1 * 1000000); // 0.1 Second Interval
            }
            else if (recv_byte == EDIT_SELECT)
            {
                spi_current_series = EDIT_SELECT_SERIES;
                spi_series_count = 1;
                send_byte = VERIFY_EDIT;
            }
            else if (recv_byte == MASTER_ASK_INS)
            {
                spi_current_series = PKT_INJECT_SERIES;
                spi_series_count = 1;

                // Generate First Half of DPI Packet (SEL3, SEL2, SEL1, SEL0, UP, DOWN, RIGHT, LEFT)
                uint8_t original_index = next_dpi_index;
                uint8_t valid_found = false;
                do {
                    next_dpi_index = (next_dpi_index + 1) % 4;

                    // A DPI should only be broadcast if it has a selection and isn't the same as another V/P/D controller selection.
                    if (selects[next_dpi_index + 8] != 0x0F && !contains(selects, 8, selects[next_dpi_index + 8])) {
                        valid_found = true;
                        break;  // Found the valid next index
                    }

                } while (next_dpi_index != original_index);  // Stop if we've wrapped around back to the original index

                if (valid_found) { // Only generate a send_byte if a valid controller was found
                    send_byte = (selects[next_dpi_index + 8] + 1) << 4;
                    if (dpi_up[next_dpi_index]) {
                        send_byte |= 0b00001000;
                    }
                    if (dpi_down[next_dpi_index]) {
                        send_byte |= 0b00000100;
                    }
                    if (dpi_right[next_dpi_index]) {
                        send_byte |= 0b00000010;
                    }
                    if (dpi_left[next_dpi_index]) {
                        send_byte |= 0b00000001;
                    }
                } else {
                    send_byte = 0b00000000; // Send NULL Upper Half
                }
            }
            else
            {
                spi_current_series = NO_SERIES;
                spi_series_count = 0;
                send_byte = NULL_CMD;
            }
        }
        // SYNC SERIES
        else if (spi_current_series == SYNC_SERIES)
        {
            if (spi_series_count == 1)
            {
                spi_series_count = 2;
                send_byte = ENABLE_ATTRIB_BYTE;
                for (uint8_t i = 8; i < 12; i++) { // Only enable DPI if needed.
                    if (selects[i] != 0x0F) {
                        send_byte = PACKET_INJECT_ATTRIB_BYTE;
                        break;
                    }
                }
            }
            else if (spi_series_count == 2)
            {
                spi_series_count = 3;
                send_byte = NO_SEL_TIMEOUT;
            }
            else if (spi_series_count == 3)
            {
                spi_current_series = NO_SERIES;
                spi_series_count = 0;
                send_byte = NULL_CMD;
            }
            else
            {
                spi_current_series = NO_SERIES;
                spi_series_count = 0;
                send_byte = NULL_CMD;
            }
        }
        // EDIT TPADS SERIES
        else if (spi_current_series == EDIT_TPADS_SERIES)
        {
            if (spi_series_count == 1)
            {
                spi_series_count = 2; // Select Button
                // spi_rec_tpads[0] = recv_byte;
                send_byte = recv_byte; //(recv_byte & enabled_controllers) | sp_sel_button;
            }
            else if (spi_series_count == 2)
            {
                spi_series_count = 3; // Left Trigger (Last Select)
                // spi_rec_tpads[1] = recv_byte;
                send_byte = recv_byte; //(recv_byte & enabled_controllers) | sp_lt;
            }
            else if (spi_series_count == 3)
            {
                spi_series_count = 4; // Sharing Mode (1 = Allow Sharing)
                // spi_rec_tpads[2] = recv_byte;
                if (share_mode) {
                    send_byte = recv_byte | (~enabled_controllers); // Enable Sharing For Virtual Controllers
                } else {
                    send_byte = recv_byte & enabled_controllers; // Disable Sharing For Virtual Controllers
                }
                //send_byte = recv_byte; //(recv_byte & enabled_controllers) | sp_share;
            }
            else if (spi_series_count == 4)
            {
                spi_series_count = 5; // RESERVED (0 when plugged in, 1 when not)
                // spi_rec_tpads[3] = recv_byte;
                send_byte = recv_byte;

                for (uint8_t i = 4; i < 8; i++)
                { // Check if P1-P4 are plugged in.
                    uint8_t bitwise_index = (i + 4) % 8;
                    if (!(recv_byte & (1 << bitwise_index)))
                    { // If it is plugged in...
                        if (control_keys[i] != 1)
                        { // And it is not currently tracked as physical controlled...
                            // Clear out virtual controller data and free for use.
                            control_keys[i] = 1;
                            selects[i] = 0x0F;
                            sp_a &= ~(0b00000001 << bitwise_index);
                            sp_b &= ~(0b00000001 << bitwise_index);
                            sp_x &= ~(0b00000001 << bitwise_index);
                            sp_y &= ~(0b00000001 << bitwise_index);
                            sp_up &= ~(0b00000001 << bitwise_index);
                            sp_down &= ~(0b00000001 << bitwise_index);
                            sp_left &= ~(0b00000001 << bitwise_index);
                            sp_right &= ~(0b00000001 << bitwise_index);
                            sp_rt &= ~(0b00000001 << bitwise_index);
                            sp_priority_byte |= (0b00000001 << bitwise_index);
                            enable_control[i] = false;
                            enabled_controllers |= (0b00000001 << bitwise_index); // THIS MAY BE BORKED????
                        }
                        timeouts[i] = true;
                    }
                    else
                    { // If it is not plugged in...
                        if (control_keys[i] == 1)
                        { // And if it was being used...
                            // Free it back up for use.
                            control_keys[i] = 0;
                            selects[i] = 0x0F;
                            sp_a &= ~(0b00000001 << bitwise_index);
                            sp_b &= ~(0b00000001 << bitwise_index);
                            sp_x &= ~(0b00000001 << bitwise_index);
                            sp_y &= ~(0b00000001 << bitwise_index);
                            sp_up &= ~(0b00000001 << bitwise_index);
                            sp_down &= ~(0b00000001 << bitwise_index);
                            sp_left &= ~(0b00000001 << bitwise_index);
                            sp_right &= ~(0b00000001 << bitwise_index);
                            sp_rt &= ~(0b00000001 << bitwise_index);
                            sp_priority_byte |= (0b00000001 << bitwise_index);
                            timeouts[i] = false;
                        }
                    }
                }
            }
            else if (spi_series_count == 5)
            {
                spi_series_count = 6; // IS16SEL? (0 when plugged in, 1 when not)
                // spi_rec_tpads[4] = recv_byte;
                if (is16sel_mode) {
                    send_byte = 0xFF; //(recv_byte & enabled_controllers) | sp_is16SEL;
                } else {
                    send_byte = 0x00;
                }
                
            }
            else if (spi_series_count == 6)
            {
                spi_series_count = 7; // D Pad Up
                // spi_rec_tpads[5] = recv_byte;
                send_byte = (recv_byte & enabled_controllers) | sp_up;
            }
            else if (spi_series_count == 7)
            {
                spi_series_count = 8; // D Pad Down
                // spi_rec_tpads[6] = recv_byte;
                send_byte = (recv_byte & enabled_controllers) | sp_down;
            }
            else if (spi_series_count == 8)
            {
                spi_series_count = 9; // D Pad Right
                // spi_rec_tpads[7] = recv_byte;
                send_byte = (recv_byte & enabled_controllers) | sp_right;
            }
            else if (spi_series_count == 9)
            {
                spi_series_count = 10; // D Pad Left
                // spi_rec_tpads[8] = recv_byte;
                send_byte = (recv_byte & enabled_controllers) | sp_left;
            }
            else if (spi_series_count == 10)
            {
                spi_series_count = 11; // A
                // spi_rec_tpads[9] = recv_byte;
                send_byte = (recv_byte & enabled_controllers) | (~sp_rt & sp_a);
            }
            else if (spi_series_count == 11)
            {
                spi_series_count = 12; // B
                // spi_rec_tpads[10] = recv_byte;
                send_byte = (recv_byte & enabled_controllers) | (~sp_rt & sp_b);
            }
            else if (spi_series_count == 12)
            {
                spi_series_count = 13; // X
                // spi_rec_tpads[11] = recv_byte;
                send_byte = (recv_byte & enabled_controllers) | sp_x;
            }
            else if (spi_series_count == 13)
            {
                spi_series_count = 14; // Y
                // spi_rec_tpads[12] = recv_byte;
                send_byte = (recv_byte & enabled_controllers) | sp_y;
            }
            else if (spi_series_count == 14)
            {
                spi_series_count = 15; // RESERVED FOR A' (RT+A) (0 when plugged in, 1 when not)
                // spi_rec_tpads[13] = recv_byte;
                send_byte = (recv_byte & enabled_controllers) | (sp_rt & sp_a);
            }
            else if (spi_series_count == 15)
            {
                spi_series_count = 16; // RESERVED FOR B' (RT+B) (0 when plugged in, 1 when not)
                // spi_rec_tpads[14] = recv_byte;
                send_byte = (recv_byte & enabled_controllers) | (sp_rt & sp_b);
            }
            else if (spi_series_count == 16)
            {
                spi_series_count = 17; // Right Trigger (Slow)
                // spi_rec_tpads[15] = recv_byte;
                send_byte = (recv_byte & enabled_controllers) | sp_rt;
            }
            else if (spi_series_count == 17)
            {
                spi_series_count = 18; // Spare (0 regardless if plugged in or not)
                // spi_rec_tpads[16] = recv_byte;
                send_byte = 0x00; // recv_byte;
            }
            else if (spi_series_count == 18)
            {
                spi_series_count = 19; // Priority Byte
                // spi_rec_tpads[17] = recv_byte;
                send_byte = recv_byte | sp_priority_byte;
                sp_priority_byte = 0x00;
            }
            else if (spi_series_count == 19)
            {
                spi_current_series = NO_SERIES;
                spi_series_count = 0;
                send_byte = NULL_CMD;
            }
            else
            {
                spi_current_series = NO_SERIES;
                spi_series_count = 0;
                send_byte = NULL_CMD;
            }
        }
        // EDIT SELECT SERIES
        else if (spi_current_series == EDIT_SELECT_SERIES)
        {
            if (spi_series_count == 1)
            {
                spi_series_count = 2; // P1 Select
                // spi_rec_select[0] = recv_byte;
                if (enable_control[4])
                {
                    send_byte = selects[4];
                }
                else
                {
                    selects[4] = recv_byte;
                    send_byte = recv_byte;
                }
            }
            else if (spi_series_count == 2)
            {
                spi_series_count = 3; // P2 Select
                // spi_rec_select[1] = recv_byte;
                if (enable_control[5])
                {
                    send_byte = selects[5];
                }
                else
                {
                    selects[5] = recv_byte;
                    send_byte = recv_byte;
                }
            }
            else if (spi_series_count == 3)
            {
                spi_series_count = 4; // P3 Select
                // spi_rec_select[2] = recv_byte;
                if (enable_control[6])
                {
                    send_byte = selects[6];
                }
                else
                {
                    selects[6] = recv_byte;
                    send_byte = recv_byte;
                }
            }
            else if (spi_series_count == 4)
            {
                spi_series_count = 5; // P4 Select
                // spi_rec_select[3] = recv_byte;
                if (enable_control[7])
                {
                    send_byte = selects[7];
                }
                else
                {
                    selects[7] = recv_byte;
                    send_byte = recv_byte;
                }
            }
            else if (spi_series_count == 5)
            {
                spi_series_count = 6; // V1 Select
                // spi_rec_select[4] = recv_byte;
                send_byte = selects[0];
            }
            else if (spi_series_count == 6)
            {
                spi_series_count = 7; // V2 Select
                // spi_rec_select[5] = recv_byte;
                send_byte = selects[1];
            }
            else if (spi_series_count == 7)
            {
                spi_series_count = 8; // V3 Select
                // spi_rec_select[6] = recv_byte;
                send_byte = selects[2];
            }
            else if (spi_series_count == 8)
            {
                spi_series_count = 9; // V4 Select
                // spi_rec_select[7] = recv_byte;
                send_byte = selects[3];
            }
            else if (spi_series_count == 9)
            {
                spi_series_count = 10; // Timer Value (Counts Up From 0-15 and Repeats)
                send_byte = recv_byte;
            }
            else if (spi_series_count == 10)
            {
                spi_current_series = NO_SERIES;
                spi_series_count = 0;
                send_byte = SLAVE_WAIT_INS; // Ready to Insert Packet
            }
            else
            {
                spi_current_series = NO_SERIES;
                spi_series_count = 0;
                send_byte = NULL_CMD;
            }
        }
        // PKT INJECT SERIES
        else if (spi_current_series == PKT_INJECT_SERIES)
        {
            if (spi_series_count == 1)
            {
                spi_current_series = NO_SERIES;
                spi_series_count = 0;

                // Generate First Half of DPI Packet (A, B, X, Y, A', B', RT, ?)
                send_byte = 0b00000000;

                if (dpi_a[next_dpi_index]) {
                    send_byte |= 0b10000000;
                }
                if (dpi_b[next_dpi_index]) {
                    send_byte |= 0b01000000;
                }
                if (dpi_x[next_dpi_index]) {
                    send_byte |= 0b00100000;
                }
                if (dpi_y[next_dpi_index]) {
                    send_byte |= 0b00010000;
                }
                if (dpi_rt[next_dpi_index]) {
                    send_byte |= 0b00000010;
                }
            }
        }
        // CATCH ALL
        else
        {
            spi_current_series = NO_SERIES;
            spi_series_count = 0;
            send_byte = NULL_CMD;
        }
    }
}

//  __          _______ ______ _____   //
//  \ \        / /_   _|  ____|_   _|  //
//   \ \  /\  / /  | | | |__    | |    //
//    \ \/  \/ /   | | |  __|   | |    //
//     \  /\  /   _| |_| |     _| |_   //
//      \/  \/   |_____|_|    |_____|  //

/// @brief Opens NVS and Erases It
/// @param void
/// @return void
static void reset_erase_nvs(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Open NVS
    nvs_handle_t nvs_handle;
    ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle!", esp_err_to_name(ret));
        return;
    }

    ret = nvs_flash_erase();
    if (ret == ESP_OK)
    {
        ESP_LOGI("NVS", "NVS Succesfully Erased");
    }
    else if (ret == ESP_ERR_NOT_FOUND)
    {
        ESP_LOGW("NVS", "NVS partition to be erased not found.");
    }
    else
    {
        ESP_LOGE("NVS", "Error (%s) while attempting to erase NVS!", esp_err_to_name(ret));
    }

    // Close NVS
    nvs_close(nvs_handle);
}

/// @brief Event handler for all WiFi events.
/// @param arg Unused
/// @param event_base Unused
/// @param event_id ID of WiFi Event for Decoding
/// @param event_data Unused
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    // STATION EVENTS
    if (event_id == WIFI_EVENT_STA_START)
    {
        esp_timer_start_periodic(wifi_led_timer, 0.25 * 1000000); // 0.25 Second Interval
        //ESP_LOGI("WIFI", "WiFi station started. Connecting to the saved SSID.");
        esp_wifi_connect();
    }
    else if (event_id == WIFI_EVENT_STA_CONNECTED)
    {
        //ESP_LOGI("WIFI", "WiFi station connected to saved credentials.");
    }
    else if (event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        esp_timer_start_periodic(wifi_led_timer, 0.25 * 1000000); // 0.25 Second Interval
        ESP_LOGE("WIFI", "WiFi station disconnected. Reconnecting...");
        esp_wifi_connect();
        connection_attempts++;
        if (connection_attempts > WIFI_CONNECT_ATTEMPTS)
        {
            ESP_LOGE("WIFI", "Maximum WiFi station connection attempts reached. Clearing NVS and rebooting.");
            reset_erase_nvs();
            esp_restart();
        }
    }
    else if (event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI("WIFI", "WiFi station received IP address.");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
        esp_timer_stop(wifi_led_timer);
        gpio_set_level(GPIO_WIFI_LED, 1); // Turn WIFI_LED on.
    }
    // ACCESS POINT EVENTS
    else if (event_id == WIFI_EVENT_AP_START)
    {
        ESP_LOGI("WIFI", "WiFi Access Point Started");
        esp_timer_start_periodic(wifi_led_timer, 1 * 1000000); // 1 Second Interval
    }
    else if (event_id == WIFI_EVENT_AP_STACONNECTED)
    {
        ESP_LOGI("WIFI", "Station Connected to Access Point ");
    }
    else if (event_id == WIFI_EVENT_AP_STADISCONNECTED)
    {
        ESP_LOGI("WIFI", "Station Disconnected from Access Point ");
    }
}

/// @brief Initialize WiFi as Access Point for Provisioning
/// @param void
/// @return void
static void init_wifi_ap(void)
{
    // Initialize the TCP/IP Stack
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t *netif_int = esp_netif_create_default_wifi_ap();

    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // Set Up the Event Handler
    esp_event_handler_instance_t instance_any_id, instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    // Configure the Access Point
    wifi_config_t wifi_config = {
        .ap = {
            .ssid = AP_SSID,
            .ssid_len = strlen(AP_SSID),
            .password = AP_PASSWORD,
            .max_connection = 4,
            .authmode = WIFI_AUTH_WPA2_PSK},
    };

    // Set the WiFi Mode to SoftAP (Access Point)
    esp_wifi_set_mode(WIFI_MODE_AP);

    // Set the Configuration
    esp_wifi_set_config(WIFI_IF_AP, &wifi_config);

    // Stop the DHCP Server
    esp_netif_dhcps_stop(netif_int);

    // Set the IP Configuration for the SoftAP
    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, 1, 2, 3, 4);            // Set your desired gateway IP address
    IP4_ADDR(&ip_info.gw, 1, 2, 3, 4);            // Set your desired gateway IP address
    IP4_ADDR(&ip_info.netmask, 255, 255, 255, 0); // Set your desired netmask
    esp_netif_set_ip_info(netif_int, &ip_info);   // Set the IP information

    // Restart the DHCP Server
    esp_netif_dhcps_start(netif_int);

    // Start the WiFi Access Point
    esp_wifi_start();
}

/// @brief Initialize WiFi as a Station
/// @param ssid Saved SSID Pulled from NVS
/// @param password Saved Password Pulled from NVS
/// @param gateway Saved Gateway Address Pulled from NVS
/// @param static_ip Saved Static IP Address Pulled from NVS
/// @param netmask Saved Netmask Address Pulled from NVS
/// @return void
void init_wifi_sta(const char *ssid, const char *password, const uint8_t *gateway, const uint8_t *static_ip, const uint8_t *netmask)
{
    // Initialize the Networking Interface
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t *netif_int = esp_netif_create_default_wifi_sta();

    // Stop DHCP to Assign Custom IP Info
    esp_netif_dhcpc_stop(netif_int);

    esp_netif_ip_info_t ip_info;
    IP4_ADDR(&ip_info.ip, static_ip[0], static_ip[1], static_ip[2], static_ip[3]);
    IP4_ADDR(&ip_info.gw, gateway[0], gateway[1], gateway[2], gateway[3]);
    IP4_ADDR(&ip_info.netmask, netmask[0], netmask[1], netmask[2], netmask[3]);

    esp_netif_set_ip_info(netif_int, &ip_info);

    esp_netif_set_hostname(netif_int, "SmartPortWiFi");

    // Set Up DNS
    esp_netif_dns_info_t main_dns_info;
    ip_addr_t main_dns_ip;
    IP_ADDR4(&main_dns_ip, 9, 9, 9, 9);
    main_dns_info.ip.u_addr.ip4.addr = main_dns_ip.u_addr.ip4.addr;
    main_dns_info.ip.type = IPADDR_TYPE_V4;
    esp_netif_set_dns_info(netif_int, ESP_NETIF_DNS_MAIN, &main_dns_info);

    esp_netif_dns_info_t backup_dns_info;
    ip_addr_t backup_dns_ip;
    IP_ADDR4(&backup_dns_ip, 8, 8, 8, 8);
    backup_dns_info.ip.u_addr.ip4.addr = backup_dns_ip.u_addr.ip4.addr;
    backup_dns_info.ip.type = IPADDR_TYPE_V4;
    esp_netif_set_dns_info(netif_int, ESP_NETIF_DNS_BACKUP, &backup_dns_info);

    // Set the Hostname
    esp_netif_set_hostname(netif_int, "Smart Port WiFi Interface");

    // Initialize WiFi From Default Station Config
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);

    // Register Event Handlers for WiFi Events
    esp_event_handler_instance_t instance_any_id, instance_got_ip;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip);

    // Configuration For the Station
    wifi_config_t sta_config = {
        .sta = {
            .ssid = "",
            .password = "",
        },
    };

    // Set the Credentials
    strncpy((char *)sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char *)sta_config.sta.password, password, sizeof(sta_config.sta.password) - 1);

    // Configure and Start the WiFi Station
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &sta_config);
    esp_wifi_start();

    // Wait Until Connected to the WiFi Network
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);
}

/// @brief Checks if WiFi credentials are saved in NVS.  If yes, initialize as a station.  If not, initialize as an access point.
/// @param void
/// @return void
static void wifi_init_from_nvs(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Open NVS
    nvs_handle_t nvs_handle;
    ret = nvs_open("storage", NVS_READWRITE, &nvs_handle);
    if (ret != ESP_OK)
    {
        ESP_LOGE("NVS", "Error (%s) opening NVS handle!", esp_err_to_name(ret));
        return;
    }

    // Get Share Mode, 16sel Mode, and Controller Timeout from NVS
    nvs_get_u8(nvs_handle, "share_mode", &share_mode);
    nvs_get_u8(nvs_handle, "16sel_mode", &is16sel_mode);
    nvs_get_u8(nvs_handle, "cont_timeout", &controller_timeout);

    // Get Stored Vehicle Names from NVS
    size_t size = sizeof(nvs_vehicle_names[0]);
    ret = nvs_get_str(nvs_handle, "v1_name", nvs_vehicle_names[0], &size);
    if (ret == ESP_OK)
    {
        size = sizeof(nvs_vehicle_names[1]);
        nvs_get_str(nvs_handle, "v2_name", nvs_vehicle_names[1], &size);
        size = sizeof(nvs_vehicle_names[2]);
        nvs_get_str(nvs_handle, "v3_name", nvs_vehicle_names[2], &size);
        size = sizeof(nvs_vehicle_names[3]);
        nvs_get_str(nvs_handle, "v4_name", nvs_vehicle_names[3], &size);
        size = sizeof(nvs_vehicle_names[4]);
        nvs_get_str(nvs_handle, "v5_name", nvs_vehicle_names[4], &size);
        size = sizeof(nvs_vehicle_names[5]);
        nvs_get_str(nvs_handle, "v6_name", nvs_vehicle_names[5], &size);
        size = sizeof(nvs_vehicle_names[6]);
        nvs_get_str(nvs_handle, "v7_name", nvs_vehicle_names[6], &size);
        size = sizeof(nvs_vehicle_names[7]);
        nvs_get_str(nvs_handle, "v8_name", nvs_vehicle_names[7], &size);
        size = sizeof(nvs_vehicle_names[8]);
        nvs_get_str(nvs_handle, "v9_name", nvs_vehicle_names[8], &size);
        size = sizeof(nvs_vehicle_names[9]);
        nvs_get_str(nvs_handle, "v10_name", nvs_vehicle_names[9], &size);
        size = sizeof(nvs_vehicle_names[10]);
        nvs_get_str(nvs_handle, "v11_name", nvs_vehicle_names[10], &size);
        size = sizeof(nvs_vehicle_names[11]);
        nvs_get_str(nvs_handle, "v12_name", nvs_vehicle_names[11], &size);
        size = sizeof(nvs_vehicle_names[12]);
        nvs_get_str(nvs_handle, "v13_name", nvs_vehicle_names[12], &size);
        size = sizeof(nvs_vehicle_names[13]);
        nvs_get_str(nvs_handle, "v14_name", nvs_vehicle_names[13], &size);
        size = sizeof(nvs_vehicle_names[14]);
        nvs_get_str(nvs_handle, "v15_name", nvs_vehicle_names[14], &size);
    }
    // else {
    //     strcpy(nvs_vehicle_names[0], "NULL");
    //     strcpy(nvs_vehicle_names[1], "NULL");
    //     strcpy(nvs_vehicle_names[2], "NULL");
    //     strcpy(nvs_vehicle_names[3], "NULL");
    //     strcpy(nvs_vehicle_names[4], "NULL");
    //     strcpy(nvs_vehicle_names[5], "NULL");
    //     strcpy(nvs_vehicle_names[6], "NULL");
    //     strcpy(nvs_vehicle_names[7], "NULL");
    //     strcpy(nvs_vehicle_names[8], "NULL");
    //     strcpy(nvs_vehicle_names[9], "NULL");
    //     strcpy(nvs_vehicle_names[10], "NULL");
    //     strcpy(nvs_vehicle_names[11], "NULL");
    //     strcpy(nvs_vehicle_names[12], "NULL");
    //     strcpy(nvs_vehicle_names[13], "NULL");
    //     strcpy(nvs_vehicle_names[14], "NULL");
    // }

    char nvs_ssid[MAX_WIFI_SSID_LEN];
    char nvs_password[MAX_WIFI_PASSWORD_LEN];
    uint8_t nvs_gateway[4] = {0}; // Array to store each part of the Gateway IP
    uint8_t nvs_static[4] = {0};  // Array to store each part of the Static IP
    uint8_t nvs_netmask[4] = {0}; // Array to store each part of the Net Mask IP

    // Retrieve SSID from NVS
    size = sizeof(nvs_ssid);
    ret = nvs_get_str(nvs_handle, "wifi_ssid", nvs_ssid, &size);

    // If credentials are found, initialize as a station.
    if (ret == ESP_OK)
    {
        // Retrieve WiFi Password
        size = sizeof(nvs_password);
        ret = nvs_get_str(nvs_handle, "wifi_password", nvs_password, &size);
        if (ret == ESP_OK)
        {
            // Retrieve Gateway IP Address parts
            size = sizeof(nvs_gateway);
            ret = nvs_get_blob(nvs_handle, "gateway_ip", nvs_gateway, &size);
            if (ret == ESP_OK)
            {
                // Retrieve Static IP Address parts
                size = sizeof(nvs_static);
                ret = nvs_get_blob(nvs_handle, "static_ip", nvs_static, &size);
                if (ret == ESP_OK)
                {
                    // Retrieve Net Mask IP Address parts
                    size = sizeof(nvs_netmask);
                    ret = nvs_get_blob(nvs_handle, "netmask_ip", nvs_netmask, &size);
                    if (ret == ESP_OK)
                    {
                        // Retrieve Admin Passowd
                        size = sizeof(nvs_admin_password);
                        ret = nvs_get_str(nvs_handle, "admin_password", nvs_admin_password, &size);
                        if (ret == ESP_OK)
                        {
                            // Connect to WiFi using retrieved information including IP arrays.
                            ESP_LOGI("NVS", "WiFi credentials found in NVS. Connecting to WiFi as a station.");
                            //ESP_LOGI("NVS", "Retrieved Admin Password: %s", nvs_admin_password);
                            //ESP_LOGI("NVS", "Retrieved SSID: %s", nvs_ssid);
                            //ESP_LOGI("NVS", "Retrieved Password: %s", nvs_password);
                            if ((nvs_password[0] == 'X') && (nvs_password[1] == '\0'))
                            {
                                //ESP_LOGI("NVS", "Stored password was X, indicating no password");
                                nvs_password[0] = '\0';
                            }
                            init_wifi_sta(nvs_ssid, nvs_password, nvs_gateway, nvs_static, nvs_netmask);
                            start_http_webserver(0); // Begin the http Webserver (0 = Normal Version)
                        }
                    }
                    else
                    {
                        ESP_LOGE("NVS", "Error (%s) reading Net Mask IP from NVS!", esp_err_to_name(ret));
                    }
                }
                else
                {
                    ESP_LOGE("NVS", "Error (%s) reading Static IP from NVS!", esp_err_to_name(ret));
                }
            }
            else
            {
                ESP_LOGE("NVS", "Error (%s) reading Gateway IP from NVS!", esp_err_to_name(ret));
            }
        }
        else
        {
            ESP_LOGE("NVS", "Error (%s) reading WiFi password from NVS!", esp_err_to_name(ret));
        }
    }
    // If credentials are not found, initialize as an AP and start the webserver.
    else
    {
        ESP_LOGI("NVS", "WiFi credentials not found in NVS. Creating WiFi access point.");

        init_wifi_ap(); // Create a WiFi access point

        httpd_handle_t server = start_http_webserver(1); // Begin the http Webserver (1 = Initialization Version)

        //ESP_LOGI("WEBSERVER", "HTTP server started. Open a browser and connect to the ESP32 access point to enter WiFi credentials.");

        // Wait for the user to enter WiFi credentials and reboot the ESP32
        while (1)
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);

            // Check if WiFi credentials are now available in NVS
            size = sizeof(nvs_ssid);
            ret = nvs_get_str(nvs_handle, "wifi_ssid", nvs_ssid, &size);
            if (ret == ESP_OK)
            {
                size = sizeof(nvs_password);
                ret = nvs_get_str(nvs_handle, "wifi_password", nvs_password, &size);
                if (ret == ESP_OK)
                {
                    ESP_LOGI("WIFI", "WiFi credentials entered by user. Rebooting...");

                    // Stop the HTTP server before rebooting
                    httpd_stop(server);

                    // Delay to allow the response to be sent before rebooting
                    vTaskDelay(pdMS_TO_TICKS(1000));

                    // Reboot the ESP32
                    esp_restart();
                }
            }
        }
    }

    // Close NVS
    nvs_close(nvs_handle);
}

//   _______ _____ __  __ ______ _____      _____          _      _      ____          _____ _  __ _____   //
//  |__   __|_   _|  \/  |  ____|  __ \    / ____|   /\   | |    | |    |  _ \   /\   / ____| |/ // ____|  //
//     | |    | | | \  / | |__  | |__) |  | |       /  \  | |    | |    | |_) | /  \ | |    | ' /| (___    //
//     | |    | | | |\/| |  __| |  _  /   | |      / /\ \ | |    | |    |  _ < / /\ \| |    |  <  \___ \   //
//     | |   _| |_| |  | | |____| | \ \   | |____ / ____ \| |____| |____| |_) / ____ \ |____| . \ ____) |  //
//     |_|  |_____|_|  |_|______|_|  \_\   \_____/_/    \_\______|______|____/_/    \_\_____|_|\_\_____/   //

/// @brief Runs if the sync timer expires. Normally it is reset before this occurs, indicating sync.
/// @param void
/// @return void
static void IRAM_ATTR sync_timer_callback(void *arg)
{
    if (sp_status == 1)
    {
        sp_status = 0;
        gpio_set_level(GPIO_SP_LED, 0);

        for (int i = 0; i < 8; i++)
        {
            selects[i] = 0x0F;
            enable_control[i] = false;
        }

        enabled_controllers = 0b11111111;
        sp_a = 0x00;
        sp_b = 0x00;
        sp_x = 0x00;
        sp_y = 0x00;
        sp_up = 0x00;
        sp_down = 0x00;
        sp_right = 0x00;
        sp_left = 0x00;
        sp_rt = 0x00;
        sp_priority_byte = 0x00;

        spi_current_series = 0;
        spi_series_count = 0;
    }
}

/// @brief Runs every CONTROLLER_TIMEOUT seconds. Detects which controllers haven't been used and frees them.
/// @param void
/// @return void
static void IRAM_ATTR timeout_timer_callback(void *arg)
{
    // ESP_LOGI("TIMER", "TIMEOUT");
    //  timeouts[0] = true;
    //  timeouts[1] = true;
    //  timeouts[2] = true;
    //  timeouts[3] = true;

    for (uint8_t i = 0; i < 8; i++)
    {
        if ((control_keys[i] > 0) && (timeouts[i] == false)) // "If that controller was being used and hasn't been used in this timeout cycle."
        {
            uint8_t bitwise_index = (i + 4) % 8;
            control_keys[i] = 0;
            selects[i] = 0x0F;
            sp_a &= ~(0b00000001 << bitwise_index);
            sp_b &= ~(0b00000001 << bitwise_index);
            sp_x &= ~(0b00000001 << bitwise_index);
            sp_y &= ~(0b00000001 << bitwise_index);
            sp_up &= ~(0b00000001 << bitwise_index);
            sp_down &= ~(0b00000001 << bitwise_index);
            sp_left &= ~(0b00000001 << bitwise_index);
            sp_right &= ~(0b00000001 << bitwise_index);
            sp_rt &= ~(0b00000001 << bitwise_index);
            sp_priority_byte |= (0b00000001 << bitwise_index);
        }
        timeouts[i] = false;
    }
    for (uint8_t i = 8; i < 12; i++)
    {
        if ((control_keys[i] > 0) && (timeouts[i] == false)) // "If that controller was being used and hasn't been used in this timeout cycle."
        {
            control_keys[i] = 0;
            selects[i] = 0x0F;
            dpi_a[i - 8] = false;
            dpi_b[i - 8] = false;
            dpi_x[i - 8] = false;
            dpi_y[i - 8] = false;
            dpi_up[i - 8] = false;
            dpi_down[i - 8] = false;
            dpi_left[i - 8] = false;
            dpi_right[i - 8] = false;
            dpi_rt[i - 8] = false;
        }
        timeouts[i] = false;
    }

    esp_timer_start_once(post_timeout_timer, 0.1 * 1000000); // 0.1 Second Interval

    // ESP_LOGI("RAM", "RAM left %lu", esp_get_free_heap_size()); // Log remaining heap in bytes.
}

/// @brief Runs 0.1 seconds after the timeout_timer_callback. Gives time for selections to propagate before disabling.
/// @param void
/// @return void
static void IRAM_ATTR post_timeout_timer_callback(void *arg)
{
    // ESP_LOGI("TIMER", "POST TIMEOUT");
    for (uint8_t i = 0; i < 12; i++)
    {
        if (control_keys[i] == 0)
        {
            enable_control[i] = false;
            if (i < 8) // Only For Virtual and Physical Controllers
            {
                enabled_controllers |= (0b00000001 << ((i + 4) % 8));
            }
        }
    }
}

/// @brief Runs 5 seconds after the reset button is pressed.  If it is still pressed, clear NVS and reset.
/// @param void
/// @return void
static void IRAM_ATTR reset_button_timer_callback(void *arg)
{
    if (gpio_get_level(GPIO_RESET) == 0)
    {
        reset_erase_nvs();
        esp_restart();
    }
}

/// @brief Runs every second after the wifi AP is turned on.
/// @param void
/// @return void
static void IRAM_ATTR wifi_led_timer_callback(void *arg)
{
    if (wifi_led_status)
    {
        wifi_led_status = false;
    }
    else
    {
        wifi_led_status = true;
    }

    gpio_set_level(GPIO_WIFI_LED, wifi_led_status);
}

//   _____  _____ _____        //
//  |_   _|/ ____|  __ \       //
//    | | | (___ | |__) |___   //
//    | |  \___ \|  _  // __|  //
//   _| |_ ____) | | \ \\__ \  //
//  |_____|_____/|_|  \_\___/  //

/// @brief Starts a 5 second timer when the reset button is pressed.
/// @param arg Unused
/// @return void
static void IRAM_ATTR reset_isr_handler()
{
    if (gpio_get_level(GPIO_RESET) == 0) { // Reset Pressed
        esp_timer_stop(wifi_led_timer);
        esp_timer_start_periodic(wifi_led_timer, 0.1 * 1000000); // 0.1 Second Interval
        esp_timer_start_once(reset_button_timer, 5 * 1000000); // 5 Second Interval
    } else { // Reset Released Before 5 Second Timer
        esp_timer_stop(reset_button_timer);
        esp_restart();
    }
    return;
}

//   __  __          _____ _   _   //
//  |  \/  |   /\   |_   _| \ | |  //
//  | \  / |  /  \    | | |  \| |  //
//  | |\/| | / /\ \   | | | . ` |  //
//  | |  | |/ ____ \ _| |_| |\  |  //
//  |_|  |_/_/    \_\_____|_| \_|  //

void app_main(void)
{
    // Mark Current OTA App As Valid
    const esp_partition_t *partition = esp_ota_get_running_partition();
    printf("Currently Running Partition: %s\r\n", partition->label);

    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(partition, &ota_state) == ESP_OK)
    {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
        {
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    // Initialize User Names for Control Keys 0 and 1
    snprintf(control_key_user_names[0], 17, "No User");
    snprintf(control_key_user_names[1], 17, "Plugged In");

    // Setup GPIO Pins and Interrupts
    gpio_reset_pin(GPIO_SR);
    gpio_set_direction(GPIO_SR, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_SR, 1);

    gpio_reset_pin(GPIO_SP_LED);
    gpio_set_direction(GPIO_SP_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_SP_LED, 0);

    gpio_reset_pin(GPIO_WIFI_LED);
    gpio_set_direction(GPIO_WIFI_LED, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_WIFI_LED, 0);

    gpio_reset_pin(GPIO_RESET);
    gpio_set_direction(GPIO_RESET, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_RESET, GPIO_PULLUP_ONLY);

    gpio_reset_pin(GPIO_FE);
    gpio_set_direction(GPIO_FE, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_FE, GPIO_PULLUP_ONLY);

    // Configure Reset Interrupt
    gpio_intr_enable(GPIO_RESET);
    gpio_set_intr_type(GPIO_RESET, GPIO_INTR_ANYEDGE);
    gpio_install_isr_service(ESP_INTR_FLAG_LEVEL1);
    gpio_isr_handler_add(GPIO_RESET, reset_isr_handler, NULL);

    gpio_pin_glitch_filter_config_t SCLK_config = {
        .clk_src = SOC_ROOT_CLK_INT_RC_SLOW,
        .gpio_num = GPIO_SCLK,
    };
    gpio_glitch_filter_handle_t SCLK_filter;
    gpio_new_pin_glitch_filter(&SCLK_config, &SCLK_filter);
    gpio_glitch_filter_enable(SCLK_filter);

    // Initialize SP Sync Timer
    const esp_timer_create_args_t sync_timer_args = {
        .callback = &sync_timer_callback,
        .name = "sync_timer"};
    esp_timer_create(&sync_timer_args, &sync_timer);
    esp_timer_start_periodic(sync_timer, 0.1 * 1000000); // 0.1 Second Interval

    // Initialize Post Timeout Timer
    const esp_timer_create_args_t post_timeout_timer_args = {
        .callback = &post_timeout_timer_callback,
        .name = "post_timeout_timer"};
    esp_timer_create(&post_timeout_timer_args, &post_timeout_timer);

    // Initialize Reset Button Timer
    const esp_timer_create_args_t reset_button_timer_args = {
        .callback = &reset_button_timer_callback,
        .name = "reset_button_timer"};
    esp_timer_create(&reset_button_timer_args, &reset_button_timer);

    // Initialize WiFi LED Timer
    const esp_timer_create_args_t wifi_led_timer_args = {
        .callback = &wifi_led_timer_callback,
        .name = "wifi_led_timer"};
    esp_timer_create(&wifi_led_timer_args, &wifi_led_timer);

    // Configuration for the SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = GPIO_MOSI,
        .miso_io_num = GPIO_MISO,
        .sclk_io_num = GPIO_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };

    // Configuration for the SPI slave interface
    spi_slave_interface_config_t slvcfg = {
        .mode = 0,
        .spics_io_num = GPIO_FE,
        .queue_size = 1,
        .flags = 0,
        .post_setup_cb = post_setup_callback,
        .post_trans_cb = post_transaction_callback};

    // Initialize the SPI Slave Interface
    spi_slave_initialize(SPI2_HOST, &buscfg, &slvcfg, SPI_DMA_DISABLED);

    // Initialize the SPI Task
    xTaskCreate(&spi_task, "SPI_TASK", 4096, NULL, 1, NULL);

    // Initialize the WiFi event group
    wifi_event_group = xEventGroupCreate();

    // Initialize NVS and check/configure WiFi
    wifi_init_from_nvs();

    // Initialize Controller Timeout Timer (Must Be After wifi_init_from_nvs)
    const esp_timer_create_args_t timeout_timer_args = {
        .callback = &timeout_timer_callback,
        .name = "timeout_timer"};
    esp_timer_create(&timeout_timer_args, &timeout_timer);
    esp_timer_start_periodic(timeout_timer, controller_timeout * 1000000); // controller_timeout Second Interval

    // while (1) {
    //     vTaskDelay(pdMS_TO_TICKS(100));
    // }
}