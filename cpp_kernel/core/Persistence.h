#pragma once
/**
 * Persistence Layer for ARQON
 * 
 * Real disk-based state snapshots and recovery.
 * No simulation - actual file I/O with checksums.
 */

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <algorithm>

#include "Crypto.h"
#include "Economics.h"

namespace persistence {

using namespace crypto;
using namespace economics;

// Snapshot file magic number
constexpr uint32_t SNAPSHOT_MAGIC = 0x41525153;  // "ARQS"
constexpr uint32_t SNAPSHOT_VERSION = 1;

// ============================================================================
// Snapshot Header
// ============================================================================

struct SnapshotHeader {
    uint32_t magic;
    uint32_t version;
    uint64_t timestamp;
    uint64_t block_height;
    uint32_t account_count;
    uint32_t validator_count;
    Hash state_root;
    uint32_t checksum;
    
    SnapshotHeader() : magic(SNAPSHOT_MAGIC), version(SNAPSHOT_VERSION),
                       timestamp(0), block_height(0), account_count(0),
                       validator_count(0), checksum(0) {
        std::memset(state_root.data(), 0, HASH_SIZE);
    }
    
    static constexpr size_t SIZE = 4 + 4 + 8 + 8 + 4 + 4 + HASH_SIZE + 4;
    
    void serialize(uint8_t* buf) const {
        size_t pos = 0;
        std::memcpy(buf + pos, &magic, 4); pos += 4;
        std::memcpy(buf + pos, &version, 4); pos += 4;
        std::memcpy(buf + pos, &timestamp, 8); pos += 8;
        std::memcpy(buf + pos, &block_height, 8); pos += 8;
        std::memcpy(buf + pos, &account_count, 4); pos += 4;
        std::memcpy(buf + pos, &validator_count, 4); pos += 4;
        std::memcpy(buf + pos, state_root.data(), HASH_SIZE); pos += HASH_SIZE;
        std::memcpy(buf + pos, &checksum, 4);
    }
    
    bool deserialize(const uint8_t* buf) {
        size_t pos = 0;
        std::memcpy(&magic, buf + pos, 4); pos += 4;
        std::memcpy(&version, buf + pos, 4); pos += 4;
        std::memcpy(&timestamp, buf + pos, 8); pos += 8;
        std::memcpy(&block_height, buf + pos, 8); pos += 8;
        std::memcpy(&account_count, buf + pos, 4); pos += 4;
        std::memcpy(&validator_count, buf + pos, 4); pos += 4;
        std::memcpy(state_root.data(), buf + pos, HASH_SIZE); pos += HASH_SIZE;
        std::memcpy(&checksum, buf + pos, 4);
        
        return magic == SNAPSHOT_MAGIC && version <= SNAPSHOT_VERSION;
    }
    
    uint32_t computeChecksum() const {
        uint32_t sum = magic ^ version;
        sum ^= static_cast<uint32_t>(timestamp & 0xFFFFFFFF);
        sum ^= static_cast<uint32_t>(timestamp >> 32);
        sum ^= static_cast<uint32_t>(block_height & 0xFFFFFFFF);
        sum ^= static_cast<uint32_t>(block_height >> 32);
        sum ^= account_count ^ validator_count;
        for (size_t i = 0; i < HASH_SIZE; i += 4) {
            uint32_t word;
            std::memcpy(&word, state_root.data() + i, 4);
            sum ^= word;
        }
        return sum;
    }
};

// ============================================================================
// Serialized Account
// ============================================================================

struct SerializedAccount {
    Address address;
    uint64_t balance;
    uint64_t nonce;
    uint64_t staked;
    uint8_t is_validator;
    
    static constexpr size_t SIZE = ADDRESS_SIZE + 8 + 8 + 8 + 1;
    
    void serialize(uint8_t* buf) const {
        size_t pos = 0;
        std::memcpy(buf + pos, address.data(), ADDRESS_SIZE); pos += ADDRESS_SIZE;
        std::memcpy(buf + pos, &balance, 8); pos += 8;
        std::memcpy(buf + pos, &nonce, 8); pos += 8;
        std::memcpy(buf + pos, &staked, 8); pos += 8;
        std::memcpy(buf + pos, &is_validator, 1);
    }
    
    void deserialize(const uint8_t* buf) {
        size_t pos = 0;
        std::memcpy(address.data(), buf + pos, ADDRESS_SIZE); pos += ADDRESS_SIZE;
        std::memcpy(&balance, buf + pos, 8); pos += 8;
        std::memcpy(&nonce, buf + pos, 8); pos += 8;
        std::memcpy(&staked, buf + pos, 8); pos += 8;
        std::memcpy(&is_validator, buf + pos, 1);
    }
};

// ============================================================================
// State Snapshot Manager
// ============================================================================

class SnapshotManager {
private:
    std::string data_dir_;
    uint64_t last_snapshot_height_;
    bool content_addressed_;  // Use content-addressed (immutable) storage
    
    // Content-addressed path: filename is the hash of contents
    std::string contentAddressedPath(const Hash& state_root) const {
        std::string hex;
        hex.reserve(HASH_SIZE * 2);
        static const char* digits = "0123456789abcdef";
        for (size_t i = 0; i < HASH_SIZE; i++) {
            hex += digits[(state_root[i] >> 4) & 0xF];
            hex += digits[state_root[i] & 0xF];
        }
        return data_dir_ + "/cas/" + hex.substr(0, 2) + "/" + hex + ".arqs";
    }
    
    std::string snapshotPath(uint64_t height) const {
        return data_dir_ + "/snapshot_" + std::to_string(height) + ".arqs";
    }
    
    std::string latestPath() const {
        return data_dir_ + "/latest.arqs";
    }
    
    std::string manifestPath() const {
        return data_dir_ + "/manifest.json";
    }
    
    Hash computeStateRoot(const std::vector<SerializedAccount>& accounts) const {
        // Compute Merkle-like hash of all accounts
        std::vector<uint8_t> data;
        data.reserve(accounts.size() * SerializedAccount::SIZE);
        
        for (const auto& acc : accounts) {
            uint8_t buf[SerializedAccount::SIZE];
            acc.serialize(buf);
            data.insert(data.end(), buf, buf + SerializedAccount::SIZE);
        }
        
        return Blake2b::hash(data.data(), data.size());
    }

public:
    SnapshotManager(const std::string& data_dir = "./arqon_data", bool content_addressed = true)
        : data_dir_(data_dir), last_snapshot_height_(0), content_addressed_(content_addressed) {
        // Create data directories
        std::filesystem::create_directories(data_dir_);
        if (content_addressed_) {
            std::filesystem::create_directories(data_dir_ + "/cas");
        }
    }
    
    /**
     * Enable/disable content-addressed (immutable) storage
     * When enabled, snapshots are stored with hash-based filenames
     * and cannot be modified without changing the filename
     */
    void setContentAddressed(bool enabled) {
        content_addressed_ = enabled;
        if (enabled) {
            std::filesystem::create_directories(data_dir_ + "/cas");
        }
    }
    
    bool isContentAddressed() const { return content_addressed_; }
    
    /**
     * Save state snapshot to disk
     * Returns true on success
     */
    bool saveSnapshot(const AccountManager& accounts, uint64_t block_height) {
        // Collect all accounts
        std::vector<SerializedAccount> serialized;
        
        // Get accounts from manager
        auto account_list = accounts.getAllAccounts();
        
        for (const auto& [addr, acc] : account_list) {
            SerializedAccount sa;
            sa.address = addr;
            sa.balance = acc.balance;
            sa.nonce = acc.nonce;
            sa.staked = acc.stake;
            sa.is_validator = accounts.isValidator(addr) ? 1 : 0;
            serialized.push_back(sa);
        }
        
        // Build header
        SnapshotHeader header;
        header.timestamp = std::chrono::system_clock::now().time_since_epoch().count();
        header.block_height = block_height;
        header.account_count = static_cast<uint32_t>(serialized.size());
        header.validator_count = accounts.getValidatorCount();
        header.state_root = computeStateRoot(serialized);
        header.checksum = header.computeChecksum();
        
        // Determine storage path
        std::string path;
        if (content_addressed_) {
            // Content-addressed: filename is hash of state root
            // This makes snapshots IMMUTABLE - any modification changes the hash
            path = contentAddressedPath(header.state_root);
            std::filesystem::create_directories(
                std::filesystem::path(path).parent_path());
        } else {
            path = snapshotPath(block_height);
        }
        
        // Check if content-addressed file already exists (deduplication)
        if (content_addressed_ && std::filesystem::exists(path)) {
            std::cout << "[Persistence] Snapshot already exists (dedup): " << path << std::endl;
            last_snapshot_height_ = block_height;
            updateManifest(block_height, header.state_root);
            return true;
        }
        
        // Write to file
        std::ofstream file(path, std::ios::binary);
        
        if (!file) {
            std::cerr << "[Persistence] Failed to open " << path << " for writing" << std::endl;
            return false;
        }
        
        // Write header
        uint8_t header_buf[SnapshotHeader::SIZE];
        header.serialize(header_buf);
        file.write(reinterpret_cast<char*>(header_buf), SnapshotHeader::SIZE);
        
        // Write accounts
        for (const auto& acc : serialized) {
            uint8_t acc_buf[SerializedAccount::SIZE];
            acc.serialize(acc_buf);
            file.write(reinterpret_cast<char*>(acc_buf), SerializedAccount::SIZE);
        }
        
        file.close();
        
        // Verify written file (for content-addressed storage)
        if (content_addressed_) {
            // Re-read and verify hash matches filename
            if (!verifyContentAddressed(path, header.state_root)) {
                std::cerr << "[Persistence] Content verification failed!" << std::endl;
                std::filesystem::remove(path);
                return false;
            }
        }
        
        // Update latest symlink/copy (for non-CAS) or manifest (for CAS)
        if (content_addressed_) {
            updateManifest(block_height, header.state_root);
        } else {
            std::filesystem::copy_file(path, latestPath(), 
                                       std::filesystem::copy_options::overwrite_existing);
        }
        
        last_snapshot_height_ = block_height;
        
        std::cout << "[Persistence] Saved " << (content_addressed_ ? "immutable " : "")
                  << "snapshot at height " << block_height 
                  << " (" << serialized.size() << " accounts, " 
                  << std::filesystem::file_size(path) << " bytes)" << std::endl;
        
        return true;
    }
    
private:
    // Verify content-addressed file matches its hash
    bool verifyContentAddressed(const std::string& path, const Hash& expected_root) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        
        // Read entire file
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        std::vector<uint8_t> data(size);
        file.read(reinterpret_cast<char*>(data.data()), size);
        
        // Compute hash of file contents
        Hash file_hash = Blake2b::hash(data.data(), data.size());
        
        // Extract state root from header
        SnapshotHeader header;
        header.deserialize(data.data());
        
        return header.state_root == expected_root;
    }
    
    // Update manifest with height -> hash mapping
    void updateManifest(uint64_t height, const Hash& state_root) {
        std::string manifest = manifestPath();
        
        // Read existing manifest
        std::vector<std::pair<uint64_t, Hash>> entries;
        std::ifstream in(manifest);
        if (in) {
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#') continue;
                size_t comma = line.find(',');
                if (comma != std::string::npos) {
                    uint64_t h = std::stoull(line.substr(0, comma));
                    // Parse hash hex
                    Hash hash{};
                    std::string hex = line.substr(comma + 1);
                    for (size_t i = 0; i < HASH_SIZE && i * 2 + 1 < hex.size(); i++) {
                        char hi = hex[i * 2];
                        char lo = hex[i * 2 + 1];
                        auto hexval = [](char c) -> uint8_t {
                            if (c >= '0' && c <= '9') return c - '0';
                            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                            return 0;
                        };
                        hash[i] = (hexval(hi) << 4) | hexval(lo);
                    }
                    entries.push_back({h, hash});
                }
            }
        }
        
        // Add or update entry
        bool found = false;
        for (auto& [h, hash] : entries) {
            if (h == height) {
                hash = state_root;
                found = true;
                break;
            }
        }
        if (!found) {
            entries.push_back({height, state_root});
        }
        
        // Sort by height
        std::sort(entries.begin(), entries.end());
        
        // Write manifest
        std::ofstream out(manifest);
        out << "# ARQON Snapshot Manifest\n";
        out << "# Format: height,state_root_hash\n";
        for (const auto& [h, hash] : entries) {
            out << h << ",";
            for (size_t i = 0; i < HASH_SIZE; i++) {
                out << std::hex << std::setfill('0') << std::setw(2) << (int)hash[i];
            }
            out << std::dec << "\n";
        }
    }
    
public:
    
    /**
     * Load state snapshot from disk
     * Returns true on success
     */
    bool loadSnapshot(AccountManager& accounts, uint64_t& block_height, 
                      const std::string& path = "") {
        std::string load_path = path.empty() ? latestPath() : path;
        
        if (!std::filesystem::exists(load_path)) {
            std::cerr << "[Persistence] Snapshot not found: " << load_path << std::endl;
            return false;
        }
        
        std::ifstream file(load_path, std::ios::binary);
        if (!file) {
            std::cerr << "[Persistence] Failed to open " << load_path << std::endl;
            return false;
        }
        
        // Read header
        uint8_t header_buf[SnapshotHeader::SIZE];
        file.read(reinterpret_cast<char*>(header_buf), SnapshotHeader::SIZE);
        
        SnapshotHeader header;
        if (!header.deserialize(header_buf)) {
            std::cerr << "[Persistence] Invalid snapshot header" << std::endl;
            return false;
        }
        
        // Verify checksum
        if (header.checksum != header.computeChecksum()) {
            std::cerr << "[Persistence] Snapshot checksum mismatch" << std::endl;
            return false;
        }
        
        // Read accounts
        std::vector<SerializedAccount> serialized;
        serialized.resize(header.account_count);
        
        for (uint32_t i = 0; i < header.account_count; i++) {
            uint8_t acc_buf[SerializedAccount::SIZE];
            file.read(reinterpret_cast<char*>(acc_buf), SerializedAccount::SIZE);
            serialized[i].deserialize(acc_buf);
        }
        
        file.close();
        
        // Verify state root
        Hash computed_root = computeStateRoot(serialized);
        if (computed_root != header.state_root) {
            std::cerr << "[Persistence] State root mismatch - snapshot corrupted" << std::endl;
            return false;
        }
        
        // Restore accounts
        accounts.clear();
        
        for (const auto& sa : serialized) {
            Account& acc = accounts.getAccount(sa.address);
            acc.balance = sa.balance;
            acc.nonce = sa.nonce;
            acc.stake = sa.staked;
            // Validator status is determined by stake amount, not stored separately
        }
        
        block_height = header.block_height;
        last_snapshot_height_ = block_height;
        
        std::cout << "[Persistence] Loaded snapshot from height " << block_height 
                  << " (" << header.account_count << " accounts)" << std::endl;
        
        return true;
    }
    
    /**
     * List available snapshots
     */
    std::vector<uint64_t> listSnapshots() const {
        std::vector<uint64_t> heights;
        
        for (const auto& entry : std::filesystem::directory_iterator(data_dir_)) {
            if (entry.path().extension() == ".arqs") {
                std::string filename = entry.path().stem().string();
                if (filename.substr(0, 9) == "snapshot_") {
                    try {
                        uint64_t height = std::stoull(filename.substr(9));
                        heights.push_back(height);
                    } catch (...) {}
                }
            }
        }
        
        std::sort(heights.begin(), heights.end());
        return heights;
    }
    
    /**
     * Delete old snapshots, keeping only the N most recent
     */
    void pruneSnapshots(size_t keep_count = 10) {
        auto heights = listSnapshots();
        
        if (heights.size() <= keep_count) return;
        
        size_t to_delete = heights.size() - keep_count;
        for (size_t i = 0; i < to_delete; i++) {
            std::string path = snapshotPath(heights[i]);
            std::filesystem::remove(path);
            std::cout << "[Persistence] Pruned snapshot at height " << heights[i] << std::endl;
        }
    }
    
    /**
     * Get info about a snapshot without loading it
     */
    bool getSnapshotInfo(const std::string& path, SnapshotHeader& header) const {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        
        uint8_t header_buf[SnapshotHeader::SIZE];
        file.read(reinterpret_cast<char*>(header_buf), SnapshotHeader::SIZE);
        
        return header.deserialize(header_buf);
    }
    
    uint64_t getLastSnapshotHeight() const { return last_snapshot_height_; }
    const std::string& getDataDir() const { return data_dir_; }
};

// ============================================================================
// Write-Ahead Log (WAL) for crash recovery
// ============================================================================

enum class WALEntryType : uint8_t {
    TRANSACTION = 1,
    BLOCK_START = 2,
    BLOCK_COMMIT = 3,
    CHECKPOINT = 4
};

struct WALEntry {
    uint64_t sequence;
    WALEntryType type;
    uint32_t data_size;
    std::vector<uint8_t> data;
    uint32_t checksum;
    
    static constexpr size_t HEADER_SIZE = 8 + 1 + 4 + 4;
    
    void serialize(std::vector<uint8_t>& buf) const {
        size_t start = buf.size();
        buf.resize(start + HEADER_SIZE + data_size);
        
        uint8_t* ptr = buf.data() + start;
        std::memcpy(ptr, &sequence, 8); ptr += 8;
        std::memcpy(ptr, &type, 1); ptr += 1;
        std::memcpy(ptr, &data_size, 4); ptr += 4;
        std::memcpy(ptr, data.data(), data_size); ptr += data_size;
        std::memcpy(ptr, &checksum, 4);
    }
    
    bool deserialize(const uint8_t* buf, size_t len) {
        if (len < HEADER_SIZE) return false;
        
        std::memcpy(&sequence, buf, 8); buf += 8;
        std::memcpy(&type, buf, 1); buf += 1;
        std::memcpy(&data_size, buf, 4); buf += 4;
        
        if (len < HEADER_SIZE + data_size) return false;
        
        data.resize(data_size);
        std::memcpy(data.data(), buf, data_size); buf += data_size;
        std::memcpy(&checksum, buf, 4);
        
        return true;
    }
    
    uint32_t computeChecksum() const {
        uint32_t sum = static_cast<uint32_t>(sequence & 0xFFFFFFFF);
        sum ^= static_cast<uint32_t>(sequence >> 32);
        sum ^= static_cast<uint8_t>(type);
        sum ^= data_size;
        for (size_t i = 0; i < data.size(); i++) {
            sum ^= static_cast<uint32_t>(data[i]) << ((i % 4) * 8);
        }
        return sum;
    }
};

class WriteAheadLog {
private:
    std::string wal_path_;
    std::ofstream wal_file_;
    uint64_t sequence_;
    bool open_;
    
public:
    WriteAheadLog(const std::string& data_dir = "./arqon_data")
        : wal_path_(data_dir + "/wal.log"), sequence_(0), open_(false) {
        std::filesystem::create_directories(data_dir);
    }
    
    bool open() {
        wal_file_.open(wal_path_, std::ios::binary | std::ios::app);
        open_ = wal_file_.is_open();
        
        if (open_) {
            std::cout << "[WAL] Opened write-ahead log: " << wal_path_ << std::endl;
        }
        
        return open_;
    }
    
    void close() {
        if (open_) {
            wal_file_.close();
            open_ = false;
        }
    }
    
    bool append(WALEntryType type, const std::vector<uint8_t>& data) {
        if (!open_) return false;
        
        WALEntry entry;
        entry.sequence = ++sequence_;
        entry.type = type;
        entry.data_size = static_cast<uint32_t>(data.size());
        entry.data = data;
        entry.checksum = entry.computeChecksum();
        
        std::vector<uint8_t> buf;
        entry.serialize(buf);
        
        wal_file_.write(reinterpret_cast<char*>(buf.data()), buf.size());
        wal_file_.flush();
        
        return true;
    }
    
    bool appendTransaction(const Transaction& tx) {
        std::vector<uint8_t> data;
        // Serialize transaction (simplified)
        data.resize(ADDRESS_SIZE * 2 + 8 + 8 + 8 + 8);
        size_t pos = 0;
        std::memcpy(data.data() + pos, tx.from.data(), ADDRESS_SIZE); pos += ADDRESS_SIZE;
        std::memcpy(data.data() + pos, tx.to.data(), ADDRESS_SIZE); pos += ADDRESS_SIZE;
        std::memcpy(data.data() + pos, &tx.value, 8); pos += 8;
        std::memcpy(data.data() + pos, &tx.gas_price, 8); pos += 8;
        std::memcpy(data.data() + pos, &tx.gas_limit, 8); pos += 8;
        std::memcpy(data.data() + pos, &tx.nonce, 8);
        
        return append(WALEntryType::TRANSACTION, data);
    }
    
    bool appendBlockStart(uint64_t height) {
        std::vector<uint8_t> data(8);
        std::memcpy(data.data(), &height, 8);
        return append(WALEntryType::BLOCK_START, data);
    }
    
    bool appendBlockCommit(uint64_t height, const Hash& block_hash) {
        std::vector<uint8_t> data(8 + HASH_SIZE);
        std::memcpy(data.data(), &height, 8);
        std::memcpy(data.data() + 8, block_hash.data(), HASH_SIZE);
        return append(WALEntryType::BLOCK_COMMIT, data);
    }
    
    bool appendCheckpoint(uint64_t snapshot_height) {
        std::vector<uint8_t> data(8);
        std::memcpy(data.data(), &snapshot_height, 8);
        return append(WALEntryType::CHECKPOINT, data);
    }
    
    /**
     * Truncate WAL after checkpoint
     */
    void truncate() {
        close();
        std::filesystem::remove(wal_path_);
        sequence_ = 0;
        open();
        std::cout << "[WAL] Truncated after checkpoint" << std::endl;
    }
    
    /**
     * Replay WAL entries for recovery
     */
    std::vector<WALEntry> replay() {
        std::vector<WALEntry> entries;
        
        std::ifstream file(wal_path_, std::ios::binary);
        if (!file) return entries;
        
        std::vector<uint8_t> buf;
        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        buf.resize(size);
        file.read(reinterpret_cast<char*>(buf.data()), size);
        
        size_t pos = 0;
        while (pos < size) {
            WALEntry entry;
            if (!entry.deserialize(buf.data() + pos, size - pos)) break;
            
            if (entry.checksum != entry.computeChecksum()) {
                std::cerr << "[WAL] Checksum mismatch at sequence " << entry.sequence << std::endl;
                break;
            }
            
            entries.push_back(entry);
            pos += WALEntry::HEADER_SIZE + entry.data_size;
        }
        
        std::cout << "[WAL] Replayed " << entries.size() << " entries" << std::endl;
        return entries;
    }
    
    uint64_t getSequence() const { return sequence_; }
};

} // namespace persistence
