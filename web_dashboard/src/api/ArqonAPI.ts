/**
 * Arqon Network API Client
 * Connects to real arqon-node backend
 */

// @ts-ignore - Vite env
const API_BASE = (typeof import.meta !== 'undefined' && (import.meta as any).env?.VITE_API_URL) || 'http://localhost:8080';

export interface NetworkStatus {
    status: string;
    epoch: number;
    height: number;
    validators: number;
    total_staked: number;
    inference_nodes: number;
    gpu_nodes: number;
    total_tflops: number;
}

export interface AccountBalance {
    address: string;
    balance: number;
    balance_arqon: number;
    stake: number;
    stake_arqon: number;
    nonce: number;
}

export interface Validator {
    address: string;
    stake: number;
    stake_arqon: number;
}

export interface NodeStats {
    total: number;
    gpu: number;
    relay: number;
    tflops: number;
    epoch: number;
}

export interface ConsensusState {
    epoch: number;
    height: number;
    validators: number;
    total_staked: number;
    total_staked_arqon: number;
}

class ArqonAPI {
    private baseUrl: string;

    constructor(baseUrl: string = API_BASE) {
        this.baseUrl = baseUrl;
    }

    private async fetch<T>(endpoint: string): Promise<T> {
        const response = await fetch(`${this.baseUrl}${endpoint}`);
        if (!response.ok) {
            throw new Error(`API error: ${response.status}`);
        }
        return response.json();
    }

    async getStatus(): Promise<NetworkStatus> {
        return this.fetch<NetworkStatus>('/api/status');
    }

    async getBalance(address: string): Promise<AccountBalance> {
        return this.fetch<AccountBalance>(`/api/balance/${address}`);
    }

    async getValidators(): Promise<{ count: number; validators: Validator[] }> {
        return this.fetch<{ count: number; validators: Validator[] }>('/api/validators');
    }

    async getNodes(): Promise<NodeStats> {
        return this.fetch<NodeStats>('/api/nodes');
    }

    async getConsensus(): Promise<ConsensusState> {
        return this.fetch<ConsensusState>('/api/consensus');
    }

    async sendTransaction(from: string, to: string, amount: number): Promise<{ success: boolean; txid?: string; error?: string }> {
        const response = await fetch(`${this.baseUrl}/api/tx`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ from, to, amount })
        });
        return response.json();
    }

    async stake(address: string, amount: number): Promise<{ success: boolean; error?: string }> {
        const response = await fetch(`${this.baseUrl}/api/stake`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ address, amount })
        });
        return response.json();
    }

    async unstake(address: string, amount: number): Promise<{ success: boolean; error?: string }> {
        const response = await fetch(`${this.baseUrl}/api/unstake`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ address, amount })
        });
        return response.json();
    }

    // Inference endpoint - connects to Petals bridge
    async inference(prompt: string, model: string = 'meta-llama/Llama-3.1-70B'): Promise<{ response: string; tokens: number; node: string }> {
        const response = await fetch(`${this.baseUrl}/api/inference`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ prompt, model })
        });
        if (!response.ok) {
            throw new Error('Inference failed');
        }
        return response.json();
    }

    // Check if API is reachable
    async ping(): Promise<boolean> {
        try {
            await this.getStatus();
            return true;
        } catch {
            return false;
        }
    }
}

// Singleton instance
export const api = new ArqonAPI();

// React hook for API with loading/error states
export function useAPI<T>(fetcher: () => Promise<T>, deps: unknown[] = []) {
    const [data, setData] = useState<T | null>(null);
    const [loading, setLoading] = useState(true);
    const [error, setError] = useState<string | null>(null);

    useEffect(() => {
        let cancelled = false;
        setLoading(true);
        setError(null);

        fetcher()
            .then(result => {
                if (!cancelled) {
                    setData(result);
                    setLoading(false);
                }
            })
            .catch(err => {
                if (!cancelled) {
                    setError(err.message);
                    setLoading(false);
                }
            });

        return () => { cancelled = true; };
    }, deps);

    return { data, loading, error, refetch: () => fetcher().then(setData) };
}

// Need to import these for the hook
import { useState, useEffect } from 'react';
