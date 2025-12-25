#!/usr/bin/env python3
"""
Petals Bridge - Real distributed inference via Petals network

This script provides a socket server that the C++ PetalsClient can connect to
for real LLM inference over the Petals distributed network.

Usage:
    pip install petals transformers
    python petals_bridge.py [--port 8765] [--model meta-llama/Llama-2-70b-chat-hf]
"""

import argparse
import json
import socket
import threading
import sys
from typing import Optional

# Check for petals
try:
    from petals import AutoDistributedModelForCausalLM
    from transformers import AutoTokenizer
    PETALS_AVAILABLE = True
except ImportError:
    PETALS_AVAILABLE = False
    print("[WARNING] Petals not installed. Run: pip install petals transformers")


class PetalsBridge:
    def __init__(self, model_name: str = "meta-llama/Llama-2-70b-chat-hf", port: int = 8765):
        self.model_name = model_name
        self.port = port
        self.model = None
        self.tokenizer = None
        self.running = False
        self.server_socket = None
        
    def load_model(self) -> bool:
        """Load the distributed model via Petals"""
        if not PETALS_AVAILABLE:
            print("[ERROR] Petals not available")
            return False
            
        try:
            print(f"[Petals] Loading tokenizer for {self.model_name}...")
            self.tokenizer = AutoTokenizer.from_pretrained(self.model_name)
            
            print(f"[Petals] Connecting to distributed model {self.model_name}...")
            self.model = AutoDistributedModelForCausalLM.from_pretrained(self.model_name)
            
            print(f"[Petals] Model loaded successfully!")
            return True
        except Exception as e:
            print(f"[ERROR] Failed to load model: {e}")
            return False
    
    def generate(self, prompt: str, max_tokens: int = 256, temperature: float = 0.7) -> dict:
        """Generate text using the distributed model"""
        if not self.model or not self.tokenizer:
            return {"error": "Model not loaded", "tokens": 0, "text": ""}
        
        try:
            inputs = self.tokenizer(prompt, return_tensors="pt")
            
            outputs = self.model.generate(
                **inputs,
                max_new_tokens=max_tokens,
                temperature=temperature,
                do_sample=temperature > 0,
            )
            
            generated_text = self.tokenizer.decode(outputs[0], skip_special_tokens=True)
            # Remove the prompt from the output
            response_text = generated_text[len(prompt):].strip()
            
            num_tokens = len(outputs[0]) - len(inputs['input_ids'][0])
            
            return {
                "text": response_text,
                "tokens": num_tokens,
                "error": None
            }
        except Exception as e:
            return {"error": str(e), "tokens": 0, "text": ""}
    
    def handle_client(self, client_socket: socket.socket, address: tuple):
        """Handle a client connection"""
        print(f"[Petals] Client connected from {address}")
        
        try:
            while self.running:
                # Receive request
                data = client_socket.recv(65536)
                if not data:
                    break
                
                try:
                    request = json.loads(data.decode('utf-8'))
                    
                    if request.get('type') == 'generate':
                        result = self.generate(
                            prompt=request.get('prompt', ''),
                            max_tokens=request.get('max_tokens', 256),
                            temperature=request.get('temperature', 0.7)
                        )
                        response = json.dumps(result)
                        client_socket.send(response.encode('utf-8'))
                    
                    elif request.get('type') == 'status':
                        status = {
                            "model": self.model_name,
                            "ready": self.model is not None,
                            "petals_available": PETALS_AVAILABLE
                        }
                        client_socket.send(json.dumps(status).encode('utf-8'))
                    
                    elif request.get('type') == 'shutdown':
                        self.running = False
                        client_socket.send(b'{"status": "shutting_down"}')
                        break
                    
                except json.JSONDecodeError:
                    client_socket.send(b'{"error": "Invalid JSON"}')
                    
        except Exception as e:
            print(f"[Petals] Client error: {e}")
        finally:
            client_socket.close()
            print(f"[Petals] Client disconnected from {address}")
    
    def start_server(self):
        """Start the socket server"""
        self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind(('127.0.0.1', self.port))
        self.server_socket.listen(5)
        self.running = True
        
        print(f"[Petals] Server listening on port {self.port}")
        
        while self.running:
            try:
                self.server_socket.settimeout(1.0)
                try:
                    client_socket, address = self.server_socket.accept()
                    thread = threading.Thread(target=self.handle_client, args=(client_socket, address))
                    thread.daemon = True
                    thread.start()
                except socket.timeout:
                    continue
            except Exception as e:
                if self.running:
                    print(f"[Petals] Server error: {e}")
                break
        
        self.server_socket.close()
        print("[Petals] Server stopped")
    
    def stop(self):
        """Stop the server"""
        self.running = False


def main():
    parser = argparse.ArgumentParser(description='Petals Bridge Server')
    parser.add_argument('--port', type=int, default=8765, help='Port to listen on')
    parser.add_argument('--model', type=str, default='meta-llama/Llama-2-70b-chat-hf', 
                        help='Model to load')
    parser.add_argument('--no-load', action='store_true', help='Start server without loading model')
    args = parser.parse_args()
    
    bridge = PetalsBridge(model_name=args.model, port=args.port)
    
    if not args.no_load:
        if not bridge.load_model():
            print("[WARNING] Starting server without model (status-only mode)")
    
    try:
        bridge.start_server()
    except KeyboardInterrupt:
        print("\n[Petals] Shutting down...")
        bridge.stop()


if __name__ == '__main__':
    main()
