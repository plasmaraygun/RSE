import { Suspense, useMemo, useState } from 'react'
import { Canvas, useFrame } from '@react-three/fiber'
import { OrbitControls, Stars } from '@react-three/drei'
import { RSEKernel } from './core/RSEKernel'
import { TorusVisualizer } from './visuals/TorusVisualizer'
import { SystemMonitor } from './ui/SystemMonitor'
import { Desktop } from './ui/Desktop'

const EngineTicker = ({ kernel }: { kernel: RSEKernel }) => {
    useFrame(() => {
        kernel.step();
    });
    return null;
}

function App() {
    const rootKernel = useMemo(() => {
        const k = new RSEKernel();
        k.init();
        return k;
    }, []);

    // Stack of universes. Top is active.
    const [universeStack, setUniverseStack] = useState<RSEKernel[]>([rootKernel]);
    const activeKernel = universeStack[universeStack.length - 1];

    const handleDive = (agentId: string) => {
        const inner = activeKernel.getInnerUniverse(agentId);
        if (inner) {
            setUniverseStack([...universeStack, inner]);
        }
    };

    const handleAscend = () => {
        if (universeStack.length > 1) {
            setUniverseStack(universeStack.slice(0, -1));
        }
    };

    return (
        <div className="w-full h-screen bg-black relative">
            {/* 3D Viewport */}
            <Canvas camera={{ position: [0, 0, 15], fov: 60 }} gl={{ antialias: false }}>
                <color attach="background" args={['#000005']} />
                <Suspense fallback={null}>
                    <Stars radius={100} depth={50} count={3000} factor={3} saturation={0} fade speed={0.5} />
                    <ambientLight intensity={0.2} />
                    <pointLight position={[10, 10, 10]} intensity={1} />

                    <mesh rotation={[Math.PI / 2, 0, 0]}>
                        <torusGeometry args={[8, 4, 16, 64]} />
                        <meshStandardMaterial color="#201040" wireframe transparent opacity={0.1} />
                    </mesh>

                    {/* Key change: Pass handleDive */}
                    <TorusVisualizer kernel={activeKernel} onDive={handleDive} />
                    <EngineTicker kernel={activeKernel} />

                    <OrbitControls autoRotate autoRotateSpeed={0.5} />
                </Suspense>
            </Canvas>

            {/* UI Overlay */}
            <SystemMonitor kernel={activeKernel} />
            
            {/* Desktop Environment */}
            <Desktop kernel={activeKernel} />

            {/* Navigation Controls */}
            {universeStack.length > 1 && (
                <div className="absolute top-4 right-4 pointer-events-auto">
                    <button
                        onClick={handleAscend}
                        className="bg-red-500 text-white font-bold py-2 px-6 rounded hover:bg-red-400 shadow-[0_0_15px_rgba(255,0,0,0.5)] border border-red-300"
                    >
                        ▲ ASCEND TO PARENT UNIVERSE
                    </button>
                    <div className="text-right mt-2 text-xs text-red-400 font-mono">
                        DEPTH LEVEL: {universeStack.length - 1}
                    </div>
                </div>
            )}
        </div>
    )
}

export default App
