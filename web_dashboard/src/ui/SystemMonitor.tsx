import { useState, useEffect } from 'react';
import { RSEKernel } from '../core/RSEKernel';
import { RSEBenchmark, ComparisonResult } from '../core/Benchmarks';
import { FoldingEngine } from '../core/FoldingEngine';

// Sample text for demo
const SAMPLE_TEXT = "The General Theory of Recursive Symbolic Execution (RSE)... (truncated)";

export function SystemMonitor({ kernel }: { kernel: RSEKernel }) {
    const [stats, setStats] = useState({ cycle: 0, agents: 0, entropy: 0 });
    const [running, setRunning] = useState(false);
    const [folding, setFolding] = useState(false);
    const [vsMode, setVsMode] = useState(false);
    const [vsStats, setVsStats] = useState<ComparisonResult | null>(null);

    // Time Crystal State
    const [timeParams, setTimeParams] = useState({ cycle: 0, min: 0, max: 0, active: false });

    const [folder] = useState(() => new FoldingEngine(kernel.space));
    // Benchmark instance
    const [bench] = useState(() => new RSEBenchmark(kernel));
    const [vsGen, setVsGen] = useState<Generator<ComparisonResult> | null>(null);

    useEffect(() => {
        const timer = setInterval(() => {
            // Logic Hooks
            if (folding) {
                const finished = folder.step();
                if (finished) setFolding(false);
            }

            if (vsMode && vsGen) {
                const next = vsGen.next();
                if (!next.done) {
                    setVsStats(next.value);
                }
            }

            // Update UI Stats
            setStats({
                cycle: kernel.cycle,
                agents: kernel.space.grid.size,
                entropy: kernel.entropyTotal
            });

            // Update Time Slider
            if (!timeParams.active && kernel.history.length > 0) {
                const min = kernel.history[0].cycle;
                const max = kernel.history[kernel.history.length - 1].cycle;
                setTimeParams(p => ({ ...p, cycle: kernel.cycle, min, max }));
            }

        }, 50); // Faster tick for VS mode
        return () => clearInterval(timer);
    }, [kernel, folding, timeParams.active, vsMode, vsGen]);

    const runBench = async () => {
        setRunning(true);
        await bench.runThroughputTest(1500);
        setRunning(false);
    };

    const startBlackHole = () => {
        folder.ingestText(SAMPLE_TEXT);
        folder.active = true;
        setFolding(true);
    };

    const toggleVsMode = () => {
        if (vsMode) {
            setVsMode(false);
            setVsGen(null);
            setVsStats(null);
        } else {
            setVsMode(true);
            setVsGen(bench.runComparativeTest());
        }
    };

    const handleSeek = (e: React.ChangeEvent<HTMLInputElement>) => {
        const target = parseInt(e.target.value);
        kernel.seek(target);
        setTimeParams(p => ({ ...p, cycle: target }));
    };

    return (
        <div className="absolute top-0 left-0 w-full h-full pointer-events-none p-6 font-mono">
            {/* Top Left Header */}
            <div className="pointer-events-auto bg-black/50 backdrop-blur-md p-4 max-w-sm border border-[var(--rse-primary)] text-[var(--rse-primary)] glow-box">
                <h1 className="text-2xl font-bold mb-2">RSE // ENGINE</h1>

                {/* VS MODE OVERLAY */}
                {vsStats ? (
                    <div className="mb-4 bg-black p-3 border border-red-500/50">
                        <div className="text-white font-bold text-center mb-2 border-b border-red-500 pb-1">VS MODE: RECURSION TEST</div>

                        <div className="grid grid-cols-2 gap-4 text-xs">
                            <div className="text-center">
                                <div className="text-red-400 font-bold mb-1">TRADITIONAL</div>
                                <div className="text-xl mb-1">{vsStats.traditionalDepth.toLocaleString()}</div>
                                <div className={vsStats.traditionalStatus === 'CRASHED' ? "text-red-600 font-bold animate-pulse" : "text-green-500"}>
                                    {vsStats.traditionalStatus === 'CRASHED' ? "STACK OVERFLOW" : "RUNNING"}
                                </div>
                            </div>

                            <div className="text-center border-l border-gray-700">
                                <div className="text-cyan-400 font-bold mb-1">RSE (SYMBOLIC)</div>
                                <div className="text-xl mb-1">{vsStats.rseDepth.toLocaleString()}</div>
                                <div className="text-green-500 font-bold">
                                    UNBOUNDED
                                </div>
                            </div>
                        </div>
                    </div>
                ) : (
                    <div className="space-y-1 text-sm">
                        <div className="flex justify-between">
                            <span>CYCLE:</span>
                            <span>{stats.cycle}</span>
                        </div>
                        <div className="flex justify-between">
                            <span>ENTITIES:</span>
                            <span>{stats.agents}</span>
                        </div>
                    </div>
                )}

                {/* Time Crystal Slider */}
                <div className="mt-4 border-t border-[var(--rse-primary)] pt-2 pb-2">
                    <div className="text-xs text-blue-400 font-bold mb-1">TIME CRYSTAL (REVERSIBILITY)</div>
                    <input
                        type="range"
                        min={timeParams.min}
                        max={timeParams.max}
                        value={timeParams.cycle}
                        onChange={handleSeek}
                        onMouseDown={() => setTimeParams(p => ({ ...p, active: true }))}
                        onMouseUp={() => setTimeParams(p => ({ ...p, active: false }))}
                        className="w-full accent-blue-500 cursor-pointer"
                    />
                </div>

                <div className="mt-2 border-t border-[var(--rse-primary)] pt-4 space-y-2">
                    <button
                        onClick={toggleVsMode}
                        className={`w-full font-bold py-2 transition-colors ${vsMode ? 'bg-red-500 text-white' : 'border border-red-500 text-red-500 hover:bg-red-900/30'}`}
                    >
                        {vsMode ? "STOP VS TEST" : "TEST: RSE vs TRADITIONAL"}
                    </button>

                    <button
                        onClick={runBench} disabled={running || folding || vsMode}
                        className="w-full bg-[var(--rse-primary)] text-black font-bold py-2 hover:bg-white transition-colors disabled:opacity-50"
                    >
                        {running ? "BENCHMARKING..." : "RUN DIAGNOSTICS"}
                    </button>

                    <button
                        onClick={startBlackHole} disabled={running || folding || vsMode}
                        className="w-full border border-purple-500 text-purple-400 font-bold py-2 hover:bg-purple-900/50 transition-colors disabled:opacity-50"
                    >
                        {folding ? "FOLDING..." : "INITIATE BLACK HOLE"}
                    </button>
                </div>
            </div>
        </div>
    );
}
