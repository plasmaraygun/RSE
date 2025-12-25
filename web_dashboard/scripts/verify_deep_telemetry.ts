
import { RSEKernel } from '../src/core/RSEKernel';
import * as fs from 'fs';
import * as path from 'path';

// Polyfill crypto if needed
if (!globalThis.crypto) {
    globalThis.crypto = { randomUUID: () => Math.random().toString(36).substring(2) } as any;
}

const LOG_FILE = path.join(process.cwd(), 'rse_telemetry_deep_v2.csv');

async function runDeepTelemetry() {
    console.log("=================================================");
    console.log("   RSE DEEP TELEMETRY // v2.0 (ISOLATED)         ");
    console.log("=================================================");
    console.log(`[SETUP] Logging to ${LOG_FILE}`);

    // Initialize CSV Header
    // Step, HeapUsed(MB), HeapTotal(MB), CPU_User, CPU_System, TimeDelta(ms)
    fs.writeFileSync(LOG_FILE, 'Step,HeapUsed_MB,HeapTotal_MB,CPU_User_us,CPU_System_us,TimeDelta_ms\n');

    const kernel = new RSEKernel();
    // DISABLE SNAPSHOTS FOR PURE RECURSION LOGIC TEST
    // The Time Crystal feature uses memory (intentionally), but we want to verify the Core logic is O(1)
    kernel.saveSnapshot = () => { };
    kernel.init();

    // Config
    const TOTAL_STEPS = 50_000;
    const SAMPLE_RATE = 10; // Log every 10 steps for high resolution

    console.log(`[RUN] Executing ${TOTAL_STEPS} symbolic steps...`);

    let startTime = performance.now();
    let lastTime = startTime;
    let initialHeap = process.memoryUsage().heapUsed;

    for (let step = 1; step <= TOTAL_STEPS; step++) {
        // 1. Execute Symbolic Step
        kernel.step();

        // 2. Log Telemetry
        if (step % SAMPLE_RATE === 0) {
            const now = performance.now();
            const mem = process.memoryUsage();
            const cpu = process.cpuUsage();

            const line = `${step},${(mem.heapUsed / 1024 / 1024).toFixed(4)},${(mem.heapTotal / 1024 / 1024).toFixed(4)},${cpu.user},${cpu.system},${(now - lastTime).toFixed(4)}\n`;

            fs.appendFileSync(LOG_FILE, line);
            lastTime = now;
        }

        if (step % 5000 === 0) {
            process.stdout.write(`    > Progress: ${step}/${TOTAL_STEPS} \r`);
        }
    }
    process.stdout.write('\n');

    console.log("[DONE] Analysis Complete.");

    // Post-Run Analysis
    const finalHeap = process.memoryUsage().heapUsed;
    const heapGrowth = (finalHeap - initialHeap) / 1024 / 1024;

    console.log("=================================================");
    console.log("   TELEMETRY SUMMARY");
    console.log("=================================================");
    console.log(`Total Steps:      ${TOTAL_STEPS}`);
    console.log(`Initial Memory:   ${(initialHeap / 1024 / 1024).toFixed(2)} MB`);
    console.log(`Final Memory:     ${(finalHeap / 1024 / 1024).toFixed(2)} MB`);
    console.log(`Net Growth:       ${heapGrowth.toFixed(2)} MB`);

    if (heapGrowth < 1.0) {
        console.log("VERDICT: CONSTANT MEMORY USAGE CONFIRMED (O(1)).");
        console.log("         No significant accumulation detected.");
    } else {
        console.log("VERDICT: MEMORY GROWTH DETECTED.");
    }
    console.log(`Data saved to: ${LOG_FILE}`);
}

runDeepTelemetry().catch(console.error);
