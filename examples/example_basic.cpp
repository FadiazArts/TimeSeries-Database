/**
 * @file example_basic.cpp
 * @brief Basic usage example of Micro TSDB
 * 
 * Demonstrates:
 * - Initialization with HAL
 * - Writing records
 * - Reading records by index
 * - Automatic cleanup via RAII
 */

#include <iostream>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include "micro_tsdb.hpp"

using namespace micro_tsdb;

// ============================================================================
// Mock HAL Implementation (for demonstration)
// Replace with your actual SD Card/Filesystem implementation
// ============================================================================

struct MockFile {
    FILE* fp;
};

int mock_open_file(const char* path, bool append_mode, void** handle) {
    const char* mode = append_mode ? "ab+" : "rb";
    FILE* fp = std::fopen(path, mode);
    if (!fp) {
        // Try creating if doesn't exist
        fp = std::fopen(path, "wb+");
        if (!fp) return -1;
    }
    
    // Get file size and seek to end for append mode
    if (append_mode) {
        std::fseek(fp, 0, SEEK_END);
    }
    
    MockFile* file = new MockFile{fp};
    *handle = file;
    return 0;
}

int mock_close_file(void* handle) {
    if (!handle) return -1;
    MockFile* file = static_cast<MockFile*>(handle);
    if (file->fp) {
        std::fclose(file->fp);
    }
    delete file;
    return 0;
}

int mock_write_file(void* handle, const void* data, size_t size, size_t* written) {
    if (!handle || !data) return -1;
    MockFile* file = static_cast<MockFile*>(handle);
    *written = std::fwrite(data, 1, size, file->fp);
    return (*written == size) ? 0 : -1;
}

int mock_read_file(void* handle, void* data, size_t size, size_t offset, size_t* read) {
    if (!handle || !data) return -1;
    MockFile* file = static_cast<MockFile*>(handle);
    
    if (std::fseek(file->fp, static_cast<long>(offset), SEEK_SET) != 0) {
        return -1;
    }
    
    *read = std::fread(data, 1, size, file->fp);
    return (*read == size) ? 0 : -1;
}

int mock_seek_file(void* handle, size_t offset) {
    if (!handle) return -1;
    MockFile* file = static_cast<MockFile*>(handle);
    return std::fseek(file->fp, static_cast<long>(offset), SEEK_SET);
}

size_t mock_get_file_size(void* handle) {
    if (!handle) return 0;
    MockFile* file = static_cast<MockFile*>(handle);
    
    long current_pos = std::ftell(file->fp);
    std::fseek(file->fp, 0, SEEK_END);
    long size = std::ftell(file->fp);
    std::fseek(file->fp, current_pos, SEEK_SET);
    
    return static_cast<size_t>(size);
}

int mock_sync_file(void* handle) {
    if (!handle) return -1;
    MockFile* file = static_cast<MockFile*>(handle);
    std::fflush(file->fp);
    return 0;
}

int mock_create_directory(const char* path) {
    // Simple mkdir - platform specific
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

// ============================================================================
// Main Example
// ============================================================================

int main() {
    std::cout << "=== Micro TSDB Basic Example ===" << std::endl;
    
    // Initialize HAL
    tsdb_hal_t hal{};
    hal.open_file = mock_open_file;
    hal.close_file = mock_close_file;
    hal.write_file = mock_write_file;
    hal.read_file = mock_read_file;
    hal.seek_file = mock_seek_file;
    hal.get_file_size = mock_get_file_size;
    hal.sync_file = mock_sync_file;
    hal.create_directory = mock_create_directory;
    hal.context = nullptr;
    
    // Configure TSDB
    MicroTSDB<4>::Config config{};
    config.base_path = "./data";
    config.filename_prefix = "sensor_log";
    config.max_file_size_bytes = 10 * 1024 * 1024; // 10 MB rotation
    config.sync_interval_ms = 5000;
    config.enable_indexing = true;
    config.enable_rotation = true;
    config.buffer_sectors = 4;
    
    // Create TSDB instance
    MicroTSDB<4> db;
    
    // Initialize
    auto err = db.init(config, hal);
    if (err != TSDB_OK) {
        std::cerr << "Failed to initialize: " << tsdb_error_string(err) << std::endl;
        return 1;
    }
    
    std::cout << "Database initialized successfully!" << std::endl;
    
    // Write some sensor data
    std::cout << "\nWriting sensor data..." << std::endl;
    
    uint64_t base_timestamp = 1700000000000ULL; // Example timestamp
    
    for (int i = 0; i < 100; ++i) {
        MicroTSDB<4>::Record record{};
        record.timestamp = base_timestamp + (i * 1000); // 1 second intervals
        record.value = 2500 + (i % 100); // Simulated sensor value
        record.flags = 0x0001; // Sensor ID or status flags
        
        err = db.write(record);
        if (err != TSDB_OK) {
            std::cerr << "Write error: " << tsdb_error_string(err) << std::endl;
            break;
        }
        
        if ((i + 1) % 20 == 0) {
            std::cout << "  Written " << (i + 1) << " records" << std::endl;
        }
    }
    
    // Flush and sync
    err = db.sync();
    if (err != TSDB_OK) {
        std::cerr << "Sync error: " << tsdb_error_string(err) << std::endl;
    }
    
    // Get statistics
    auto stats = db.get_stats();
    std::cout << "\nStatistics:" << std::endl;
    std::cout << "  Total records written: " << stats.total_records_written << std::endl;
    std::cout << "  Total bytes written: " << stats.total_bytes_written << std::endl;
    std::cout << "  Buffer flushes: " << stats.buffer_flushes << std::endl;
    std::cout << "  Files created: " << stats.files_created << std::endl;
    
    // Read back some records
    std::cout << "\nReading records by index..." << std::endl;
    
    for (uint64_t i = 0; i < 5; ++i) {
        MicroTSDB<4>::Record record;
        err = db.read_at_index(i, record);
        
        if (err == TSDB_OK) {
            std::cout << "  [" << i << "] Timestamp: " << record.timestamp 
                      << ", Value: " << record.value 
                      << ", Flags: 0x" << std::hex << record.flags << std::dec
                      << std::endl;
        } else {
            std::cout << "  [" << i << "] Read error: " << tsdb_error_string(err) << std::endl;
        }
    }
    
    // Read by time range
    std::cout << "\nReading records by time range..." << std::endl;
    
    MicroTSDB<4>::Record range_records[10];
    size_t actual_count = 0;
    
    err = db.read_by_time_range(
        base_timestamp + 10000,  // Start: 10 seconds in
        base_timestamp + 20000,  // End: 20 seconds in
        range_records,
        10,
        actual_count
    );
    
    if (err == TSDB_OK) {
        std::cout << "  Found " << actual_count << " records in time range" << std::endl;
        for (size_t i = 0; i < actual_count; ++i) {
            std::cout << "    [" << i << "] Timestamp: " << range_records[i].timestamp 
                      << ", Value: " << range_records[i].value << std::endl;
        }
    }
    
    // Database will be automatically closed and synced when 'db' goes out of scope
    // thanks to RAII!
    
    std::cout << "\nExample completed successfully!" << std::endl;
    std::cout << "Data files saved to: ./data/" << std::endl;
    
    return 0;
}
