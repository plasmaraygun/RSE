import { SymbolicAgent } from './types';

export class ToroidalSpace {
    width: number;
    height: number;
    depth: number;
    grid: Map<string, SymbolicAgent[]>; // Sparse storage key "x,y,z"

    constructor(width = 16, height = 16, depth = 16) {
        this.width = width;
        this.height = height;
        this.depth = depth;
        this.grid = new Map();
    }

    // Wrap coordinate to [0, dim)
    wrap(v: number, max: number): number {
        return ((v % max) + max) % max;
    }

    // Get 3D coordinate key
    key(x: number, y: number, z: number): string {
        return `${this.wrap(x, this.width)},${this.wrap(y, this.height)},${this.wrap(z, this.depth)}`;
    }

    addAgent(agent: SymbolicAgent): void {
        const k = this.key(agent.x, agent.y, agent.z);
        if (!this.grid.has(k)) {
            this.grid.set(k, []);
        }
        this.grid.get(k)!.push(agent);
    }

    getAgentsAt(x: number, y: number, z: number): SymbolicAgent[] {
        return this.grid.get(this.key(x, y, z)) || [];
    }

    moveAgent(agent: SymbolicAgent, dx: number, dy: number, dz: number): void {
        const oldKey = this.key(agent.x, agent.y, agent.z);
        const cell = this.grid.get(oldKey);
        if (cell) {
            const idx = cell.findIndex(a => a.id === agent.id);
            if (idx !== -1) cell.splice(idx, 1);
            if (cell.length === 0) this.grid.delete(oldKey);
        }

        agent.x = this.wrap(agent.x + dx, this.width);
        agent.y = this.wrap(agent.y + dy, this.height);
        agent.z = this.wrap(agent.z + dz, this.depth);

        const newKey = this.key(agent.x, agent.y, agent.z);
        if (!this.grid.has(newKey)) this.grid.set(newKey, []);
        this.grid.get(newKey)!.push(agent);
    }

    // Von Neumann Neighborhood (6 neighbors in 3D)
    getNeighbors(x: number, y: number, z: number): (SymbolicAgent | undefined)[] {
        const neighbors = [];
        const dirs = [
            [1, 0, 0], [-1, 0, 0],
            [0, 1, 0], [0, -1, 0],
            [0, 0, 1], [0, 0, -1]
        ];

        for (const [dx, dy, dz] of dirs) {
            neighbors.push(...this.getAgentsAt(x + dx, y + dy, z + dz));
        }
        return neighbors;
    }
}
