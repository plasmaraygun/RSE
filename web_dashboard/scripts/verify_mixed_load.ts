
import { RSEKernel } from '../src/core/RSEKernel';
import * as os from 'os';

// Polyfill
if (!globalThis.crypto) globalThis.crypto = { randomUUID: () => Math.random().toString(36).substring(2) } as any;

async function runMixedLoadTest() {
    console.log("=================================================");
    console.log("   BETTI-OS MIXED WORKLOAD (CHAOS) BENCHMARK    ");
    console.log("=================================================");

    const kernel = new RSEKernel();
    kernel.saveSnapshot = () => { }; // Disable snapshots for raw perf
    kernel.init();

    const DURATION_MS = 10000;
    const TARGET_LOAD = 5000; // Target active processes
    const CHURN_RATE = 500;   // Process spawn/kill per tick

    console.log(`[CONFIG] Duration: ${DURATION_MS}ms | Target Load: ${TARGET_LOAD} | Churn: ${CHURN_RATE}/tick`);

    let tickCount = 0;
    let maxJitter = 0;
    let totalTime = 0;
    const latencies: number[] = [];

    const start = performance.now();
    let lastTick = start;

    while (performance.now() - start < DURATION_MS) {
        const tickStart = performance.now();

        // 1. Scheduler Tick
        kernel.step();

        // 2. Chaos (Spawn/Kill)
        // Get total agents safely handling the array-based grid
        const agents = Array.from(kernel.space.grid.values()).flat();
        const currentLoad = agents.length;

        // Auto-balance to target load
        if (currentLoad < TARGET_LOAD) {
            for (let i = 0; i < CHURN_RATE; i++) kernel.spawnAgent();
        } else {
            // Randomly kill agents to simulate process exit
            // We'll just delete random keys or pop from arrays for speed in demo
            // Real OS would accept kill signals
            const keys = Array.from(kernel.space.grid.keys());
            for (let i = 0; i < CHURN_RATE; i++) {
                if (keys.length === 0) break;
                const rKey = keys[Math.floor(Math.random() * keys.length)];
                const cell = kernel.space.grid.get(rKey);
                if (cell && cell.length > 0) {
                    cell.pop(); // Kill process
                    if (cell.length === 0) kernel.space.grid.delete(rKey);
                }
            }
        }

        const tickEnd = performance.now();
        const delta = tickEnd - tickStart;
        latencies.push(delta);

        if (tickCount > 0) { // Skip first tick warmup
            const jitter = Math.abs(delta - (totalTime / tickCount));
            if (jitter > maxJitter) maxJitter = jitter;
        }

        totalTime += delta;
        tickCount++;

        if (tickCount % 100 === 0) {
            process.stdout.write(`    > Cycle: ${tickCount} | Procs: ${currentLoad} | Latency: ${delta.toFixed(2)}ms\r`);
        }
    }

    const avgLatency = totalTime / tickCount;
    // Calculate Standard Deviation
    const variance = latencies.reduce((sum, val) => sum + Math.pow(val - avgLatency, 2), 0) / tickCount;
    const stdDev = Math.sqrt(variance);

    console.log("\n=================================================");
    console.log("   CHAOS RESULTS");
    console.log("=================================================");
    console.log(`Total Ticks:    ${tickCount}`);
    console.log(`Avg Latency:    ${avgLatency.toFixed(3)} ms`);
    console.log(`Max Jitter:     ${maxJitter.toFixed(3)} ms`);
    console.log(`Standard Dev:   ${stdDev.toFixed(3)} ms`);

    if (stdDev < 5.0) { // Strict requirement: <5ms jitter
        console.log("VERDICT: PASS (Real-Time Stable)");
    } else if (stdDev < 15.0) {
        console.log("VERDICT: PASS (Soft Real-Time)");
    } else {
        console.log("VERDICT: FAIL (High Jitter)");
    }
}

runMixedLoadTest().catch(console.error);
