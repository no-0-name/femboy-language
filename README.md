[![Made with: ❤️](https://img.shields.io/badge/Made%20with-❤️-red.svg)]()
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Language: C11](https://img.shields.io/badge/Language-C-blue.svg)](https://en.wikipedia.org/wiki/C)

# Femboy

Femboy is a small dynamically-typed scripting language with its own bytecode
compiler and stack-based virtual machine, written in portable C. The project was created purely as a joke.

> **Language Model:** Femboy speaks its own dialect — all keywords are replaced
> with Femboy-themed slang. `let` → `insert_into`, `fn` → `roleplay`, `print` → `moan`,
> `if` → `harder_if`, `else` → `or_im_gonna_cum`, `while` → `ride_until`, etc.
> It's the same powerful language, just with a lot more personality.

```femboy
roleplay fib(n) {
    harder_if (n < 2) { cuming n; }
    cuming fib(n - 1) + fib(n - 2);
}

insert_into numbers = [1, 2, 3];
push(numbers, fib(10));
moan(numbers);

try_me {
    throw_me "oops!";
} catch_me (e) {
    moan("caught: " + e);
}
```

Every stage is a separate, documented module. Error handling never calls
`exit()` from inside the pipeline — every function returns a status code and
fills in a `FemboyError`, so the whole thing can be embedded, tested, or have
its error paths fuzzed without the process dying unexpectedly. The codebase is
deliberately kept small so a newcomer can read it start to finish in an
afternoon.

## Features

- Types: numbers (`double`), strings, booleans, `nil` (`empty_balls`), arrays
- Variables: `insert_into x = 10;`
- Arithmetic and logic: `+ - * / % ++ --`, `== != < > <= >=`, `&& || !`
- Control flow: `harder_if`/`or_im_gonna_cum`, `ride_until`, `femboy_hours (init; cond; step) { ... }`, `stop_riding`, `keep_riding`
- Functions with recursion: `roleplay name(a, b) { ... cuming ...; }`
- Arrays: literals `[1, 2, 3]`, indexing `a[i]`, `a[i] = x`, `len(a)`, `push(a, x)`
  (arrays are reference types — assigning or passing one shares the same
  underlying object, just like in Python or JavaScript)
- Maps (hash tables): literals `{"key": value, ...}`, indexing `m["key"]`,
  `m["key"] = value`, `len(m)`, `has(m, key)`, `keys(m)`, `delete(m, key)`.
  Keys are always strings; maps are reference types, same as arrays.
- Exceptions: `try_me { ... } catch_me (e) { ... }`, `throw_me expr;`
- Math built-ins: `abs`, `sqrt`, `pow`, `floor`, `ceil`, `round`, `min`, `max`, `random`
- String built-ins: `substr`, `indexOf`, `toUpperCase`, `toLowerCase`, `trim`,
  `split`, `charAt`, `replace` (all operate on bytes, so non-ASCII text like
  Cyrillic is preserved correctly by length/copy operations, but case
  conversion only affects ASCII letters)
- A real garbage collector: mark-and-sweep, runs automatically when the heap
  grows past a threshold, correctly handles cycles (an array can contain
  itself without leaking or crashing)
- `moan(expr);` for output, `// line comments`, `/* block comments */`
- String escapes: `\n`, `\t`, `\r`, `\"`, `\\` inside string literals (an
  unrecognized escape like `\d` is a lexer error, not silently dropped)
- Multi-file programs: `import "path/to/file.femboy";` — expanded at the text
  level before lexing (like C's `#include`, not like Python's `import`):
  every function and global from the imported file becomes visible with
  no namespacing. The path resolves relative to the importing file. The
  same file imported more than once (directly or transitively) is only
  included once; a cyclic import is a clean error, not a hang.

## The Femboy Dialect

| Standard Syntax | Femboy Slang | Meaning |
|-----------------|--------------|---------|
| `let` | `insert_into` | Declare a variable |
| `fn` | `roleplay` | Declare a function |
| `if` | `harder_if` | Conditional |
| `else` | `or_im_gonna_cum` | Else branch |
| `while` | `ride_until` | While loop |
| `for` | `femboy_hours` | For loop |
| `break` | `stop_riding` | Break out of loop |
| `continue` | `keep_riding` | Continue loop |
| `return` | `cuming` | Return from function |
| `print` | `moan` | Print to console |
| `try` | `try_me` | Try block |
| `catch` | `catch_me` | Catch exception |
| `throw` | `throw_me` | Throw exception |
| `true` | `oh_yes` | Boolean true |
| `false` | `not_there` | Boolean false |
| `null` | `empty_balls` | Null/nothing |

## Building

Femboy has no dependencies beyond a C compiler and libm.

```sh
gcc -O2 -Iinclude src/*.c -o femboy -lm
./femboy examples/hello.fmb
```

A `Makefile` is also provided:

```sh
make        # builds ./femboy
make clean
```

### Debugging

Run with `--dump` to print the disassembled bytecode before execution:

```sh
./femboy examples/test_ok.fmb --dump
```

Set `FEMBOY_GC_LOG=1` to print a line every time the garbage collector runs:

```sh
FEMBOY_GC_LOG=1 ./femboy examples/test_gc_references.fmb
```

## Exit codes

Femboy never crashes or calls `exit()` from inside the lexer, parser,
compiler, or VM for ordinary user errors — every failure produces a specific
exit code and a message with the line/column where possible:

| Code | Meaning                                                    |
|------|-------------------------------------------------------------|
| 0    | Success                                                    |
| 1    | Usage error (bad command-line arguments)                   |
| 2    | Out of memory (fatal, cannot be recovered from)             |
| 3    | I/O error (couldn't read the source file)                  |
| 4    | Lexical error (invalid token)                               |
| 5    | Parse error (invalid syntax)                                 |
| 6    | Compile error (e.g. calling an undefined function)           |
| 7    | Runtime error (e.g. division by zero, uncaught exception)    |

## Contributing

Contributions are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md) for the
workflow, coding style, and how to run the test suite locally.

## License

Femboy is released under the [MIT License](LICENSE).
