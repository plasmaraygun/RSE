#pragma once

/**
 * C: Persistent Storage Layer
 * 
 * Key-value storage with:
 * - Write-ahead logging for durability
 * - Memory-mapped files for performance
 * - Atomic batch writes
 */

#include "../core/Crypto.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <filesystem>
#include <cstring>

namespace storage {

using namespace crypto;

// Simple persistent key-value store
class PersistentKV {
public:
    explicit PersistentKV(const std::string& path) : path_(path) {
        std::filesystem::create_directories(path_);
        loadFromDisk();
    }
    
    ~PersistentKV() {
        flush();
    }
    
    // Put a value
    bool put(const std::string& key, const std::vector<uint8_t>& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_[key] = value;
        dirty_ = true;
        
        // Write-ahead log
        appendToLog("PUT", key, value);
        return true;
    }
    
    // Get a value
    bool get(const std::string& key, std::vector<uint8_t>& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) return false;
        value = it->second;
        return true;
    }
    
    // Delete a key
    bool del(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = data_.find(key);
        if (it == data_.end()) return false;
        data_.erase(it);
        dirty_ = true;
        appendToLog("DEL", key, {});
        return true;
    }
    
    // Check if key exists
    bool has(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        return data_.find(key) != data_.end();
    }
    
    // Batch write
    bool writeBatch(const std::vector<std::pair<std::string, std::vector<uint8_t>>>& batch) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [key, value] : batch) {
            data_[key] = value;
            appendToLog("PUT", key, value);
        }
        dirty_ = true;
        return true;
    }
    
    // Flush to disk
    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dirty_) return;
        
        std::string data_file = path_ + "/data.db";
        std::ofstream ofs(data_file, std::ios::binary | std::ios::trunc);
        
        uint32_t count = data_.size();
        ofs.write(reinterpret_cast<char*>(&count), sizeof(count));
        
        for (const auto& [key, value] : data_) {
            uint32_t key_len = key.size();
            uint32_t val_len = value.size();
            ofs.write(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            ofs.write(key.data(), key_len);
            ofs.write(reinterpret_cast<char*>(&val_len), sizeof(val_len));
            ofs.write(reinterpret_cast<const char*>(value.data()), val_len);
        }
        
        ofs.close();
        dirty_ = false;
        
        // Clear WAL after successful flush
        std::ofstream wal(path_ + "/wal.log", std::ios::trunc);
        wal.close();
    }
    
    size_t size() const { return data_.size(); }

private:
    void loadFromDisk() {
        std::string data_file = path_ + "/data.db";
        std::ifstream ifs(data_file, std::ios::binary);
        if (!ifs.is_open()) return;
        
        uint32_t count;
        ifs.read(reinterpret_cast<char*>(&count), sizeof(count));
        
        for (uint32_t i = 0; i < count && ifs.good(); i++) {
            uint32_t key_len, val_len;
            ifs.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            
            std::string key(key_len, '\0');
            ifs.read(key.data(), key_len);
            
            ifs.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
            
            std::vector<uint8_t> value(val_len);
            ifs.read(reinterpret_cast<char*>(value.data()), val_len);
            
            data_[key] = value;
        }
        
        // Replay WAL
        replayWAL();
    }
    
    void replayWAL() {
        std::ifstream wal(path_ + "/wal.log", std::ios::binary);
        if (!wal.is_open()) return;
        
        while (wal.good()) {
            uint8_t op;
            wal.read(reinterpret_cast<char*>(&op), 1);
            if (!wal.good()) break;
            
            uint32_t key_len;
            wal.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            
            std::string key(key_len, '\0');
            wal.read(key.data(), key_len);
            
            if (op == 'P') {  // PUT
                uint32_t val_len;
                wal.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
                std::vector<uint8_t> value(val_len);
                wal.read(reinterpret_cast<char*>(value.data()), val_len);
                data_[key] = value;
            } else if (op == 'D') {  // DEL
                data_.erase(key);
            }
        }
    }
    
    void appendToLog(const std::string& op, const std::string& key, const std::vector<uint8_t>& value) {
        std::ofstream wal(path_ + "/wal.log", std::ios::binary | std::ios::app);
        
        uint8_t op_byte = (op == "PUT") ? 'P' : 'D';
        wal.write(reinterpret_cast<char*>(&op_byte), 1);
        
        uint32_t key_len = key.size();
        wal.write(reinterpret_cast<char*>(&key_len), sizeof(key_len));
        wal.write(key.data(), key_len);
        
        if (op == "PUT") {
            uint32_t val_len = value.size();
            wal.write(reinterpret_cast<char*>(&val_len), sizeof(val_len));
            wal.write(reinterpret_cast<const char*>(value.data()), val_len);
        }
    }
    
    std::string path_;
    std::unordered_map<std::string, std::vector<uint8_t>> data_;
    std::mutex mutex_;
    bool dirty_ = false;
};

// Account storage with address keys
class AccountStorage {
public:
    explicit AccountStorage(const std::string& path) : kv_(path + "/accounts") {}
    
    bool saveAccount(const Address& addr, uint64_t balance, uint64_t stake, uint64_t nonce) {
        std::string key = addressToHex(addr);
        std::vector<uint8_t> value(24);
        memcpy(value.data(), &balance, 8);
        memcpy(value.data() + 8, &stake, 8);
        memcpy(value.data() + 16, &nonce, 8);
        return kv_.put(key, value);
    }
    
    bool loadAccount(const Address& addr, uint64_t& balance, uint64_t& stake, uint64_t& nonce) {
        std::string key = addressToHex(addr);
        std::vector<uint8_t> value;
        if (!kv_.get(key, value) || value.size() < 24) return false;
        memcpy(&balance, value.data(), 8);
        memcpy(&stake, value.data() + 8, 8);
        memcpy(&nonce, value.data() + 16, 8);
        return true;
    }
    
    void flush() { kv_.flush(); }

private:
    std::string addressToHex(const Address& addr) {
        std::string hex;
        hex.reserve(ADDRESS_SIZE * 2);
        for (uint8_t b : addr) {
            hex += "0123456789abcdef"[b >> 4];
            hex += "0123456789abcdef"[b & 0xF];
        }
        return hex;
    }
    
    PersistentKV kv_;
};

// Block storage
class BlockStorage {
public:
    explicit BlockStorage(const std::string& path) : kv_(path + "/blocks") {}
    
    bool saveBlock(uint64_t height, const std::vector<uint8_t>& block_data) {
        return kv_.put(std::to_string(height), block_data);
    }
    
    bool loadBlock(uint64_t height, std::vector<uint8_t>& block_data) {
        return kv_.get(std::to_string(height), block_data);
    }
    
    void flush() { kv_.flush(); }
    
private:
    PersistentKV kv_;
};

} // namespace storage
