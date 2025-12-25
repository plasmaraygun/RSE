#!/usr/bin/env python3
"""
Arqon Inference Bridge
Unified interface for distributed (Petals) and local (Ollama) LLM inference.

Features:
- Ollama integration for local models (fast, private)
- Petals integration for distributed inference (large models)
- Auto-discovery of available models
- Model pull/update system
- Socket server for C++ kernel communication
"""

import asyncio
import json
import socket
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, asdict
from enum import Enum
from typing import Optional, Dict, List, Any
import os

# Try imports
try:
    import requests
    HAS_REQUESTS = True
except ImportError:
    HAS_REQUESTS = False

try:
    from petals import AutoDistributedModelForCausalLM
    from transformers import AutoTokenizer
    HAS_PETALS = True
except ImportError:
    HAS_PETALS = False


class InferenceBackend(Enum):
    OLLAMA = "ollama"
    PETALS = "petals"
    MOCK = "mock"


@dataclass
class ModelInfo:
    name: str
    backend: str
    size_gb: float
    parameters: str
    quantization: str
    available: bool
    running: bool = False


@dataclass
class InferenceRequest:
    session_id: int
    prompt: str
    max_tokens: int = 256
    temperature: float = 0.7
    model: Optional[str] = None


@dataclass
class InferenceResponse:
    session_id: int
    text: str
    tokens_generated: int
    tokens_per_second: float
    model_used: str
    backend: str
    error: Optional[str] = None


class OllamaClient:
    """Client for Ollama local inference."""
    
    def __init__(self, host: str = "localhost", port: int = 11434):
        self.base_url = f"http://{host}:{port}"
        self.available = self._check_available()
        
    def _check_available(self) -> bool:
        if not HAS_REQUESTS:
            return False
        try:
            r = requests.get(f"{self.base_url}/api/tags", timeout=2)
            return r.status_code == 200
        except:
            return False
    
    def list_models(self) -> List[ModelInfo]:
        """Get list of available Ollama models."""
        if not self.available:
            return []
        try:
            r = requests.get(f"{self.base_url}/api/tags", timeout=5)
            data = r.json()
            models = []
            for m in data.get("models", []):
                name = m.get("name", "unknown")
                size = m.get("size", 0) / (1024**3)  # bytes to GB
                details = m.get("details", {})
                models.append(ModelInfo(
                    name=name,
                    backend="ollama",
                    size_gb=round(size, 2),
                    parameters=details.get("parameter_size", "unknown"),
                    quantization=details.get("quantization_level", "unknown"),
                    available=True,
                    running=False
                ))
            return models
        except Exception as e:
            print(f"[Ollama] Error listing models: {e}")
            return []
    
    def list_running(self) -> List[str]:
        """Get list of currently running models."""
        if not self.available:
            return []
        try:
            r = requests.get(f"{self.base_url}/api/ps", timeout=5)
            data = r.json()
            return [m.get("name") for m in data.get("models", [])]
        except:
            return []
    
    def pull_model(self, model_name: str) -> bool:
        """Pull a model from Ollama registry."""
        if not self.available:
            return False
        try:
            print(f"[Ollama] Pulling model: {model_name}")
            r = requests.post(
                f"{self.base_url}/api/pull",
                json={"name": model_name, "stream": False},
                timeout=600  # 10 min timeout for large models
            )
            return r.status_code == 200
        except Exception as e:
            print(f"[Ollama] Pull failed: {e}")
            return False
    
    def generate(self, model: str, prompt: str, max_tokens: int = 256, 
                 temperature: float = 0.7) -> InferenceResponse:
        """Generate text using Ollama."""
        start = time.time()
        try:
            r = requests.post(
                f"{self.base_url}/api/generate",
                json={
                    "model": model,
                    "prompt": prompt,
                    "stream": False,
                    "options": {
                        "num_predict": max_tokens,
                        "temperature": temperature
                    }
                },
                timeout=120
            )
            data = r.json()
            elapsed = time.time() - start
            
            response_text = data.get("response", "")
            eval_count = data.get("eval_count", len(response_text.split()))
            
            return InferenceResponse(
                session_id=0,
                text=response_text,
                tokens_generated=eval_count,
                tokens_per_second=eval_count / elapsed if elapsed > 0 else 0,
                model_used=model,
                backend="ollama"
            )
        except Exception as e:
            return InferenceResponse(
                session_id=0,
                text="",
                tokens_generated=0,
                tokens_per_second=0,
                model_used=model,
                backend="ollama",
                error=str(e)
            )


class PetalsClient:
    """Client for Petals distributed inference."""
    
    SUPPORTED_MODELS = [
        "meta-llama/Meta-Llama-3.1-70B-Instruct",
        "meta-llama/Meta-Llama-3.1-8B-Instruct",
        "mistralai/Mixtral-8x22B-Instruct-v0.1",
        "mistralai/Mistral-7B-Instruct-v0.3",
    ]
    
    def __init__(self):
        self.available = HAS_PETALS
        self.models: Dict[str, Any] = {}
        self.tokenizers: Dict[str, Any] = {}
        
    def list_models(self) -> List[ModelInfo]:
        """Get list of available Petals models."""
        models = []
        for name in self.SUPPORTED_MODELS:
            # Extract size from name
            if "70B" in name:
                size, params = 140, "70B"
            elif "22B" in name:
                size, params = 44, "22B"
            elif "8B" in name:
                size, params = 16, "8B"
            elif "7B" in name:
                size, params = 14, "7B"
            else:
                size, params = 0, "unknown"
                
            models.append(ModelInfo(
                name=name,
                backend="petals",
                size_gb=size,
                parameters=params,
                quantization="distributed",
                available=self.available
            ))
        return models
    
    def load_model(self, model_name: str) -> bool:
        """Load a model for inference."""
        if not self.available:
            return False
        if model_name in self.models:
            return True
        try:
            print(f"[Petals] Loading distributed model: {model_name}")
            self.tokenizers[model_name] = AutoTokenizer.from_pretrained(model_name)
            self.models[model_name] = AutoDistributedModelForCausalLM.from_pretrained(model_name)
            return True
        except Exception as e:
            print(f"[Petals] Failed to load {model_name}: {e}")
            return False
    
    def generate(self, model: str, prompt: str, max_tokens: int = 256,
                 temperature: float = 0.7) -> InferenceResponse:
        """Generate text using Petals distributed network."""
        if model not in self.models:
            if not self.load_model(model):
                return InferenceResponse(
                    session_id=0,
                    text="",
                    tokens_generated=0,
                    tokens_per_second=0,
                    model_used=model,
                    backend="petals",
                    error="Model not loaded"
                )
        
        start = time.time()
        try:
            tokenizer = self.tokenizers[model]
            model_obj = self.models[model]
            
            inputs = tokenizer(prompt, return_tensors="pt")
            outputs = model_obj.generate(
                **inputs,
                max_new_tokens=max_tokens,
                temperature=temperature,
                do_sample=True
            )
            
            text = tokenizer.decode(outputs[0], skip_special_tokens=True)
            text = text[len(prompt):]  # Remove prompt from output
            
            elapsed = time.time() - start
            tokens = len(tokenizer.encode(text))
            
            return InferenceResponse(
                session_id=0,
                text=text,
                tokens_generated=tokens,
                tokens_per_second=tokens / elapsed if elapsed > 0 else 0,
                model_used=model,
                backend="petals"
            )
        except Exception as e:
            return InferenceResponse(
                session_id=0,
                text="",
                tokens_generated=0,
                tokens_per_second=0,
                model_used=model,
                backend="petals",
                error=str(e)
            )


class ArqonInferenceBridge:
    """Main inference bridge server."""
    
    # Recommended models by use case
    RECOMMENDED_MODELS = {
        "fast": "llama3.2:3b",
        "balanced": "llama3.2:latest",
        "quality": "llama3.1:8b",
        "code": "qwen2.5-coder:7b",
        "reasoning": "qwen2.5:14b",
        "large": "llama3.1:70b"
    }
    
    def __init__(self, port: int = 8765):
        self.port = port
        self.ollama = OllamaClient()
        self.petals = PetalsClient()
        self.sessions: Dict[int, dict] = {}
        self.next_session_id = 1
        self.running = False
        self.default_model = "llama3.2:latest"
        
    def system_check(self) -> dict:
        """Run comprehensive system check."""
        result = {
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            "ollama": {
                "available": self.ollama.available,
                "models": [],
                "running": []
            },
            "petals": {
                "available": self.petals.available,
                "models": []
            },
            "recommended_actions": []
        }
        
        # Check Ollama
        if self.ollama.available:
            result["ollama"]["models"] = [asdict(m) for m in self.ollama.list_models()]
            result["ollama"]["running"] = self.ollama.list_running()
            
            # Check if recommended models are installed
            installed = {m["name"].split(":")[0] for m in result["ollama"]["models"]}
            for use_case, model in self.RECOMMENDED_MODELS.items():
                base_model = model.split(":")[0]
                if base_model not in installed:
                    result["recommended_actions"].append(
                        f"Pull '{model}' for {use_case} use case"
                    )
        else:
            result["recommended_actions"].append(
                "Install Ollama: curl -fsSL https://ollama.com/install.sh | sh"
            )
        
        # Check Petals
        if self.petals.available:
            result["petals"]["models"] = [asdict(m) for m in self.petals.list_models()]
        else:
            result["recommended_actions"].append(
                "Install Petals: pip install petals transformers"
            )
        
        return result
    
    def pull_recommended_models(self) -> dict:
        """Pull all recommended Ollama models."""
        results = {}
        if not self.ollama.available:
            return {"error": "Ollama not available"}
        
        installed = {m.name.split(":")[0] for m in self.ollama.list_models()}
        
        for use_case, model in self.RECOMMENDED_MODELS.items():
            base_model = model.split(":")[0]
            if base_model in installed:
                results[model] = "already_installed"
            else:
                success = self.ollama.pull_model(model)
                results[model] = "success" if success else "failed"
        
        return results
    
    def pull_model(self, model_name: str) -> bool:
        """Pull a specific model."""
        return self.ollama.pull_model(model_name)
    
    def create_session(self, model: Optional[str] = None) -> int:
        """Create a new inference session."""
        session_id = self.next_session_id
        self.next_session_id += 1
        
        self.sessions[session_id] = {
            "model": model or self.default_model,
            "created": time.time(),
            "requests": 0
        }
        
        return session_id
    
    def close_session(self, session_id: int):
        """Close an inference session."""
        if session_id in self.sessions:
            del self.sessions[session_id]
    
    def infer(self, request: InferenceRequest) -> InferenceResponse:
        """Run inference on a request."""
        # Determine model
        if request.session_id in self.sessions:
            model = request.model or self.sessions[request.session_id]["model"]
            self.sessions[request.session_id]["requests"] += 1
        else:
            model = request.model or self.default_model
        
        # Route to appropriate backend
        if self.ollama.available and not model.startswith("meta-llama/"):
            response = self.ollama.generate(
                model, request.prompt, request.max_tokens, request.temperature
            )
        elif self.petals.available and model in PetalsClient.SUPPORTED_MODELS:
            response = self.petals.generate(
                model, request.prompt, request.max_tokens, request.temperature
            )
        else:
            # No backend available - return error, NOT mock data
            response = InferenceResponse(
                session_id=request.session_id,
                text="",
                tokens_generated=0,
                tokens_per_second=0,
                model_used=model,
                backend="none",
                error="No inference backend available. Install Ollama (curl -fsSL https://ollama.com/install.sh | sh) or Petals (pip install petals transformers)"
            )
        
        response.session_id = request.session_id
        return response
    
    def handle_client(self, conn: socket.socket, addr):
        """Handle a client connection."""
        print(f"[Bridge] Client connected: {addr}")
        
        try:
            while self.running:
                data = conn.recv(65536)
                if not data:
                    break
                
                try:
                    request = json.loads(data.decode())
                    response = self.process_request(request)
                    conn.send(json.dumps(response).encode())
                except json.JSONDecodeError:
                    conn.send(json.dumps({"error": "Invalid JSON"}).encode())
                except Exception as e:
                    conn.send(json.dumps({"error": str(e)}).encode())
        except Exception as e:
            print(f"[Bridge] Client error: {e}")
        finally:
            conn.close()
            print(f"[Bridge] Client disconnected: {addr}")
    
    def process_request(self, request: dict) -> dict:
        """Process a request from the C++ kernel."""
        action = request.get("action", "infer")
        
        if action == "system_check":
            return self.system_check()
        
        elif action == "list_models":
            models = []
            models.extend([asdict(m) for m in self.ollama.list_models()])
            models.extend([asdict(m) for m in self.petals.list_models()])
            return {"models": models}
        
        elif action == "pull_model":
            model = request.get("model", "")
            success = self.pull_model(model)
            return {"success": success, "model": model}
        
        elif action == "pull_recommended":
            return self.pull_recommended_models()
        
        elif action == "create_session":
            model = request.get("model")
            session_id = self.create_session(model)
            return {"session_id": session_id}
        
        elif action == "close_session":
            session_id = request.get("session_id", 0)
            self.close_session(session_id)
            return {"success": True}
        
        elif action == "infer":
            req = InferenceRequest(
                session_id=request.get("session_id", 0),
                prompt=request.get("prompt", ""),
                max_tokens=request.get("max_tokens", 256),
                temperature=request.get("temperature", 0.7),
                model=request.get("model")
            )
            response = self.infer(req)
            return asdict(response)
        
        else:
            return {"error": f"Unknown action: {action}"}
    
    def start(self):
        """Start the bridge server."""
        self.running = True
        
        server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server.bind(("0.0.0.0", self.port))
        server.listen(5)
        server.settimeout(1.0)
        
        print(f"[Bridge] Arqon Inference Bridge started on port {self.port}")
        print(f"[Bridge] Ollama: {'✓ Available' if self.ollama.available else '✗ Not available'}")
        print(f"[Bridge] Petals: {'✓ Available' if self.petals.available else '✗ Not available'}")
        
        # Run system check on startup
        check = self.system_check()
        if check["ollama"]["models"]:
            print(f"[Bridge] Ollama models: {len(check['ollama']['models'])}")
        if check["recommended_actions"]:
            print("[Bridge] Recommended actions:")
            for action in check["recommended_actions"][:3]:
                print(f"  - {action}")
        
        try:
            while self.running:
                try:
                    conn, addr = server.accept()
                    thread = threading.Thread(target=self.handle_client, args=(conn, addr))
                    thread.daemon = True
                    thread.start()
                except socket.timeout:
                    continue
        except KeyboardInterrupt:
            print("\n[Bridge] Shutting down...")
        finally:
            self.running = False
            server.close()
    
    def stop(self):
        """Stop the bridge server."""
        self.running = False


def main():
    import argparse
    
    parser = argparse.ArgumentParser(description="Arqon Inference Bridge")
    parser.add_argument("--port", type=int, default=8765, help="Server port")
    parser.add_argument("--check", action="store_true", help="Run system check only")
    parser.add_argument("--pull", action="store_true", help="Pull recommended models")
    parser.add_argument("--model", type=str, help="Pull specific model")
    args = parser.parse_args()
    
    bridge = ArqonInferenceBridge(port=args.port)
    
    if args.check:
        check = bridge.system_check()
        print(json.dumps(check, indent=2))
        return
    
    if args.pull:
        print("Pulling recommended models...")
        results = bridge.pull_recommended_models()
        for model, status in results.items():
            print(f"  {model}: {status}")
        return
    
    if args.model:
        success = bridge.pull_model(args.model)
        print(f"Pull {args.model}: {'success' if success else 'failed'}")
        return
    
    bridge.start()


if __name__ == "__main__":
    main()
