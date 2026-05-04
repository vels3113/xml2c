# xml2c: Signal-Flow Graph Code Generator

A C++23 header-only library and CLI tool that transforms XML-described signal-flow diagrams (block diagrams) into optimized C code.

## Overview

`xml2c` parses XML representations of control systems and generates equivalent C code with a typed struct, initialization function, step function, and external I/O port table. The library implements a three-stage pipeline:

1. **Parse**: pugixml DOM parser converts XML → in-memory graph structure
2. **Sort**: Kahn's algorithm topological sort respects UnitDelay feedback-edge semantics
3. **Emit**: C code generator produces ordered, correct computations

## Features

- **Header-only library**: `src/xml_to_c.hpp` — no separate compilation required
- **Zero external dependencies**: pugixml vendored in `third_party/pugixml/`
- **C++23 with safety**: Uses modern C++ features; builds with `-Wall -Wextra -Wpedantic`

## Architecture

### Block Types

| Type      | Behavior |
|-----------|----------|
| Inport    | Named input port (external, struct field) |
| Outport   | Named output port (references driving field) |
| Sum       | Multi-input add/subtract with per-port signs |
| Gain      | Scalar multiply by parameter |
| UnitDelay | One-step memory; output = prev input, state updated last |
| Unknown   | Unrecognized types skipped in codegen |

## Building

```bash
./build.sh [preset]        # debug (default), asan, ubsan, release, coverage
```

All builds use `clang++` with C++23. Build artifacts land in `build/<preset>/`.

## Usage

### As a Library

```cpp
#include "xml_to_c.hpp"

// Generate C code
auto c_source = Xml2C::convert(xml_string, "controller");

std::cout << c_source;
```

### CLI Tool

```bash
# From stdin
./build/debug/main prefix_name < input.xml > output.c

# From file
./build/debug/main prefix_name input.xml > output.c

# Example
./build/debug/main pi_controller example.xml > pi_controller.c
```

The generated C code includes:
- Static struct `prefix` with fields for all blocks
- `void prefix_generated_init()` — resets UnitDelay states to 0
- `void prefix_generated_step()` — executes one computation cycle in topologically sorted order
- `prefix_generated_ext_ports[]` — table of named I/O ports with pointers and input/output flags
- `prefix_generated_ext_ports_size` — size of the ports table

## XML Input Format

```xml
<?xml version="1.0"?>
<System>
  <Block BlockType="Inport" Name="setpoint" SID="16">
    <Port><P Name="PortNumber">1</P></Port>
  </Block>
  <Block BlockType="Gain" Name="P_gain" SID="19">
    <P Name="Gain">3.0</P>
  </Block>
  <Block BlockType="Sum" Name="error" SID="17">
    <P Name="Inputs">+-</P>
  </Block>
  <Block BlockType="Outport" Name="output" SID="20"/>
  
  <Line><P Name="Src">16#out:1</P><P Name="Dst">17#in:1</P></Line>
  <Line>
    <P Name="Src">17#out:1</P>
    <Branch><P Name="Dst">19#in:1</P></Branch>
    <Branch><P Name="Dst">20#in:1</P></Branch>
  </Line>
</System>
```

**Port References**: `"SID#out:N"` or `"SID#in:N"` where SID is block ID, port is 1-indexed (omit for port 1).

**Branching**: Multiple `<Branch>` elements fan a single output to multiple inputs.

## Testing

Run the full test suite:

```bash
./test.sh unit     # Unit tests
./test.sh asan     # AddressSanitizer
./test.sh ubsan    # Undefined Behavior Sanitizer
./test.sh valgrind # Valgrind leak check
./test.sh tidy     # clang-tidy
./test.sh cppcheck # cppcheck static analysis
./test.sh coverage # Coverage report (HTML: build/coverage/html/index.html)
./test.sh all      # Run all checks
```

## Project Structure

```
.
├── src/
│   ├── xml_to_c.hpp       # Core library (header-only)
│   └── main.cpp           # CLI tool
├── tests/
│   ├── test_xml_to_c.cpp  # GoogleTest suite
├── third_party/
│   └── pugixml/           # Vendored pugixml 1.15
├── CMakeLists.txt         # Build configuration
├── CMakePresets.json      # CMake presets
├── Makefile               # Convenience targets
├── build.sh               # Build script
└── test.sh                # Test script
```

## License

See individual files for details. pugixml is licensed under the MIT license (see `third_party/pugixml/`).
