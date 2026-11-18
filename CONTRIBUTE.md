# Contributing to greenServeFE

Thank you for your interest in contributing to greenServeFE.

greenServeFE is a C-based programming language and virtual machine project. Its source code is maintained to support long-term development, readability, reliability, and future improvements.

## Code Style

Contributors should write readable, conventional C code.

- Use meaningful variable and function names.
- Use consistent indentation.
- Keep functions focused on clearly defined responsibilities.
- Separate logically different operations.
- Add comments to explain important implementation decisions.
- Document public interfaces and non-obvious behavior.
- Avoid unnecessary global state.
- Check allocation and system-call failures.
- Release dynamically allocated resources appropriately.
- Avoid duplicated logic when a shared implementation is appropriate.

Comments should explain why something is necessary, not simply repeat what the next line does.

## Preserve Existing Language Behavior

The language implementation must remain consistent.

Before modifying the lexer, parser, compiler, bytecode, or VM:

1. Understand the existing implementation.
2. Identify every component affected by the change.
3. Check how the components communicate.
4. Preserve existing syntax and runtime behavior unless the change explicitly introduces a documented language update.
5. Add regression tests for affected functionality.

Do not casually change:

- Token definitions or token ordering.
- AST structures.
- Bytecode instruction values.
- Runtime value representation.
- Public function signatures.
- Native module ABI structures.
- Ownership and memory-management rules.

Changes to these areas can affect multiple parts of the runtime.


## Download .zip file and modify or contribute yourself. You own this if you contribute. 
