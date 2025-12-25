export type SymbolType = string;

// Forward declaration interface for Kernel to avoid circular imports in types if strict
// For now using any or a simple interface trigger
export interface RecursiveContext {
    // This will hold the RSEKernel instance
    cycle: number;
    // ... other kernel props
}

export interface SymbolicAgent {
    id: string;
    symbol: SymbolType;
    x: number;
    y: number;
    z: number; // 3D Torus
    memory: SymbolType[]; // Finite tape
    entropy: number; // Calculated Shannon entropy of memory
    age: number;

    // The "Infinite" part: An entire universe inside this agent
    // Lazy loaded: undefined until "dived" into
    innerKernel?: any; // typed as any to prevent circular dependency with RSEKernel class
}


// Instruction Set Architecture for the Symbolic VM
export type OpCode =
    | 'FOLD'      // Compress pattern
    | 'UNFOLD'    // Expand pattern
    | 'MUTATE'    // Random bit flip
    | 'BOND'      // Link with neighbor
    | 'DECAY';    // Entropy loss

export interface Instruction {
    op: OpCode;
    operands: SymbolType[];
}
