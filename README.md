# greenServeFE

**The source code, compiler, bytecode engine, and virtual machine of greenServeFE.**

This repository contains the core implementation of the greenServeFE programming language.

It is mainly for maintaining the source code and helping future developers understand how the language works. The code is kept here so it can be studied, improved, and continued by other developers.

## About

greenServeFE is written in C. It has its own lexer, parser, compiler, bytecode system, and virtual machine.

A `.gsve` program goes through these stages:

```text
Source Code
    |
   Lexer
    |
  Parser
    |
 Compiler
    |
  Bytecode
    |
 Virtual Machine
    |
 Execution
```

## Source Files

| File | Description |
|---|---|
| `main.c` | Program entry point |
| `lexer.c / lexer.h` | Source code tokenizer |
| `parser.c / parser.h` | Parser and AST |
| `compiler.c / compiler.h` | Bytecode compiler |
| `bytecode.c / bytecode.h` | Bytecode instructions and code objects |
| `value.c / value.h` | Runtime values and memory management |
| `vm.c / vm.h` | Virtual machine |
| `module.h` | Native module interface |
| `profiler.c / profiler.h` | Execution profiler |
| `Makefile` | Build configuration |
| `.github/workflows/main.yml` | Build and release workflow |

## Language Example

```gsve
define name = "greenServeFE";

func greet(person) {
    print("Hello, " + person);
}

greet(name);
```

A simple loop:

```gsve
for (number in range(1, 6)) {
    print(number);
}
```

Importing a native module:

```gsve
import server;
```

## Build

Requirements:
- C11 compiler
- GNU Make
- Linux development environment

Clone the repository:

```bash
git clone https://github.com/MacoraOne/greenServeFE.git
cd greenServeFE
```

Build:

```bash
make clean all
```

Run a program:

```bash
./greenServeFE program.gsve
```

Other options:

```bash
./greenServeFE --version
./greenServeFE --dump program.gsve
./greenServeFE --trace program.gsve
./greenServeFE --profile program.gsve
```

## For Future Developers

If you want to work on greenServeFE, start by reading the header files and then explore the related `.c` files.

Please be careful when changing the bytecode instructions, runtime values, memory management, or native module interface. These parts are connected, and a small change in one place can affect other parts of the language.

Try to keep the code readable, test your changes, and update the documentation when necessary.

For contribution guidelines, see [CONTRIBUTE.md](CONTRIBUTE.md).

## Future Development

greenServeFE is an ongoing project. There is still room to improve its compiler, VM, runtime, documentation, and testing.

The goal of this repository is to keep the core implementation available and understandable so development can continue in the future.

---

**Project:** greenServeFE  
**Repository:** https://github.com/MacoraOne/greenServeFE  
**Language:** C  
**Source extension:** `.gsve`
