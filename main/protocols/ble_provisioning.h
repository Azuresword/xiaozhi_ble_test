#ifndef BLE_PROVISIONING_H
#define BLE_PROVISIONING_H

#include <string>
#include <functional>
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"

// Forward declaration
struct ble_gatt_access_ctxt;
struct ble_gap_event;

class BleProvisioning {
public:
    BleProvisioning();
    ~BleProvisioning();

    void start();
    void stop();

    // Callback function, used to notify the application layer of provisioning results
    void on_provisioned(std::function<void(const std::string& ssid, const std::string& password)> cb);

    // Send data to the App (handles packet splitting)
    void send_data(const std::string& json_data);

    // --- Callbacks for NimBLE stack (must be public) ---
    static int gatt_svr_access_cb(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg);
    static int gap_event_cb(struct ble_gap_event *event, void *arg);
    static void ble_host_task(void *param);

    // --- Public handle for stack access ---
    uint16_t characteristic_val_handle_ = 0; // The handle will be populated by the stack

private:
    // Instance methods to handle specific events
    void handle_gap_event(struct ble_gap_event *event);
    void handle_gatt_write(uint16_t conn_handle, struct os_mbuf *om);
    void advertise();
    void on_sync();

    uint8_t own_addr_type_;
    uint16_t conn_handle_ = BLE_HS_CONN_HANDLE_NONE;
    
    std::function<void(const std::string&, const std::string&)> on_provisioned_cb_;
};

#endif // BLE_PROVISIONING_H