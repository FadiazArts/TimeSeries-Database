/**
 * @file micro_tsdb.hpp
 * @brief C++ Template Implementation of Micro TSDB
 * 
 * Zero-cost abstractions with compile-time buffer allocation
 */

#ifndef MICRO_TSDB_HPP
#define MICRO_TSDB_HPP

#include "micro_tsdb.h"
#include <mutex>
#include <vector>
#include <string>
#include <cstring>

#ifdef __cplusplus

namespace micro_tsdb {

/**
 * @brief Double-buffer manager for non-blocking I/O
 * Implements ping-pong buffering aligned to SD card sectors
 * 
 * @tparam BufferSectors Number of 512-byte sectors per buffer
 */
template<size_t BufferSectors = TSDB_CONFIG_BUFFER_SECTORS>
class DoubleBuffer {
public:
    static constexpr size_t BUFFER_SIZE = BufferSectors * TSDB_CONFIG_SECTOR_SIZE;
    static_assert(BUFFER_SIZE % sizeof(tsdb_record_t) == 0, 
                  "Buffer size must be multiple of record size");
    static_assert(BufferSectors >= 1, "Must have at least 1 sector per buffer");
    
    DoubleBuffer() : write_buffer_idx_(0), buffer_bytes_used_(0) {
        std::memset(buffers_, 0, sizeof(buffers_));
    }
    
    /**
     * @brief Get pointer to current write buffer
     */
    uint8_t* get_write_buffer() {
        return buffers_[write_buffer_idx_];
    }
    
    /**
     * @brief Get pointer to current read/flush buffer
     */
    const uint8_t* get_flush_buffer() const {
        return buffers_[1 - write_buffer_idx_];
    }
    
    /**
     * @brief Swap buffers (called when write buffer is full)
     */
    void swap() {
        write_buffer_idx_ = 1 - write_buffer_idx_;
        buffer_bytes_used_ = 0;
    }
    
    /**
     * @brief Add record to buffer
     * @param record Data record to add
     * @return true if successful, false if buffer is full
     */
    bool add_record(const tsdb_record_t& record) {
        if (buffer_bytes_used_ + sizeof(tsdb_record_t) > BUFFER_SIZE) {
            return false; // Buffer full
        }
        std::memcpy(get_write_buffer() + buffer_bytes_used_, &record, sizeof(tsdb_record_t));
        buffer_bytes_used_ += sizeof(tsdb_record_t);
        return true;
    }
    
    /**
     * @brief Get number of records in current buffer
     */
    size_t record_count() const {
        return buffer_bytes_used_ / sizeof(tsdb_record_t);
    }
    
    /**
     * @brief Get bytes used in current buffer
     */
    size_t bytes_used() const {
        return buffer_bytes_used_;
    }
    
    /**
     * @brief Check if buffer is full
     */
    bool is_full() const {
        return buffer_bytes_used_ >= BUFFER_SIZE;
    }
    
    /**
     * @brief Check if buffer is empty
     */
    bool is_empty() const {
        return buffer_bytes_used_ == 0;
    }
    
    /**
     * @brief Reset buffer
     */
    void reset() {
        buffer_bytes_used_ = 0;
    }
    
    /**
     * @brief Get remaining capacity in bytes
     */
    size_t available_space() const {
        return BUFFER_SIZE - buffer_bytes_used_;
    }
    
private:
    alignas(TSDB_CONFIG_SECTOR_SIZE) uint8_t buffers_[2][BUFFER_SIZE];
    size_t write_buffer_idx_;
    size_t buffer_bytes_used_;
};

/**
 * @brief RAII wrapper for file handles
 * Automatically closes file on destruction
 */
class FileHandle {
public:
    explicit FileHandle() : handle_(nullptr), hal_(nullptr), valid_(false) {}
    
    explicit FileHandle(void* handle, const tsdb_hal_t* hal) 
        : handle_(handle), hal_(hal), valid_(handle != nullptr && hal != nullptr) {}
    
    ~FileHandle() {
        close();
    }
    
    // Non-copyable
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    
    // Movable
    FileHandle(FileHandle&& other) noexcept 
        : handle_(other.handle_), hal_(other.hal_), valid_(other.valid_) {
        other.handle_ = nullptr;
        other.hal_ = nullptr;
        other.valid_ = false;
    }
    
    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            close();
            handle_ = other.handle_;
            hal_ = other.hal_;
            valid_ = other.valid_;
            other.handle_ = nullptr;
            other.hal_ = nullptr;
            other.valid_ = false;
        }
        return *this;
    }
    
    /**
     * @brief Get underlying handle
     */
    void* get() const { return valid_ ? handle_ : nullptr; }
    
    /**
     * @brief Check if handle is valid
     */
    bool is_valid() const { return valid_; }
    
    /**
     * @brief Explicit bool conversion
     */
    explicit operator bool() const { return valid_; }
    
    /**
     * @brief Close the file manually
     */
    void close() {
        if (valid_ && hal_ && hal_->close_file) {
            hal_->close_file(handle_);
            handle_ = nullptr;
            valid_ = false;
        }
    }
    
private:
    void* handle_;
    const tsdb_hal_t* hal_;
    bool valid_;
};

/**
 * @brief Main TSDB class - Thread-Safe Time-Series Database
 * 
 * Features:
 * - Append-only writes
 * - Fixed-length records for O(1) access
 * - Sparse indexing for fast seeks
 * - CRC16 integrity checking
 * - Double-buffering for performance
 * - File rotation
 * - FreeRTOS thread safety
 * 
 * @tparam BufferSectors Number of sectors per buffer (compile-time)
 */
template<size_t BufferSectors = TSDB_CONFIG_BUFFER_SECTORS>
class MicroTSDB {
public:
    using Record = tsdb_record_t;
    using Error = tsdb_error_t;
    using Stats = tsdb_stats_t;
    using Config = tsdb_config_t;
    using IndexEntry = tsdb_index_entry_t;
    
    /**
     * @brief Default constructor
     */
    MicroTSDB();
    
    /**
     * @brief Destructor - automatically flushes and closes
     */
    ~MicroTSDB();
    
    // Non-copyable
    MicroTSDB(const MicroTSDB&) = delete;
    MicroTSDB& operator=(const MicroTSDB&) = delete;
    
    /**
     * @brief Initialize the database
     * @param config Configuration parameters
     * @param hal Hardware abstraction layer
     * @return TSDB_OK on success, error code otherwise
     */
    Error init(const Config& config, const tsdb_hal_t& hal);
    
    /**
     * @brief Write a single record (thread-safe, buffered)
     * @param record Data record to write
     * @return TSDB_OK on success, error code otherwise
     */
    Error write(const Record& record);
    
    /**
     * @brief Write multiple records (batch write)
     * @param records Array of records
     * @param count Number of records
     * @return TSDB_OK on success, error code otherwise
     */
    Error write_batch(const Record* records, size_t count);
    
    /**
     * @brief Read a single record by index (O(1) with index, O(n) without)
     * @param index Record index (0-based from file start)
     * @param record Output record
     * @return TSDB_OK on success, error code otherwise
     */
    Error read_at_index(uint64_t index, Record& record);
    
    /**
     * @brief Read records by timestamp range
     * @param start_ts Start timestamp (inclusive)
     * @param end_ts End timestamp (inclusive)
     * @param records Output array (caller must allocate)
     * @param max_records Maximum records to read
     * @param actual_records Actual records read
     * @return TSDB_OK on success, error code otherwise
     */
    Error read_by_time_range(uint64_t start_ts, uint64_t end_ts, 
                             Record* records, size_t max_records, 
                             size_t& actual_records);
    
    /**
     * @brief Force flush buffer to disk
     * @return TSDB_OK on success, error code otherwise
     */
    Error flush();
    
    /**
     * @brief Sync file to disk (update FAT)
     * @return TSDB_OK on success, error code otherwise
     */
    Error sync();
    
    /**
     * @brief Get database statistics
     * @return Statistics structure
     */
    Stats get_stats() const;
    
    /**
     * @brief Get current record count
     * @return Total number of records
     */
    uint64_t record_count() const;
    
    /**
     * @brief Check if database is initialized
     */
    bool is_initialized() const { return initialized_; }
    
    /**
     * @brief Close the database (auto-called by destructor)
     * @return TSDB_OK on success, error code otherwise
     */
    Error close();
    
    /**
     * @brief Calculate CRC16 checksum (static)
     * @param data Data buffer
     * @param length Data length
     * @return CRC16 value
     */
    static uint16_t calculate_crc16(const void* data, size_t length);
    
    /**
     * @brief Verify record CRC (static)
     * @param record Record to verify
     * @return true if CRC matches
     */
    static bool verify_record(const Record& record);

private:
    // Internal methods
    Error rotate_file_if_needed();
    Error update_index(uint64_t timestamp, uint32_t offset);
    Error load_index();
    Error save_index();
    std::string generate_filename(uint32_t file_index) const;
    std::string generate_index_filename(uint32_t file_index) const;
    
    // State
    tsdb_config_t config_;
    tsdb_hal_t hal_;
    DoubleBuffer<BufferSectors> buffer_;
    std::mutex mutex_;
    
    // File handles
    FileHandle current_file_;
    FileHandle index_file_;
    
    // Tracking
    uint64_t total_records_ = 0;
    uint32_t current_file_index_ = 0;
    uint32_t current_file_size_ = 0;
    uint64_t last_sync_time_ms_ = 0;
    
    // Index cache (sparse)
    std::vector<IndexEntry> index_cache_;
    
    // Statistics
    Stats stats_{};
    
    // Initialized flag
    bool initialized_ = false;
};

// ============================================================================
// Template Implementation
// ============================================================================

template<size_t BufferSectors>
MicroTSDB<BufferSectors>::MicroTSDB() : hal_{} {
    std::memset(&config_, 0, sizeof(config_));
    std::memset(&stats_, 0, sizeof(stats_));
}

template<size_t BufferSectors>
MicroTSDB<BufferSectors>::~MicroTSDB() {
    close();
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::init(const Config& config, const tsdb_hal_t& hal) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return TSDB_ERR_INVALID_PARAM; // Already initialized
    }
    
    // Validate HAL - require minimum function pointers
    if (!hal.open_file || !hal.close_file || !hal.write_file || !hal.read_file) {
        return TSDB_ERR_INVALID_PARAM;
    }
    
    config_ = config;
    hal_ = hal;
    
    // Create base directory if needed
    if (config_.base_path && hal_.create_directory) {
        hal_.create_directory(config_.base_path);
    }
    
    // Load existing index if enabled
    if (config_.enable_indexing) {
        load_index();
    }
    
    // Open or create current data file
    auto filename = generate_filename(current_file_index_);
    void* file_handle = nullptr;
    int result = hal_.open_file(filename.c_str(), true, &file_handle);
    
    if (result != 0 || !file_handle) {
        return TSDB_ERR_FILE_NOT_FOUND;
    }
    
    current_file_ = FileHandle(file_handle, &hal_);
    current_file_size_ = hal_.get_file_size(file_handle);
    total_records_ = current_file_size_ / sizeof(Record);
    
    initialized_ = true;
    stats_.files_created++;
    
    return TSDB_OK;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::write(const Record& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return TSDB_ERR_NOT_INITIALIZED;
    }
    
    // Copy record and calculate CRC if not set
    Record record_with_crc = record;
    if (record_with_crc.crc16 == 0) {
        record_with_crc.crc16 = calculate_crc16(&record_with_crc, sizeof(Record) - 2);
    }
    
    // Try to add to buffer
    if (!buffer_.add_record(record_with_crc)) {
        // Buffer full, flush first
        Error flush_err = flush();
        if (flush_err != TSDB_OK) {
            return flush_err;
        }
        
        // Try again after flush
        if (!buffer_.add_record(record_with_crc)) {
            return TSDB_ERR_BUFFER_FULL;
        }
    }
    
    // Check if we need to auto-flush
    if (buffer_.is_full()) {
        Error flush_err = flush();
        if (flush_err != TSDB_OK) {
            return flush_err;
        }
    }
    
    return TSDB_OK;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::write_batch(const Record* records, size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || !records || count == 0) {
        return TSDB_ERR_INVALID_PARAM;
    }
    
    for (size_t i = 0; i < count; ++i) {
        Error err = write(records[i]);
        if (err != TSDB_OK) {
            return err;
        }
    }
    
    return TSDB_OK;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::read_at_index(uint64_t index, Record& record) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return TSDB_ERR_NOT_INITIALIZED;
    }
    
    if (index >= total_records_) {
        return TSDB_ERR_INVALID_PARAM;
    }
    
    // Calculate byte offset (O(1) thanks to fixed-length records!)
    uint32_t byte_offset = static_cast<uint32_t>(index * sizeof(Record));
    
    // Read from file
    size_t bytes_read = 0;
    int result = hal_.read_file(current_file_.get(), &record, sizeof(Record), 
                                 byte_offset, &bytes_read);
    
    if (result != 0 || bytes_read != sizeof(Record)) {
        return TSDB_ERR_TIMEOUT;
    }
    
    // Verify CRC
    if (!verify_record(record)) {
        stats_.crc_errors_detected++;
        return TSDB_ERR_CRC_MISMATCH;
    }
    
    stats_.total_records_read++;
    return TSDB_OK;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::read_by_time_range(uint64_t start_ts, uint64_t end_ts,
                                              Record* records, size_t max_records,
                                              size_t& actual_records) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || !records || max_records == 0) {
        return TSDB_ERR_INVALID_PARAM;
    }
    
    actual_records = 0;
    
    // Use index to find starting point if available
    uint64_t start_index = 0;
    
    if (config_.enable_indexing && !index_cache_.empty()) {
        // Binary search in index for faster lookup
        for (const auto& entry : index_cache_) {
            if (entry.end_timestamp >= start_ts) {
                start_index = entry.byte_offset / sizeof(Record);
                break;
            }
        }
    }
    
    // Sequential read from start_index
    uint64_t current_index = start_index;
    
    while (actual_records < max_records && current_index < total_records_) {
        Record record;
        Error err = read_at_index(current_index, record);
        
        if (err == TSDB_ERR_CRC_MISMATCH) {
            // Skip corrupted record
            current_index++;
            continue;
        }
        
        if (err != TSDB_OK) {
            return err;
        }
        
        // Check timestamp range
        if (record.timestamp >= start_ts && record.timestamp <= end_ts) {
            records[actual_records++] = record;
        }
        
        // Early exit if past end timestamp (data is time-ordered)
        if (record.timestamp > end_ts) {
            break;
        }
        
        current_index++;
    }
    
    return TSDB_OK;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::flush() {
    if (!initialized_ || buffer_.is_empty()) {
        return TSDB_OK;
    }
    
    // Write buffer to file
    size_t bytes_written = 0;
    int result = hal_.write_file(current_file_.get(), 
                                  buffer_.get_flush_buffer(),
                                  buffer_.bytes_used(), 
                                  &bytes_written);
    
    if (result != 0 || bytes_written != buffer_.bytes_used()) {
        return TSDB_ERR_TIMEOUT;
    }
    
    // Update tracking
    uint32_t records_flushed = buffer_.record_count();
    current_file_size_ += static_cast<uint32_t>(bytes_written);
    total_records_ += records_flushed;
    stats_.total_records_written += records_flushed;
    stats_.total_bytes_written += bytes_written;
    stats_.buffer_flushes++;
    
    // Update last timestamp from last record in buffer
    if (records_flushed > 0) {
        Record last_record;
        std::memcpy(&last_record, 
                    buffer_.get_flush_buffer() + buffer_.bytes_used() - sizeof(Record),
                    sizeof(Record));
        stats_.last_timestamp = last_record.timestamp;
        
        // Update index if enabled
        if (config_.enable_indexing) {
            update_index(last_record.timestamp, current_file_size_);
        }
    }
    
    // Swap buffers (ping-pong)
    buffer_.swap();
    
    // Check file rotation
    if (config_.enable_rotation && config_.max_file_size_bytes > 0) {
        rotate_file_if_needed();
    }
    
    return TSDB_OK;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::sync() {
    Error flush_err = flush();
    if (flush_err != TSDB_OK) {
        return flush_err;
    }
    
    if (hal_.sync_file && current_file_.is_valid()) {
        int result = hal_.sync_file(current_file_.get());
        if (result != 0) {
            return TSDB_ERR_TIMEOUT;
        }
    }
    
    return TSDB_OK;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Stats 
MicroTSDB<BufferSectors>::get_stats() const {
    Stats stats_copy = stats_;
    stats_copy.current_file_size = current_file_size_;
    return stats_copy;
}

template<size_t BufferSectors>
uint64_t MicroTSDB<BufferSectors>::record_count() const {
    return total_records_;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return TSDB_OK;
    }
    
    // Flush remaining data
    Error flush_err = flush();
    if (flush_err != TSDB_OK) {
        return flush_err;
    }
    
    // Sync to disk
    Error sync_err = sync();
    if (sync_err != TSDB_OK) {
        return sync_err;
    }
    
    // Save index before closing
    if (config_.enable_indexing) {
        save_index();
    }
    
    // Close files (RAII handles this, but explicit is clearer)
    current_file_.close();
    index_file_.close();
    
    initialized_ = false;
    return TSDB_OK;
}

template<size_t BufferSectors>
uint16_t MicroTSDB<BufferSectors>::calculate_crc16(const void* data, size_t length) {
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

template<size_t BufferSectors>
bool MicroTSDB<BufferSectors>::verify_record(const Record& record) {
    uint16_t calculated = calculate_crc16(&record, sizeof(Record) - 2);
    return calculated == record.crc16;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::rotate_file_if_needed() {
    if (current_file_size_ < config_.max_file_size_bytes) {
        return TSDB_OK;
    }
    
    // Save index for current file before rotation
    if (config_.enable_indexing) {
        save_index();
        index_cache_.clear();
    }
    
    // Close current file
    current_file_.close();
    
    // Increment file index
    current_file_index_++;
    current_file_size_ = 0;
    
    // Open new file
    auto filename = generate_filename(current_file_index_);
    void* file_handle = nullptr;
    int result = hal_.open_file(filename.c_str(), true, &file_handle);
    
    if (result != 0 || !file_handle) {
        return TSDB_ERR_FILE_NOT_FOUND;
    }
    
    current_file_ = FileHandle(file_handle, &hal_);
    stats_.files_created++;
    
    return TSDB_OK;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::update_index(uint64_t timestamp, uint32_t offset) {
    // Simplified index update - batch updates in production
    if (index_cache_.empty() || 
        index_cache_.back().end_timestamp != timestamp) {
        IndexEntry entry{};
        entry.start_timestamp = index_cache_.empty() ? timestamp : index_cache_.back().end_timestamp;
        entry.end_timestamp = timestamp;
        entry.byte_offset = offset;
        entry.record_count = 1;
        index_cache_.push_back(entry);
    } else {
        // Update existing entry
        index_cache_.back().end_timestamp = timestamp;
        index_cache_.back().record_count++;
    }
    
    return TSDB_OK;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::load_index() {
    auto idx_filename = generate_index_filename(current_file_index_);
    void* idx_handle = nullptr;
    
    int result = hal_.open_file(idx_filename.c_str(), false, &idx_handle);
    if (result != 0 || !idx_handle) {
        // Index doesn't exist yet, that's OK
        return TSDB_OK;
    }
    
    index_file_ = FileHandle(idx_handle, &hal_);
    
    // Read index entries
    size_t file_size = hal_.get_file_size(idx_handle);
    size_t num_entries = file_size / sizeof(IndexEntry);
    
    if (num_entries > 0) {
        index_cache_.resize(num_entries);
        size_t bytes_read = 0;
        result = hal_.read_file(idx_handle, index_cache_.data(), 
                                file_size, 0, &bytes_read);
        
        if (result != 0 || bytes_read != file_size) {
            index_cache_.clear();
            return TSDB_ERR_INDEX_CORRUPT;
        }
    }
    
    return TSDB_OK;
}

template<size_t BufferSectors>
typename MicroTSDB<BufferSectors>::Error 
MicroTSDB<BufferSectors>::save_index() {
    if (index_cache_.empty()) {
        return TSDB_OK;
    }
    
    auto idx_filename = generate_index_filename(current_file_index_);
    void* idx_handle = nullptr;
    
    int result = hal_.open_file(idx_filename.c_str(), true, &idx_handle);
    if (result != 0 || !idx_handle) {
        return TSDB_ERR_FILE_NOT_FOUND;
    }
    
    FileHandle idx_file(idx_handle, &hal_);
    
    // Write index entries
    size_t bytes_to_write = index_cache_.size() * sizeof(IndexEntry);
    size_t bytes_written = 0;
    result = hal_.write_file(idx_handle, index_cache_.data(), 
                             bytes_to_write, &bytes_written);
    
    if (result != 0 || bytes_written != bytes_to_write) {
        return TSDB_ERR_TIMEOUT;
    }
    
    // Sync index file
    if (hal_.sync_file) {
        hal_.sync_file(idx_handle);
    }
    
    return TSDB_OK;
}

template<size_t BufferSectors>
std::string MicroTSDB<BufferSectors>::generate_filename(uint32_t file_index) const {
    char filename[TSDB_CONFIG_MAX_FILENAME_LEN];
    
    if (config_.enable_rotation) {
        std::snprintf(filename, sizeof(filename), "%s_%04u.dat", 
                      config_.filename_prefix ? config_.filename_prefix : "log", 
                      file_index);
    } else {
        std::snprintf(filename, sizeof(filename), "%s.dat", 
                      config_.filename_prefix ? config_.filename_prefix : "log");
    }
    
    if (config_.base_path) {
        return std::string(config_.base_path) + "/" + filename;
    }
    
    return filename;
}

template<size_t BufferSectors>
std::string MicroTSDB<BufferSectors>::generate_index_filename(uint32_t file_index) const {
    char filename[TSDB_CONFIG_MAX_FILENAME_LEN];
    std::snprintf(filename, sizeof(filename), "%s_%04u.idx", 
                  config_.filename_prefix ? config_.filename_prefix : "log", 
                  file_index);
    
    if (config_.base_path) {
        return std::string(config_.base_path) + "/" + filename;
    }
    
    return filename;
}

} // namespace micro_tsdb

#endif // __cplusplus

#endif // MICRO_TSDB_HPP
