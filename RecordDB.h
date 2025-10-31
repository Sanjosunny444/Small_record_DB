// RecordDB.h - Enhanced generic record database
#pragma once

#include <Arduino.h>
#include <SPIFFS.h>

// Optional: Enable CRC8 for data integrity (small overhead)
#define RECORDDB_ENABLE_CRC 1

// Generic database template
template<typename T>
class RecordDB {
public:
    struct Record {
        char key[32];
        T data;
#if RECORDDB_ENABLE_CRC
        uint8_t crc;  // Simple CRC8 checksum
#endif
    };

    explicit RecordDB(const char* dbPath) : dbPath(dbPath) {}

    // Insert or replace (prevents duplicates)
    bool insert(const char* key, const T& data) {
        if (!key || strlen(key) >= 32) return false;

        // Remove old first → ensures no dupes
        remove(key);

        File file = SPIFFS.open(dbPath, "a");
        if (!file) return false;

        Record rec;
        memset(&rec, 0, sizeof(Record));
        strlcpy(rec.key, key, 32);
        rec.data = data;
#if RECORDDB_ENABLE_CRC
        rec.crc = computeCRC(&rec);
#endif

        bool ok = file.write((uint8_t*)&rec, sizeof(Record)) == sizeof(Record);
        file.close();
        return ok;
    }

    // Query by key
    bool query(const char* key, T& data) {
        if (!key) return false;

        File file = SPIFFS.open(dbPath, "r");
        if (!file) return false;

        Record rec;
        while (file.read((uint8_t*)&rec, sizeof(Record)) == sizeof(Record)) {
#if RECORDDB_ENABLE_CRC
            if (computeCRC(&rec) != rec.crc) {
                continue; // Skip corrupted
            }
#endif
            if (strcmp(rec.key, key) == 0) {
                data = rec.data;
                file.close();
                return true;
            }
        }
        file.close();
        return false;
    }

    // Check if key exists
    bool exists(const char* key) {
        T tmp;
        return query(key, tmp);
    }

    // Delete and compact: shift all remaining records forward
    bool remove(const char* key) {
        if (!key) return false;

        File src = SPIFFS.open(dbPath, "r");
        if (!src) return false;

        File dst = SPIFFS.open("/temp.db", "w");
        if (!dst) {
            src.close();
            return false;
        }

        Record rec;
        bool found = false;
        while (src.read((uint8_t*)&rec, sizeof(Record)) == sizeof(Record)) {
#if RECORDDB_ENABLE_CRC
            if (computeCRC(&rec) != rec.crc) continue;
#endif
            if (strcmp(rec.key, key) != 0) {
                dst.write((uint8_t*)&rec, sizeof(Record));  // keep
            } else {
                found = true;
            }
        }
        src.close();
        dst.close();

        // Replace only if we found and deleted
        if (found) {
            SPIFFS.remove(dbPath);
            SPIFFS.rename("/temp.db", dbPath);
        } else {
            SPIFFS.remove("/temp.db");
        }

        return found;
    }

    // Count how many records exist
    size_t count() {
        File file = SPIFFS.open(dbPath, "r");
        if (!file) return 0;

        size_t n = 0;
        Record rec;
        while (file.read((uint8_t*)&rec, sizeof(Record)) == sizeof(Record)) {
#if RECORDDB_ENABLE_CRC
            if (computeCRC(&rec) != rec.crc) continue;
#endif
            n++;
        }
        file.close();
        return n;
    }

    // Iterate all valid records
// Updated selectAll: supports lambdas!
template<typename F>
void selectAll(F callback) {
    File file = SPIFFS.open(dbPath, "r");
    if (!file) return;  // Only check file

    Record rec;
    while (file.read((uint8_t*)&rec, sizeof(Record)) == sizeof(Record)) {
#if RECORDDB_ENABLE_CRC
        if (computeCRC(&rec) != rec.crc) continue;
#endif
        callback(rec.key, rec.data);
    }
    file.close();
}
    // Clear all data
    void clear() {
        SPIFFS.remove(dbPath);
    }

private:
#if RECORDDB_ENABLE_CRC
    // Simple CRC8 (polynomial: x^8 + x^2 + x^1 + 1)
    uint8_t computeCRC(const Record* rec) {
        uint8_t crc = 0;
        const uint8_t* p = (const uint8_t*)rec;
        // Hash everything except the CRC byte itself
        size_t len = sizeof(Record) - 1;
        for (size_t i = 0; i < len; i++) {
            crc ^= p[i];
            for (int j = 0; j < 8; j++) {
                crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
            }
        }
        return crc;
    }
#endif

    const char* dbPath;
};