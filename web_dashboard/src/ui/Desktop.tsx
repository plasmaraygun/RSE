import { useState, useCallback, useEffect } from 'react';
import { RSEKernel } from '../core/RSEKernel';
import { api } from '../api/ArqonAPI';

// Window state
interface WindowState {
    id: string;
    title: string;
    icon: string;
    x: number;
    y: number;
    width: number;
    height: number;
    minimized: boolean;
    maximized: boolean;
    zIndex: number;
    component: string;
}

// Dock apps
const DOCK_APPS = [
    { id: 'terminal', title: 'Terminal', icon: '⌨️', component: 'terminal' },
    { id: 'wallet', title: 'Wallet', icon: '💰', component: 'wallet' },
    { id: 'chat', title: 'Inference', icon: '🤖', component: 'chat' },
    { id: 'explorer', title: 'Explorer', icon: '🔍', component: 'explorer' },
    { id: 'network', title: 'Network', icon: '🌐', component: 'network' },
    { id: 'settings', title: 'Settings', icon: '⚙️', component: 'settings' },
];

// Terminal Component - Connects to real API
function Terminal({ kernel }: { kernel: RSEKernel }) {
    const [history, setHistory] = useState<string[]>([
        'RSE Shell v1.0 - Braidchain Network',
        'Type "help" for available commands',
        ''
    ]);
    const [input, setInput] = useState('');

    const executeCommand = async (cmd: string) => {
        const parts = cmd.trim().split(' ');
        const command = parts[0].toLowerCase();
        let output: string[] = [];

        switch (command) {
            case 'help':
                output = [
                    'Available commands:',
                    '  status    - Show network status (from API)',
                    '  balance   - Show wallet balance (from API)',
                    '  nodes     - List network nodes (from API)',
                    '  validators- List validators (from API)',
                    '  clear     - Clear terminal',
                    '  kernel    - Show local kernel stats',
                ];
                break;
            case 'status':
                try {
                    const status = await api.getStatus();
                    output = [
                        '═══ Network Status ═══',
                        `  Status: ${status.status}`,
                        `  Epoch: ${status.epoch.toLocaleString()}`,
                        `  Height: ${status.height.toLocaleString()}`,
                        `  Validators: ${status.validators}`,
                        `  Inference Nodes: ${status.inference_nodes}`,
                        `  GPU Nodes: ${status.gpu_nodes}`,
                    ];
                } catch {
                    output = ['Error: Node offline. Start arqon-node first.'];
                }
                break;
            case 'balance':
                try {
                    const addr = parts[1] || '0x0000000000000000000000000000000000000000';
                    const bal = await api.getBalance(addr);
                    output = [
                        '═══ Wallet Balance ═══',
                        `  Address: ${bal.address}`,
                        `  Available: ${bal.balance_arqon.toFixed(4)} ARQN`,
                        `  Staked: ${bal.stake_arqon.toFixed(4)} ARQN`,
                        `  Nonce: ${bal.nonce}`,
                    ];
                } catch {
                    output = ['Error: Node offline or invalid address.'];
                }
                break;
            case 'kernel':
                output = [
                    '═══ Local Kernel Stats ═══',
                    `  Cycle: ${kernel.cycle}`,
                    `  Agents: ${kernel.space.grid.size}`,
                    `  Entropy: ${kernel.entropyTotal.toFixed(2)}`,
                ];
                break;
            case 'nodes':
                try {
                    const nodes = await api.getNodes();
                    output = [
                        '═══ Network Nodes ═══',
                        `  Total: ${nodes.total}`,
                        `  GPU: ${nodes.gpu}`,
                        `  Relay: ${nodes.relay}`,
                        `  Network TPS: ${nodes.tflops.toFixed(1)}`,
                        `  Epoch: ${nodes.epoch}`,
                    ];
                } catch {
                    output = ['Error: Node offline.'];
                }
                break;
            case 'validators':
                try {
                    const v = await api.getValidators();
                    output = [`═══ Validators (${v.count}) ═══`];
                    v.validators.slice(0, 5).forEach(val => {
                        output.push(`  ${val.address.slice(0,10)}... ${val.stake_arqon.toFixed(2)} ARQN`);
                    });
                } catch {
                    output = ['Error: Node offline.'];
                }
                break;
            case 'clear':
                setHistory(['']);
                setInput('');
                return;
            case '':
                break;
            default:
                output = [`Command not found: ${command}`];
        }

        setHistory([...history, `arqon> ${cmd}`, ...output, '']);
    };

    const handleKeyDown = (e: React.KeyboardEvent) => {
        if (e.key === 'Enter') {
            executeCommand(input);
            setInput('');
        }
    };

    return (
        <div className="h-full bg-black/90 text-green-400 font-mono text-sm p-4 overflow-auto">
            {history.map((line, i) => (
                <div key={i} className="whitespace-pre">{line}</div>
            ))}
            <div className="flex">
                <span className="text-cyan-400">arqon&gt; </span>
                <input
                    type="text"
                    value={input}
                    onChange={(e) => setInput(e.target.value)}
                    onKeyDown={handleKeyDown}
                    className="flex-1 bg-transparent outline-none text-green-400"
                    autoFocus
                />
            </div>
        </div>
    );
}

// Wallet Component - Fetches from real API
function Wallet() {
    const [balance, setBalance] = useState({ balance_arqon: 0, stake_arqon: 0 });
    const [loading, setLoading] = useState(true);
    const [error, setError] = useState(false);

    useEffect(() => {
        api.getBalance('0x0000000000000000000000000000000000000000')
            .then(b => { setBalance(b); setLoading(false); })
            .catch(() => { setError(true); setLoading(false); });
    }, []);

    return (
        <div className="h-full bg-slate-900 text-white p-4 overflow-auto">
            <div className="bg-gradient-to-r from-indigo-600 to-purple-600 rounded-xl p-6 mb-4">
                <div className="text-sm opacity-70">Total Balance</div>
                <div className="text-3xl font-bold">
                    {loading ? '...' : error ? 'Node Offline' : `${balance.balance_arqon.toFixed(4)} ARQN`}
                </div>
            </div>
            <div className="grid grid-cols-2 gap-3 mb-4">
                <div className="bg-white/10 rounded-lg p-4">
                    <div className="text-sm text-gray-400">Staked</div>
                    <div className="text-xl font-semibold">
                        {loading ? '...' : `${balance.stake_arqon.toFixed(2)} ARQN`}
                    </div>
                </div>
                <div className="bg-white/10 rounded-lg p-4">
                    <div className="text-sm text-gray-400">Status</div>
                    <div className="text-xl font-semibold text-green-400">
                        {error ? 'Offline' : 'Online'}
                    </div>
                </div>
            </div>
            <div className="flex gap-2">
                <button className="flex-1 bg-indigo-600 hover:bg-indigo-700 py-2 rounded-lg disabled:opacity-50" disabled={error}>Send</button>
                <button className="flex-1 bg-white/10 hover:bg-white/20 py-2 rounded-lg">Receive</button>
            </div>
        </div>
    );
}

// Chat Component - Connects to real inference API
function Chat() {
    const [messages, setMessages] = useState<{role: string; content: string}[]>([
        { role: 'assistant', content: 'Connected to Braidchain inference network. Send a message to generate a response via Petals.' }
    ]);
    const [input, setInput] = useState('');
    const [loading, setLoading] = useState(false);
    const [nodeCount, setNodeCount] = useState(0);

    useEffect(() => {
        api.getNodes().then(n => setNodeCount(n.gpu)).catch(() => {});
    }, []);

    const send = async () => {
        if (!input.trim() || loading) return;
        const userMsg = input;
        setInput('');
        setMessages(prev => [...prev, { role: 'user', content: userMsg }]);
        setLoading(true);

        try {
            const result = await api.inference(userMsg);
            setMessages(prev => [...prev, { 
                role: 'assistant', 
                content: result.response || 'No response received.'
            }]);
        } catch {
            setMessages(prev => [...prev, { 
                role: 'assistant', 
                content: '⚠️ Inference failed. Make sure arqon-node is running with Petals bridge enabled.' 
            }]);
        }
        setLoading(false);
    };

    return (
        <div className="h-full bg-slate-900 text-white flex flex-col">
            <div className="p-3 border-b border-white/10 flex items-center gap-2">
                <span className="text-lg">🤖</span>
                <span className="font-semibold">Llama 3.1 70B</span>
                <span className={`text-xs px-2 py-0.5 rounded ${nodeCount > 0 ? 'bg-green-500/20 text-green-400' : 'bg-red-500/20 text-red-400'}`}>
                    {nodeCount > 0 ? `${nodeCount} GPU nodes` : 'No nodes'}
                </span>
            </div>
            <div className="flex-1 p-4 overflow-auto space-y-3">
                {messages.map((m, i) => (
                    <div key={i} className={`flex ${m.role === 'user' ? 'justify-end' : 'justify-start'}`}>
                        <div className={`max-w-[80%] rounded-2xl px-4 py-2 ${
                            m.role === 'user' ? 'bg-indigo-600' : 'bg-white/10'
                        }`}>{m.content}</div>
                    </div>
                ))}
                {loading && (
                    <div className="flex justify-start">
                        <div className="bg-white/10 rounded-2xl px-4 py-2 animate-pulse">Generating...</div>
                    </div>
                )}
            </div>
            <div className="p-3 border-t border-white/10 flex gap-2">
                <input
                    type="text"
                    value={input}
                    onChange={(e) => setInput(e.target.value)}
                    onKeyDown={(e) => e.key === 'Enter' && send()}
                    placeholder="Message..."
                    className="flex-1 bg-white/10 rounded-lg px-4 py-2 outline-none"
                    disabled={loading}
                />
                <button onClick={send} className="bg-indigo-600 px-4 rounded-lg disabled:opacity-50" disabled={loading}>↑</button>
            </div>
        </div>
    );
}

// Explorer Component - Fetches consensus state from API
function Explorer() {
    const [consensus, setConsensus] = useState({ epoch: 0, height: 0, validators: 0, total_staked_arqon: 0 });
    const [loading, setLoading] = useState(true);
    const [error, setError] = useState(false);
    const [searchAddr, setSearchAddr] = useState('');

    useEffect(() => {
        api.getConsensus()
            .then(c => { setConsensus(c); setLoading(false); })
            .catch(() => { setError(true); setLoading(false); });
    }, []);

    return (
        <div className="h-full bg-slate-900 text-white p-4 overflow-auto">
            <div className="mb-4">
                <input
                    type="text"
                    placeholder="Search address..."
                    value={searchAddr}
                    onChange={(e) => setSearchAddr(e.target.value)}
                    className="w-full bg-white/10 rounded-lg px-4 py-2 outline-none font-mono text-sm"
                />
            </div>
            {error && (
                <div className="bg-red-500/20 border border-red-500 rounded-lg p-3 mb-4 text-center text-sm">
                    Node offline - start arqon-node
                </div>
            )}
            <div className="grid grid-cols-3 gap-2 mb-4 text-center">
                <div className="bg-white/10 rounded-lg p-3">
                    <div className="text-2xl font-bold">{loading ? '...' : consensus.height.toLocaleString()}</div>
                    <div className="text-xs text-gray-400">Snapshots</div>
                </div>
                <div className="bg-white/10 rounded-lg p-3">
                    <div className="text-2xl font-bold">{loading ? '...' : consensus.epoch.toLocaleString()}</div>
                    <div className="text-xs text-gray-400">Epoch</div>
                </div>
                <div className="bg-white/10 rounded-lg p-3">
                    <div className="text-2xl font-bold">{loading ? '...' : consensus.validators}</div>
                    <div className="text-xs text-gray-400">Validators</div>
                </div>
            </div>
            <div className="bg-white/5 rounded-lg p-3 mb-4">
                <div className="text-gray-400 text-sm">Total Staked</div>
                <div className="text-xl font-bold">{loading ? '...' : `${consensus.total_staked_arqon.toFixed(2)} ARQN`}</div>
            </div>
            <div className="text-xs text-gray-500 text-center">
                Braidchain Explorer - Topological Consensus
            </div>
        </div>
    );
}

// Network Component - Fetches from real API
function Network() {
    const [stats, setStats] = useState({ total: 0, gpu: 0, relay: 0, tflops: 0, epoch: 0 });
    const [loading, setLoading] = useState(true);
    const [error, setError] = useState(false);

    useEffect(() => {
        const fetchStats = () => {
            api.getNodes()
                .then(s => { setStats(s); setLoading(false); setError(false); })
                .catch(() => { setError(true); setLoading(false); });
        };
        fetchStats();
        const interval = setInterval(fetchStats, 5000); // Refresh every 5s
        return () => clearInterval(interval);
    }, []);

    return (
        <div className="h-full bg-slate-900 text-white p-4 overflow-auto">
            {error && (
                <div className="bg-red-500/20 border border-red-500 rounded-lg p-3 mb-4 text-center">
                    Node offline - start arqon-node
                </div>
            )}
            <div className="grid grid-cols-2 gap-3 mb-4">
                <div className="bg-white/10 rounded-lg p-3">
                    <div className="text-gray-400 text-sm">Total Nodes</div>
                    <div className="text-2xl font-bold">{loading ? '...' : stats.total}</div>
                </div>
                <div className="bg-white/10 rounded-lg p-3">
                    <div className="text-gray-400 text-sm">GPU Nodes</div>
                    <div className="text-2xl font-bold">{loading ? '...' : stats.gpu}</div>
                </div>
            </div>
            <div className="grid grid-cols-2 gap-3 mb-4">
                <div className="bg-white/10 rounded-lg p-3">
                    <div className="text-gray-400 text-sm">Relay Nodes</div>
                    <div className="text-2xl font-bold">{loading ? '...' : stats.relay}</div>
                </div>
                <div className="bg-white/10 rounded-lg p-3">
                    <div className="text-gray-400 text-sm">Network TPS</div>
                    <div className="text-2xl font-bold">{loading ? '...' : stats.tflops.toFixed(1)}</div>
                </div>
            </div>
            <div className="bg-white/5 rounded-lg p-3">
                <div className="text-gray-400 text-sm">Current Epoch</div>
                <div className="text-xl font-bold">{loading ? '...' : stats.epoch.toLocaleString()}</div>
            </div>
        </div>
    );
}

// Settings Component - Shows real node status
function Settings() {
    const [status, setStatus] = useState<{status: string; validators: number; inference_nodes: number} | null>(null);
    const [error, setError] = useState(false);

    useEffect(() => {
        api.getStatus()
            .then(s => { setStatus(s); setError(false); })
            .catch(() => setError(true));
    }, []);

    return (
        <div className="h-full bg-slate-900 text-white p-4 overflow-auto">
            <h3 className="font-semibold mb-4">Node Status</h3>
            
            {error ? (
                <div className="bg-red-500/20 border border-red-500 rounded-lg p-4 mb-4">
                    <div className="font-semibold text-red-400">Node Offline</div>
                    <div className="text-sm text-gray-400 mt-1">Start arqon-node to connect</div>
                    <code className="block mt-2 text-xs bg-black/50 p-2 rounded">./build/arqon-node --api-port 8080</code>
                </div>
            ) : (
                <div className="bg-green-500/20 border border-green-500 rounded-lg p-4 mb-4">
                    <div className="font-semibold text-green-400">Node Online</div>
                    <div className="text-sm text-gray-400 mt-1">Connected to localhost:8080</div>
                </div>
            )}

            <div className="space-y-3">
                <div className="bg-white/5 rounded-lg p-3">
                    <div className="text-gray-400 text-sm">Validators</div>
                    <div className="text-xl font-bold">{status?.validators ?? '—'}</div>
                </div>
                <div className="bg-white/5 rounded-lg p-3">
                    <div className="text-gray-400 text-sm">Inference Nodes</div>
                    <div className="text-xl font-bold">{status?.inference_nodes ?? '—'}</div>
                </div>
            </div>

            <div className="mt-6 pt-4 border-t border-white/10">
                <h4 className="font-semibold mb-2 text-sm text-gray-400">API Endpoints</h4>
                <div className="text-xs font-mono space-y-1 text-gray-500">
                    <div>GET /api/status</div>
                    <div>GET /api/balance/:addr</div>
                    <div>GET /api/validators</div>
                    <div>GET /api/nodes</div>
                    <div>GET /api/consensus</div>
                    <div>POST /api/inference</div>
                </div>
            </div>
        </div>
    );
}

// Window Component
function Window({ 
    window, 
    kernel,
    onClose, 
    onMinimize, 
    onMaximize,
    onFocus,
    onDrag 
}: { 
    window: WindowState;
    kernel: RSEKernel;
    onClose: () => void;
    onMinimize: () => void;
    onMaximize: () => void;
    onFocus: () => void;
    onDrag: (x: number, y: number) => void;
}) {
    const [dragging, setDragging] = useState(false);
    const [dragOffset, setDragOffset] = useState({ x: 0, y: 0 });

    const handleMouseDown = (e: React.MouseEvent) => {
        onFocus();
        setDragging(true);
        setDragOffset({ x: e.clientX - window.x, y: e.clientY - window.y });
    };

    const handleMouseMove = useCallback((e: MouseEvent) => {
        if (dragging) {
            onDrag(e.clientX - dragOffset.x, e.clientY - dragOffset.y);
        }
    }, [dragging, dragOffset, onDrag]);

    const handleMouseUp = useCallback(() => {
        setDragging(false);
    }, []);

    // Global mouse events for dragging
    useState(() => {
        if (dragging) {
            document.addEventListener('mousemove', handleMouseMove);
            document.addEventListener('mouseup', handleMouseUp);
            return () => {
                document.removeEventListener('mousemove', handleMouseMove);
                document.removeEventListener('mouseup', handleMouseUp);
            };
        }
    });

    if (window.minimized) return null;

    const style = window.maximized ? {
        left: 0, top: 0, width: '100%', height: 'calc(100% - 60px)', zIndex: window.zIndex
    } : {
        left: window.x, top: window.y, width: window.width, height: window.height, zIndex: window.zIndex
    };

    const renderContent = () => {
        switch (window.component) {
            case 'terminal': return <Terminal kernel={kernel} />;
            case 'wallet': return <Wallet />;
            case 'chat': return <Chat />;
            case 'explorer': return <Explorer />;
            case 'network': return <Network />;
            case 'settings': return <Settings />;
            default: return <div className="p-4">Unknown app</div>;
        }
    };

    return (
        <div 
            className="absolute rounded-xl overflow-hidden shadow-2xl border border-white/10 flex flex-col"
            style={style}
            onClick={onFocus}
        >
            {/* Title bar */}
            <div 
                className="h-8 bg-slate-800 flex items-center px-3 gap-2 cursor-move select-none"
                onMouseDown={handleMouseDown}
            >
                <div className="flex gap-1.5">
                    <button onClick={onClose} className="w-3 h-3 rounded-full bg-red-500 hover:bg-red-400" />
                    <button onClick={onMinimize} className="w-3 h-3 rounded-full bg-yellow-500 hover:bg-yellow-400" />
                    <button onClick={onMaximize} className="w-3 h-3 rounded-full bg-green-500 hover:bg-green-400" />
                </div>
                <span className="text-xs text-gray-400 ml-2">{window.icon} {window.title}</span>
            </div>
            {/* Content */}
            <div className="flex-1 overflow-hidden">
                {renderContent()}
            </div>
        </div>
    );
}

// Dock Component
function Dock({ onLaunch }: { onLaunch: (app: typeof DOCK_APPS[0]) => void }) {
    return (
        <div className="absolute bottom-4 left-1/2 -translate-x-1/2 bg-white/10 backdrop-blur-xl rounded-2xl px-4 py-2 flex gap-2 border border-white/20">
            {DOCK_APPS.map(app => (
                <button
                    key={app.id}
                    onClick={() => onLaunch(app)}
                    className="w-12 h-12 rounded-xl bg-white/10 hover:bg-white/20 flex items-center justify-center text-2xl transition-all hover:scale-110"
                    title={app.title}
                >
                    {app.icon}
                </button>
            ))}
        </div>
    );
}

// Main Desktop Component
export function Desktop({ kernel }: { kernel: RSEKernel }) {
    const [windows, setWindows] = useState<WindowState[]>([]);
    const [nextZ, setNextZ] = useState(100);

    const launchApp = (app: typeof DOCK_APPS[0]) => {
        const newWindow: WindowState = {
            id: `${app.id}-${Date.now()}`,
            title: app.title,
            icon: app.icon,
            x: 100 + (windows.length % 5) * 30,
            y: 50 + (windows.length % 5) * 30,
            width: app.id === 'terminal' ? 600 : 400,
            height: app.id === 'terminal' ? 400 : 500,
            minimized: false,
            maximized: false,
            zIndex: nextZ,
            component: app.component,
        };
        setWindows([...windows, newWindow]);
        setNextZ(nextZ + 1);
    };

    const closeWindow = (id: string) => {
        setWindows(windows.filter(w => w.id !== id));
    };

    const minimizeWindow = (id: string) => {
        setWindows(windows.map(w => w.id === id ? { ...w, minimized: !w.minimized } : w));
    };

    const maximizeWindow = (id: string) => {
        setWindows(windows.map(w => w.id === id ? { ...w, maximized: !w.maximized } : w));
    };

    const focusWindow = (id: string) => {
        setWindows(windows.map(w => w.id === id ? { ...w, zIndex: nextZ } : w));
        setNextZ(nextZ + 1);
    };

    const dragWindow = (id: string, x: number, y: number) => {
        setWindows(windows.map(w => w.id === id ? { ...w, x, y } : w));
    };

    return (
        <div className="absolute inset-0 pointer-events-auto">
            {/* Windows */}
            {windows.map(w => (
                <Window
                    key={w.id}
                    window={w}
                    kernel={kernel}
                    onClose={() => closeWindow(w.id)}
                    onMinimize={() => minimizeWindow(w.id)}
                    onMaximize={() => maximizeWindow(w.id)}
                    onFocus={() => focusWindow(w.id)}
                    onDrag={(x, y) => dragWindow(w.id, x, y)}
                />
            ))}
            
            {/* Dock */}
            <Dock onLaunch={launchApp} />
        </div>
    );
}
