#ifndef CLOUD_AUTH_H
#define CLOUD_AUTH_H

#include <Arduino.h>
#include <Preferences.h>
#include "HttpTransport.h"

class CloudAuth {
public:
    CloudAuth(HttpTransport& transport, Preferences& prefs,
              String& deviceId, String& jwtToken);

    bool loginUser(const String& username, const String& password,
                   const String& requestedDeviceId);
    bool refreshToken();
    bool isLoggedIn() const;
    bool isTokenValid() const;

    String loadToken();
    void saveToken(const String& token);

    String getSiteId() const { return siteId_; }

private:
    HttpTransport& transport_;
    Preferences& preferences_;
    String& deviceId_;
    String& jwtToken_;
    String refreshTokenValue_;
    String siteId_;
    bool isAuthenticated_ = false;

    // ⚠️ توجه: پیاده‌سازی loginOffline/verifyUserPassword از این کلاس حذف شد.
    // این دو متد قبلاً همیشه false برمی‌گرداندند چون CloudAuth هیچ ارجاعی
    // به CloudStorage نداشت. منطق واقعی offline-login در CloudStorage است
    // و CloudManager::loginOffline مستقیماً از storage_.loginOffline()
    // استفاده می‌کند. اگر نیاز به offline-login از این کلاس بود، باید
    // CloudStorage& را به سازنده تزریق کرد.
};

#endif