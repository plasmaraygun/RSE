
import { RSEKernel } from '../src/core/RSEKernel';
import * as fs from 'fs';
import * as path from 'path';

// Polyfill
if (!globalThis.crypto) globalThis.crypto = { randomUUID: () => Math.random().toString(36).substring(2) } as any;

const LOG_FILE = path.join(process.cwd(), 'rse_endurance_test.csv');

async function runEnduranceTest() {
    console.log("=================================================");
    console.log("   RSE ENDURANCE PROTOCOL (1,000,000 STEPS)      ");
    console.log("=================================================");

    // Header
    fs.writeFileSync(LOG_FILE, 'Step,HeapUsed_MB,TimeDelta_ms\n');

    const kernel = new RSEKernel();
    // Disable snapshots for pure logic endurance
    kernel.saveSnapshot = () => { };
    kernel.init();

    const TOTAL_STEPS = 1_000_000;
    const SAMPLE_RATE = 1000; // Log every 1000 steps to keep CSV manageable

    console.log(`[RUN] Executing ${TOTAL_STEPS} steps...`);
    console.log(`[LOG] Writing to ${LOG_FILE} (Sample Rate: ${SAMPLE_RATE})`);

    let start = performance.now();
    let lastLog = start;
    let initialMem = process.memoryUsage().heapUsed;

    for (let i = 1; i <= TOTAL_STEPS; i++) {
        kernel.step();

        if (i % SAMPLE_RATE === 0) {
            const now = performance.now();
            const mem = process.memoryUsage().heapUsed / 1024 / 1024;
            const delta = now - lastLog;

            fs.appendFileSync(LOG_FILE, `${i},${mem.toFixed(4)},${delta.toFixed(4)}\n`);
            lastLog = now;

            // Visual Progress Bar
            if (i % 50000 === 0) {
                const percent = (i / TOTAL_STEPS * 100).toFixed(0);
                const memDelta = mem - (initialMem / 1024 / 1024);
                process.stdout.write(`    > ${percent}% | Cycle: ${i} | Mem: ${mem.toFixed(2)} MB (Δ ${memDelta > 0 ? '+' : ''}${memDelta.toFixed(2)})\r`);
            }
        }
    }

    const end = performance.now();
    const finalMem = process.memoryUsage().heapUsed;
    const netGrowth = (finalMem - initialMem) / 1024 / 1024;

    console.log("\n=================================================");
    console.log("   ENDURANCE RESULTS");
    console.log("=================================================");
    console.log(`Duration:       ${((end - start) / 1000).toFixed(2)} seconds`);
    console.log(`Total Steps:    ${TOTAL_STEPS}`);
    console.log(`Net Mem Change: ${netGrowth.toFixed(4)} MB`);

    if (Math.abs(netGrowth) < 5.0) {
        console.log("VERDICT: STABLE (Industrial Grade)");
    } else {
        console.log("VERDICT: POTENTIAL LEAK");
    }
}

runEnduranceTest().catch(console.error);
