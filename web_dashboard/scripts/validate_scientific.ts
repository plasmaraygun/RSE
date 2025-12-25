
import { RSEKernel } from '../src/core/RSEKernel';
import * as fs from 'fs';
import * as path from 'path';

// Polyfill
if (!globalThis.crypto) globalThis.crypto = { randomUUID: () => Math.random().toString(36).substring(2) } as any;

const LOG_FILE = path.join(process.cwd(), 'rse_scientific_validation.csv');

async function runScientificValidation() {
    console.log("=================================================");
    console.log("   RSE SCIENTIFIC VALIDATION PROTOCOL            ");
    console.log("=================================================");

    const CSV_HEADER = "TestType,Step,Memory_MB,TimeDelta_ms\n";
    fs.writeFileSync(LOG_FILE, CSV_HEADER);

    const STEPS = 20000;
    const SAMPLE_RATE = 10;

    // --- TEST 1: CONTROL (Iterative Loop) ---
    console.log("[1] RUNNING CONTROL: Standard Iterative Loop");
    // This represents the baseline cost of a loop in JS
    let start = performance.now();
    let last = start;
    let buffer = new Array(50).fill(0).map(() => ({ x: 0, y: 0, z: 0 })); // 50 items like RSE

    for (let i = 0; i <= STEPS; i++) {
        // Work
        for (let item of buffer) {
            item.x++;
            item.y++;
        }

        if (i % SAMPLE_RATE === 0) {
            const now = performance.now();
            const mem = process.memoryUsage().heapUsed / 1024 / 1024;
            fs.appendFileSync(LOG_FILE, `Control_Iterative,${i},${mem.toFixed(4)},${(now - last).toFixed(4)}\n`);
            last = now;
        }
    }
    console.log("    > Control Run Complete.");

    // --- TEST 2: RSE KERNEL ---
    console.log("[2] RUNNING TEST: RSE Symbolic Kernel");
    const kernel = new RSEKernel();
    kernel.saveSnapshot = () => { }; // Disable history for fair comparison
    kernel.init();

    start = performance.now();
    last = start;

    for (let i = 0; i <= STEPS; i++) {
        kernel.step();

        if (i % SAMPLE_RATE === 0) {
            const now = performance.now();
            const mem = process.memoryUsage().heapUsed / 1024 / 1024;
            fs.appendFileSync(LOG_FILE, `RSE_Kernel,${i},${mem.toFixed(4)},${(now - last).toFixed(4)}\n`);
            last = now;
        }
    }
    console.log("    > RSE Run Complete.");

    console.log(`[DONE] Data written to ${LOG_FILE}`);
}

runScientificValidation().catch(console.error);
