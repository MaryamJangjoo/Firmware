#ifndef CLOUD_STORAGE_H
#define CLOUD_STORAGE_H

#include <Arduino.h>
#include <vector>
#include <FS.h>

struct UserInfo {
    String username;
    String passwordHash;
    String publicKey;
    String role;
    uint32_t lastLogin = 0;
};

struct SiteInfo {
    String siteId;
    String siteName;
    String licenseKey;
    uint32_t expiryDate = 0;
    uint32_t maxUsers = 10;
};

class CloudStorage {
public:
    bool init();
    bool isMounted() const;

    bool readFile(const String& path, String& content);
    bool writeFile(const String& path, const String& content);
    void listFiles();
    void printFileContent(const String& path);

    bool loadUsers(std::vector<UserInfo>& users);
    bool saveUsers(const std::vector<UserInfo>& users);
    bool loadSiteInfo(SiteInfo& info);
    bool saveSiteInfo(const SiteInfo& info);

    bool loginOffline(const String& username, const String& password);
    bool verifyUserPassword(const String& username, const String& password);

    bool createDefaultUsersFile();
    bool createDefaultSiteInfoFile();

private:
    bool mounted_ = false;
};

#endif