# Micro TSDB - Lightweight Time-Series Database for Embedded Systems

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-green.svg)](https://en.cppreference.com/w/cpp/17)
[![Platform](https://img.shields.io/badge/platform-ESP32%20%7C%20STM32%20%7C%20Arduino-orange.svg)]()

A **microcontroller-optimized** time-series database library designed for SD Card storage on embedded devices. Built with modern C++17 for zero-cost abstractions while maintaining the performance of pure C.

## 📚 Documentation Layers

This library provides **two API layers** to suit different needs:

### 1. Easy API (Recommended for Most Users) ⭐
High-level abstraction with intuitive names, fluent configuration, and flexible usage patterns. Perfect for rapid development.

```cpp
#include "tsdb_easy.hpp"
using namespace micro_tsdb::easy;

Database db;
db.open(DBConfig::create().path("/sdcard"));

// Write data
db.write_value(42);                          // Auto timestamp
db.write({{t1, v1}, {t2, v2}});             // Initializer list
db.write(DataPoint::now(temperature));      // Named constructor

// Query data
auto results = db.query(TimeRange::last(3600000));
printf("Average: %.2f\n", results.average());
```

### 2. Core API (Advanced Users)
Low-level control with direct access to all features. Use when you need maximum performance or customization.

```cpp
#include "micro_tsdb.hpp"
using namespace micro_tsdb;

MicroTSDB<4> db;
db.init(config, hal);
db.write(record);
```

---

## Key Features

### 🚀 Performance Optimized
- **Fixed-Length Binary Records**: 16-byte records enable O(1) random access
- **Append-Only Writes**: Prevents flash memory fragmentation and wear
- **Double-Buffering**: Ping-pong buffers (512-byte sector aligned) for non-blocking I/O
- **Sparse Indexing**: Fast timestamp-based lookups without full file scans
- **SDIO Support**: Designed for 4-bit SDIO mode (4-12 MB/s)

### 🛡️ Industrial Reliability
- **CRC16 Integrity**: Every record validated against corruption
- **Thread-Safe**: Mutex-protected for FreeRTOS multi-task environments
- **Power-Loss Resilient**: Minimal sync points protect against sudden power loss
- **File Rotation**: Automatic file splitting by size or date

### 💻 Modern C++ Design
- **Zero-Cost Abstractions**: Templates for compile-time buffer allocation
- **RAII**: Automatic resource cleanup (no manual close required)
- **HAL Interface**: Hardware abstraction for ESP-IDF, STM32 HAL, Arduino
- **Type-Safe Errors**: Enum-based error codes instead of magic numbers

## Quick Start

### Installation

Copy the files to your project:

```bash
# For Easy API (recommended)
cp include/tsdb_easy.hpp include/micro_tsdb.h include/micro_tsdb.hpp /your/project/include/
cp src/micro_tsdb.cpp /your/project/src/

# Or just Core API
cp include/micro_tsdb.h include/micro_tsdb.hpp /your/project/include/
```

### Easy API Usage (Recommended) ⭐

```cpp
#include "tsdb_easy.hpp"
using namespace micro_tsdb::easy;

// 1. Create database instance
Database db;

// 2. Configure with fluent interface
auto config = DBConfig::create()
    .path("/sdcard/data")
    .prefix("sensor")
    .max_file_size_mb(50)
    .enable_indexing()
    .build();

// 3. Open database
if (!db.open(config, hal)) {
    // Handle error
}

// 4. Write data (multiple ways!)
db.write_value(42);                        // Auto timestamp
db.write(DataPoint{millis(), 25});        // Explicit timestamp
db.write({{t1, v1}, {t2, v2}});           // Initializer list
db.write(DataPoint::now(temperature));    // Named constructor

// 5. Query data
auto results = db.query(TimeRange::last(3600000));  // Last hour
printf("Records: %zu\n", results.size());
printf("Average: %.2f\n", results.average());
printf("Min: %d, Max: %d\n", 
       results.min_value(), results.max_value());

// 6. Get latest record
DataPoint latest;
if (db.latest(latest)) {
    printf("Latest: %d\n", latest.value);
}

// 7. Auto-closed on destruction (RAII)
```

### Core API Usage

```cpp
#include "micro_tsdb.hpp"

using namespace micro_tsdb;

// 1. Implement HAL (Hardware Abstraction Layer)
tsdb_hal_t hal{};
hal.open_file = your_open_function;
hal.close_file = your_close_function;
hal.write_file = your_write_function;
hal.read_file = your_read_function;
hal.get_file_size = your_size_function;
hal.sync_file = your_sync_function;

// 2. Configure database
MicroTSDB<4>::Config config{};
config.base_path = "/sdcard/data";
config.filename_prefix = "sensor_log";
config.max_file_size_bytes = 50 * 1024 * 1024; // 50MB rotation
config.enable_indexing = true;
config.enable_rotation = true;

// 3. Initialize
MicroTSDB<4> db;
auto err = db.init(config, hal);
if (err != TSDB_OK) {
    // Handle error
}

// 4. Write data (automatically buffered)
MicroTSDB<4>::Record record{};
record.timestamp = millis();  // Your timestamp source
record.value = read_sensor();
record.flags = SENSOR_ID_TEMP;

db.write(record);  // Thread-safe, non-blocking

// 5. Read data
MicroTSDB<4>::Record read_record;
err = db.read_at_index(0, read_record);  // O(1) access!

// 6. Query by time range
MicroTSDB<4>::Record results[100];
size_t count;
db.read_by_time_range(start_time, end_time, results, 100, count);

// 7. Auto-closed on destruction (RAII) or call explicitly
db.close();
```

## Easy API Reference

The Easy API provides intuitive names and flexible patterns:

### Core Types

| Type | Description | Example |
|------|-------------|---------|
| `DataPoint` | Simple data wrapper | `DataPoint{time, value, flags}` |
| `TimeRange` | Time query range | `TimeRange::last(3600000)` |
| `QueryResult` | Query results with stats | `results.average()` |
| `Database<N>` | Main database class | `Database<> db` |
| `DBConfig` | Fluent config builder | `DBConfig::create().path("/sd")` |

### Database Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `open(config, hal)` | `bool` | Open database |
| `close()` | `void` | Close database |
| `write(dp)` | `bool` | Write single point |
| `write({{t1,v1},...})` | `bool` | Write multiple points |
| `write_value(v)` | `bool` | Write with auto timestamp |
| `query(range)` | `QueryResult` | Query by time range |
| `last(n)` | `QueryResult` | Get last N records |
| `latest(out)` | `bool` | Get latest record |
| `count()` | `uint64_t` | Total record count |
| `flush()` | `bool` | Flush buffer to disk |
| `stats()` | `Stats` | Get statistics |

### QueryResult Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `size_t` | Number of records |
| `average()` | `double` | Average value |
| `min_value()` | `int32_t` | Minimum value |
| `max_value()` | `int32_t` | Maximum value |
| `first_or_default()` | `DataPoint` | First record |
| `last_or_default()` | `DataPoint` | Last record |
| Range-based for | iterator | `for (auto& dp : result)` |

---

## Configuration Options

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `base_path` | `const char*` | `nullptr` | Directory for data files |
| `filename_prefix` | `const char*` | `"log"` | Prefix for `.dat` files |
| `max_file_size_bytes` | `uint32_t` | `0` | Max file size before rotation (0 = unlimited) |
| `sync_interval_ms` | `uint32_t` | `0` | Auto-sync interval (0 = manual only) |
| `enable_indexing` | `bool` | `false` | Enable sparse indexing for fast seeks |
| `enable_rotation` | `bool` | `false` | Enable automatic file rotation |
| `buffer_sectors` | `uint8_t` | `4` | Number of 512-byte sectors per buffer |

## Error Codes

| Code | Value | Description |
|------|-------|-------------|
| `TSDB_OK` | 0 | Success |
| `TSDB_ERR_TIMEOUT` | -1 | SD Card timeout (wear-leveling delay) |
| `TSDB_ERR_FS_CORRUPT` | -2 | Filesystem corruption detected |
| `TSDB_ERR_NO_SPACE` | -3 | SD Card full |
| `TSDB_ERR_INVALID_PARAM` | -4 | Invalid parameter passed |
| `TSDB_ERR_FILE_NOT_FOUND` | -5 | Data file not found |
| `TSDB_ERR_CRC_MISMATCH` | -6 | Record checksum invalid (data corrupt) |
| `TSDB_ERR_BUFFER_FULL` | -7 | Write buffer full |
| `TSDB_ERR_NOT_INITIALIZED` | -8 | Database not initialized |
| `TSDB_ERR_INDEX_CORRUPT` | -10 | Index file corrupted |

## Memory Footprint

| Component | Size (bytes) | Notes |
|-----------|--------------|-------|
| Record | 16 | Fixed size |
| Index Entry | 24 | Per block |
| Double Buffer | 4096 | Default (4 sectors x 2 buffers x 512 bytes) |
| MicroTSDB Instance | ~200 | Excluding buffers |
| **Total RAM** | **~4.3 KB** | For default configuration |

## Platform Support

| Platform | Status | Notes |
|----------|--------|-------|
| ESP32 / ESP-IDF | ✅ Compatible | Use SDMMC or SPI driver |
| STM32 (HAL) | ✅ Compatible | Use FatFS middleware |
| Arduino | ✅ Compatible | Use SdFat library |
| Raspberry Pi Pico | ✅ Compatible | Use FatFs port |
| Linux (POSIX) | ✅ Tested | For development/testing |

## License

MIT License - See [LICENSE](LICENSE) for details.

---

**Built for industrial IoT applications where reliability matters.**
