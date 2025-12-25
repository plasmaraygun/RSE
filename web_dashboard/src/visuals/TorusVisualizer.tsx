import { useRef, useMemo } from 'react'
import { useFrame } from '@react-three/fiber'
import * as THREE from 'three'
import { RSEKernel } from '../core/RSEKernel';


interface TorusVisualizerProps {
    kernel: RSEKernel;
    onDive?: (agentId: string) => void;
}

export function TorusVisualizer({ kernel, onDive }: TorusVisualizerProps) {
    const meshRef = useRef<THREE.InstancedMesh>(null);
    const lightRef = useRef<THREE.PointLight>(null);

    // Create a dummy object for calculating matrices
    const dummy = useMemo(() => new THREE.Object3D(), []);

    // Store agent IDs to map instanceId -> agentId on click
    const instanceMap = useRef<string[]>([]);

    useFrame((state) => {
        if (!meshRef.current) return;

        // Flatten all agent arrays from the grid
        const agents = Array.from(kernel.space.grid.values()).flat();

        // Resize check would go here in prod

        // Reset instance map
        instanceMap.current = [];

        let i = 0;
        for (const agent of agents) {
            if (i >= 5000) break;

            instanceMap.current[i] = agent.id;

            // Map Toroidal Coords (0..32) to World Coords (-10..10)
            const x = (agent.x / kernel.space.width) * 20 - 10;
            const y = (agent.y / kernel.space.height) * 20 - 10;
            const z = (agent.z / kernel.space.depth) * 20 - 10;

            dummy.position.set(x, y, z);
            dummy.rotation.x = state.clock.elapsedTime + agent.id.charCodeAt(0);
            dummy.rotation.y = state.clock.elapsedTime * 0.5;
            dummy.scale.setScalar(0.4);

            dummy.updateMatrix();
            meshRef.current.setMatrixAt(i, dummy.matrix);

            const hue = (agent.symbol.charCodeAt(0) - 65) / 26;
            meshRef.current.setColorAt(i, new THREE.Color().setHSL(hue, 1, 0.5));

            i++;
        }

        // Hide unused instances
        for (let j = i; j < 5000; j++) {
            dummy.position.set(0, 0, 0);
            dummy.scale.setScalar(0);
            dummy.updateMatrix();
            meshRef.current.setMatrixAt(j, dummy.matrix);
        }

        meshRef.current.instanceMatrix.needsUpdate = true;
        if (meshRef.current.instanceColor) meshRef.current.instanceColor.needsUpdate = true;

        if (lightRef.current) {
            lightRef.current.intensity = 1 + Math.sin(state.clock.elapsedTime * 2) * 0.5;
        }
    });

    const handleClick = (e: any) => {
        e.stopPropagation();
        const instanceId = e.instanceId;
        if (instanceId !== undefined && instanceMap.current[instanceId]) {
            const agentId = instanceMap.current[instanceId];
            console.log("Clicked Agent:", agentId);
            onDive?.(agentId);
        }
    };

    return (
        <group>
            <instancedMesh
                ref={meshRef}
                args={[undefined, undefined, 5000]}
                onClick={handleClick}
                onPointerOver={() => document.body.style.cursor = 'pointer'}
                onPointerOut={() => document.body.style.cursor = 'auto'}
            >
                <boxGeometry args={[1, 1, 1]} />
                <meshStandardMaterial toneMapped={false} />
            </instancedMesh>

            <pointLight ref={lightRef} position={[0, 0, 0]} color="#00ffff" distance={20} decay={2} />
        </group>
    )
}
