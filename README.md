# CircuitForge

C++17/CUDA combinational logic and single stuck-at fault simulation for the GTX 1650 (`sm_75`). The modular engine extends the existing educational project. All six standalone prototypes in `src/` are preserved; CMake explicitly selects the new sources so their `main()` functions do not collide.

## Build on Windows

The local validation used MSVC **19.29.30154 (VS2019)**, CUDA **13.4.92**, and an NVIDIA GTX 1650 (4 GB), driver **592.82**. This particular toolkit/compiler pair compiled and ran successfully; do not assume that other CUDA versions support the same host compiler. The driver's displayed CUDA version is not the installed toolkit version.

From an **x64 Native Tools Command Prompt for VS2019**, with CMake, Ninja, and the chosen CUDA toolkit's `bin` on PATH:

```bat
cmake -S . -B build-cpu -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-cpu
ctest --test-dir build-cpu --output-on-failure

cmake -S . -B build-gpu -G Ninja -DCMAKE_BUILD_TYPE=Release -DCIRCUITFORGE_CUDA=ON
cmake --build build-gpu
ctest --test-dir build-gpu --output-on-failure
```

CPU-only builds do not require CUDA. `build-local.cmd` and `build-gpu-local.cmd` contain the exact paths used on this machine; edit them for other installations. Ninja avoided a sandbox permission error in MSBuild's FileTracker. Never use `--allow-unsupported-compiler` as a substitute for compatibility checks. CMake defaults to CPU-only, C++17, and GPU architecture 75 when enabled.

Individual historical examples still build independently from that developer prompt:

```bat
nvcc -std=c++17 -arch=sm_75 src/BitParallelFaultSimulator.cu -o Prototype.exe
Prototype.exe circuits/FullAdder.txt 65536
```

Historical executables in the repository root are not the new CLI. Use `build-gpu/CircuitForge.exe` or `build-cpu/CircuitForge.exe`.

## Commands

```powershell
.\build-gpu\CircuitForge.exe simulate circuits/FullAdder.txt
.\build-gpu\CircuitForge.exe faults circuits/FullAdder.txt --gpu --patterns 65536 --seed 17
.\build-gpu\CircuitForge.exe faults circuits/FullAdder.txt --gpu --patterns 65536 --fault-dropping --output faults.csv
.\build-gpu\CircuitForge.exe collapse circuits/FullAdder.txt --output equivalences.csv
.\build-gpu\CircuitForge.exe atpg circuits/FullAdder.txt --search-limit 100000 --output tests.csv
.\build-gpu\CircuitForge.exe benchmark circuits/benchmarks/Generated128.txt --gpu --patterns 1024 --seed 17 --block-size 128 --repetitions 5 --output report.csv
.\build-gpu\CircuitForge.exe --help
```

`simulate`, `collapse`, and ATPG are CPU commands. GPU fault simulation and benchmarking use `--gpu`; `--cpu` is the default. GPU fault reports verify every mask against the CPU backend before reporting. Output is CSV; coverage goes to stderr. Errors return a nonzero exit code. ATPG `Aborted` is a valid reported search outcome, not a command failure. Patterns are printed in primary-input declaration order.

Defaults: exhaustive patterns for at most 16 primary inputs, otherwise 1024 sampled patterns; seed 1; block size 128; three benchmark repetitions. Explicit pattern counts select exhaustive enumeration only when the count is exactly `2^inputs` (up to 24 inputs); all other counts use seeded MT19937 packed words. Small circuits therefore do not simply cycle through their truth table to inflate workloads. Pattern count is bounded to 16,777,216; allocation failures are reported. Sampling is with replacement.

## Architecture

| Module | Responsibility |
| --- | --- |
| `Circuit.h` | Compact integer-ID gates/fault sites; host wire-name metadata |
| `NetlistParser` | Original text format and restricted `.bench` import; line-numbered syntax errors |
| `CircuitCompiler` | Driver/reference validation, Kahn topological sort, execution level per sorted gate |
| `PatternGenerator` | Reproducible exhaustive or sampled 32-bit inputs |
| `CpuSimulator` | Independent scalar Boolean oracle and packed simulation |
| `GpuSimulator` | CUDA fault-free and faulty kernels, bounded global scratch, timing |
| `FaultGenerator` | Enumeration and coverage/first-detection statistics |
| `FaultCollapser` | Conservative union-find equivalence mapping |
| `AtpgEngine` | Targeted ternary implication and PI backtracking |
| `Benchmark` | Matched scalar/packed/GPU measurements and verification |

Always compile a parsed circuit before simulation. Gate order in source is arbitrary. The compiler rejects combinational cycles, invalid references, undriven wires, multiple drivers, and gates driving primary inputs. Wire IDs are assigned in first-appearance order. Types containing strings/vectors remain on the host; only flat POD gate/fault arrays and integer buffers cross the CUDA boundary.

## Netlists

Original syntax remains supported, including `#` comments:

```text
INPUT A B CIN
OUTPUT SUM COUT
XOR X1 A B
XOR SUM X1 CIN
AND T1 A B
AND T2 X1 CIN
OR COUT T1 T2
```

The `.bench` subset accepts `INPUT(a)`, `OUTPUT(z)`, and assignments such as `z = NAND(a,b)`. Supported gates are binary AND, OR, XOR, NAND, NOR, XNOR and unary NOT, BUF. Wire names use letters, digits, underscore, period, and dollar sign. Numeric names are identifiers, not constant literals. More than two fanins, constants, sequential gates such as DFF, buses, and Verilog are unsupported and rejected. `circuits/benchmarks/Smoke.bench` is a syntax fixture, not a claimed ISCAS benchmark dataset. Standard combinational `.bench` files within this subset can be imported directly; no external benchmark corpus is bundled.

## Packed CUDA execution

One bit of a `uint32_t` is one independent test. Bitwise gate operations evaluate 32 patterns at once, including multiple output comparisons and correct masking of partial batches. A CUDA thread evaluates one fault across one 32-pattern batch. A separate fault-free kernel computes reference outputs once per batch on the device.

Faulty wire scratch is wire-major: neighboring threads access adjacent fault states. Scratch is tiled to approximately 128 MiB, with other device allocations reported separately. This removes the prototype's 64-wire limit and per-thread `Good[64]`/`Bad[64]` arrays. The local compiler reported **24 registers, zero stack frame and zero spills** for the fault kernel; the good kernel used 22 registers, also without spills. These are compiler resource reports, not an occupancy or hardware-profiler measurement.

The first implementation processes batches sequentially and uses synchronous transfers/events. Small circuits cannot amortize launches, transfers, allocation, and synchronization. A single invariant non-dropping fault tile is uploaded once per simulation call. No claim is made that GPU execution is always faster. The global scratch approach trades explicit device memory traffic for bounded per-thread storage. The 128 MiB tiling boundary has not been stress-tested on this machine.

## Fault model and collapsing

Two faults per modeled wire: SA0 and SA1. Stable IDs are `2 * WireId + Value`; IDs are stable for a fixed parsed netlist, not across arbitrary source reordering. Injection covers primary inputs, internal wires, and primary outputs. Reports include detection state, first detecting pattern (blank when undetected), and detection count.

This is a **stem-only model**: a fault affects every sink of its wire. It does not enumerate separate fanout-branch faults. The collapser only applies local rules when an input wire has exactly one use and is not independently observed as a primary output:

- BUF preserves stuck value; NOT reverses it.
- AND/NAND input SA0 is equivalent to output SA0/SA1.
- OR/NOR input SA1 is equivalent to output SA1/SA0.
- XOR/XNOR receive no collapsing rule.

Under the single-use condition the changed gate output is identical for every input assignment and there is no alternate observation path from the input stem. Union-find composes these equal faulty-circuit functions transitively. Random detection coincidence is never a merging criterion. `collapse` exports original-to-representative IDs; fault simulation currently enumerates originals so coverage and statistics retain their original meaning.

Fault dropping uses an active list and removes detected faults **between batches**. GPU masks return to the host for compaction; transfer and compaction costs remain in end-to-end timings. Full and dropping modes have identical detected sets and first detecting patterns. Dropping counts are labeled `observed_detecting_patterns`: they include the detecting batch but omit later batches, so they are lower bounds rather than total detection multiplicities. Skipped masks are zero.

## ATPG

The CPU engine performs a bounded depth-first search over primary input decisions with conservative ternary good/faulty implication at every node. A known fault-site value equal to the stuck value prunes a branch because activation is impossible. Implication carries discrepancies through gates toward outputs; known-equal outputs prune unobservable branches. A known activated discrepancy at an output produces a test cube, with unassigned inputs filled with zero. Every returned binary pattern is independently verified by the scalar simulator.

This is a basic targeted backtracking ATPG, not full PODEM: it has no D-frontier objective selection or sophisticated backtrace heuristic. Iterative backtracking avoids recursion depth limits. `Detected` requires a verified test; `ProvenRedundant` requires exhausting the complete search with sound pruning; `Aborted` means the node budget was exhausted. A timeout/limit is never proof of redundancy. Reconvergence may reduce pruning effectiveness and runtime remains exponential. Exhaustive small-circuit tests validate both tests and redundancy classifications.

GPU candidate-assisted ATPG is deferred: this implementation produces one targeted candidate at a time, and measured small-workload GPU overhead does not establish a benefit for offloading these checks.

## Benchmark methodology

```powershell
python benchmarks/run_suite.py
# CPU-only equivalent:
python benchmarks/run_suite.py --cpu --exe build-cpu/CircuitForge.exe --output benchmarks/cpu-results.csv
```

The suite generates fixed-seed 16-, 128-, and 512-gate DAGs with 16 primary inputs, four outputs, two pattern counts (33 and 257), three block sizes (64/128/256), and both dropping modes. Generated fixtures are included for inspection. Increase pattern counts through the CLI for longer runs; scalar verification can become expensive.

Each case uses one warmup per backend and three complete timed repetitions, reporting the median. The scalar oracle uses the same patterns/faults and batch-boundary dropping policy as packed engines. It is the reusable scalar implementation derived from the prototype's semantics, not a timing of the historical hardcoded executable. All returned masks must agree. Parsing and pattern generation are outside timing for every backend. CUDA events measure kernel and transfer intervals; host wall time includes allocations, transfers, execution, synchronization, active-list processing, and resource destruction. CUDA context initialization is warmed up. Pageable-memory event timings do not substitute for host end-to-end latency. Independently computed medians need not sum exactly.

`evaluations_per_s` uses actually processed fault-pattern pairs (including valid bits of the detecting batch); `patterns_per_s` uses the input pattern count. `device_bytes` is requested live buffer memory, excluding CUDA context/driver overhead. `cpu_over_gpu` compares packed CPU median with complete GPU median, not with kernel time alone. Empty GPU columns in CPU-only reports mean unavailable.

Local baseline and final results are in `benchmarks/baseline-results.csv`, `benchmarks/results.csv`, and the corresponding full-adder CSVs. They are local observations under Windows WDDM with uncontrolled background load, not a portable performance guarantee. See `benchmarks/RESULTS.md` for measured examples and limitations.

## Validation and next steps

CPU and CUDA builds were validated phase by phase on the installed machine. CTest includes gate truth tables, adder outputs, multiple outputs, arbitrary dependency ordering, cycles, invalid references/syntax/drivers, pass-through circuits, partial batches, both stuck values on every wire, dropping, equivalence-class soundness, ATPG test verification/redundancy/aborts, and CLI error paths. Thirty seeded small DAGs are checked exhaustively against the scalar oracle. CUDA builds also compare every full and dropping mask against CPU. The core suite uses a temporary fixture file in the repository working directory; do not run the CPU/GPU core tests concurrently there.

Next steps: multi-fanin `.bench` normalization, separate branch faults, representative-only simulation with expanded coverage, smarter ATPG decisions, multi-batch GPU launch amortization, persistent device resources, on-device compaction, larger memory-boundary stress tests, and deeper Nsight/Compute Sanitizer profiling. No sequential/time-delay/X/Z simulation or general Verilog support is claimed.

A Compute Sanitizer memcheck run on the 512-gate fixture with 33 patterns and GPU dropping reported zero errors.

## Native desktop interface

The Qt 6 Widgets application is in `gui/CircuitForgeDesktop`. Run `build-desktop-local.cmd` to build and test, then `run-desktop-local.cmd --open circuits/FullAdder.txt`. `package-desktop-local.ps1` creates a local runnable package with Qt dependencies and the CLI. See [desktop instructions](gui/CircuitForgeDesktop/README.md) for editing, analysis, validation and limitations. The GUI is built separately with its Qt compiler; CUDA remains in the existing engine process.
