#pragma once

/**
 * H: GPU Detection
 * 
 * Detects and queries GPU hardware for:
 * - CUDA devices
 * - Compute capability
 * - VRAM, cores, TFLOPS
 */

#include "../inference/InferenceNode.h"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <array>
#include <memory>

namespace hardware {

using namespace inference;

// GPU vendor
enum class GPUVendor {
    UNKNOWN,
    NVIDIA,
    AMD,
    INTEL
};

// Detected GPU
struct DetectedGPU {
    std::string name;
    GPUVendor vendor;
    uint32_t device_id;
    uint32_t vram_mb;
    uint32_t cuda_cores;
    uint32_t tensor_cores;
    double tflops_fp16;
    double tflops_fp32;
    double memory_bandwidth_gbps;
    int compute_major;
    int compute_minor;
    bool is_available;
    
    std::string computeCapability() const {
        return std::to_string(compute_major) + "." + std::to_string(compute_minor);
    }
    
    ComputeMetrics toComputeMetrics() const {
        ComputeMetrics m;
        m.cuda_cores = cuda_cores;
        m.tensor_cores = tensor_cores;
        m.tflops_fp16 = tflops_fp16;
        m.tflops_fp32 = tflops_fp32;
        m.vram_mb = vram_mb;
        m.memory_bandwidth_gbps = memory_bandwidth_gbps;
        return m;
    }
};

class GPUDetector {
public:
    GPUDetector() {
        detect();
    }
    
    // Detect all GPUs
    void detect() {
        gpus_.clear();
        
        // Try NVIDIA first
        detectNvidia();
        
        // Try AMD
        detectAMD();
        
        // Try Intel
        detectIntel();
        
        // Fallback to /proc parsing
        if (gpus_.empty()) {
            detectFromProc();
        }
    }
    
    // Get all detected GPUs
    const std::vector<DetectedGPU>& getGPUs() const { return gpus_; }
    
    // Get GPU count
    size_t count() const { return gpus_.size(); }
    
    // Get total compute metrics
    ComputeMetrics getTotalCompute() const {
        ComputeMetrics total;
        for (const auto& gpu : gpus_) {
            auto m = gpu.toComputeMetrics();
            total.cuda_cores += m.cuda_cores;
            total.tensor_cores += m.tensor_cores;
            total.tflops_fp16 += m.tflops_fp16;
            total.tflops_fp32 += m.tflops_fp32;
            total.vram_mb += m.vram_mb;
            total.memory_bandwidth_gbps += m.memory_bandwidth_gbps;
        }
        return total;
    }
    
    // Print GPU info
    void print() const {
        if (gpus_.empty()) {
            std::cout << "[GPU] No GPUs detected" << std::endl;
            return;
        }
        
        std::cout << "[GPU] Detected " << gpus_.size() << " GPU(s):" << std::endl;
        for (size_t i = 0; i < gpus_.size(); i++) {
            const auto& gpu = gpus_[i];
            std::cout << "  [" << i << "] " << gpu.name << std::endl;
            std::cout << "      VRAM: " << gpu.vram_mb << " MB" << std::endl;
            std::cout << "      CUDA Cores: " << gpu.cuda_cores << std::endl;
            std::cout << "      Tensor Cores: " << gpu.tensor_cores << std::endl;
            std::cout << "      FP16: " << gpu.tflops_fp16 << " TFLOPS" << std::endl;
            std::cout << "      Compute: " << gpu.computeCapability() << std::endl;
        }
    }

private:
    // Run command and capture output
    std::string exec(const std::string& cmd) {
        std::array<char, 128> buffer;
        std::string result;
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
        if (!pipe) return "";
        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }
        return result;
    }
    
    // Detect NVIDIA GPUs using nvidia-smi
    void detectNvidia() {
        std::string output = exec("nvidia-smi --query-gpu=name,memory.total,compute_cap --format=csv,noheader,nounits 2>/dev/null");
        if (output.empty()) return;
        
        std::istringstream iss(output);
        std::string line;
        uint32_t device_id = 0;
        
        while (std::getline(iss, line)) {
            if (line.empty()) continue;
            
            DetectedGPU gpu;
            gpu.vendor = GPUVendor::NVIDIA;
            gpu.device_id = device_id++;
            gpu.is_available = true;
            
            // Parse CSV: name, memory, compute_cap
            std::istringstream ls(line);
            std::string token;
            int field = 0;
            
            while (std::getline(ls, token, ',')) {
                // Trim whitespace
                size_t start = token.find_first_not_of(" \t");
                size_t end = token.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    token = token.substr(start, end - start + 1);
                }
                
                switch (field) {
                    case 0: gpu.name = token; break;
                    case 1: gpu.vram_mb = std::stoi(token); break;
                    case 2: {
                        size_t dot = token.find('.');
                        if (dot != std::string::npos) {
                            gpu.compute_major = std::stoi(token.substr(0, dot));
                            gpu.compute_minor = std::stoi(token.substr(dot + 1));
                        }
                        break;
                    }
                }
                field++;
            }
            
            // Estimate specs based on GPU name
            estimateNvidiaSpecs(gpu);
            gpus_.push_back(gpu);
        }
    }
    
    void estimateNvidiaSpecs(DetectedGPU& gpu) {
        // Rough estimates based on common GPUs
        std::string name = gpu.name;
        
        if (name.find("4090") != std::string::npos) {
            gpu.cuda_cores = 16384;
            gpu.tensor_cores = 512;
            gpu.tflops_fp16 = 165.0;
            gpu.tflops_fp32 = 82.5;
            gpu.memory_bandwidth_gbps = 1008;
        } else if (name.find("4080") != std::string::npos) {
            gpu.cuda_cores = 9728;
            gpu.tensor_cores = 304;
            gpu.tflops_fp16 = 97.0;
            gpu.tflops_fp32 = 48.5;
            gpu.memory_bandwidth_gbps = 717;
        } else if (name.find("3090") != std::string::npos) {
            gpu.cuda_cores = 10496;
            gpu.tensor_cores = 328;
            gpu.tflops_fp16 = 71.0;
            gpu.tflops_fp32 = 35.5;
            gpu.memory_bandwidth_gbps = 936;
        } else if (name.find("3080") != std::string::npos) {
            gpu.cuda_cores = 8704;
            gpu.tensor_cores = 272;
            gpu.tflops_fp16 = 59.0;
            gpu.tflops_fp32 = 29.5;
            gpu.memory_bandwidth_gbps = 760;
        } else if (name.find("A100") != std::string::npos) {
            gpu.cuda_cores = 6912;
            gpu.tensor_cores = 432;
            gpu.tflops_fp16 = 312.0;
            gpu.tflops_fp32 = 19.5;
            gpu.memory_bandwidth_gbps = 2039;
        } else if (name.find("H100") != std::string::npos) {
            gpu.cuda_cores = 16896;
            gpu.tensor_cores = 528;
            gpu.tflops_fp16 = 989.0;
            gpu.tflops_fp32 = 67.0;
            gpu.memory_bandwidth_gbps = 3350;
        } else {
            // Default estimates
            gpu.cuda_cores = 2048;
            gpu.tensor_cores = 64;
            gpu.tflops_fp16 = 20.0;
            gpu.tflops_fp32 = 10.0;
            gpu.memory_bandwidth_gbps = 320;
        }
    }
    
    // Detect AMD GPUs
    void detectAMD() {
        std::string output = exec("rocm-smi --showproductname 2>/dev/null");
        if (output.empty()) return;
        
        // Parse AMD GPU info (simplified)
        if (output.find("GPU") != std::string::npos) {
            DetectedGPU gpu;
            gpu.vendor = GPUVendor::AMD;
            gpu.device_id = gpus_.size();
            gpu.name = "AMD GPU";
            gpu.vram_mb = 16000;  // Default estimate
            gpu.cuda_cores = 0;
            gpu.tensor_cores = 0;
            gpu.tflops_fp16 = 40.0;
            gpu.tflops_fp32 = 20.0;
            gpu.memory_bandwidth_gbps = 512;
            gpu.is_available = true;
            gpus_.push_back(gpu);
        }
    }
    
    // Detect Intel GPUs
    void detectIntel() {
        // Check for Intel Arc or integrated
        std::ifstream proc("/proc/driver/nvidia/version");
        // Intel detection is complex, skip for now
    }
    
    // Fallback: parse /proc for GPU info
    void detectFromProc() {
        std::ifstream pci("/proc/bus/pci/devices");
        if (!pci.is_open()) return;
        
        std::string line;
        while (std::getline(pci, line)) {
            // Look for display controller (class 03)
            if (line.find("03") != std::string::npos) {
                // Found potential GPU, but can't get detailed info
                // Just note that a GPU exists
            }
        }
    }
    
    std::vector<DetectedGPU> gpus_;
};

} // namespace hardware
