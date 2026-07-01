#ifndef CODEC8E_H
#define CODEC8E_H

#include <Arduino.h>
#include "SystemState.h"
#include <vector>

// Teltonika IO Property Types
struct IOElement {
    uint16_t id;
    uint8_t length; // 1, 2, 4, 8
    uint64_t value;
};

class Codec8E {
public:
    static std::vector<uint8_t> buildLoginPacket(const String& imei);
    static std::vector<uint8_t> buildAVLDataPacket(const std::vector<SystemState>& records);
    static bool parseCommand(const std::vector<uint8_t>& packet, String& outCommand);
    static std::vector<uint8_t> buildCommandResponse(uint32_t responseId, const String& responseString);

private:
    static void appendUint8(std::vector<uint8_t>& vec, uint8_t val);
    static void appendUint16(std::vector<uint8_t>& vec, uint16_t val);
    static void appendUint32(std::vector<uint8_t>& vec, uint32_t val);
    static void appendUint64(std::vector<uint8_t>& vec, uint64_t val);
};

#endif // CODEC8E_H
