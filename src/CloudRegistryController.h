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
//   - Password is returned masked as "****" (writable but
//     never readable in clear text via the WS channel).
//
// Write behavior:
//   - Each write updates CloudRegistryStore immediately.
//   - onWrite() then propagates the change to CloudManager when
//     the affected field impacts the backend connection (IP,
//     Port, Username, Password).
// ============================================================

class CloudManager;

class CloudRegistryController : public RegistryControllerBase {
public:
    explicit CloudRegistryController(CloudRegistryStore& store);

    // Called once after CloudManager is constructed so the
    // controller can push API URL / credential changes back
    // into the cloud layer.
    void attachCloudManager(CloudManager* mgr) { cloudManager_ = mgr; }

    // Mirror the current deviceId from CloudManager into the
    // store. Called by AppController once at startup.
    void syncDeviceIdFromCloudManager();

protected:
    std::vector<Registery_t*> getCandidates() override;
    void onWrite(uint16_t regAddr) override;

    bool read(uint16_t regAddr, RawRegisterValue& outValue) override;

private:
    void applyToCloudManager(uint16_t regAddr);
    String maskedPassword() const;

    CloudRegistryStore& store_;
    CloudManager*       cloudManager_ = nullptr;
};

#endif