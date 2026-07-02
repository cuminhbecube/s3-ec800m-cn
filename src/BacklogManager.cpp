#include "BacklogManager.h"
#include <esp_partition.h>

// Persistent ABI. Append fields only by introducing a new metadata version and
// an explicit migration; never silently change this layout.
struct StoredRecord {
    float lat, lon, speed, course, alt;
    uint8_t sats;
    float pdop, hdop;
    uint64_t utcTime;
    float vbatVoltage;
    uint8_t vbatPercent;
    bool accState;
    bool isValid;
    uint32_t totalMileage;
    int rssi;
    float temperature;
};

static constexpr uint32_t QUEUE_MAGIC = 0x53334751; // "S3GQ"
static constexpr uint32_t QUEUE_VERSION = 1;
static constexpr uint32_t RAW_BACKUP_MAGIC = 0x4C465342; // "LFSB"
static constexpr size_t RAW_BACKUP_DATA_OFFSET = 4096;

struct RawBackupHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t sourceSize;
    uint32_t checksum;
};

static bool backupCorruptedLittleFS(const esp_partition_t* source) {
    const esp_partition_t* destination = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40), "storage2");
    if (!source || !destination || destination->size < source->size + RAW_BACKUP_DATA_OFFSET) {
        Serial.println("LittleFS recovery: storage2 backup partition unavailable.");
        return false;
    }

    RawBackupHeader existing{};
    if (esp_partition_read(destination, 0, &existing, sizeof(existing)) == ESP_OK &&
        existing.magic == RAW_BACKUP_MAGIC && existing.version == 1 &&
        existing.sourceSize == source->size) {
        Serial.println("LittleFS recovery: preserving existing raw backup in storage2.");
        return true;
    }

    const size_t eraseSize = (source->size + RAW_BACKUP_DATA_OFFSET + 4095) & ~static_cast<size_t>(4095);
    if (esp_partition_erase_range(destination, 0, eraseSize) != ESP_OK) {
        Serial.println("LittleFS recovery: failed to erase storage2 backup area.");
        return false;
    }

    uint8_t* buffer = static_cast<uint8_t*>(malloc(4096));
    if (!buffer) return false;
    uint32_t checksum = 2166136261UL;
    bool ok = true;
    for (size_t offset = 0; offset < source->size && ok; offset += 4096) {
        const size_t length = min(static_cast<size_t>(4096),
                                  static_cast<size_t>(source->size - offset));
        ok = esp_partition_read(source, offset, buffer, length) == ESP_OK;
        for (size_t i = 0; i < length && ok; ++i) {
            checksum ^= buffer[i];
            checksum *= 16777619UL;
        }
        if (ok) {
            ok = esp_partition_write(destination, RAW_BACKUP_DATA_OFFSET + offset, buffer, length) == ESP_OK;
        }
    }
    free(buffer);
    if (!ok) {
        Serial.println("LittleFS recovery: raw backup copy failed; refusing to format.");
        return false;
    }

    // Header is written last, so its presence proves the raw copy completed.
    const RawBackupHeader header{RAW_BACKUP_MAGIC, 1, source->size, checksum};
    if (esp_partition_write(destination, 0, &header, sizeof(header)) != ESP_OK) return false;
    Serial.printf("LittleFS recovery: backed up %u bytes to storage2 (checksum %08X).\n",
                  source->size, checksum);
    return true;
}

BacklogManager::BacklogManager() {}

uint32_t BacklogManager::metadataChecksum(const QueueMeta& meta) const {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&meta);
    uint32_t hash = 2166136261UL; // FNV-1a, enough to detect torn metadata writes.
    for (size_t i = 0; i < sizeof(QueueMeta) - sizeof(meta.checksum); ++i) {
        hash ^= bytes[i];
        hash *= 16777619UL;
    }
    return hash;
}

bool BacklogManager::readMetadataSlot(uint8_t slot, QueueMeta& meta) {
    File f = LittleFS.open(_metaFiles[slot], "r");
    if (!f) return false;
    const bool fullRead = f.read(reinterpret_cast<uint8_t*>(&meta), sizeof(meta)) == sizeof(meta);
    f.close();
    return fullRead && meta.magic == QUEUE_MAGIC && meta.version == QUEUE_VERSION &&
           meta.recordSize == sizeof(StoredRecord) && meta.capacity == MAX_BACKLOG_RECORDS &&
           meta.head < MAX_BACKLOG_RECORDS && meta.count <= MAX_BACKLOG_RECORDS &&
           meta.checksum == metadataChecksum(meta);
}

bool BacklogManager::loadMetadata() {
    QueueMeta a{}, b{};
    const bool validA = readMetadataSlot(0, a);
    const bool validB = readMetadataSlot(1, b);
    if (!validA && !validB) return false;

    QueueMeta selected;
    if (validA && validB) {
        // Signed subtraction handles uint32 sequence wrap correctly.
        if (static_cast<int32_t>(b.sequence - a.sequence) > 0) {
            selected = b;
            _activeMeta = 1;
        } else {
            selected = a;
            _activeMeta = 0;
        }
    } else if (validA) {
        selected = a;
        _activeMeta = 0;
    } else {
        selected = b;
        _activeMeta = 1;
    }

    _head = selected.head;
    _recordCount = selected.count;
    _sequence = selected.sequence;
    return true;
}

bool BacklogManager::saveMetadata() {
    // FIX-CRIT-02: Alternate between two complete metadata copies. A reset or
    // brownout can tear the new slot, but the previous acknowledged state stays valid.
    const uint8_t target = (_activeMeta == 0) ? 1 : 0;
    QueueMeta meta{QUEUE_MAGIC, QUEUE_VERSION, sizeof(StoredRecord), MAX_BACKLOG_RECORDS,
                   _head, _recordCount, _sequence + 1, 0};
    meta.checksum = metadataChecksum(meta);

    File f = LittleFS.open(_metaFiles[target], "w");
    if (!f) return false;
    const bool written = f.write(reinterpret_cast<const uint8_t*>(&meta), sizeof(meta)) == sizeof(meta);
    f.flush();
    f.close();
    if (!written) return false;

    QueueMeta verify{};
    if (!readMetadataSlot(target, verify) || verify.sequence != meta.sequence) return false;
    _sequence = meta.sequence;
    _activeMeta = target;
    return true;
}

bool BacklogManager::initializeOrMigrate() {
    if (loadMetadata()) return true;

    _head = 0;
    _recordCount = 0;
    _sequence = 0;
    _activeMeta = -1;

    if (LittleFS.exists(_filename)) {
        File old = LittleFS.open(_filename, "r");
        if (!old) return false;
        const size_t size = old.size();
        old.close();
        // Migrate the former append-only layout in place.
        if (size % sizeof(StoredRecord) != 0 || size / sizeof(StoredRecord) > MAX_BACKLOG_RECORDS) {
            Serial.println("Backlog layout invalid; refusing to format or delete it.");
            return false;
        }
        _recordCount = size / sizeof(StoredRecord);
    } else {
        File data = LittleFS.open(_filename, "w");
        if (!data) return false;
        data.close();
    }
    return saveMetadata();
}

bool BacklogManager::begin() {
    _backlogMutex = xSemaphoreCreateMutex();
    if (!_backlogMutex) return false;
    // Arduino-ESP32 3.x has the maintained LittleFS component and native
    // LittleFS partition subtype support.
    if (!LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        // A factory-new partition is all 0xFF and may be formatted once. Any
        // non-erased mount failure is treated as recoverable corruption and is
        // deliberately left untouched.
        // Find by label, accepting both the legacy SPIFFS subtype and the
        // corrected LittleFS subtype during migration.
        const esp_partition_t* partition = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "littlefs");
        uint8_t probe[256] = {};
        bool erased = partition && esp_partition_read(partition, 0, probe, sizeof(probe)) == ESP_OK;
        for (uint8_t byte : probe) erased = erased && byte == 0xFF;
        if (!erased && !backupCorruptedLittleFS(partition)) {
            Serial.println("Filesystem mount failed; backup failed, existing data was not formatted.");
            return false;
        }
        // With the raw image safely preserved, erase the exact flash range.
        // This avoids esp_littlefs_format failing while parsing corrupt metadata.
        const esp_err_t eraseResult = esp_partition_erase_range(partition, 0, partition->size);
        if (eraseResult != ESP_OK) {
            Serial.printf("LittleFS partition erase failed: %s (%d).\n",
                          esp_err_to_name(eraseResult), eraseResult);
            return false;
        }

        uint8_t* verify = static_cast<uint8_t*>(malloc(4096));
        bool erasedVerified = verify != nullptr;
        for (size_t offset = 0; offset < 8192 && erasedVerified; offset += 4096) {
            erasedVerified = esp_partition_read(partition, offset, verify, 4096) == ESP_OK;
            for (size_t i = 0; i < 4096 && erasedVerified; ++i) erasedVerified = verify[i] == 0xFF;
        }
        free(verify);
        if (!erasedVerified) {
            Serial.println("LittleFS erase verification failed; refusing to format.");
            return false;
        }
        if (!LittleFS.begin(true, "/littlefs", 10, "littlefs")) {
            Serial.println("LittleFS recovery format/mount failed.");
            return false;
        }
        Serial.println(erased ? "LittleFS initialized on erased partition."
                              : "LittleFS recovered after raw backup to storage2.");
    }
    _ready = initializeOrMigrate();
    return _ready;
}

bool BacklogManager::pushRecord(const SystemState& state) {
    if (!_ready || xSemaphoreTake(_backlogMutex, portMAX_DELAY) != pdTRUE) return false;
    if (_recordCount >= MAX_BACKLOG_RECORDS) {
        xSemaphoreGive(_backlogMutex);
        return false; // Preserve oldest unsent records; never overwrite unacknowledged data.
    }

    StoredRecord rec{};
    rec.lat = state.gps.latitude;
    rec.lon = state.gps.longitude;
    rec.speed = state.gps.speed;
    rec.course = state.gps.course;
    rec.alt = state.gps.altitude;
    rec.sats = state.gps.gpsFixValid ? state.gps.satellites : 0;
    rec.pdop = state.gps.pdop;
    rec.hdop = state.gps.hdop;
    rec.utcTime = state.gps.utcTime;
    rec.vbatVoltage = state.vbatVoltage;
    rec.vbatPercent = state.vbatPercent;
    rec.accState = state.accState;
    rec.isValid = state.gps.gpsFixValid;
    rec.totalMileage = state.totalMileage;
    rec.rssi = state.rssi;
    rec.temperature = state.temperature;

    const uint32_t index = (_head + _recordCount) % MAX_BACKLOG_RECORDS;
    File f = LittleFS.open(_filename, "r+");
    if (!f || !f.seek(static_cast<size_t>(index) * sizeof(StoredRecord), SeekSet)) {
        if (f) f.close();
        xSemaphoreGive(_backlogMutex);
        return false;
    }
    const bool written = f.write(reinterpret_cast<const uint8_t*>(&rec), sizeof(rec)) == sizeof(rec);
    f.flush();
    f.close();
    if (!written) {
        xSemaphoreGive(_backlogMutex);
        return false;
    }

    ++_recordCount;
    if (!saveMetadata()) {
        --_recordCount; // Stray data is safely overwritten by the next push.
        xSemaphoreGive(_backlogMutex);
        return false;
    }
    xSemaphoreGive(_backlogMutex);
    return true;
}

std::vector<SystemState> BacklogManager::peekRecords(uint8_t count) {
    std::vector<SystemState> records;
    if (!_ready || xSemaphoreTake(_backlogMutex, portMAX_DELAY) != pdTRUE) return records;
    count = min<uint32_t>(count, _recordCount);
    records.reserve(count);

    File f = LittleFS.open(_filename, "r");
    if (!f) {
        xSemaphoreGive(_backlogMutex);
        return records;
    }
    for (uint8_t i = 0; i < count; ++i) {
        const uint32_t index = (_head + i) % MAX_BACKLOG_RECORDS;
        StoredRecord rec{};
        if (!f.seek(static_cast<size_t>(index) * sizeof(StoredRecord), SeekSet) ||
            f.read(reinterpret_cast<uint8_t*>(&rec), sizeof(rec)) != sizeof(rec)) break;

        SystemState s;
        s.gps.latitude = rec.lat;
        s.gps.longitude = rec.lon;
        s.gps.speed = rec.speed;
        s.gps.course = rec.course;
        s.gps.altitude = rec.alt;
        s.gps.satellites = rec.isValid ? rec.sats : 0;
        s.gps.pdop = rec.pdop;
        s.gps.hdop = rec.hdop;
        s.gps.utcTime = rec.utcTime;
        s.gps.gpsFixValid = rec.isValid;
        s.vbatVoltage = rec.vbatVoltage;
        s.vbatPercent = rec.vbatPercent;
        s.accState = rec.accState;
        s.totalMileage = rec.totalMileage;
        s.rssi = rec.rssi;
        s.temperature = rec.temperature;
        records.push_back(s);
    }
    f.close();
    xSemaphoreGive(_backlogMutex);
    return records;
}

bool BacklogManager::commitRecords(uint8_t count) {
    if (!_ready || xSemaphoreTake(_backlogMutex, portMAX_DELAY) != pdTRUE) return false;
    count = min<uint32_t>(count, _recordCount);
    if (count == 0) {
        xSemaphoreGive(_backlogMutex);
        return true;
    }

    const uint32_t oldHead = _head;
    const uint32_t oldCount = _recordCount;
    _head = (_head + count) % MAX_BACKLOG_RECORDS;
    _recordCount -= count;
    if (!saveMetadata()) {
        _head = oldHead;
        _recordCount = oldCount;
        xSemaphoreGive(_backlogMutex);
        return false;
    }
    xSemaphoreGive(_backlogMutex);
    return true;
}

uint32_t BacklogManager::getRecordCount() {
    if (!_ready || xSemaphoreTake(_backlogMutex, portMAX_DELAY) != pdTRUE) return 0;
    const uint32_t count = _recordCount;
    xSemaphoreGive(_backlogMutex);
    return count;
}

bool BacklogManager::getTimeRange(uint64_t& oldestUtcTime, uint64_t& newestUtcTime) {
    oldestUtcTime = 0;
    newestUtcTime = 0;
    if (!_ready || xSemaphoreTake(_backlogMutex, portMAX_DELAY) != pdTRUE) return false;
    if (_recordCount == 0) {
        xSemaphoreGive(_backlogMutex);
        return true;
    }

    File f = LittleFS.open(_filename, "r");
    const uint32_t oldestIndex = _head;
    const uint32_t newestIndex = (_head + _recordCount - 1) % MAX_BACKLOG_RECORDS;
    StoredRecord oldest{};
    StoredRecord newest{};
    const bool ok = f &&
            f.seek(static_cast<size_t>(oldestIndex) * sizeof(StoredRecord), SeekSet) &&
            f.read(reinterpret_cast<uint8_t*>(&oldest), sizeof(oldest)) == sizeof(oldest) &&
            f.seek(static_cast<size_t>(newestIndex) * sizeof(StoredRecord), SeekSet) &&
            f.read(reinterpret_cast<uint8_t*>(&newest), sizeof(newest)) == sizeof(newest);
    if (f) f.close();
    if (ok) {
        oldestUtcTime = oldest.utcTime;
        newestUtcTime = newest.utcTime;
    }
    xSemaphoreGive(_backlogMutex);
    return ok;
}

void BacklogManager::clearAll() {
    if (!_ready || xSemaphoreTake(_backlogMutex, portMAX_DELAY) != pdTRUE) return;
    LittleFS.remove(_filename);
    LittleFS.remove(_metaFiles[0]);
    LittleFS.remove(_metaFiles[1]);
    File data = LittleFS.open(_filename, "w");
    if (data) data.close();
    _head = 0;
    _recordCount = 0;
    _sequence = 0;
    _activeMeta = -1;
    _ready = saveMetadata();
    xSemaphoreGive(_backlogMutex);
}
