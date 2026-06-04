/**
 * @file tsdb_easy.hpp
 * @brief High-Level Abstraction for Micro TSDB
 * 
 * Simplified API with intuitive names and flexible usage patterns.
 * This layer provides zero-cost abstractions over the core TSDB implementation.
 * 
 * Features:
 * - Simple, chainable API
 * - Automatic type conversions
 * - Range-based iteration
 * - Query builder pattern
 * - Smart defaults
 */

#ifndef TSDB_EASY_HPP
#define TSDB_EASY_HPP

#include "micro_tsdb.hpp"
#include <functional>
#include <iterator>
#include <algorithm>

#ifdef __cplusplus

namespace micro_tsdb {
namespace easy {

/**
 * @brief Data point - simple wrapper for sensor readings
 * 
 * Usage:
 *   DataPoint dp{timestamp, value};
 *   DataPoint dp = DataPoint::now(42);  // auto timestamp
 *   DataPoint dp = DataPoint::with_flags(42, FLAG_ALARM);
 */
struct DataPoint {
    uint64_t time;      ///< Timestamp in milliseconds
    int32_t value;      ///< Sensor value
    uint16_t flags;     ///< Status flags (optional)
    
    /// Default constructor
    DataPoint() : time(0), value(0), flags(0) {}
    
    /// Construct with all fields
    DataPoint(uint64_t t, int32_t v, uint16_t f = 0) 
        : time(t), value(v), flags(f) {}
    
    /// Construct with current time (requires millis() function)
    static DataPoint now(int32_t val, uint16_t flags = 0) {
        #ifdef millis
        return DataPoint(millis(), val, flags);
        #else
        return DataPoint(0, val, flags);
        #endif
    }
    
    /// Convert to internal record format
    tsdb_record_t to_record() const {
        tsdb_record_t rec{};
        rec.timestamp = time;
        rec.value = value;
        rec.flags = flags;
        rec.crc16 = 0; // Will be calculated by TSDB
        return rec;
    }
    
    /// Create from internal record
    static DataPoint from_record(const tsdb_record_t& rec) {
        return DataPoint(rec.timestamp, rec.value, rec.flags);
    }
    
    /// Comparison operators for sorting
    bool operator<(const DataPoint& other) const {
        return time < other.time;
    }
    
    bool operator==(const DataPoint& other) const {
        return time == other.time && value == other.value;
    }
};

/**
 * @brief Time range for queries
 * 
 * Usage:
 *   TimeRange last_hour = TimeRange::last(3600000);
 *   TimeRange custom = TimeRange::between(start_ms, end_ms);
 *   TimeRange today = TimeRange::today();
 */
struct TimeRange {
    uint64_t start;
    uint64_t end;
    
    TimeRange(uint64_t s, uint64_t e) : start(s), end(e) {}
    
    /// Create range from start time to now
    static TimeRange last(uint64_t duration_ms) {
        #ifdef millis
        uint64_t now = millis();
        return TimeRange(now - duration_ms, now);
        #else
        return TimeRange(0, 0);
        #endif
    }
    
    /// Create range between two timestamps
    static TimeRange between(uint64_t start_ms, uint64_t end_ms) {
        return TimeRange(start_ms, end_ms);
    }
    
    /// Check if timestamp is within range
    bool contains(uint64_t timestamp) const {
        return timestamp >= start && timestamp <= end;
    }
    
    /// Get duration in milliseconds
    uint64_t duration() const {
        return end - start;
    }
};

/**
 * @brief Query result wrapper
 * 
 * Provides convenient methods for working with query results
 */
class QueryResult {
public:
    using Container = std::vector<DataPoint>;
    using Iterator = Container::iterator;
    using ConstIterator = Container::const_iterator;
    
    QueryResult() : valid_(false) {}
    
    explicit QueryResult(Container&& data) 
        : data_(std::move(data)), valid_(true) {}
    
    /// Check if query was successful
    bool is_valid() const { return valid_; }
    
    /// Get number of points
    size_t size() const { return data_.size(); }
    
    /// Check if empty
    bool empty() const { return data_.empty(); }
    
    /// Access by index
    DataPoint& operator[](size_t idx) { return data_[idx]; }
    const DataPoint& operator[](size_t idx) const { return data_[idx]; }
    
    /// Iterators
    Iterator begin() { return data_.begin(); }
    Iterator end() { return data_.end(); }
    ConstIterator begin() const { return data_.begin(); }
    ConstIterator end() const { return data_.end(); }
    ConstIterator cbegin() const { return data_.cbegin(); }
    ConstIterator cend() const { return data_.cend(); }
    
    /// Get underlying container
    Container& data() { return data_; }
    const Container& data() const { return data_; }
    
    /// Get first element (or default)
    DataPoint first_or_default() const {
        return empty() ? DataPoint{} : data_[0];
    }
    
    /// Get last element (or default)
    DataPoint last_or_default() const {
        return empty() ? DataPoint{} : data_.back();
    }
    
    /// Calculate average value
    double average() const {
        if (empty()) return 0.0;
        int64_t sum = 0;
        for (const auto& dp : data_) {
            sum += dp.value;
        }
        return static_cast<double>(sum) / data_.size();
    }
    
    /// Get minimum value
    int32_t min_value() const {
        if (empty()) return 0;
        return std::min_element(data_.begin(), data_.end(),
            [](const DataPoint& a, const DataPoint& b) {
                return a.value < b.value;
            })->value;
    }
    
    /// Get maximum value
    int32_t max_value() const {
        if (empty()) return 0;
        return std::max_element(data_.begin(), data_.end(),
            [](const DataPoint& a, const DataPoint& b) {
                return a.value < b.value;
            })->value;
    }
    
private:
    Container data_;
    bool valid_;
};

/**
 * @brief Configuration builder with fluent interface
 * 
 * Usage:
 *   auto config = DBConfig::create()
 *       .path("/sdcard/data")
 *       .prefix("sensor")
 *       .max_file_size(50_MB)
 *       .enable_indexing()
 *       .build();
 */
class DBConfig {
public:
    /// Create with defaults
    static DBConfig create() {
        return DBConfig();
    }
    
    /// Set base path
    DBConfig& path(const char* p) {
        config_.base_path = p;
        return *this;
    }
    
    /// Set filename prefix
    DBConfig& prefix(const char* p) {
        config_.filename_prefix = p;
        return *this;
    }
    
    /// Set max file size (in bytes)
    DBConfig& max_file_size(uint32_t size) {
        config_.max_file_size_bytes = size;
        config_.enable_rotation = (size > 0);
        return *this;
    }
    
    /// Set max file size with unit helpers
    DBConfig& max_file_size_mb(uint32_t mb) {
        return max_file_size(mb * 1024 * 1024);
    }
    
    /// Set sync interval
    DBConfig& sync_interval(uint32_t ms) {
        config_.sync_interval_ms = ms;
        return *this;
    }
    
    /// Enable indexing
    DBConfig& enable_indexing(bool enable = true) {
        config_.enable_indexing = enable;
        return *this;
    }
    
    /// Enable rotation
    DBConfig& enable_rotation(bool enable = true) {
        config_.enable_rotation = enable;
        return *this;
    }
    
    /// Set buffer sectors
    DBConfig& buffer_sectors(uint8_t sectors) {
        config_.buffer_sectors = sectors;
        return *this;
    }
    
    /// Build final config
    tsdb_config_t build() const {
        return config_;
    }
    
    /// Implicit conversion to tsdb_config_t
    operator tsdb_config_t() const {
        return config_;
    }
    
private:
    DBConfig() {
        config_ = {};
        config_.base_path = "/data";
        config_.filename_prefix = "log";
        config_.max_file_size_bytes = 0;
        config_.sync_interval_ms = 10000;
        config_.enable_indexing = true;
        config_.enable_rotation = false;
        config_.buffer_sectors = TSDB_CONFIG_BUFFER_SECTORS;
    }
    
    tsdb_config_t config_;
};

/**
 * @brief Main database class with simplified API
 * 
 * Usage:
 *   Database db;
 *   db.open(DBConfig::create().path("/sdcard"));
 *   db.write({timestamp, value});
 *   auto results = db.query(TimeRange::last(3600000));
 */
template<size_t BufferSectors = TSDB_CONFIG_BUFFER_SECTORS>
class Database {
public:
    using Record = tsdb_record_t;
    using Error = tsdb_error_t;
    using Stats = tsdb_stats_t;
    
    /// Default constructor
    Database() : db_() {}
    
    /// Destructor - auto closes
    ~Database() {
        close();
    }
    
    // Non-copyable
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    
    /**
     * @brief Open database with configuration
     * @param config Configuration (can use DBConfig::create().build())
     * @param hal Hardware abstraction layer
     * @return true on success
     */
    bool open(const tsdb_config_t& config, const tsdb_hal_t& hal) {
        last_error_ = db_.init(config, hal);
        return last_error_ == TSDB_OK;
    }
    
    /// Open with config builder
    bool open(const DBConfig& config, const tsdb_hal_t& hal) {
        return open(config.build(), hal);
    }
    
    /**
     * @brief Close database
     */
    void close() {
        db_.close();
    }
    
    /**
     * @brief Write a single data point
     * @param dp Data point to write
     * @return true on success
     */
    bool write(const DataPoint& dp) {
        last_error_ = db_.write(dp.to_record());
        return last_error_ == TSDB_OK;
    }
    
    /**
     * @brief Write multiple data points
     * @param points Vector of data points
     * @return true if all written successfully
     */
    bool write(const std::vector<DataPoint>& points) {
        for (const auto& dp : points) {
            if (!write(dp)) {
                return false;
            }
        }
        return true;
    }
    
    /**
     * @brief Write with initializer list
     * @param points Initializer list of {time, value} pairs
     * @return true on success
     * 
     * Usage:
     *   db.write({{t1, v1}, {t2, v2}, {t3, v3}});
     */
    bool write(std::initializer_list<std::pair<uint64_t, int32_t>> points) {
        for (const auto& [time, value] : points) {
            if (!write(DataPoint{time, value})) {
                return false;
            }
        }
        return true;
    }
    
    /**
     * @brief Write raw value with automatic timestamp
     * @param value Sensor value
     * @return true on success
     */
    bool write_value(int32_t value) {
        return write(DataPoint::now(value));
    }
    
    /**
     * @brief Read single record by index
     * @param index Record index (0-based)
     * @param out Output data point
     * @return true on success
     */
    bool read_at(uint64_t index, DataPoint& out) {
        Record rec;
        last_error_ = db_.read_at_index(index, rec);
        if (last_error_ == TSDB_OK) {
            out = DataPoint::from_record(rec);
            return true;
        }
        return false;
    }
    
    /**
     * @brief Query by time range
     * @param range Time range to query
     * @return QueryResult with matching points
     */
    QueryResult query(const TimeRange& range) {
        return query(range.start, range.end);
    }
    
    /**
     * @brief Query by start and end timestamps
     * @param start Start timestamp
     * @param end End timestamp
     * @return QueryResult with matching points
     */
    QueryResult query(uint64_t start, uint64_t end) {
        constexpr size_t MAX_RECORDS = 1024;
        std::vector<Record> records(MAX_RECORDS);
        size_t actual = 0;
        
        last_error_ = db_.read_by_time_range(start, end, 
                                              records.data(), 
                                              MAX_RECORDS, 
                                              actual);
        
        if (last_error_ != TSDB_OK) {
            return QueryResult{};
        }
        
        // Convert to DataPoints
        std::vector<DataPoint> points;
        points.reserve(actual);
        for (size_t i = 0; i < actual; ++i) {
            points.push_back(DataPoint::from_record(records[i]));
        }
        
        return QueryResult(std::move(points));
    }
    
    /**
     * @brief Get last N records
     * @param count Number of records
     * @return QueryResult with last N points
     */
    QueryResult last(size_t count) {
        if (count == 0 || db_.record_count() == 0) {
            return QueryResult{};
        }
        
        uint64_t start_idx = (db_.record_count() > count) 
                           ? db_.record_count() - count 
                           : 0;
        
        std::vector<DataPoint> points;
        points.reserve(count);
        
        for (size_t i = start_idx; i < db_.record_count(); ++i) {
            DataPoint dp;
            if (read_at(i, dp)) {
                points.push_back(dp);
            }
        }
        
        return QueryResult(std::move(points));
    }
    
    /**
     * @brief Get latest record
     * @param out Output data point
     * @return true if record exists
     */
    bool latest(DataPoint& out) {
        if (db_.record_count() == 0) {
            return false;
        }
        return read_at(db_.record_count() - 1, out);
    }
    
    /**
     * @brief Get total record count
     */
    uint64_t count() const {
        return db_.record_count();
    }
    
    /**
     * @brief Flush buffer to disk
     * @return true on success
     */
    bool flush() {
        last_error_ = const_cast<MicroTSDB<BufferSectors>&>(db_).flush();
        return last_error_ == TSDB_OK;
    }
    
    /**
     * @brief Sync file to disk
     * @return true on success
     */
    bool sync() {
        last_error_ = const_cast<MicroTSDB<BufferSectors>&>(db_).sync();
        return last_error_ == TSDB_OK;
    }
    
    /**
     * @brief Get statistics
     */
    Stats stats() const {
        return db_.get_stats();
    }
    
    /**
     * @brief Get last error
     */
    Error last_error() const {
        return last_error_;
    }
    
    /**
     * @brief Check if database is open
     */
    bool is_open() const {
        return db_.is_initialized();
    }
    
    /**
     * @brief Iterate over all records in time range
     * 
     * Usage:
     *   db.for_each(TimeRange::last(3600000), [](const DataPoint& dp) {
     *       printf("Value: %d\n", dp.value);
     *   });
     */
    void for_each(const TimeRange& range, 
                  std::function<void(const DataPoint&)> callback) {
        auto result = query(range);
        for (const auto& dp : result) {
            callback(dp);
        }
    }
    
    /**
     * @brief Transform values in time range
     * 
     * Usage:
     *   auto values = db.transform(TimeRange::last(3600000), 
     *                              [](const DataPoint& dp) { return dp.value; });
     */
    template<typename Func>
    std::vector<decltype(Func{}(DataPoint{}))> 
    transform(const TimeRange& range, Func func) {
        using ResultType = decltype(func(DataPoint{}));
        std::vector<ResultType> results;
        
        auto result = query(range);
        results.reserve(result.size());
        
        for (const auto& dp : result) {
            results.push_back(func(dp));
        }
        
        return results;
    }
    
private:
    MicroTSDB<BufferSectors> db_;
    Error last_error_ = TSDB_OK;
};

// Type aliases for convenience
using EasyDB = Database<>;  // Default buffer size

} // namespace easy
} // namespace micro_tsdb

#endif // __cplusplus

#endif // TSDB_EASY_HPP
