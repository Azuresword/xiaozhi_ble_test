#ifndef WIFI_BOARD_H
#define WIFI_BOARD_H

#include "board.h"
#include "protocols/ble_provisioning.h"

class WifiBoard : public Board {
protected:
    bool wifi_config_mode_ = false;

    WifiBoard();
    void EnterWifiConfigMode();
    virtual std::string GetBoardJson() override;
    std::unique_ptr<BleProvisioning> ble_provisioner_;

public:
    virtual std::string GetBoardType() override;
    virtual bool StartNetwork() override;
    virtual Http* CreateHttp() override;
    virtual WebSocket* CreateWebSocket() override;
    virtual Mqtt* CreateMqtt() override;
    virtual Udp* CreateUdp() override;
    virtual const char* GetNetworkStateIcon() override;
    virtual void SetPowerSaveMode(bool enabled) override;
    virtual void ResetWifiConfiguration();
};

#endif // WIFI_BOARD_H
