# Quantum Circuit Simulator

A GPU-accelerated state-vector quantum circuit simulator in C23. It runs
OpenQASM 3 programs (a subset) on the GPU, as WebGPU compute shaders on
[allyria-gfx](https://github.com/ravvio/allyria-gfx), or on an f64 CPU
simulator that every GPU result is checked against.

Supported: registers, `const`, `input`, `stdgates.inc`, gate definitions,
`ctrl`/`negctrl`/`inv`/`pow` modifiers, `gphase`, `for` loops, and
mid-circuit `measure`, `reset` and `if`/`else`.

## Requirements

- CMake 3.22+
- A C23 compiler with `#embed`
- A checkout of [allyria-gfx](https://github.com/ravvio/allyria-gfx) next to
  this repo (`../allyria-gfx`), or pass `-DQCS_GFX_DIR=<path>`:
  ```sh
  git clone https://github.com/ravvio/allyria-gfx ../allyria-gfx
  ```
- A WebGPU adapter (Metal, Vulkan or D3D12) for the GPU simulator

CMake downloads the engine's dependencies (Dawn prebuilt, GLFW, glfw3webgpu)
on the first configure.

## Build

Debug, for development and tests:

```sh
cmake -B build
cmake --build build --parallel
```

Release, for benchmarks, in its own directory:

```sh
cmake -B build/release -DCMAKE_BUILD_TYPE=Release
cmake --build build/release --parallel
```

## Run

```sh
./build/qcs run examples/teleportation.qasm               # counts per result
./build/qcs run file.qasm --mode cpu --shots 1000 --seed 1
./build/qcs run file.qasm --input theta=0.3               # input float theta;
./build/qcs dump examples/teleportation.qasm              # print the program
./build/qcs                                               # GPU qubit limit
```

`run` prints how many shots gave each value of the classical bits, highest
bit first. A program without bits gets every qubit measured. `--mode` is
`gpu` by default.

## Test

```sh
ctest --test-dir build              # all tests
ctest --test-dir build -L cpu       # GPU-free tests
ctest --test-dir build -L gpu       # needs a WebGPU adapter
```

With AddressSanitizer and UBSan: `cmake -B build -DQCS_SANITIZE=ON`.

## Benchmark

```sh
./build/release/bench cpu|gpu [qubits|min-max] [gates]
```

Times H gates on random targets and prints milliseconds per gate and
effective memory bandwidth. Defaults: 16-24 qubits, 100 gates. For example,
`./build/release/bench gpu 16-28` goes up to the GPU's ceiling. Use the
Release build: the CPU is about 4× slower in Debug.
