/**
 * @file example_freertos.cpp
 * @brief FreeRTOS Multi-Task Example for Micro TSDB
 * 
 * Demonstrates:
 * - Thread-safe operations with multiple FreeRTOS tasks
 * - Non-blocking sensor data logging
 * - Proper mutex usage
 */

#include <iostream>
#include <cstdio>
#include <cstring>
#include "micro_tsdb.hpp"

// Mock FreeRTOS types for demonstration (replace with actual FreeRTOS headers)
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define MOCK_DELAY_MS(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#else
// Mock implementation for non-FreeRTOS platforms
#define MOCK_DELAY_MS(ms) std::this_thread::sleep_for(std::chrono::milliseconds(ms))
#endif

// ============================================================================
// HAL Implementation (same as basic example - use your actual implementation)
// ============================================================================

struct MockFile {
    FILE* fp;
};

int mock_open_file(const char* path, bool append_mode, void** handle) {
    const char* mode = append_mode ? "ab+" : "rb";
    FILE* fp = std::fopen(path, mode);
    if (!fp) {
        fp = std::fopen(path, "wb+");
        if (!fp) return -1;
    }
    if (append_mode) std::fseek(fp, 0, SEEK_END);
    MockFile* file = new MockFile{fp};
    *handle = file;
    return 0;
}

int mock_close_file(void* handle) {
    if (!handle) return -1;
    MockFile* file = static_cast<MockFile*>(handle);
    if (file->fp) std::fclose(file->fp);
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
    if (std::fseek(file->fp, static_cast<long>(offset), SEEK_SET) != 0) return -1;
    *read = std::fread(data, 1, size, file->fp);
    return (*read == size) ? 0 : -1;
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
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0755);
#endif
}

// ============================================================================
// Global TSDB Instance
// ============================================================================

using TSDB = micro_tsdb::MicroTSDB<4>;
static TSDB g_tsdb;
static bool g_running = false;

// ============================================================================
// Sensor Reader Task (simulates reading from hardware sensors)
// ============================================================================

void sensor_reader_task(void* pvParameters) {
    std::cout << "[Sensor Task] Started" << std::endl;
    
    uint64_t base_timestamp = 1700000000000ULL;
    int sensor_id = reinterpret_cast<intptr_t>(pvParameters);
    int count = 0;
    
    while (g_running && count < 50) {
        // Simulate sensor reading
        TSDB::Record record{};
        record.timestamp = base_timestamp + (count * 100); // 100ms intervals
        record.value = 1000 + (sensor_id * 100) + (count % 50);
        record.flags = static_cast<uint16_t>(sensor_id);
        
        // Write to TSDB (thread-safe!)
        auto err = g_tsdb.write(record);
        if (err != micro_tsdb::TSDB_OK) {
            std::cerr << "[Sensor Task] Write error: " << tsdb_error_string(err) << std::endl;
        }
        
        count++;
        
        // Simulate sensor sampling interval (100ms)
        #ifndef ESP_PLATFORM
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        #endif
        MOCK_DELAY_MS(100);
    }
    
    std::cout << "[Sensor Task] Completed (" << count << " samples)" << std::endl;
    
    #ifdef ESP_PLATFORM
    vTaskDelete(NULL);
    #endif
}

// ============================================================================
// Data Analyzer Task (reads and processes data concurrently)
// ============================================================================

void data_analyzer_task(void* pvParameters) {
    std::cout << "[Analyzer Task] Started" << std::endl;
    
    int analysis_count = 0;
    
    while (g_running && analysis_count < 10) {
        // Wait a bit between analyses
        #ifndef ESP_PLATFORM
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        #endif
        MOCK_DELAY_MS(500);
        
        // Read recent data (thread-safe!)
        TSDB::Record records[5];
        size_t actual_count = 0;
        
        uint64_t current_time = 1700000000000ULL + (analysis_count * 5000);
        
        auto err = g_tsdb.read_by_time_range(
            current_time - 1000,
            current_time,
            records,
            5,
            actual_count
        );
        
        if (err == micro_tsdb::TSDB_OK && actual_count > 0) {
            int32_t sum = 0;
            for (size_t i = 0; i < actual_count; ++i) {
                sum += records[i].value;
            }
            int32_t avg = sum / static_cast<int32_t>(actual_count);
            
            std::cout << "[Analyzer] Avg value: " << avg 
                      << " (from " << actual_count << " samples)" << std::endl;
        }
        
        analysis_count++;
    }
    
    std::cout << "[Analyzer Task] Completed" << std::endl;
    
    #ifdef ESP_PLATFORM
    vTaskDelete(NULL);
    #endif
}

// ============================================================================
// Main Function
// ============================================================================

int main() {
    std::cout << "=== Micro TSDB FreeRTOS Multi-Task Example ===" << std::endl;
    
    // Initialize HAL
    micro_tsdb::tsdb_hal_t hal{};
    hal.open_file = mock_open_file;
    hal.close_file = mock_close_file;
    hal.write_file = mock_write_file;
    hal.read_file = mock_read_file;
    hal.get_file_size = mock_get_file_size;
    hal.sync_file = mock_sync_file;
    hal.create_directory = mock_create_directory;
    
    // Configure TSDB
    TSDB::Config config{};
    config.base_path = "./freertos_data";
    config.filename_prefix = "multi_sensor";
    config.max_file_size_bytes = 5 * 1024 * 1024; // 5 MB
    config.enable_indexing = true;
    config.enable_rotation = true;
    
    // Initialize database
    auto err = g_tsdb.init(config, hal);
    if (err != micro_tsdb::TSDB_OK) {
        std::cerr << "Failed to initialize: " << tsdb_error_string(err) << std::endl;
        return 1;
    }
    
    std::cout << "Database initialized!" << std::endl;
    g_running = true;
    
#ifdef ESP_PLATFORM
    // Create FreeRTOS tasks
    xTaskCreate(sensor_reader_task, "Sensor1", 4096, 
                reinterpret_cast<void*>(1), 5, NULL);
    xTaskCreate(sensor_reader_task, "Sensor2", 4096, 
                reinterpret_cast<void*>(2), 5, NULL);
    xTaskCreate(data_analyzer_task, "Analyzer", 4096, NULL, 4, NULL);
    
    // Let tasks run
    MOCK_DELAY_MS(10000);
    
    g_running = false;
    MOCK_DELAY_MS(1000);
#else
    // For non-FreeRTOS demo, run sequentially with threading
    std::cout << "\nRunning in simulation mode (non-FreeRTOS platform)..." << std::endl;
    
    // Start sensor tasks in background threads
    std::thread sensor1([&]() { sensor_reader_task(reinterpret_cast<void*>(1)); });
    std::thread sensor2([&]() { sensor_reader_task(reinterpret_cast<void*>(2)); });
    std::thread analyzer([&]() { data_analyzer_task(nullptr); });
    
    // Wait for completion
    sensor1.join();
    sensor2.join();
    analyzer.join();
#endif
    
    // Get final statistics
    auto stats = g_tsdb.get_stats();
    std::cout << "\n=== Final Statistics ===" << std::endl;
    std::cout << "Total records written: " << stats.total_records_written << std::endl;
    std::cout << "Total bytes written: " << stats.total_bytes_written << std::endl;
    std::cout << "Buffer flushes: " << stats.buffer_flushes << std::endl;
    std::cout << "CRC errors detected: " << stats.crc_errors_detected << std::endl;
    
    // Close database (RAII will also do this automatically)
    g_tsdb.close();
    
    std::cout << "\nExample completed successfully!" << std::endl;
    
    return 0;
}
