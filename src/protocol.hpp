#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <string>

// Builders
bool buildDevicePubkey(const std::string& curve, const std::string& pubB64, std::string& outJson);
bool buildReadyForKdfAck(std::string& outJson);
bool buildChallengeReply(const std::string& nonceB64, const std::string& hmacB64, std::string& outJson);

// Parsers
bool parseHello(const std::string& json, std::string& proto, std::string& curve, int& version);
bool parsePwaPubkey(const std::string& json, std::string& curve,
                    std::string& pubRawB64, std::string& saltB64, std::string& infoB64);

#endif // PROTOCOL_HPP
