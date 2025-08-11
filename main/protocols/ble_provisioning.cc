#include "ble_provisioning.h"
#include "esp_log.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs_mbuf.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/util/util.h"

#include "cJSON.h"
static const char *TAG = "BleProvisioning";

// UUIDs from your specification
static const ble_uuid128_t gatt_svr_svc_uuid =
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xAA, 0xAA, 0x00, 0x00);

static const ble_uuid128_t gatt_svr_chr_uuid =
    BLE_UUID128_INIT(0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xB1, 0xBB, 0x00, 0x00);

// C-style pointer to our instance to be used in static callbacks

static BleProvisioning* instance_ptr = nullptr;

static uint16_t gatt_svr_chr_val_handle;

// --- CORRECTED SECTION ---
// First, define the characteristics array as a separate static const variable.
static const struct ble_gatt_chr_def gatt_svr_characteristics[] = {
    {
        .uuid = &gatt_svr_chr_uuid.u,
        .access_cb = BleProvisioning::gatt_svr_access_cb,
        .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
        // This is now safe because instance_ptr is set before this struct is used by the stack.
        .val_handle = &gatt_svr_chr_val_handle
    },
    {0} /* End of characteristics */
};

// Now, define the service and point it to the static characteristics array.
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &gatt_svr_svc_uuid.u,
        .characteristics = gatt_svr_characteristics // Point to the static array
    },
    {0} /* End of services */
};
// --- END OF CORRECTION ---

BleProvisioning::BleProvisioning() {
    instance_ptr = this; // Store instance pointer for static callbacks
}

BleProvisioning::~BleProvisioning() {
    instance_ptr = nullptr;
}

void BleProvisioning::start() {
    ESP_LOGI(TAG, "Starting BLE Provisioning Service");
    nimble_port_init();

    // Configure the host callbacks
    ble_hs_cfg.sync_cb = []() { if(instance_ptr) instance_ptr->on_sync(); };
    ble_hs_cfg.reset_cb = [](int reason) { ESP_LOGE(TAG, "Resetting state; reason=%d", reason); };
    
    // Initialize services
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return;
    }

    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return;
    }
    
    // Set the device name
    rc = ble_svc_gap_device_name_set("XZP");
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set device name: %d", rc);
    }
    
    nimble_port_freertos_init(ble_host_task);
}

void BleProvisioning::stop() {
    int ret = nimble_port_stop();
    if (ret == 0) {
        nimble_port_deinit();
        ESP_LOGI(TAG, "BLE Provisioning Service stopped");
    }
}

void BleProvisioning::ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE Host Task Started");
    nimble_port_run();
    // This function will not return, the task will be deleted after nimble_port_stop()
    nimble_port_freertos_deinit();
}

void BleProvisioning::on_sync() {
    ESP_LOGI(TAG, "BLE Host synced");
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &own_addr_type_);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }
    advertise();
}

void BleProvisioning::advertise() {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    const char *name;
    int rc;

    memset(&fields, 0, sizeof fields);

    /* Advertise two things:
     * - General discoverability.
     * - Our name.
     */
    fields.flags = BLE_HS_ADV_F_DISC_GEN |
                   BLE_HS_ADV_F_BREDR_UNSUP;

    /* Include our complete name in the advertising packet */
    name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "error setting advertising data; rc=%d", rc);
        return;
    }

    /* Begin advertising. */
    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(own_addr_type_, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event_cb, this);
    if (rc != 0) {
        ESP_LOGE(TAG, "error enabling advertising; rc=%d", rc);
        return;
    }
     ESP_LOGI(TAG, "Advertising started with name: %s", name);
}

int BleProvisioning::gap_event_cb(struct ble_gap_event *event, void *arg) {
    // The 'arg' here is the 'this' pointer we passed in ble_gap_adv_start
    BleProvisioning* self = static_cast<BleProvisioning*>(arg);
    if (self) {
        self->handle_gap_event(event);
    }
    return 0;
}

void BleProvisioning::handle_gap_event(struct ble_gap_event *event) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "Connection established; status=%d", event->connect.status);
            if (event->connect.status == 0) {
                conn_handle_ = event->connect.conn_handle;
            } else {
                advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
            conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
            advertise();
            break;
        case BLE_GAP_EVENT_MTU:
            ESP_LOGI(TAG, "MTU updated; conn_handle=%d, mtu=%d", event->mtu.conn_handle, event->mtu.value);
            break;
        default:
            break;
    }
}

int BleProvisioning::gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    // *** 修改点 3: 使用静态句柄进行比较 ***
    if (attr_handle == gatt_svr_chr_val_handle) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            ESP_LOGI(TAG, "Received write request on characteristic");
            if (instance_ptr) {
                instance_ptr->handle_gatt_write(conn_handle, ctxt->om);
            }
        }
    }
    return 0;
}

void BleProvisioning::handle_gatt_write(uint16_t conn_handle, struct os_mbuf *om) {
    uint16_t len = OS_MBUF_PKTLEN(om);
    char* data = (char*)malloc(len + 1);
    if (!data) return;

    if (ble_hs_mbuf_to_flat(om, data, len, NULL) == 0) {
        data[len] = '\0';
        ESP_LOGI(TAG, "Data received: %s", data);

        // 使用cJSON库解析收到的字符串
        cJSON *root = cJSON_Parse(data);
        if (root) {
            cJSON *ssid_item = cJSON_GetObjectItem(root, "ssid");
            cJSON *password_item = cJSON_GetObjectItem(root, "password");

            // 如果成功解析出ssid和password，并且回调函数已经设置
            if (ssid_item && password_item && on_provisioned_cb_) {
                // 就调用回调函数，把凭据传递出去
                on_provisioned_cb_(ssid_item->valuestring, password_item->valuestring);
            }
            cJSON_Delete(root);
        }
    }
    free(data);
}

void BleProvisioning::on_provisioned(std::function<void(const std::string& ssid, const std::string& password)> cb) {
    on_provisioned_cb_ = cb;
}

void BleProvisioning::send_data(const std::string& json_data) {
    if (conn_handle_ == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "Not connected, cannot send data.");
        return;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(json_data.c_str(), json_data.length());
    if (!om) {
        ESP_LOGE(TAG, "Failed to allocate mbuf for notification.");
        return;
    }

    int rc = ble_gatts_notify_custom(conn_handle_, gatt_svr_chr_val_handle, om);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to send notification: %d", rc);
    } else {
        ESP_LOGI(TAG, "Notification sent.");
    }
}
