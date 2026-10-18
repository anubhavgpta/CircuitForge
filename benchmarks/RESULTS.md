# Local benchmark results — 2026-10-18

Hardware: NVIDIA GeForce GTX 1650, 4 GB, WDDM, driver 592.82. Compiler: MSVC 19.29.30154 and CUDA 13.4.92, Release, sm_75. Windows desktop background load and GPU clocks were not controlled. These are functional benchmark samples, not statistically controlled performance claims.

Each row uses 257 sampled patterns, seed 20261018, 128 threads per block, one warmup and three measured repetitions per backend. All scalar/packed/GPU detection masks matched. Times are milliseconds, medians of complete runs. The speed ratio is packed CPU / GPU end-to-end; values below one mean the GPU is slower.

| Gates | Dropping | Scalar CPU | Packed CPU | CUDA kernels | GPU end-to-end | CPU/GPU |
| ---: | :---: | ---: | ---: | ---: | ---: | ---: |
| 16 | no | 10.486 | 0.296 | 0.598 | 3.926 | 0.075 |
| 16 | yes | 3.210 | 0.111 | 5.291 | 26.784 | 0.004 |
| 128 | no | 329.464 | 4.052 | 0.926 | 3.009 | 1.346 |
| 128 | yes | 148.314 | 11.026 | 3.284 | 17.635 | 0.625 |
| 512 | no | 1775.022 | 37.544 | 7.249 | 18.297 | 2.052 |
| 512 | yes | 1441.622 | 51.391 | 8.216 | 24.324 | 2.113 |

The complete 36-row matrix includes 16/128/512 gates, 33/257 patterns, block sizes 64/128/256, and full/dropping modes. Full-adder results are stored separately. The generated DAGs may contain unobservable logic; they are reproducible workloads, not substitutes for representative industry benchmarks.

The baseline showed small circuits dominated by host/device coordination. The final implementation avoids re-uploading an invariant fault list after the first batch when it fits one tile and dropping is off. At 257 patterns this removes eight fault-list transfers per call. `baseline-results.csv` and `results.csv` retain before/after observations. Background activity, including build/test work during parts of the runs, makes timing variation substantial; these samples do **not** establish a causal optimization speedup. Dropping is not uniformly faster, and no universally optimal block size is inferred.

Compiler resource report: fault kernel 24 registers, good kernel 22 registers, zero stack bytes and zero spill loads/stores for both. No Nsight occupancy or bandwidth profile has been collected. Requested device-buffer bytes are in the CSV, not total process VRAM.

Validation: CPU CTest 8/8; CUDA CTest 9/9. Core tests include every supported gate, all existing circuits, 30 seeded exhaustive small DAGs, full/partial words, all wire faults, collapse soundness, dropping, ATPG detection and redundancy. Benchmark runs independently compare every mask, including generated 512-gate circuits.

Remaining performance work: concurrent pattern batches, persistent device allocations, device-side active-list compaction, and profiling representative imported netlists. CUDA ATPG candidate acceleration remains deferred until a batched candidate workflow demonstrates benefit.

Compute Sanitizer memcheck also completed on the generated 512-gate circuit, 33 patterns, GPU fault dropping: **0 errors**. This is a smoke memory check, not exhaustive memory-safety proof.
