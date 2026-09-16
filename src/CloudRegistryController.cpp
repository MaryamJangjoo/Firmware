#include "CloudRegistryController.h"

#include "CloudManager.h"
#include "ecosmart_registeries.h"
#include "cloud.hpp"
#include "Logging.h"

static const char* TAG = "CLOUD-REG-CTL";

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


bool CloudRegistryController::read(uint16_t regAddr,
                                   RawRegisterValue& outValue)
{
    if (regAddr == REG_ADD_CLOUD_PASSWORD) {
        outValue.datatype    = reg_datatype_string;
        outValue.isString    = true;
        outValue.stringValue = maskedPassword();
        outValue.byteLen     = 0;
        return true;
    }

    return RegistryControllerBase::read(regAddr, outValue);
}


void CloudRegistryController::onWrite(uint16_t regAddr)
{
    switch (regAddr) {
        case REG_ADD_CLOUD_SERVER_FQDN:
            store_.setServerFqdn(cloud_object.server_fqdn);
            applyToCloudManager(regAddr);
            break;

        case REG_ADD_CLOUD_SERVER_IP:
            store_.setServerIp(cloud_object.server_ip);
            applyToCloudManager(regAddr);
            break;

        case REG_ADD_CLOUD_SERVER_PORT:
            if (!store_.setServerPort(cloud_object.server_port)) {
             
                cloud_object.server_port = store_.getServerPort();
                break;
            }
            applyToCloudManager(regAddr);
            break;

        case REG_ADD_CLOUD_USERNAME:
            store_.setUsername(cloud_object.username);
            applyToCloudManager(regAddr);
            break;

        case REG_ADD_CLOUD_PASSWORD: {
            const String incoming = cloud_object.password;

            if (incoming == maskedPassword()) {
                ECOSMART_LOGW(TAG,
                    "Masked password echoed back, write ignored");
                cloud_object.password = maskedPassword();
                break;
            }

            if (incoming.isEmpty()) {
                ECOSMART_LOGW(TAG, "Empty password write ignored");
                cloud_object.password = maskedPassword();
                break;
            }

            store_.setPassword(incoming);

            cloud_object.password = maskedPassword();

            applyToCloudManager(regAddr);
            break;
        }

        case REG_ADD_CLOUD_DEVICE_ID:
            ECOSMART_LOGW(TAG, "DeviceId is read-only, write ignored");
            cloud_object.device_id = store_.getDeviceId();
            break;

        default:
            break;
    }
}


void CloudRegistryController::rebuildAndApplyUrl()
{
    if (cloudManager_ == nullptr) return;

    const String url = store_.buildApiBaseUrl();
    if (url.isEmpty()) {
        ECOSMART_LOGW(TAG,
            "No host configured, keeping current apiBaseUrl");
        return;
    }

    ECOSMART_LOGI(TAG, "Applying new apiBaseUrl: %s", url.c_str());
    cloudManager_->setApiBaseUrl(url);
}

void CloudRegistryController::applyToCloudManager(uint16_t regAddr)
{
    if (cloudManager_ == nullptr) {
        ECOSMART_LOGW(TAG, "No CloudManager attached, change not applied");
        return;
    }

    switch (regAddr) {
        case REG_ADD_CLOUD_SERVER_FQDN:
        case REG_ADD_CLOUD_SERVER_IP:
        case REG_ADD_CLOUD_SERVER_PORT:
            rebuildAndApplyUrl();
            break;

        case REG_ADD_CLOUD_USERNAME:
        case REG_ADD_CLOUD_PASSWORD:
            ECOSMART_LOGI(TAG,
                "Credentials updated; new values take effect "
                "on next login/handshake");
            break;

        default:
            break;
    }
}


void CloudRegistryController::mirrorStoreIntoRegistry()
{
    cloud_object.server_fqdn = store_.getServerFqdn();
    cloud_object.server_ip   = store_.getServerIp();
    cloud_object.server_port = store_.getServerPort();
    cloud_object.username    = store_.getUsername();
    cloud_object.device_id   = store_.getDeviceId();
    cloud_object.password    = maskedPassword();
}

void CloudRegistryController::applyStoredConfig()
{
    if (cloudManager_ == nullptr) {
        ECOSMART_LOGW(TAG,
            "applyStoredConfig() called before attachCloudManager()");
        return;
    }

    mirrorStoreIntoRegistry();

    if (!store_.isConfigured()) {
        ECOSMART_LOGI(TAG,
            "NVS holds no server config, keeping built-in default URL");
        return;
    }

    const String url = store_.buildApiBaseUrl();
    if (url.isEmpty()) {
        ECOSMART_LOGW(TAG,
            "buildApiBaseUrl() empty despite isConfigured()==true");
        return;
    }

    ECOSMART_LOGI(TAG, "Applying stored apiBaseUrl: %s", url.c_str());
    cloudManager_->setApiBaseUrl(url);
}

void CloudRegistryController::syncDeviceIdFromCloudManager()
{
    if (cloudManager_ == nullptr) return;

    const String id = cloudManager_->getDeviceId();
    if (id.isEmpty()) {
        ECOSMART_LOGW(TAG, "CloudManager returned empty deviceId");
        return;
    }

    store_.setDeviceIdFromCloudManager(id);
    cloud_object.device_id = store_.getDeviceId();
}