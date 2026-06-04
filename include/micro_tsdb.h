/**
 * @file micro_tsdb.h
 * @brief Micro Time-Series Database Library for Embedded Systems
 * 
 * A lightweight, flash-friendly TSDB designed for SD Card storage with:
 * - Fixed-length binary records for O(1) access
 * - Append-only writes to prevent fragmentation
 * - Double-buffering for non-blocking I/O
 * - CRC16 integrity checking
 * - Thread-safe with FreeRTOS support
 * - Hardware Abstraction Layer (HAL)
 */

#ifndef MICRO_TSDB_H
#define MICRO_TSDB_H

#include <cstdint>
#include <cstddef>
#include <cstring>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Constants (Compile-time)
// ============================================================================

#ifndef TSDB_CONFIG_SECTOR_SIZE
#define TSDB_CONFIG_SECTOR_SIZE 512
#endif

#ifndef TSDB_CONFIG_BUFFER_SECTORS
#define TSDB_CONFIG_BUFFER_SECTORS 4
#endif

#ifndef TSDB_CONFIG_MAX_FILENAME_LEN
#define TSDB_CONFIG_MAX_FILENAME_LEN 32
#endif

#ifndef TSDB_CONFIG_INDEX_ENTRIES_PER_BLOCK
#define TSDB_CONFIG_INDEX_ENTRIES_PER_BLOCK 64
#endif

// ============================================================================
// Error Codes (Enum for type safety)
// ============================================================================

typedef enum : int8_t {
    TSDB_OK = 0,
    TSDB_ERR_TIMEOUT = -1,
    TSDB_ERR_FS_CORRUPT = -2,
    TSDB_ERR_NO_SPACE = -3,
    TSDB_ERR_INVALID_PARAM = -4,
    TSDB_ERR_FILE_NOT_FOUND = -5,
    TSDB_ERR_CRC_MISMATCH = -6,
    TSDB_ERR_BUFFER_FULL = -7,
    TSDB_ERR_NOT_INITIALIZED = -8,
    TSDB_ERR_READ_ONLY = -9,
    TSDB_ERR_INDEX_CORRUPT = -10
} tsdb_error_t;

// Convert error to string
const char* tsdb_error_string(tsdb_error_t err);

// ============================================================================
// Data Structures
// ============================================================================

/**
 * @brief Core data record structure (fixed-size, binary format)
 * 
 * Total size: 16 bytes (configurable via template)
 * Layout: [timestamp:8][value:4][flags:2][crc16:2]
 */
#pragma pack(push, 1)
typedef struct {
    uint64_t timestamp;   // Unix timestamp in milliseconds (8 bytes)
    int32_t value;        // Sensor value or payload (4 bytes)
    uint16_t flags;       // Status flags, sensor ID, etc. (2 bytes)
    uint16_t crc16;       // CRC16 checksum (2 bytes)
} tsdb_record_t;
#pragma pack(pop)

static_assert(sizeof(tsdb_record_t) == 16, "Record size must be 16 bytes");

/**
 * @brief Index entry for sparse indexing
 * Maps timestamp ranges to file offsets
 */
#pragma pack(push, 1)
typedef struct {
    uint64_t start_timestamp;  // Start timestamp of this block
    uint64_t end_timestamp;    // End timestamp of this block
    uint32_t byte_offset;      // Byte offset in main .dat file
    uint16_t record_count;     // Number of records in this block
    uint16_t reserved;         // Padding for alignment
} tsdb_index_entry_t;
#pragma pack(pop)

static_assert(sizeof(tsdb_index_entry_t) == 24, "Index entry size must be 24 bytes");

/**
 * @brief Configuration structure for TSDB initialization
 */
typedef struct {
    const char* base_path;           // Base directory for data files
    const char* filename_prefix;     // Prefix for data files (e.g., "log")
    uint32_t max_file_size_bytes;    // Max file size before rotation (0 = unlimited)
    uint32_t sync_interval_ms;       // Auto-sync interval in milliseconds
    bool enable_indexing;            // Enable sparse indexing
    bool enable_rotation;            // Enable file rotation
    uint8_t buffer_sectors;          // Number of sectors per buffer (must be >= 1)
} tsdb_config_t;

/**
 * @brief Statistics structure for diagnostics
 */
typedef struct {
    uint64_t total_records_written;
    uint64_t total_records_read;
    uint64_t total_bytes_written;
    uint32_t files_created;
    uint32_t crc_errors_detected;
    uint32_t buffer_flushes;
    uint64_t last_timestamp;
    uint32_t current_file_size;
} tsdb_stats_t;

// ============================================================================
// Hardware Abstraction Layer (HAL) Interface
// ============================================================================

/**
 * @brief HAL function pointers - inject your hardware implementation here
 */
typedef struct {
    // File operations
    int (*open_file)(const char* path, bool append_mode, void** handle);
    int (*close_file)(void* handle);
    int (*write_file)(void* handle, const void* data, size_t size, size_t* written);
    int (*read_file)(void* handle, void* data, size_t size, size_t offset, size_t* read);
    int (*seek_file)(void* handle, size_t offset);
    size_t (*get_file_size)(void* handle);
    int (*delete_file)(const char* path);
    int (*rename_file)(const char* old_path, const char* new_path);
    
    // Synchronization
    int (*sync_file)(void* handle);
    
    // Directory operations
    int (*create_directory)(const char* path);
    
    // Optional: Direct SDIO/SPI access for advanced users
    int (*sd_write_blocks)(uint32_t block_addr, const void* data, size_t num_blocks);
    int (*sd_read_blocks)(uint32_t block_addr, void* data, size_t num_blocks);
    
    // Context pointer (passed to all callbacks)
    void* context;
} tsdb_hal_t;

// ============================================================================
// C API Functions
// ============================================================================

/**
 * @brief Calculate CRC16 checksum
 * @param data Data buffer
 * @param length Data length
 * @return CRC16 value
 */
uint16_t tsdb_calculate_crc16(const void* data, size_t length);

/**
 * @brief Verify record CRC
 * @param record Record to verify
 * @return 1 if CRC matches, 0 otherwise
 */
int tsdb_verify_record(const tsdb_record_t* record);

/**
 * @brief Initialize a record with CRC
 * @param record Record to initialize
 * @param timestamp Timestamp value
 * @param value Sensor value
 * @param flags Status flags
 */
void tsdb_init_record(tsdb_record_t* record, uint64_t timestamp, int32_t value, uint16_t flags);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // MICRO_TSDB_H
