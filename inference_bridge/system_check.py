#!/usr/bin/env python3
"""
Arqon System Check
Quick diagnostic for inference capabilities and model availability.
"""

import json
import subprocess
import sys
import os

def check_ollama():
    """Check Ollama installation and models."""
    result = {
        "installed": False,
        "running": False,
        "models": [],
        "running_models": [],
        "version": None
    }
    
    # Check if ollama is installed
    try:
        proc = subprocess.run(["ollama", "--version"], capture_output=True, text=True, timeout=5)
        if proc.returncode == 0:
            result["installed"] = True
            result["version"] = proc.stdout.strip()
    except:
        pass
    
    if not result["installed"]:
        return result
    
    # Check if ollama is running
    try:
        import requests
        r = requests.get("http://localhost:11434/api/tags", timeout=2)
        if r.status_code == 200:
            result["running"] = True
            data = r.json()
            for m in data.get("models", []):
                result["models"].append({
                    "name": m.get("name"),
                    "size_gb": round(m.get("size", 0) / (1024**3), 2),
                    "modified": m.get("modified_at", "")[:10]
                })
        
        # Check running models
        r = requests.get("http://localhost:11434/api/ps", timeout=2)
        if r.status_code == 200:
            data = r.json()
            result["running_models"] = [m.get("name") for m in data.get("models", [])]
    except:
        pass
    
    return result

def check_petals():
    """Check Petals installation."""
    result = {
        "installed": False,
        "version": None
    }
    
    try:
        import petals
        result["installed"] = True
        result["version"] = getattr(petals, "__version__", "unknown")
    except ImportError:
        pass
    
    return result

def check_gpu():
    """Check GPU availability."""
    result = {
        "nvidia": False,
        "cuda_available": False,
        "gpus": []
    }
    
    # Check nvidia-smi
    try:
        proc = subprocess.run(
            ["nvidia-smi", "--query-gpu=name,memory.total,compute_cap", "--format=csv,noheader"],
            capture_output=True, text=True, timeout=5
        )
        if proc.returncode == 0:
            result["nvidia"] = True
            for line in proc.stdout.strip().split("\n"):
                if line:
                    parts = [p.strip() for p in line.split(",")]
                    if len(parts) >= 2:
                        result["gpus"].append({
                            "name": parts[0],
                            "vram": parts[1] if len(parts) > 1 else "unknown",
                            "compute": parts[2] if len(parts) > 2 else "unknown"
                        })
    except:
        pass
    
    # Check PyTorch CUDA
    try:
        import torch
        result["cuda_available"] = torch.cuda.is_available()
    except:
        pass
    
    return result

def get_recommended_models():
    """Get list of recommended models to install."""
    return {
        "fast_local": {
            "model": "llama3.2:3b",
            "description": "Fast local inference, 3B params, ~2GB",
            "use_case": "Quick responses, resource-constrained"
        },
        "balanced": {
            "model": "llama3.2:latest", 
            "description": "Good balance of speed and quality, ~4GB",
            "use_case": "General purpose"
        },
        "quality": {
            "model": "llama3.1:8b",
            "description": "Higher quality, 8B params, ~5GB",
            "use_case": "Better reasoning"
        },
        "coding": {
            "model": "qwen2.5-coder:7b",
            "description": "Specialized for code, 7B params, ~4GB",
            "use_case": "Code generation and review"
        },
        "reasoning": {
            "model": "qwen2.5:14b",
            "description": "Strong reasoning, 14B params, ~9GB",
            "use_case": "Complex analysis"
        },
        "distributed": {
            "model": "meta-llama/Meta-Llama-3.1-70B-Instruct",
            "description": "Large model via Petals network",
            "use_case": "Maximum capability (requires Petals)"
        }
    }

def pull_model(model_name: str) -> bool:
    """Pull a model from Ollama."""
    try:
        print(f"Pulling {model_name}...")
        proc = subprocess.run(
            ["ollama", "pull", model_name],
            capture_output=False,
            timeout=1800  # 30 min timeout
        )
        return proc.returncode == 0
    except Exception as e:
        print(f"Error: {e}")
        return False

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="Arqon System Check")
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    parser.add_argument("--pull", type=str, help="Pull a specific model")
    parser.add_argument("--pull-recommended", action="store_true", help="Pull all recommended models")
    parser.add_argument("--list-recommended", action="store_true", help="List recommended models")
    args = parser.parse_args()
    
    if args.list_recommended:
        models = get_recommended_models()
        print("\nRecommended Models:")
        print("=" * 60)
        for key, info in models.items():
            print(f"\n{key}:")
            print(f"  Model: {info['model']}")
            print(f"  Description: {info['description']}")
            print(f"  Use Case: {info['use_case']}")
        return
    
    if args.pull:
        success = pull_model(args.pull)
        sys.exit(0 if success else 1)
    
    if args.pull_recommended:
        models = get_recommended_models()
        for key, info in models.items():
            if not info["model"].startswith("meta-llama/"):
                success = pull_model(info["model"])
                print(f"  {info['model']}: {'✓' if success else '✗'}")
        return
    
    # Run full system check
    result = {
        "ollama": check_ollama(),
        "petals": check_petals(),
        "gpu": check_gpu(),
        "recommended": get_recommended_models()
    }
    
    if args.json:
        print(json.dumps(result, indent=2))
        return
    
    # Pretty print
    print("\n" + "=" * 60)
    print("  ARQON INFERENCE SYSTEM CHECK")
    print("=" * 60)
    
    # Ollama
    print("\n[Ollama]")
    if result["ollama"]["installed"]:
        print(f"  ✓ Installed: {result['ollama']['version']}")
        if result["ollama"]["running"]:
            print(f"  ✓ Running")
            print(f"  Models installed: {len(result['ollama']['models'])}")
            for m in result["ollama"]["models"]:
                status = "●" if m["name"] in result["ollama"]["running_models"] else "○"
                print(f"    {status} {m['name']} ({m['size_gb']} GB)")
        else:
            print("  ✗ Not running - start with: ollama serve")
    else:
        print("  ✗ Not installed")
        print("  Install: curl -fsSL https://ollama.com/install.sh | sh")
    
    # Petals
    print("\n[Petals]")
    if result["petals"]["installed"]:
        print(f"  ✓ Installed: {result['petals']['version']}")
    else:
        print("  ✗ Not installed")
        print("  Install: pip install petals transformers")
    
    # GPU
    print("\n[GPU]")
    if result["gpu"]["nvidia"]:
        print(f"  ✓ NVIDIA GPU detected")
        for gpu in result["gpu"]["gpus"]:
            print(f"    {gpu['name']} - {gpu['vram']}")
        if result["gpu"]["cuda_available"]:
            print("  ✓ CUDA available via PyTorch")
        else:
            print("  ✗ CUDA not available in PyTorch")
    else:
        print("  ○ No NVIDIA GPU (CPU inference only)")
    
    # Recommendations
    print("\n[Recommendations]")
    if not result["ollama"]["installed"]:
        print("  1. Install Ollama for local inference")
    elif not result["ollama"]["running"]:
        print("  1. Start Ollama: ollama serve")
    elif len(result["ollama"]["models"]) == 0:
        print("  1. Pull a model: ollama pull llama3.2")
    else:
        missing = []
        installed_names = {m["name"].split(":")[0] for m in result["ollama"]["models"]}
        for key, info in result["recommended"].items():
            model_base = info["model"].split(":")[0]
            if not model_base.startswith("meta-llama/") and model_base not in installed_names:
                missing.append(info["model"])
        
        if missing:
            print(f"  Consider installing: {', '.join(missing[:3])}")
            print(f"  Run: python system_check.py --pull-recommended")
        else:
            print("  ✓ System ready for inference")
    
    print("\n" + "=" * 60 + "\n")


if __name__ == "__main__":
    main()
