#ifndef CLOUD_REGISTRY_CONTROLLER_H
#define CLOUD_REGISTRY_CONTROLLER_H

#include <Arduino.h>
#include <vector>

#include "RegistryControllerBase.h"
#include "CloudRegistryStore.h"

// ============================================================
// CloudRegistryController
//
// Exposes Cloud Connectivity registers to the registry
// framework (WS read/write, mYBUS forwarding, etc.) using the
// same pattern as the Audio/RGB/Curtain/Outputs controllers.
//
// Read behavior:
//   - All string registers are returned as RegRawType::STRING.
//   - ServerPort is returned as RegRawType::UINT16.
//   - DeviceId is returned as RegRawType::STRING.
//   - Password is returned masked as "****" and is never
//     readable in clear text via the WS channel.
//
// Write behavior:
//   - Each write updates CloudRegistryStore immediately.
//   - onWrite() then propagates the change to CloudManager when
//     the affected field impacts the backend connection
//     (FQDN, IP, Port, Username, Password).
//   - A write of the literal mask "****" to the password
//     register is rejected, so a read-all/write-all client
//     cannot destroy the stored secret.
//
// Startup order (AppController):
//   1. store.begin()
//   2. ctl.attachCloudManager(&cloudManager)
//   3. ctl.applyStoredConfig()          <-- before login()
//   4. cloudManager.login() / handshake
//   5. ctl.syncDeviceIdFromCloudManager()
// ============================================================

class CloudManager;

class CloudRegistryController : public RegistryControllerBase {
public:
    explicit CloudRegistryController(CloudRegistryStore& store);

    void attachCloudManager(CloudManager* mgr) { cloudManager_ = mgr; }
   
    void applyStoredConfig();

    void syncDeviceIdFromCloudManager();

protected:
    std::vector<Registery_t*> getCandidates() override;
    void onWrite(uint16_t regAddr) override;

    bool read(uint16_t regAddr, RawRegisterValue& outValue) override;

private:
    void   applyToCloudManager(uint16_t regAddr);
    void   rebuildAndApplyUrl();
    void   mirrorStoreIntoRegistry();
    String maskedPassword() const;

    CloudRegistryStore& store_;
    CloudManager*       cloudManager_ = nullptr;
};

#endif 