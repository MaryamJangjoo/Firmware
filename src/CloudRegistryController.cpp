#include "CloudRegistryController.h"

#include "CloudManager.h"
#include "ecosmart_registeries.h"
#include "cloud.hpp"

CloudRegistryController::CloudRegistryController(CloudRegistryStore& store)
    : store_(store)
{
}

std::vector<Registery_t*> CloudRegistryController::getCandidates()
{
    return {
        &reg_module_cloud.server_fqdn,
        &reg_module_cloud.server_ip,
        &reg_module_cloud.server_port,
        &reg_module_cloud.device_id,
        &reg_module_cloud.username,
        &reg_module_cloud.password,
    };
}

String CloudRegistryController::maskedPassword() const
{
    return "****";
}

bool CloudRegistryController::read(uint16_t regAddr, RawRegisterValue& outValue)
{
    if (regAddr == REG_ADD_CLOUD_PASSWORD) {
        outValue.datatype = reg_datatype_string;
        outValue.isString = true;
        outValue.stringValue = maskedPassword();
        outValue.byteLen = 0;
        return true;
    }

    return RegistryControllerBase::read(regAddr, outValue);
}

void CloudRegistryController::onWrite(uint16_t regAddr)
{
    // The base class write() has already updated the corresponding
    // field in cloud_object (the process image). Copy the new value
    // into the persistent store so it survives reboot, then push
    // connection-impacting fields to CloudManager.
    switch (regAddr) {
        case REG_ADD_CLOUD_SERVER_FQDN:
            store_.setServerFqdn(cloud_object.server_fqdn);
            break;

        case REG_ADD_CLOUD_SERVER_IP:
            store_.setServerIp(cloud_object.server_ip);
            applyToCloudManager(regAddr);
            break;

        case REG_ADD_CLOUD_SERVER_PORT:
            store_.setServerPort(cloud_object.server_port);
            applyToCloudManager(regAddr);
            break;

        case REG_ADD_CLOUD_USERNAME:
            store_.setUsername(cloud_object.username);
            applyToCloudManager(regAddr);
            break;

        case REG_ADD_CLOUD_PASSWORD:
            store_.setPassword(cloud_object.password);
            applyToCloudManager(regAddr);
            break;

        default:
            break;
    }
}

void CloudRegistryController::applyToCloudManager(uint16_t regAddr)
{
    if (cloudManager_ == nullptr) return;

    switch (regAddr) {
        case REG_ADD_CLOUD_SERVER_FQDN:
            Serial.printf("[CLOUD-REG] FQDN changed -> '%s' (informational)\n",
                          store_.getServerFqdn().c_str());
            break;

        case REG_ADD_CLOUD_SERVER_IP:
        case REG_ADD_CLOUD_SERVER_PORT: {
            const String url = store_.buildApiBaseUrl();
            if (!url.isEmpty()) {
                Serial.printf("[CLOUD-REG] Applying new apiBaseUrl: %s\n",
                              url.c_str());
                cloudManager_->setApiBaseUrl(url);
            }
            break;
        }

        case REG_ADD_CLOUD_USERNAME:
        case REG_ADD_CLOUD_PASSWORD:
            Serial.println(
                "[CLOUD-REG] Credentials updated; new values take effect "
                "on next login/handshake");
            break;

        default:
            break;
    }
}

void CloudRegistryController::syncDeviceIdFromCloudManager()
{
    if (cloudManager_ == nullptr) return;

    const String id = cloudManager_->getDeviceId();
    if (!id.isEmpty()) {
        store_.setDeviceIdFromCloudManager(id);
    }
}