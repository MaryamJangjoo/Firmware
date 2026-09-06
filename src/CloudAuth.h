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

    bool loginOffline(const String& username, const String& password);
    bool verifyUserPassword(const String& username, const String& password);

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

    // برای loginOffline/verifyUserPassword نیاز به CloudStorage داره
    // (وابستگی رو یا از طریق تزریق std::function بگیر یا CloudStorage& بگیر)
};

#endif