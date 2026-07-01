#ifndef BACKLOG_MANAGER_H
#define BACKLOG_MANAGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <vector>
#include "SystemState.h"
#include "config.h"

class BacklogManager {
public:
    BacklogManager();
    bool begin();

    bool pushRecord(const SystemState& record);
    std::vector<SystemState> peekRecords(uint8_t count);
    bool commitRecords(uint8_t count);

    uint32_t getRecordCount();
    bool getTimeRange(uint64_t& oldestUtcTime, uint64_t& newestUtcTime);
    void clearAll();
    bool isReady() const { return _ready; }

private:
    struct QueueMeta {
        uint32_t magic;
        uint32_t version;
        uint32_t recordSize;
        uint32_t capacity;
        uint32_t head;
        uint32_t count;
        uint32_t sequence;
        uint32_t checksum;
    };

    const char* _filename = "/backlog.dat";
    const char* _metaFiles[2] = {"/backlog.a.meta", "/backlog.b.meta"};
    uint32_t _recordCount = 0;
    uint32_t _head = 0;
    uint32_t _sequence = 0;
    int8_t _activeMeta = -1;
    bool _ready = false;
    SemaphoreHandle_t _backlogMutex = nullptr;

    bool loadMetadata();
    bool saveMetadata();
    bool readMetadataSlot(uint8_t slot, QueueMeta& meta);
    uint32_t metadataChecksum(const QueueMeta& meta) const;
    bool initializeOrMigrate();
};

#endif
