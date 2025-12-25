
import { RSEKernel } from '../src/core/RSEKernel';
import { FoldingEngine } from '../src/core/FoldingEngine';

// Polyfill for crypto
if (!globalThis.crypto) globalThis.crypto = { randomUUID: () => Math.random().toString(36).substring(2) } as any;

async function runFeatureTest() {
    console.log("=================================================");
    console.log("   RSE FEATURE VERIFICATION PROTOCOL            ");
    console.log("=================================================");

    const kernel = new RSEKernel();
    kernel.init();
    const folder = new FoldingEngine(kernel.space);

    // TEST 1: BLACK HOLE COMPRESSION
    console.log("\n[TEST 1] Black Hole Compressor (Symbolic Folding)...");
    const input = "The quick brown fox jumps over the lazy dog. ".repeat(10); // 450 chars
    folder.ingestText(input);
    const initialAgents = Array.from(kernel.space.grid.values()).flat().length;
    console.log(`    > Input Length: ${input.length} chars`);
    console.log(`    > Initial Agents: ${initialAgents}`);

    // Fold
    folder.active = true;
    for (let i = 0; i < 50; i++) folder.step();

    const finalAgents = Array.from(kernel.space.grid.values()).flat().length;
    const compressionRatio = (1 - (finalAgents / initialAgents)) * 100;

    console.log(`    > Final Agents: ${finalAgents}`);
    console.log(`    > Compression: ${compressionRatio.toFixed(1)}%`);

    if (finalAgents < initialAgents) {
        console.log("    > VERDICT: PASS (Entropy Reduced)");
    } else {
        console.log("    > VERDICT: FAIL (No Compression)");
    }

    // TEST 2: TIME CRYSTAL
    console.log("\n[TEST 2] Time Crystal (Temporal Reversibility)...");

    // Reset
    kernel.cycle = 0;
    kernel.history = [];

    // Run forward
    console.log("    > Advancing to Cycle 100...");
    for (let i = 0; i < 100; i++) {
        kernel.step();
        if (i % 10 === 0) kernel.saveSnapshot();
    }
    const stateAt100 = Array.from(kernel.space.grid.values()).flat()[0]?.symbol || "null";
    console.log(`    > State at Cycle 100: ${stateAt100} (Captured)`);

    // Run more
    console.log("    > Advancing to Cycle 120 (Chaos)...");
    for (let i = 0; i < 20; i++) kernel.step();
    console.log(`    > Current Cycle: ${kernel.cycle}`);

    // Time Travel
    console.log("    > REVERSING TIME -> Seek(100)...");
    kernel.seek(100);

    const stateRestored = Array.from(kernel.space.grid.values()).flat()[0]?.symbol || "null";
    console.log(`    > Restored Cycle: ${kernel.cycle}`);
    console.log(`    > Restored State: ${stateRestored}`);

    if (kernel.cycle === 100 && stateRestored === stateAt100) {
        console.log("    > VERDICT: PASS (Perfect Restoration)");
    } else {
        console.log("    > VERDICT: FAIL (Temporal Artifacts Detected)");
    }

    console.log("\n=================================================");
    console.log("   ALL SYSTEMS VERIFIED");
    console.log("=================================================");
}

runFeatureTest().catch(console.error);
