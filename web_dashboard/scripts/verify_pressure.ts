
import { RSEKernel } from '../src/core/RSEKernel';
import { FoldingEngine } from '../src/core/FoldingEngine';

// Polyfill
if (!globalThis.crypto) globalThis.crypto = { randomUUID: () => Math.random().toString(36).substring(2) } as any;

async function runPressureTest() {
    console.log("=================================================");
    console.log("   BETTI-OS MEMORY PRESSURE (GC) BENCHMARK      ");
    console.log("=================================================");

    const kernel = new RSEKernel();
    const folder = new FoldingEngine(kernel.space);
    kernel.init();

    // 1. Fill Memory (Saturate Grid)
    console.log("[PHASE 1] Saturating Memory Address Space...");
    const TARGET_AGENTS = 5000;

    for (let i = 0; i < TARGET_AGENTS; i++) {
        kernel.spawnAgent();
    }

    const initialAgents = Array.from(kernel.space.grid.values()).flat().length;
    console.log(`    > Initial Load: ${initialAgents} Processes`);

    // 2. Trigger Compaction (Folding)
    console.log("[PHASE 2] Triggering Memory Compression (Folding)...");
    folder.active = true;
    folder.centerOfGravity = { x: 16, y: 16, z: 16 };

    const start = performance.now();
    let steps = 0;
    const MAX_STEPS = 100;

    while (steps < MAX_STEPS) {
        const collapsed = folder.step();
        if (collapsed) break;
        steps++;
    }
    const end = performance.now();

    const finalAgents = Array.from(kernel.space.grid.values()).flat().length;
    const released = initialAgents - finalAgents;
    const compressionRatio = (released / initialAgents) * 100;

    console.log("\n=================================================");
    console.log("   COMPRESSION RESULTS");
    console.log("=================================================");
    console.log(`Duration:       ${(end - start).toFixed(2)} ms`);
    console.log(`Initial Procs:  ${initialAgents}`);
    console.log(`Final Procs:    ${finalAgents}`);
    console.log(`Freed Procs:    ${released}`);
    console.log(`Comp. Ratio:    ${compressionRatio.toFixed(1)}%`);

    if (compressionRatio > 50.0) {
        console.log("VERDICT: PASS (High-Efficiency GC)");
    } else if (compressionRatio > 10.0) {
        console.log("VERDICT: PASS (Standard GC)");
    } else {
        console.log("VERDICT: FAIL (Ineffective Compression)");
    }
}

runPressureTest().catch(console.error);
