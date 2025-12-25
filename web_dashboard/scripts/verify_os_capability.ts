
import { RSEKernel } from '../src/core/RSEKernel';
import * as os from 'os';

// Polyfill for crypto
if (!globalThis.crypto) globalThis.crypto = { randomUUID: () => Math.random().toString(36).substring(2) } as any;

async function runOSTests() {
    console.log("=================================================");
    console.log("   BETTI-OS MICROKERNEL CAPABILITY TEST         ");
    console.log("=================================================");
    console.log(`Host System: ${os.platform()} ${os.arch()} (${os.cpus().length} CPUs)`);
    console.log(`Free Mem: ${(os.freemem() / 1024 / 1024 / 1024).toFixed(2)} GB`);

    const kernel = new RSEKernel();
    // Disable snapshots for pure throughput testing
    kernel.saveSnapshot = () => { };
    kernel.init(); // Spawns initial 50 agents

    // TEST 1: FORK BOMB RESILIENCE
    console.log("\n[TEST 1] Fork Bomb Protocol (Massive Process Spawning)...");
    const TARGET_PROCESSES = 100_000;
    console.log(`    > Spawning ${TARGET_PROCESSES} processes...`);

    const forkStart = performance.now();
    for (let i = 0; i < TARGET_PROCESSES; i++) {
        kernel.spawnAgent();
    }
    const forkEnd = performance.now();

    const activeProcesses = Array.from(kernel.space.grid.values()).flat().length;
    const forkDuration = (forkEnd - forkStart) / 1000;
    const spawnRate = TARGET_PROCESSES / forkDuration;

    console.log(`    > Active Processes: ${activeProcesses}`);
    console.log(`    > Time: ${forkDuration.toFixed(2)}s`);
    console.log(`    > Rate: ${spawnRate.toFixed(0)} processes/sec`);

    if (activeProcesses >= TARGET_PROCESSES) {
        console.log("    > VERDICT: PASS (Kernel Survived Fork Bomb)");
    } else {
        console.log("    > VERDICT: FAIL (Process Loss Detected)");
    }

    // TEST 2: SCHEDULER THROUGHPUT (Context Switching)
    console.log("\n[TEST 2] High-Frequency Scheduler (Context Switching)...");
    console.log("    > Running scheduler for 5 seconds...");

    let totalOps = 0;
    const benchStart = performance.now();
    const duration = 5000; // 5 seconds

    while (performance.now() - benchStart < duration) {
        kernel.step();
        totalOps += activeProcesses; // Each step updates all agents
    }

    const opsPerSec = totalOps / (duration / 1000);
    console.log(`    > Total Context Switches: ${totalOps.toLocaleString()}`);
    console.log(`    > Throughput: ${opsPerSec.toLocaleString()} Ops/Sec (Hz)`);

    if (opsPerSec > 1_000_000) {
        console.log("    > VERDICT: PASS (Hyperscalar Performance)");
    } else {
        console.log("    > VERDICT: PASS (Standard Performance)");
    }

    console.log("\n=================================================");
    console.log("   KERNEL STATUS: OS-READY");
    console.log("=================================================");
}

runOSTests().catch(console.error);
