#pragma once

#include "../core/RealCrypto.h"
#include <vector>
#include <cmath>
#include <cstring>

/**
 * Merkle Tree for Compact State Proofs
 * 
 * Allows proving inclusion of data in O(log n) space.
 */

namespace crypto_merkle {

using namespace crypto_real;

// ============================================================================
// Merkle Tree Node
// ============================================================================

struct MerkleNode {
    Hash256 hash;
    MerkleNode* left;
    MerkleNode* right;
    
    MerkleNode() : left(nullptr), right(nullptr) {
        hash.fill(0);
    }
    
    ~MerkleNode() {
        delete left;
        delete right;
    }
};

// ============================================================================
// Merkle Proof
// ============================================================================

struct MerkleProof {
    std::vector<Hash256> hashes;  // Sibling hashes along path to root
    std::vector<bool> directions;  // true = right sibling, false = left sibling
    Hash256 root;
    
    size_t size() const {
        return hashes.size() * HASH_SIZE + directions.size() + HASH_SIZE;
    }
    
    // Serialize proof
    std::vector<uint8_t> serialize() const {
        std::vector<uint8_t> data;
        data.reserve(size() + 8);
        
        // Write count
        uint32_t count = static_cast<uint32_t>(hashes.size());
        data.insert(data.end(), reinterpret_cast<const uint8_t*>(&count), 
                    reinterpret_cast<const uint8_t*>(&count) + 4);
        
        // Write hashes
        for (const auto& h : hashes) {
            data.insert(data.end(), h.begin(), h.end());
        }
        
        // Write directions (packed into bytes)
        uint8_t dir_byte = 0;
        for (size_t i = 0; i < directions.size(); i++) {
            if (directions[i]) {
                dir_byte |= (1 << (i % 8));
            }
            
            if ((i % 8) == 7 || i == directions.size() - 1) {
                data.push_back(dir_byte);
                dir_byte = 0;
            }
        }
        
        // Write root
        data.insert(data.end(), root.begin(), root.end());
        
        return data;
    }
    
    // Deserialize proof
    static MerkleProof deserialize(const uint8_t* data, size_t len) {
        MerkleProof proof;
        size_t pos = 0;
        
        // Read count
        uint32_t count;
        std::memcpy(&count, data + pos, 4);
        pos += 4;
        
        // Read hashes
        proof.hashes.resize(count);
        for (uint32_t i = 0; i < count; i++) {
            std::memcpy(proof.hashes[i].data(), data + pos, HASH_SIZE);
            pos += HASH_SIZE;
        }
        
        // Read directions
        proof.directions.resize(count);
        for (uint32_t i = 0; i < count; i++) {
            uint8_t dir_byte = data[pos + (i / 8)];
            proof.directions[i] = (dir_byte & (1 << (i % 8))) != 0;
        }
        pos += (count + 7) / 8;
        
        // Read root
        std::memcpy(proof.root.data(), data + pos, HASH_SIZE);
        
        return proof;
    }
};

// ============================================================================
// Merkle Tree
// ============================================================================

class MerkleTree {
private:
    MerkleNode* root_;
    std::vector<Hash256> leaves_;
    
    // Build tree from leaves
    MerkleNode* buildTree(size_t start, size_t end) {
        if (start >= end) return nullptr;
        
        MerkleNode* node = new MerkleNode();
        
        if (end - start == 1) {
            // Leaf node
            node->hash = leaves_[start];
            return node;
        }
        
        // Internal node
        size_t mid = (start + end) / 2;
        node->left = buildTree(start, mid);
        node->right = buildTree(mid, end);
        
        // Compute hash of concatenated children
        std::vector<uint8_t> combined;
        if (node->left) {
            combined.insert(combined.end(), node->left->hash.begin(), node->left->hash.end());
        }
        if (node->right) {
            combined.insert(combined.end(), node->right->hash.begin(), node->right->hash.end());
        }
        
        node->hash = SHA256::hash(combined);
        
        return node;
    }
    
    // Generate proof for leaf at index
    bool generateProof(MerkleNode* node, size_t index, size_t start, size_t end, MerkleProof& proof) {
        if (!node || start >= end) return false;
        
        if (end - start == 1) {
            // Found leaf
            return start == index;
        }
        
        size_t mid = (start + end) / 2;
        
        if (index < mid) {
            // Target is in left subtree
            if (generateProof(node->left, index, start, mid, proof)) {
                // Add right sibling hash
                if (node->right) {
                    proof.hashes.push_back(node->right->hash);
                    proof.directions.push_back(true);  // Right sibling
                }
                return true;
            }
        } else {
            // Target is in right subtree
            if (generateProof(node->right, index, mid, end, proof)) {
                // Add left sibling hash
                if (node->left) {
                    proof.hashes.push_back(node->left->hash);
                    proof.directions.push_back(false);  // Left sibling
                }
                return true;
            }
        }
        
        return false;
    }
    
public:
    MerkleTree() : root_(nullptr) {}
    
    ~MerkleTree() {
        delete root_;
    }
    
    // Build tree from data items
    void build(const std::vector<std::vector<uint8_t>>& items) {
        delete root_;
        root_ = nullptr;
        leaves_.clear();
        
        if (items.empty()) return;
        
        // Hash all items to create leaves
        for (const auto& item : items) {
            leaves_.push_back(SHA256::hash(item));
        }
        
        // Pad to power of 2
        size_t target_size = 1;
        while (target_size < leaves_.size()) {
            target_size *= 2;
        }
        
        while (leaves_.size() < target_size) {
            leaves_.push_back(Hash256{});  // Zero hash for padding
        }
        
        // Build tree
        root_ = buildTree(0, leaves_.size());
    }
    
    // Get root hash
    Hash256 getRoot() const {
        if (!root_) {
            Hash256 zero;
            zero.fill(0);
            return zero;
        }
        return root_->hash;
    }
    
    // Generate proof for item at index
    MerkleProof prove(size_t index) {
        MerkleProof proof;
        
        if (!root_ || index >= leaves_.size()) {
            return proof;
        }
        
        generateProof(root_, index, 0, leaves_.size(), proof);
        proof.root = root_->hash;
        
        return proof;
    }
    
    // Verify proof
    static bool verify(const Hash256& leaf, const MerkleProof& proof) {
        Hash256 current = leaf;
        
        for (size_t i = 0; i < proof.hashes.size(); i++) {
            std::vector<uint8_t> combined;
            
            if (proof.directions[i]) {
                // Right sibling
                combined.insert(combined.end(), current.begin(), current.end());
                combined.insert(combined.end(), proof.hashes[i].begin(), proof.hashes[i].end());
            } else {
                // Left sibling
                combined.insert(combined.end(), proof.hashes[i].begin(), proof.hashes[i].end());
                combined.insert(combined.end(), current.begin(), current.end());
            }
            
            current = SHA256::hash(combined);
        }
        
        return current == proof.root;
    }
    
    size_t getLeafCount() const { return leaves_.size(); }
};

// ============================================================================
// Sparse Merkle Tree (for account state)
// ============================================================================

class SparseMerkleTree {
private:
    static constexpr size_t TREE_DEPTH = 256;  // For 256-bit keys
    
    struct Node {
        Hash256 hash;
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
        
        Node() { hash.fill(0); }
    };
    
    std::unique_ptr<Node> root_;
    Hash256 default_hash_;  // Hash of empty node
    
    // Get bit at position in key
    bool getBit(const Hash256& key, size_t pos) const {
        size_t byte_idx = pos / 8;
        size_t bit_idx = 7 - (pos % 8);
        return (key[byte_idx] & (1 << bit_idx)) != 0;
    }
    
    // Update tree recursively
    Hash256 update(Node* node, const Hash256& key, const Hash256& value, size_t depth) {
        if (depth == TREE_DEPTH) {
            // Leaf node
            node->hash = value;
            return value;
        }
        
        bool go_right = getBit(key, depth);
        
        if (go_right) {
            if (!node->right) {
                node->right = std::make_unique<Node>();
            }
            Hash256 right_hash = update(node->right.get(), key, value, depth + 1);
            
            Hash256 left_hash = node->left ? node->left->hash : default_hash_;
            
            // Compute parent hash
            std::vector<uint8_t> combined;
            combined.insert(combined.end(), left_hash.begin(), left_hash.end());
            combined.insert(combined.end(), right_hash.begin(), right_hash.end());
            
            node->hash = SHA256::hash(combined);
        } else {
            if (!node->left) {
                node->left = std::make_unique<Node>();
            }
            Hash256 left_hash = update(node->left.get(), key, value, depth + 1);
            
            Hash256 right_hash = node->right ? node->right->hash : default_hash_;
            
            // Compute parent hash
            std::vector<uint8_t> combined;
            combined.insert(combined.end(), left_hash.begin(), left_hash.end());
            combined.insert(combined.end(), right_hash.begin(), right_hash.end());
            
            node->hash = SHA256::hash(combined);
        }
        
        return node->hash;
    }
    
public:
    SparseMerkleTree() {
        default_hash_.fill(0);
        root_ = std::make_unique<Node>();
    }
    
    // Update value for key
    void set(const Hash256& key, const Hash256& value) {
        update(root_.get(), key, value, 0);
    }
    
    // Get root hash
    Hash256 getRoot() const {
        return root_ ? root_->hash : default_hash_;
    }
    
    // Generate inclusion proof for key
    MerkleProof prove(const Hash256& key) {
        MerkleProof proof;
        proof.root = getRoot();
        
        Node* node = root_.get();
        for (size_t depth = 0; depth < TREE_DEPTH && node; depth++) {
            bool go_right = getBit(key, depth);
            
            if (go_right) {
                // Going right, sibling is left
                if (node->left) {
                    proof.hashes.push_back(node->left->hash);
                } else {
                    proof.hashes.push_back(default_hash_);
                }
                proof.directions.push_back(false);  // Left sibling
                node = node->right.get();
            } else {
                // Going left, sibling is right
                if (node->right) {
                    proof.hashes.push_back(node->right->hash);
                } else {
                    proof.hashes.push_back(default_hash_);
                }
                proof.directions.push_back(true);  // Right sibling
                node = node->left.get();
            }
        }
        
        return proof;
    }
    
    // Verify sparse merkle proof
    static bool verify(const Hash256& key, const Hash256& value, const MerkleProof& proof) {
        Hash256 current = value;
        
        for (size_t i = proof.hashes.size(); i > 0; i--) {
            std::vector<uint8_t> combined;
            size_t idx = i - 1;
            
            if (proof.directions[idx]) {
                // Right sibling
                combined.insert(combined.end(), current.begin(), current.end());
                combined.insert(combined.end(), proof.hashes[idx].begin(), proof.hashes[idx].end());
            } else {
                // Left sibling
                combined.insert(combined.end(), proof.hashes[idx].begin(), proof.hashes[idx].end());
                combined.insert(combined.end(), current.begin(), current.end());
            }
            
            current = SHA256::hash(combined);
        }
        
        return current == proof.root;
    }
};

} // namespace crypto_merkle
