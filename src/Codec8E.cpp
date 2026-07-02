#include "Codec8E.h"
#include <math.h>

void Codec8E::appendUint8(std::vector<uint8_t>& vec, uint8_t val) {
    vec.push_back(val);
}

void Codec8E::appendUint16(std::vector<uint8_t>& vec, uint16_t val) {
    vec.push_back((val >> 8) & 0xFF);
    vec.push_back(val & 0xFF);
}

void Codec8E::appendUint32(std::vector<uint8_t>& vec, uint32_t val) {
    vec.push_back((val >> 24) & 0xFF);
    vec.push_back((val >> 16) & 0xFF);
    vec.push_back((val >> 8) & 0xFF);
    vec.push_back(val & 0xFF);
}

void Codec8E::appendUint64(std::vector<uint8_t>& vec, uint64_t val) {
    vec.push_back((val >> 56) & 0xFF);
    vec.push_back((val >> 48) & 0xFF);
    vec.push_back((val >> 40) & 0xFF);
    vec.push_back((val >> 32) & 0xFF);
    vec.push_back((val >> 24) & 0xFF);
    vec.push_back((val >> 16) & 0xFF);
    vec.push_back((val >> 8) & 0xFF);
    vec.push_back(val & 0xFF);
}

std::vector<uint8_t> Codec8E::buildLoginPacket(const String& imei) {
    std::vector<uint8_t> packet;
    uint16_t length = imei.length();
    appendUint16(packet, length);
    for (int i = 0; i < length; i++) {
        packet.push_back(imei.charAt(i));
    }
    return packet;
}

// CRC16-IBM implementation
uint16_t crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static uint16_t clampU16(float value) {
    if (!isfinite(value) || value <= 0.0f) return 0;
    if (value >= 65535.0f) return 65535;
    return static_cast<uint16_t>(lroundf(value));
}

static uint8_t gsmSignalLevel(int dbm) {
    if (dbm <= -111 || dbm == 0) return 0;
    if (dbm <= -101) return 1;
    if (dbm <= -91) return 2;
    if (dbm <= -81) return 3;
    if (dbm <= -71) return 4;
    return 5;
}

std::vector<uint8_t> Codec8E::buildAVLDataPacket(const std::vector<SystemState>& records) {
    std::vector<uint8_t> packet;
    std::vector<uint8_t> payload;

    uint8_t numRecords = records.size();

    // Codec ID
    appendUint8(payload, 0x8E);
    // Number of Data 1 (1 byte per Codec8E spec — only IO fields are extended to 2 bytes)
    appendUint8(payload, numRecords);

    for (const auto& record : records) {
        // Timestamp (8 bytes)
        appendUint64(payload, record.gps.utcTime);

        // Priority
        appendUint8(payload, 0); // Low priority

        // GPS Element
        int32_t longitude = (int32_t)(record.gps.longitude * 10000000.0);
        int32_t latitude = (int32_t)(record.gps.latitude * 10000000.0);
        appendUint32(payload, longitude);
        appendUint32(payload, latitude);
        appendUint16(payload, clampU16(record.gps.altitude));
        appendUint16(payload, clampU16(record.gps.course));
        appendUint8(payload, record.gps.gpsFixValid ? record.gps.satellites : 0);
        appendUint16(payload, clampU16(record.gps.speed));

        // IO Elements (Event IO ID, N of Total IO)
        appendUint16(payload, 0); // Event IO ID = 0 (No specific event)
        
        // Define IO elements list
        std::vector<IOElement> ioElements;
        ioElements.push_back({239, 1, record.accState ? 1ULL : 0ULL}); // Ignition
        // FIX-CRIT-05: ID 67 is battery percentage in this project's mapping,
        // not a duplicate voltage field.
        ioElements.push_back({66, 2, clampU16(record.vbatVoltage * 1000.0f)}); // External voltage (mV)
        ioElements.push_back({67, 1, record.vbatPercent}); // Battery level (%)
        ioElements.push_back({70, 2, (uint64_t)(int16_t)(record.temperature * 10)}); // Temperature (x10)
        ioElements.push_back({240, 1, gsmSignalLevel(record.rssi)}); // GSM signal 0..5
        ioElements.push_back({1, 1, record.accState ? 1ULL : 0ULL}); // DIN 1
        ioElements.push_back({69, 1, record.gps.gpsFixValid ? 1ULL : 0ULL}); // GNSS status
        ioElements.push_back({179, 4, record.totalMileage}); // Total mileage
        ioElements.push_back({181, 2, (uint64_t)(record.gps.pdop * 10)}); // PDOP
        ioElements.push_back({182, 2, (uint64_t)(record.gps.hdop * 10)}); // HDOP

        appendUint16(payload, ioElements.size()); // N of Total IO

        // N of 1-byte IO
        uint16_t n1 = 0;
        for (const auto& io : ioElements) if (io.length == 1) n1++;
        appendUint16(payload, n1);
        for (const auto& io : ioElements) {
            if (io.length == 1) {
                appendUint16(payload, io.id);
                appendUint8(payload, io.value);
            }
        }

        // N of 2-byte IO
        uint16_t n2 = 0;
        for (const auto& io : ioElements) if (io.length == 2) n2++;
        appendUint16(payload, n2);
        for (const auto& io : ioElements) {
            if (io.length == 2) {
                appendUint16(payload, io.id);
                appendUint16(payload, io.value);
            }
        }

        // N of 4-byte IO
        uint16_t n4 = 0;
        for (const auto& io : ioElements) if (io.length == 4) n4++;
        appendUint16(payload, n4);
        for (const auto& io : ioElements) {
            if (io.length == 4) {
                appendUint16(payload, io.id);
                appendUint32(payload, io.value);
            }
        }

        // N of 8-byte IO
        uint16_t n8 = 0;
        for (const auto& io : ioElements) if (io.length == 8) n8++;
        appendUint16(payload, n8);
        for (const auto& io : ioElements) {
            if (io.length == 8) {
                appendUint16(payload, io.id);
                appendUint64(payload, io.value);
            }
        }
        
        // N of X-byte IO
        appendUint16(payload, 0); // 0 X-byte IOs
    }

    // Number of Data 2 (1 byte per Codec8E spec)
    appendUint8(payload, numRecords);

    // Build final packet
    appendUint32(packet, 0); // 4 zero bytes
    appendUint32(packet, payload.size()); // Data field length
    packet.insert(packet.end(), payload.begin(), payload.end());
    
    // CRC
    uint16_t crc = crc16(payload.data(), payload.size());
    appendUint32(packet, crc);

    return packet;
}

bool Codec8E::parseCommand(const std::vector<uint8_t>& packet, String& outCommand) {
    outCommand = "";
    // FIX-CRIT-04: Strict Codec12 command parsing and CRC validation.
    if (packet.size() < 20 || packet[0] || packet[1] || packet[2] || packet[3]) return false;
    const uint32_t dataLength = (static_cast<uint32_t>(packet[4]) << 24) |
                                (static_cast<uint32_t>(packet[5]) << 16) |
                                (static_cast<uint32_t>(packet[6]) << 8) | packet[7];
    if (packet.size() != 8 + dataLength + 4 || dataLength < 8) return false;
    if (packet[8] != 0x0C || packet[9] != 1 || packet[10] != 0x05) return false;

    const uint32_t commandLength = (static_cast<uint32_t>(packet[11]) << 24) |
                                   (static_cast<uint32_t>(packet[12]) << 16) |
                                   (static_cast<uint32_t>(packet[13]) << 8) | packet[14];
    const size_t commandStart = 15;
    const size_t quantity2 = commandStart + commandLength;
    if (commandLength == 0 || quantity2 != 8 + dataLength - 1 || packet[quantity2] != 1) return false;

    const uint16_t expectedCrc = crc16(packet.data() + 8, dataLength);
    const size_t crcOffset = 8 + dataLength;
    const uint32_t receivedCrc = (static_cast<uint32_t>(packet[crcOffset]) << 24) |
                                 (static_cast<uint32_t>(packet[crcOffset + 1]) << 16) |
                                 (static_cast<uint32_t>(packet[crcOffset + 2]) << 8) |
                                 packet[crcOffset + 3];
    if (receivedCrc != expectedCrc) {
        Serial.println("Codec12: CRC mismatch");
        return false;
    }

    // Traccar custom commands can be configured as HEX. In that mode typing
    // "acc0" / "acc1" produces two binary bytes AC C0 / AC C1 instead of the
    // four ASCII bytes 61 63 63 30 / 31. Accept only these two explicit binary
    // aliases; arbitrary non-printable Codec12 payloads remain rejected.
    if (commandLength == 2 && packet[commandStart] == 0xAC &&
        (packet[commandStart + 1] == 0xC0 || packet[commandStart + 1] == 0xC1)) {
        outCommand = packet[commandStart + 1] == 0xC1 ? "acc1" : "acc0";
        Serial.println("Codec12: decoded HEX ACC command as " + outCommand);
        return true;
    }

    outCommand.reserve(commandLength);
    for (size_t i = commandStart; i < quantity2; ++i) {
        const char c = static_cast<char>(packet[i]);
        if (c < 32 || c > 126) return false;
        outCommand += c;
    }
    outCommand.trim();
    return !outCommand.isEmpty();
}

std::vector<uint8_t> Codec8E::buildCommandResponse(uint32_t responseId, const String& responseString) {
    (void)responseId; // Codec12 has no response-id field.
    // Traccar accepts normal Codec8/Codec8E response format, or we can just send the string back wrapped if needed.
    // To simplify, some trackers just send an AVL packet with a specific IO.
    // But since the requirement says "Phản hồi server bằng Codec8E Command Response", we will structure a Codec8E response (type 0x06).
    std::vector<uint8_t> packet;
    std::vector<uint8_t> payload;
    
    appendUint8(payload, 0x0C); // FIX-CRIT-04: Codec12 command response
    appendUint8(payload, 1); // Quantity 1
    
    appendUint8(payload, 0x06); // Type: Command Response
    appendUint32(payload, responseString.length()); // Command Size
    for (size_t i = 0; i < responseString.length(); i++) {
        payload.push_back(responseString.charAt(i));
    }
    
    appendUint8(payload, 1); // Quantity 2
    
    appendUint32(packet, 0); // Preamble
    appendUint32(packet, payload.size()); // Length
    packet.insert(packet.end(), payload.begin(), payload.end());
    
    uint16_t crc = crc16(payload.data(), payload.size());
    appendUint32(packet, crc);
    
    return packet;
}
