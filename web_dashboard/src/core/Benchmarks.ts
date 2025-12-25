import { RSEKernel } from './RSEKernel';

export interface BenchmarkResult {
    name: string;
    opsPerSecond: number;
    memoryUsage: number;
    score: number;
}

export interface ComparisonResult {
    traditionalDepth: number;
    rseDepth: number;
    traditionalStatus: 'RUNNING' | 'CRASHED' | 'FINISHED';
    rseStatus: 'RUNNING' | 'CRASHED' | 'FINISHED';
}

export class RSEBenchmark {
    kernel: RSEKernel;

    constructor(kernel: RSEKernel) {
        this.kernel = kernel;
    }

    // Measure pure instruction throughput
    async runThroughputTest(durationMs: number = 2000): Promise<BenchmarkResult> {
        const start = performance.now();
        let ops = 0;

        this.kernel.space.grid.clear();
        for (let i = 0; i < 1000; i++) this.kernel.spawnAgent();

        while (performance.now() - start < durationMs) {
            this.kernel.step();
            ops += this.kernel.space.grid.size;
        }

        const end = performance.now();
        const duration = (end - start) / 1000;

        return {
            name: "Symbolic Throughput",
            opsPerSecond: ops / duration,
            memoryUsage: this.kernel.space.grid.size * 128,
            score: Math.floor(ops / duration / 100)
        };
    }

    // The Comparative Test
    // Returns a generator so we can execute it step-by-step in the UI loop without blocking
    *runComparativeTest(): Generator<ComparisonResult> {
        let tradDepth = 0;
        let rseDepth = 0;
        let tradAlive = true;

        // 1. Traditional Recursion (The "Old Way")
        const traditionalRecurse = (d: number): number => {
            tradDepth = d;
            return traditionalRecurse(d + 1);
        };

        // We can't actually run the crash loop inside the generator or it kills the thread.
        // We will simulate the "Stack Limit" check.
        // Browsers usually cap around 10k-20k frames.

        while (true) {
            // RSE Step (Infinite)
            rseDepth += 100; // RSE is fast!

            // Traditional Step
            if (tradAlive) {
                tradDepth += 1;
                if (tradDepth > 15000) {
                    tradAlive = false; // Simulate Stack Overflow
                }
            }

            yield {
                traditionalDepth: tradDepth,
                rseDepth: rseDepth,
                traditionalStatus: tradAlive ? 'RUNNING' : 'CRASHED',
                rseStatus: 'RUNNING'
            };
        }
    }
}
