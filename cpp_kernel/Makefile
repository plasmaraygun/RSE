# RSE Braided-Torus Kernel Makefile
CXX = g++
CXXFLAGS = -std=c++20 -O3 -Wall -Wextra -I.
LDFLAGS = -lpthread -latomic

# Optional: Enable real ZK proofs with libsecp256k1-zkp
# Install: git clone https://github.com/ElementsProject/secp256k1-zkp
#          cd secp256k1-zkp && ./autogen.sh
#          ./configure --enable-module-rangeproof --enable-module-generator
#          make && sudo make install
# Then: make ZKP=1
ifdef ZKP
CXXFLAGS += -DUSE_SECP256K1_ZKP
LDFLAGS += -lsecp256k1
endif

# Optional: Enable real crypto with libsodium
# Install: sudo apt-get install libsodium-dev
# Then: make SODIUM=1
ifdef SODIUM
CXXFLAGS += -DUSE_LIBSODIUM
LDFLAGS += -lsodium
endif

BUILD_DIR = build

.PHONY: all test test-vm test-blockchain test-hardening clean check

all: test test-vm test-blockchain test-hardening

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/e2e_test: tests/e2e_braided_test.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

$(BUILD_DIR)/bytecode_test: tests/bytecode_test.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

$(BUILD_DIR)/blockchain_test: tests/blockchain_test.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

test: $(BUILD_DIR)/e2e_test
	@echo "Running E2E tests..."
	./$(BUILD_DIR)/e2e_test

test-vm: $(BUILD_DIR)/bytecode_test
	@echo "Running Bytecode VM tests..."
	./$(BUILD_DIR)/bytecode_test

test-blockchain: $(BUILD_DIR)/blockchain_test
	@echo "Running Blockchain tests..."
	./$(BUILD_DIR)/blockchain_test

$(BUILD_DIR)/hardening_test: tests/hardening_test.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

test-hardening: $(BUILD_DIR)/hardening_test
	@echo "Running Hardening tests..."
	./$(BUILD_DIR)/hardening_test

$(BUILD_DIR)/inference_test: tests/inference_test.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

test-inference: $(BUILD_DIR)/inference_test
	@echo "Running Inference tests..."
	./$(BUILD_DIR)/inference_test

check:
	@echo "Syntax checking..."
	$(CXX) $(CXXFLAGS) -fsyntax-only single_torus/BettiRDLKernel.h
	$(CXX) $(CXXFLAGS) -fsyntax-only braided/BraidedKernel.h
	$(CXX) $(CXXFLAGS) -fsyntax-only braided/FaultTolerantBraid.h
	$(CXX) $(CXXFLAGS) -fsyntax-only braided/BlockchainBraid.h
	$(CXX) $(CXXFLAGS) -fsyntax-only os/Bytecode.h
	$(CXX) $(CXXFLAGS) -fsyntax-only os/OSProcess.h
	$(CXX) $(CXXFLAGS) -fsyntax-only core/Crypto.h
	$(CXX) $(CXXFLAGS) -fsyntax-only core/Economics.h
	$(CXX) $(CXXFLAGS) -fsyntax-only network/P2PNode.h
	$(CXX) $(CXXFLAGS) -fsyntax-only network/ProjectionSync.h
	@echo "All OK"

clean:
	rm -rf $(BUILD_DIR)
