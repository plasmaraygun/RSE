# Arqon Language Compiler

Compiles high-level Arqon DSL to RSE bytecode.

## Syntax Example

```arqon
contract Token {
  state balance: map[address => u64]
  
  fn transfer(to: address, amount: u64) {
    require(balance[sender] >= amount, "Insufficient balance")
    balance[sender] -= amount
    balance[to] += amount
    emit Transfer(sender, to, amount)
  }
}
```

## Features

- **Lexer**: Tokenizes source code
- **Parser**: Builds AST from tokens
- **Code Generator**: Emits RSE bytecode

## Implementation

See `ArqonCompiler.h` for full compiler implementation (1000+ LOC).

Includes:
- Complete lexer with keyword recognition
- Recursive descent parser
- Stack-based bytecode generator
- Support for: contracts, functions, state variables, maps, control flow, operators

## Usage

```cpp
#include "compiler/ArqonCompiler.h"

compiler::ArqonCompiler compiler;
auto result = compiler.compile(source_code);

if (result.success) {
    // Execute bytecode with vm::VirtualMachine
} else {
    std::cerr << "Error: " << result.error << std::endl;
}
```
