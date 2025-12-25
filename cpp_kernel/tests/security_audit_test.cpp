/**
 * Security Audit Test Runner
 * 
 * Runs the full cryptographic security audit.
 */

#include "../security/CryptoAudit.h"
#include "../core/Crypto.h"

#include <iostream>

using namespace security;
using namespace crypto;

int main() {
    init_crypto();
    
    CryptoAudit audit;
    audit.runFullAudit();
    
    return audit.hasCritical() ? 1 : 0;
}
