# CircuitForge Desktop

Native C++17 / Qt 6 Widgets editor for the existing CircuitForge CLI. No browser, QML, Python, or JavaScript runtime is used. The original CLI, CUDA sources, benchmarks and tests are unchanged by the desktop implementation.

## Run locally

From the repository root:

```powershell
.\run-desktop-local.cmd --open circuits/FullAdder.txt
```

Or launch `build-desktop/package/CircuitForgeDesktop.exe` after packaging. The package includes Qt runtime dependencies and the existing GPU engine. The application discovers a sibling `CircuitForge.exe`; override it under **Project → Engine settings**. The GPU driver and the appropriate Microsoft C++ runtime must remain installed.

## Build

The tested installation is Qt 6.12.0 MinGW 64-bit, with the installed MinGW 13.1 compiler. Qt reports its binaries were built with GCC 15.1; this installed combination built and passed local tests, but matching the compiler version used by your Qt distribution is preferable for other machines. The engine remains MSVC 2019 / CUDA 13.4, communicating across a process boundary.

```powershell
.\build-desktop-local.cmd
.\package-desktop-local.ps1
```

The scripts use exact local paths under `C:\Qt`. For another installation, configure independently:

```text
cmake -S gui/CircuitForgeDesktop -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt kit> -DCMAKE_CXX_COMPILER=<kit compiler>
cmake --build build-desktop
ctest --test-dir build-desktop --output-on-failure
```

Put Qt's and your compiler's `bin` directories on PATH for tests. `BUILD_TESTING=OFF` omits Qt Test. The optional Vulkan-header discovery warning does not affect this Widgets/QPainter application. Do not configure the GUI and CUDA targets in the same build tree with different compilers.

Packaging uses `windeployqt` and does not bundle fonts. The generated directory is a local development package, not a signed installer. Review Qt/toolchain licenses before redistributing it.

## Editor workflow

1. Open a `.txt` or supported `.bench` netlist, or choose **Project → New circuit**.
2. Drag INPUT, OUTPUT and gate entries from the library; double-click also adds a node.
3. Click a source's right pin and then a destination's left pin. Dragging between pins also works. Escape cancels. Occupied inputs and cycles are rejected.
4. Select and drag gates. Rubber-band selection moves multiple nodes. Delete removes selected gates or wires. Ctrl+Z / Ctrl+Y undo/redo edits.
5. Use the Properties inspector to rename the driven net. OUTPUT terminals inherit the observed net name.
6. Middle-drag pans; the wheel zooms; F fits into the area between the default overlay panels. The canvas context menu toggles grid snapping.
7. Save a complete circuit as `.txt`. A separate `.layout.json` stores positions. Unconnected input pins prevent simulation and netlist save, with an explanatory error.

Gate bodies use labeled rounded containers rather than ANSI gate silhouettes. The model owns connectivity; graphics items only display and edit model state. The engine parser/compiler is reused for import and validation. No simulation algorithm is duplicated in the GUI.

## Panels and appearance

Panels move by their header, resize with the lower-right grip, collapse using the minus button or header double-click, and reopen from View. Geometry, visibility, collapsed state, theme, and engine path persist through QSettings. **View → Reset workspace layout** recovers hidden/offscreen panels. Panels remain inside the main window.

Helvetica is preferred if installed, then Arial; console and netlists use the system fixed-width font. Dark is default; Ctrl+Shift+L switches themes. Native Windows title-bar controls preserve resize, maximize, Snap and keyboard behavior. DWM requests dark and rounded window frames where supported; Windows 10 may retain square native outer corners. QSS rounds the internal panels, not the native frame.

## Simulation and analysis

- **Logic · CPU** runs the existing `simulate` command and displays output bits.
- **Run faults** runs `faults` on CPU or GPU, using pattern count, seed, block size and optional dropping. GPU execution retains CLI CPU-mask verification.
- Fault selection highlights the corresponding stem. The table reports status, first detecting pattern and detection count. Dropping counts are observed counts, not full multiplicities.
- **Compute classes** displays original fault IDs and equivalence representative IDs. This does not enable representative-only simulation in the engine.
- ATPG runs the existing all-fault search and selects the requested wire/SA0/SA1 result. It displays the generated vector, input order, outcome and search nodes. Aborted is unresolved, never redundant. The current CLI has no targeted-fault argument; the GUI does not invent one.
- Benchmarks preserve scalar CPU, packed CPU, kernel, transfer and complete GPU timing columns. The speed ratio is packed CPU divided by GPU end-to-end time.
- Repeated benchmarks append rows; changing pattern count, block size or dropping permits comparisons. CSV import accepts the engine benchmark schema. Hover chart bars for details and choose the x-axis field. Charts show the first 24 rows; tables show at most 10,000 rows. Complete job CSVs can be exported through Project.

Jobs use asynchronous QProcess with separate argument values, never shell concatenation. Each job receives an isolated temporary netlist snapshot. Conflicting editing/runs are disabled until completion. stdout/stderr stream to the timestamped console; the CLI writes tabular data to its report file. Stop requests termination then kills after a short grace period. Missing executables, crashes and nonzero exits are reported. No fabricated progress percentage is shown.

GPU identity is queried asynchronously through installed `nvidia-smi`; missing information is labeled unavailable. Report import is capped at 64 MiB. Parsing/populating very large reports occurs on the UI thread and can briefly pause the UI; huge circuits and reports are not a validated production workload.

## Validation

`DesktopTests` covers model edits, duplicate-driver/cycle rejection, undo/redo, netlist/layout round trips, malformed import, command construction, CSV quoting, canvas pin connections and drop events, actual CPU/GPU/ATPG/collapse/benchmark processes, errors, cancellation and event-loop responsiveness. Window tests exercise library placement, result tables, panel dragging/resizing, collapsed-layout restoration, and dark/light rendering. Native Windows Qt tests generate `Desktop.png`, `Desktop-faults.png`, `Desktop-benchmark.png`, and `Desktop-light.png` in the build directory.

The offscreen plugin on this installation cannot discover system fonts; use the native Windows plugin for visual tests. Tests use a separate temporary settings location and do not overwrite the user's workspace settings.

## Remaining limits

- No dedicated single-fault engine invocation or all-detecting-vector export; the existing CLI exposes first detection/counts and all-fault ATPG.
- ATPG highlights the targeted net; it does not animate every internal logic value.
- No automatic benchmark sweep launcher; run cases manually or import the existing suite CSV.
- No independent floating OS tool windows, signed installer, auto-update system, or validated mixed-monitor DPI migration.
- No separate fanout-branch fault model, sequential logic or arbitrary Verilog; engine format restrictions remain.
- Layout sidecars are keyed by type/name. Netlist and sidecar writes are individually atomic, not a two-file transaction.
- Snap/drag/resize remain native for the main window; rounded outer corners depend on Windows DWM support.
