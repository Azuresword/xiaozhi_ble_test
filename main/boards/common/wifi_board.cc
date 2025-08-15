#include "wifi_board.h"

#include "display.h"
#include "application.h"
#include "system_info.h"
#include "font_awesome_symbols.h"
#include "settings.h"
#include "assets/lang_config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_http.h>
#include <esp_mqtt.h>
#include <esp_udp.h>
#include <tcp_transport.h>
#include <tls_transport.h>
#include <web_socket.h>
#include <esp_log.h>

#include <wifi_station.h>
#include <wifi_configuration_ap.h>
#include <ssid_manager.h>
#include "esp_wifi_types.h"
#include "esp_wifi.h"
#include "ble_provisioning.h"

static const char *TAG = "WifiBoard";

WifiBoard::WifiBoard() {
    Settings settings("wifi", true);
    wifi_config_mode_ = settings.GetInt("force_ap") == 1;
    if (wifi_config_mode_) {
        ESP_LOGI(TAG, "force_ap is set to 1, reset to 0");
        settings.SetInt("force_ap", 0);
    }
}

std::string WifiBoard::GetBoardType() {
    return "wifi";
}

void WifiBoard::EnterWifiConfigMode() {
#if 0
    auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateWifiConfiguring);

    auto& wifi_ap = WifiConfigurationAp::GetInstance();
    wifi_ap.SetLanguage(Lang::CODE);
    wifi_ap.SetSsidPrefix("Xiaozhi");
    wifi_ap.Start();

    // 显示 WiFi 配置 AP 的 SSID 和 Web 服务器 URL
    std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
    hint += wifi_ap.GetSsid();
    hint += Lang::Strings::ACCESS_VIA_BROWSER;
    hint += wifi_ap.GetWebServerUrl();
    hint += "\n\n";
    
    // 播报配置 WiFi 的提示
    application.Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "", Lang::Sounds::P3_WIFICONFIG);
    
    // Wait forever until reset after configuration
    while (true) {
        int free_sram = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        int min_free_sram = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        ESP_LOGI(TAG, "Free internal: %u minimal internal: %u", free_sram, min_free_sram);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
#endif
auto& application = Application::GetInstance();
    application.SetDeviceState(kDeviceStateBleProvisioning);

    // Initialize Wi-Fi stack before starting BLE
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi initialized for scanning.");

    ble_provisioner_ = std::make_unique<BleProvisioning>();
    ble_provisioner_->on_provisioned([this](const std::string& ssid, const std::string& password) {
        SsidManager::GetInstance().AddSsid(ssid, password);
        ESP_LOGI(TAG, "Free heap before restart: %d", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        esp_restart();
    });

    ble_provisioner_->start();
}
#if 0
void WifiBoard::StartNetwork() {
    // User can press BOOT button while starting to enter WiFi configuration mode
    if (wifi_config_mode_) {
        EnterWifiConfigMode();
        return;
    }

    // If no WiFi SSID is configured, enter WiFi configuration mode
    auto& ssid_manager = SsidManager::GetInstance();
    auto ssid_list = ssid_manager.GetSsidList();
    if (ssid_list.empty()) {
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }

    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.OnScanBegin([this]() {
        auto display = Board::GetInstance().GetDisplay();
        display->ShowNotification(Lang::Strings::SCANNING_WIFI, 30000);
    });
    wifi_station.OnConnect([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECT_TO;
        notification += ssid;
        notification += "...";
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.OnConnected([this](const std::string& ssid) {
        auto display = Board::GetInstance().GetDisplay();
        std::string notification = Lang::Strings::CONNECTED_TO;
        notification += ssid;
        display->ShowNotification(notification.c_str(), 30000);
    });
    wifi_station.Start();

    // Try to connect to WiFi, if failed, launch the WiFi configuration AP
    if (!wifi_station.WaitForConnected(60 * 1000)) {
        wifi_station.Stop();
        wifi_config_mode_ = true;
        EnterWifiConfigMode();
        return;
    }
}
#endif

bool WifiBoard::StartNetwork() {
    bool force_ap = false;
    nvs_handle_t nvs_handle;
    if (nvs_open("storage", NVS_READONLY, &nvs_handle) == ESP_OK) {
        int8_t force_ap_val = 0;
        if (nvs_get_i8(nvs_handle, "force_ap", &force_ap_val) == ESP_OK && force_ap_val == 1) {
            force_ap = true;
        }
        nvs_close(nvs_handle);
    }

    auto& ssid_manager = SsidManager::GetInstance();
    if (ssid_manager.GetSsidList().empty() || force_ap) {
        ESP_LOGI(TAG, "No Wi-Fi credentials found or force_ap is set. Entering BLE provisioning mode.");
        EnterWifiConfigMode();
        return false;
    }

    ESP_LOGI(TAG, "Connecting to existing Wi-Fi...");
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.Start();
    return true; // Indicates that we are attempting to connect
}


Http* WifiBoard::CreateHttp() {
    return new EspHttp();
}

WebSocket* WifiBoard::CreateWebSocket() {
#ifdef CONFIG_CONNECTION_TYPE_WEBSOCKET
    std::string url = CONFIG_WEBSOCKET_URL;
    if (url.find("wss://") == 0) {
        return new WebSocket(new TlsTransport());
    } else {
        return new WebSocket(new TcpTransport());
    }
#endif
    return nullptr;
}

Mqtt* WifiBoard::CreateMqtt() {
    return new EspMqtt();
}

Udp* WifiBoard::CreateUdp() {
    return new EspUdp();
}

const char* WifiBoard::GetNetworkStateIcon() {
    if (wifi_config_mode_) {
        return FONT_AWESOME_WIFI;
    }
    auto& wifi_station = WifiStation::GetInstance();
    if (!wifi_station.IsConnected()) {
        return FONT_AWESOME_WIFI_OFF;
    }
    int8_t rssi = wifi_station.GetRssi();
    if (rssi >= -60) {
        return FONT_AWESOME_WIFI;
    } else if (rssi >= -70) {
        return FONT_AWESOME_WIFI_FAIR;
    } else {
        return FONT_AWESOME_WIFI_WEAK;
    }
}

std::string WifiBoard::GetBoardJson() {
    // Set the board type for OTA
    auto& wifi_station = WifiStation::GetInstance();
    std::string board_json = std::string("{\"type\":\"" BOARD_TYPE "\",");
    board_json += "\"name\":\"" BOARD_NAME "\",";
    if (!wifi_config_mode_) {
        board_json += "\"ssid\":\"" + wifi_station.GetSsid() + "\",";
        board_json += "\"rssi\":" + std::to_string(wifi_station.GetRssi()) + ",";
        board_json += "\"channel\":" + std::to_string(wifi_station.GetChannel()) + ",";
        board_json += "\"ip\":\"" + wifi_station.GetIpAddress() + "\",";
    }
    board_json += "\"mac\":\"" + SystemInfo::GetMacAddress() + "\"}";
    return board_json;
}

void WifiBoard::SetPowerSaveMode(bool enabled) {
    auto& wifi_station = WifiStation::GetInstance();
    wifi_station.SetPowerSaveMode(enabled);
}

void WifiBoard::ResetWifiConfiguration() {
    // Set a flag and reboot the device to enter the network configuration mode
    {
        Settings settings("wifi", true);
        settings.SetInt("force_ap", 1);
    }
    GetDisplay()->ShowNotification(Lang::Strings::ENTERING_WIFI_CONFIG_MODE);
    vTaskDelay(pdMS_TO_TICKS(1000));
    // Reboot the device
    esp_restart();
}

void WifiBoard::TriggerWifiScan() {
    ESP_LOGI(TAG, "Starting Wi-Fi scan...");

    // The scan function is blocking, so we run it in a dedicated task
    // to avoid blocking the main application logic.
    xTaskCreate([](void* arg) {
        auto* self = static_cast<WifiBoard*>(arg);

        // 1. Start Scan
        wifi_scan_config_t scan_config = {}; // Default scan config
        esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(ret));
            // Optionally, send a failure status back to the app
            if (self->ble_provisioner_) {
                self->ble_provisioner_->send_status("wifi_scan_failed", "Failed to start Wi-Fi scan.");
            }
            vTaskDelete(NULL);
            return;
        }

        // 2. Get Scan Results
        uint16_t ap_count = 0;
        esp_wifi_scan_get_ap_num(&ap_count);
        if (ap_count == 0) {
            ESP_LOGI(TAG, "No APs found");
            cJSON *root = cJSON_CreateObject();
            cJSON_AddStringToObject(root, "type", "wifi_scan_result");
            cJSON_AddItemToObject(root, "payload", cJSON_CreateArray());
            char* json_string = cJSON_PrintUnformatted(root);
            if (self->ble_provisioner_) {
                self->ble_provisioner_->send_data(json_string);
            }
            cJSON_free(json_string);
            cJSON_Delete(root);
            vTaskDelete(NULL);
            return;
        }

        std::vector<wifi_ap_record_t> ap_records(ap_count);
        ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_records.data()));

        // 3. Format Results to JSON
        cJSON *root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", "wifi_scan_result");

        cJSON *payload = cJSON_CreateArray();
        for (const auto& ap : ap_records) {
            cJSON *ap_json = cJSON_CreateObject();
            cJSON_AddStringToObject(ap_json, "ssid", (const char*)ap.ssid);
            cJSON_AddNumberToObject(ap_json, "rssi", ap.rssi);
            const char* enc_type;
            switch (ap.authmode) {
                case WIFI_AUTH_OPEN: enc_type = "OPEN"; break;
                case WIFI_AUTH_WEP: enc_type = "WEP"; break;
                case WIFI_AUTH_WPA_PSK: enc_type = "WPA_PSK"; break;
                case WIFI_AUTH_WPA2_PSK: enc_type = "WPA2_PSK"; break;
                case WIFI_AUTH_WPA_WPA2_PSK: enc_type = "WPA_WPA2_PSK"; break;
                default: enc_type = "UNKNOWN"; break;
            }
            cJSON_AddStringToObject(ap_json, "encryption", enc_type);
            cJSON_AddItemToArray(payload, ap_json);
        }
        cJSON_AddItemToObject(root, "payload", payload);

        char* json_string = cJSON_PrintUnformatted(root);
        if (self->ble_provisioner_) {
            self->ble_provisioner_->send_data(json_string);
        }

        cJSON_free(json_string);
        cJSON_Delete(root);
        vTaskDelete(NULL);
    }, "wifi_scan_task", 4096, this, 5, NULL);
}