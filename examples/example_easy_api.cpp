/**
 * @file example_easy_api.cpp
 * @brief Contoh penggunaan Easy API - High-Level Abstraction
 * 
 * Demonstrasi bagaimana abstraction layer membuat kode lebih bersih,
 * mudah dibaca, dan fleksibel.
 */

#include "tsdb_easy.hpp"
#include <cstdio>
#include <vector>

using namespace micro_tsdb::easy;

// ============================================================================
// Mock HAL Implementation (ganti dengan implementasi SD Card Anda)
// ============================================================================

static int mock_open(const char* path, bool append, void** handle) {
    printf("[HAL] Opening file: %s (append=%d)\n", path, append);
    *handle = reinterpret_cast<void*>(0x1234); // Mock handle
    return 0;
}

static int mock_close(void* handle) {
    printf("[HAL] Closing file\n");
    return 0;
}

static int mock_write(void* handle, const void* data, size_t size, size_t* written) {
    printf("[HAL] Writing %zu bytes\n", size);
    *written = size;
    return 0;
}

static int mock_read(void* handle, void* data, size_t size, size_t offset, size_t* read) {
    printf("[HAL] Reading %zu bytes at offset %zu\n", size, offset);
    *read = size;
    return 0;
}

static size_t mock_file_size(void* handle) {
    return 0;
}

static tsdb_hal_t create_mock_hal() {
    tsdb_hal_t hal{};
    hal.open_file = mock_open;
    hal.close_file = mock_close;
    hal.write_file = mock_write;
    hal.read_file = mock_read;
    hal.get_file_size = mock_file_size;
    hal.sync_file = nullptr;
    hal.create_directory = nullptr;
    hal.context = nullptr;
    return hal;
}

// ============================================================================
// CONTOH 1: Penggunaan Paling Sederhana
// ============================================================================

void example_basic_usage() {
    printf("\n=== CONTOH 1: Basic Usage ===\n");
    
    Database db;
    
    // Konfigurasi dengan fluent interface
    auto config = DBConfig::create()
        .path("/sdcard/data")
        .prefix("sensor")
        .max_file_size_mb(50)
        .enable_indexing()
        .build();
    
    // Open database
    if (!db.open(config, create_mock_hal())) {
        printf("Failed to open database!\n");
        return;
    }
    
    // Write single data point
    db.write_value(42);  // Auto timestamp
    
    // Write with explicit timestamp
    db.write(DataPoint{1000, 25});
    db.write(DataPoint{2000, 30});
    db.write(DataPoint{3000, 35});
    
    // Write multiple points at once
    std::vector<DataPoint> batch = {
        {4000, 40},
        {5000, 45},
        {6000, 50}
    };
    db.write(batch);
    
    // Write with initializer list (paling ringkas!)
    db.write({
        {7000, 55},
        {8000, 60},
        {9000, 65}
    });
    
    printf("Total records: %llu\n", db.count());
    
    db.close();
}

// ============================================================================
// CONTOH 2: Query dengan Time Range
// ============================================================================

void example_query_time_range() {
    printf("\n=== CONTOH 2: Query Time Range ===\n");
    
    Database db;
    db.open(DBConfig::create().path("/data"), create_mock_hal());
    
    // ... (write some data) ...
    
    // Query last hour
    TimeRange last_hour = TimeRange::last(3600000); // 1 hour in ms
    auto results = db.query(last_hour);
    
    printf("Found %zu records in last hour\n", results.size());
    
    // Iterate over results
    for (const auto& dp : results) {
        printf("Time: %llu, Value: %d\n", dp.time, dp.value);
    }
    
    // Get statistics from query result
    if (!results.empty()) {
        printf("Average: %.2f\n", results.average());
        printf("Min: %d, Max: %d\n", results.min_value(), results.max_value());
    }
    
    // Query custom range
    auto custom_results = db.query(
        TimeRange::between(1000, 5000)
    );
    
    db.close();
}

// ============================================================================
// CONTOH 3: Mendapatkan Data Terbaru
// ============================================================================

void example_latest_data() {
    printf("\n=== CONTOH 3: Latest Data ===\n");
    
    Database db;
    db.open(DBConfig::create().path("/data"), create_mock_hal());
    
    // Get latest record
    DataPoint latest;
    if (db.latest(latest)) {
        printf("Latest value: %d at time %llu\n", latest.value, latest.time);
    } else {
        printf("No data available\n");
    }
    
    // Get last 10 records
    auto last_10 = db.last(10);
    printf("Last 10 records:\n");
    for (const auto& dp : last_10) {
        printf("  %llu: %d\n", dp.time, dp.value);
    }
    
    db.close();
}

// ============================================================================
// CONTOH 4: Functional Programming Style
// ============================================================================

void example_functional_style() {
    printf("\n=== CONTOH 4: Functional Style ===\n");
    
    Database db;
    db.open(DBConfig::create().path("/data"), create_mock_hal());
    
    // For each callback
    db.for_each(TimeRange::last(3600000), [](const DataPoint& dp) {
        printf("Processing: %d\n", dp.value);
    });
    
    // Transform values
    auto values = db.transform(TimeRange::last(3600000),
        [](const DataPoint& dp) -> double {
            return dp.value * 1.5;  // Convert to engineering units
        }
    );
    
    printf("Transformed values:\n");
    for (double v : values) {
        printf("  %.2f\n", v);
    }
    
    db.close();
}

// ============================================================================
// CONTOH 5: Error Handling
// ============================================================================

void example_error_handling() {
    printf("\n=== CONTOH 5: Error Handling ===\n");
    
    Database db;
    
    if (!db.open(DBConfig::create().path("/invalid"), create_mock_hal())) {
        printf("Open failed! Error code: %d\n", static_cast<int>(db.last_error()));
        return;
    }
    
    // Write operations return bool for easy checking
    if (!db.write_value(100)) {
        printf("Write failed! Error: %d\n", static_cast<int>(db.last_error()));
    }
    
    // Check if database is still open
    if (!db.is_open()) {
        printf("Database closed unexpectedly!\n");
    }
    
    db.close();
}

// ============================================================================
// CONTOH 6: Multiple Sensors / Data Streams
// ============================================================================

void example_multiple_sensors() {
    printf("\n=== CONTOH 6: Multiple Sensors ===\n");
    
    // Gunakan flags untuk membedakan sensor
    Database db;
    db.open(DBConfig::create()
        .path("/sdcard/sensors")
        .prefix("multi"), 
        create_mock_hal());
    
    // Sensor 1 (flag = 1)
    db.write(DataPoint::now(2500, 1));  // value=2500, flag=1 (temp)
    
    // Sensor 2 (flag = 2)
    db.write(DataPoint::now(65, 2));    // value=65, flag=2 (humidity)
    
    // Sensor 3 (flag = 3)
    db.write(DataPoint::now(1013, 3));  // value=1013, flag=3 (pressure)
    
    // Query semua data, lalu filter berdasarkan flag
    auto all_data = db.query(TimeRange::last(3600000));
    
    printf("All sensor data:\n");
    for (const auto& dp : all_data) {
        const char* sensor_name = "";
        switch(dp.flags) {
            case 1: sensor_name = "Temperature"; break;
            case 2: sensor_name = "Humidity"; break;
            case 3: sensor_name = "Pressure"; break;
            default: sensor_name = "Unknown"; break;
        }
        printf("  %s: %d\n", sensor_name, dp.value);
    }
    
    db.close();
}

// ============================================================================
// CONTOH 7: RAII - Resource Management Otomatis
// ============================================================================

void example_raii() {
    printf("\n=== CONTOH 7: RAII (Automatic Cleanup) ===\n");
    
    {
        Database db;
        db.open(DBConfig::create().path("/data"), create_mock_hal());
        
        db.write_value(100);
        db.write_value(200);
        db.write_value(300);
        
        // Tidak perlu close() manual!
        // Destructor akan otomatis memanggil close() dan flush()
        
    } // db otomatis ditutup di sini
    
    printf("Database automatically closed\n");
}

// ============================================================================
// CONTOH 8: Custom Buffer Size
// ============================================================================

void example_custom_buffer() {
    printf("\n=== CONTOH 8: Custom Buffer Size ===\n");
    
    // Gunakan buffer lebih besar untuk throughput lebih tinggi
    Database<8> db_large_buffer;  // 8 sectors = 4KB buffer
    
    db_large_buffer.open(
        DBConfig::create()
            .path("/sdcard/fast")
            .buffer_sectors(8),
        create_mock_hal()
    );
    
    // Write banyak data dengan cepat
    for (int i = 0; i < 100; i++) {
        db_large_buffer.write_value(i * 10);
    }
    
    printf("Wrote 100 records with large buffer\n");
    printf("Stats: %llu writes, %llu bytes\n",
           db_large_buffer.stats().total_records_written,
           db_large_buffer.stats().total_bytes_written);
    
    db_large_buffer.close();
}

// ============================================================================
// MAIN - Jalankan Semua Contoh
// ============================================================================

int main() {
    printf("=================================================\n");
    printf("Micro TSDB - Easy API Examples\n");
    printf("=================================================\n");
    
    example_basic_usage();
    example_query_time_range();
    example_latest_data();
    example_functional_style();
    example_error_handling();
    example_multiple_sensors();
    example_raii();
    example_custom_buffer();
    
    printf("\n=================================================\n");
    printf("All examples completed!\n");
    printf("=================================================\n");
    
    return 0;
}
