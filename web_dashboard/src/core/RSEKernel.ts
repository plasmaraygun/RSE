import { SymbolicAgent } from './types';
import { ToroidalSpace } from './ToroidalSpace';

// Simple Snapshot strategy for the demo
// In production RSE, we would use *Inverse Operations* for true reversibility
interface Snapshot {
    cycle: number;
    agents: SymbolicAgent[];
}

export class RSEKernel {
    id: string;
    parent: RSEKernel | null;
    space: ToroidalSpace;
    cycle: number;
    entropyTotal: number;

    // Time Crystal Buffers
    history: Snapshot[];
    maxHistory: number = 500;

    constructor(parent: RSEKernel | null = null) {
        this.id = crypto.randomUUID();
        this.parent = parent;
        this.space = new ToroidalSpace(32, 32, 32);
        this.cycle = 0;
        this.entropyTotal = 0;
        this.history = [];
    }

    // Boot the VM
    init() {
        for (let i = 0; i < 50; i++) {
            this.spawnAgent();
        }
        this.saveSnapshot();
    }

    spawnAgent() {
        const x = Math.floor(Math.random() * this.space.width);
        const y = Math.floor(Math.random() * this.space.height);
        const z = Math.floor(Math.random() * this.space.depth);

        const agent: SymbolicAgent = {
            id: crypto.randomUUID(),
            symbol: String.fromCharCode(65 + Math.floor(Math.random() * 26)), // A-Z
            x, y, z,
            memory: [],
            entropy: 1.0,
            age: 0,
            innerKernel: undefined
        };
        this.space.addAgent(agent);
    }

    saveSnapshot() {
        if (this.history.length >= this.maxHistory) {
            this.history.shift(); // circular buffer
        }
        // Deep copy agents for snapshot
        const agents = Array.from(this.space.grid.values()).flat().map(a => ({ ...a }));
        this.history.push({
            cycle: this.cycle,
            agents
        });
    }

    // "Infinite Zoom" - Lazy Load Inner Universe
    getInnerUniverse(agentId: string): RSEKernel | undefined {
        for (const agents of this.space.grid.values()) {
            for (const agent of agents) {
                if (agent.id === agentId) {
                    if (!agent.innerKernel) {
                        console.log(`[RSE] Generating inner universe for agent ${agent.symbol}...`);
                        const k = new RSEKernel(this);
                        k.init();
                        agent.innerKernel = k;
                    }
                    return agent.innerKernel;
                }
            }
        }
        return undefined;
    }

    // Time Travel
    seek(cycleTarget: number) {
        // Find closest snapshot
        const snap = this.history.find(s => s.cycle === cycleTarget) ||
            this.history.reduce((prev, curr) =>
                Math.abs(curr.cycle - cycleTarget) < Math.abs(prev.cycle - cycleTarget) ? curr : prev
            );

        if (snap) {
            this.cycle = snap.cycle;
            this.space.grid.clear();
            snap.agents.forEach(a => this.space.addAgent({ ...a })); // Restore
        }
    }

    // The "Infinite on Finite" Tick
    step() {
        this.cycle++;
        const agents = Array.from(this.space.grid.values()).flat();

        agents.forEach(agent => {
            this.executeAgent(agent);
        });

        // Save state every tick for smooth scrubbing
        this.saveSnapshot();
    }

    executeAgent(agent: SymbolicAgent) {
        agent.age++;
        // Movement Logic
        if (Math.random() > 0.8) {
            const dx = Math.floor(Math.random() * 3) - 1;
            const dy = Math.floor(Math.random() * 3) - 1;
            const dz = Math.floor(Math.random() * 3) - 1;
            this.space.moveAgent(agent, dx, dy, dz);
        }
    }
}
