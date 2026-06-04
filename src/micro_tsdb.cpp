/**
 * @file micro_tsdb.cpp
 * @brief C API Implementation for Micro TSDB
 */

#include "micro_tsdb.h"

// ============================================================================
// C API Implementation
// ============================================================================

const char* tsdb_error_string(tsdb_error_t err) {
    switch (err) {
        case TSDB_OK: return "OK";
        case TSDB_ERR_TIMEOUT: return "Timeout";
        case TSDB_ERR_FS_CORRUPT: return "Filesystem corrupt";
        case TSDB_ERR_NO_SPACE: return "No space left";
        case TSDB_ERR_INVALID_PARAM: return "Invalid parameter";
        case TSDB_ERR_FILE_NOT_FOUND: return "File not found";
        case TSDB_ERR_CRC_MISMATCH: return "CRC mismatch";
        case TSDB_ERR_BUFFER_FULL: return "Buffer full";
        case TSDB_ERR_NOT_INITIALIZED: return "Not initialized";
        case TSDB_ERR_READ_ONLY: return "Read only";
        case TSDB_ERR_INDEX_CORRUPT: return "Index corrupt";
        default: return "Unknown error";
    }
}

uint16_t tsdb_calculate_crc16(const void* data, size_t length) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    uint16_t crc = 0xFFFF;
    
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xA001 : 0);
        }
    }
    
    return crc;
}

int tsdb_verify_record(const tsdb_record_t* record) {
    if (!record) {
        return 0;
    }
    
    uint16_t calculated = tsdb_calculate_crc16(record, sizeof(tsdb_record_t) - 2);
    return (calculated == record->crc16) ? 1 : 0;
}

void tsdb_init_record(tsdb_record_t* record, uint64_t timestamp, int32_t value, uint16_t flags) {
    if (!record) {
        return;
    }
    
    std::memset(record, 0, sizeof(tsdb_record_t));
    record->timestamp = timestamp;
    record->value = value;
    record->flags = flags;
    record->crc16 = tsdb_calculate_crc16(record, sizeof(tsdb_record_t) - 2);
}
