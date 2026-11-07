# Quantum Circuit Simulator

A GPU-accelerated state-vector quantum circuit simulator in C23. Gates run as
WebGPU compute shaders on
[allyria-gfx](https://github.com/ravvio/allyria-gfx), and an f64 CPU
simulator serves as the reference that every GPU result is checked against.

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
./build/qcs [qubits]
```

Starts the GPU with room for a state of `qubits` qubits and prints how many
fit on the device. More than the adapter allows fails with an error naming
the limit.

## Test

```sh
ctest --test-dir build              # all tests
ctest --test-dir build -L cpu       # GPU-free tests
ctest --test-dir build -L gpu       # needs a WebGPU adapter
```

## Benchmark

```sh
./build/release/bench cpu|gpu [qubits|min-max] [gates]
```

Times H gates on random targets and prints milliseconds per gate and
effective memory bandwidth. Defaults: 16-24 qubits, 100 gates. For example,
`./build/release/bench gpu 16-28` goes up to the GPU's ceiling. Use the
Release build: the CPU is about 4× slower in Debug.

## Repository

| Path | What |
|---|---|
| `include/qcs/` | Public headers, included as `<qcs/qcs.h>` |
| `lib/` | The library: gates, circuits, CPU and GPU simulators |
| `shaders/` | WGSL compute kernels, loaded at runtime |
| `src/main.c` | The `qcs` CLI |
| `tests/` | CTest executables |
| `bench/` | CPU vs GPU benchmark |
| `PLAN.md` | Roadmap, phase by phase, with measured numbers |
| `SOURCES.md` | References behind the design |
