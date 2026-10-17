
#include <cuda_runtime.h>

#include <iostream>
#include <vector>
#include <queue>
#include <chrono>
#include <stdexcept>
#include <cstdlib>
#include <cstdint>
#include <algorithm>

#define CudaCheck(Call)                                         \
    do {                                                        \
        cudaError_t Error = (Call);                             \
        if (Error != cudaSuccess) {                             \
            std::cerr << "CUDA error: "                          \
                      << cudaGetErrorString(Error)              \
                      << " at line " << __LINE__ << '\n';        \
            std::exit(EXIT_FAILURE);                            \
        }                                                       \
    } while (0)

constexpr int MaxWires = 64;
constexpr int PatternCount = 65536;
constexpr int ThreadsPerBlock = 512;

enum class GateType : int {
    AND, OR, XOR, NAND, NOR, NOT, BUF
};

struct Gate {
    GateType Type;
    int InputA;
    int InputB;
    int Output;
};

// Works on both the CPU and GPU.
__host__ __device__
uint8_t EvaluateGate(GateType Type, uint8_t A, uint8_t B) {
    switch (Type) {
        case GateType::AND:  return A & B;
        case GateType::OR:   return A | B;
        case GateType::XOR:  return A ^ B;
        case GateType::NAND: return !(A & B);
        case GateType::NOR:  return !(A | B);
        case GateType::NOT:  return !A;
        case GateType::BUF:  return A;
    }
    return 0;
}

// Sort gates so their inputs are computed first.
std::vector<Gate> CompileCircuit(
    const std::vector<Gate>& Gates,
    int WireCount
) {
    if (WireCount < 4 || WireCount > MaxWires) {
        throw std::invalid_argument("Invalid wire count");
    }

    int Count = static_cast<int>(Gates.size());
    std::vector<int> Producer(WireCount, -1);

    for (int i = 0; i < Count; ++i) {
        const Gate& G = Gates[i];

        if (G.Output < 3 || G.Output >= WireCount) {
            throw std::runtime_error("Invalid output wire");
        }

        if (G.InputA < 0 || G.InputA >= WireCount) {
            throw std::runtime_error("Invalid first input");
        }

        bool Unary = G.Type == GateType::NOT ||
                     G.Type == GateType::BUF;

        if (!Unary && (G.InputB < 0 ||
                       G.InputB >= WireCount)) {
            throw std::runtime_error("Invalid second input");
        }

        if (Producer[G.Output] != -1) {
            throw std::runtime_error("Multiple wire drivers");
        }

        Producer[G.Output] = i;
    }

    std::vector<std::vector<int>> Adj(Count);
    std::vector<int> InDegree(Count, 0);

    for (int i = 0; i < Count; ++i) {
        const Gate& G = Gates[i];

        int Inputs[2] = {G.InputA, G.InputB};
        int InputCount = (G.Type == GateType::NOT ||
                          G.Type == GateType::BUF) ? 1 : 2;

        for (int j = 0; j < InputCount; ++j) {
            int Wire = Inputs[j];

            // Wires 0, 1, 2 are primary inputs.
            if (Wire >= 3 && Producer[Wire] == -1) {
                throw std::runtime_error("Undriven input wire");
            }

            int Parent = Producer[Wire];

            if (Parent != -1) {
                Adj[Parent].push_back(i);
                InDegree[i]++;
            }
        }
    }

    std::queue<int> Ready;

    for (int i = 0; i < Count; ++i) {
        if (InDegree[i] == 0) {
            Ready.push(i);
        }
    }

    std::vector<Gate> Sorted;

    while (!Ready.empty()) {
        int Current = Ready.front();
        Ready.pop();

        Sorted.push_back(Gates[Current]);

        for (int Next : Adj[Current]) {
            if (--InDegree[Next] == 0) {
                Ready.push(Next);
            }
        }
    }

    if (static_cast<int>(Sorted.size()) != Count) {
        throw std::runtime_error("Circuit contains a cycle");
    }

    return Sorted;
}

// CPU reference: evaluate one test pattern.
uint8_t SimulateCpu(
    const std::vector<Gate>& Gates,
    int Pattern
) {
    uint8_t Wires[MaxWires] = {};

    Wires[0] = (Pattern >> 2) & 1;
    Wires[1] = (Pattern >> 1) & 1;
    Wires[2] = Pattern & 1;

    for (const Gate& G : Gates) {
        uint8_t A = Wires[G.InputA];
        uint8_t B = G.InputB >= 0 ? Wires[G.InputB] : 0;

        Wires[G.Output] = EvaluateGate(G.Type, A, B);
    }

    return Wires[5];
}

// GPU kernel: one thread per test pattern.
__global__ void CircuitKernel(
    const Gate* Gates,
    int GateCount,
    uint8_t* Outputs,
    int Count
) {
    int Index = blockIdx.x * blockDim.x + threadIdx.x;

    if (Index >= Count) {
        return;
    }

    uint8_t Wires[MaxWires] = {};

    // Repeat the eight combinations across the batch.
    int Pattern = Index & 7;

    Wires[0] = (Pattern >> 2) & 1;
    Wires[1] = (Pattern >> 1) & 1;
    Wires[2] = Pattern & 1;

    for (int i = 0; i < GateCount; ++i) {
        Gate G = Gates[i];

        uint8_t A = Wires[G.InputA];
        uint8_t B = G.InputB >= 0 ? Wires[G.InputB] : 0;

        Wires[G.Output] = EvaluateGate(G.Type, A, B);
    }

    Outputs[Index] = Wires[5];
}

int main() {
    // Y = (A XOR B) AND (NOT C)
    // A=0, B=1, C=2, D=3, E=4, Y=5

    std::vector<Gate> Gates = {
        {GateType::AND, 3, 4, 5},
        {GateType::NOT, 2, -1, 4},
        {GateType::XOR, 0, 1, 3}
    };

    // Gates are deliberately unsorted.
    std::vector<Gate> Compiled = CompileCircuit(Gates, 6);

    std::cout << "CircuitForge GPU Simulator\n";
    std::cout << "Patterns: " << PatternCount << '\n';
    std::cout << "Gates: " << Compiled.size() << '\n';

    // Run the CPU reference.
    std::vector<uint8_t> CpuOutputs(PatternCount);

    auto CpuStart = std::chrono::steady_clock::now();

    for (int i = 0; i < PatternCount; ++i) {
        CpuOutputs[i] = SimulateCpu(Compiled, i & 7);
    }

    auto CpuEnd = std::chrono::steady_clock::now();

    double CpuMs =
        std::chrono::duration<double, std::milli>(
            CpuEnd - CpuStart
        ).count();

    Gate* DeviceGates = nullptr;
    uint8_t* DeviceOutputs = nullptr;

    size_t GateBytes = Compiled.size() * sizeof(Gate);
    size_t OutputBytes = PatternCount * sizeof(uint8_t);

    // Allocate GPU memory.
    CudaCheck(cudaMalloc(&DeviceGates, GateBytes));
    CudaCheck(cudaMalloc(&DeviceOutputs, OutputBytes));

    // Copy gate definitions to the GPU.
    CudaCheck(cudaMemcpy(
        DeviceGates,
        Compiled.data(),
        GateBytes,
        cudaMemcpyHostToDevice
    ));

    int Blocks =
        (PatternCount + ThreadsPerBlock - 1) /
        ThreadsPerBlock;

    // Timing events.
    cudaEvent_t StartEvent, StopEvent;

    CudaCheck(cudaEventCreate(&StartEvent));
    CudaCheck(cudaEventCreate(&StopEvent));

    // Warm up the GPU before measuring.
    CircuitKernel<<<Blocks, ThreadsPerBlock>>>(
        DeviceGates,
        static_cast<int>(Compiled.size()),
        DeviceOutputs,
        PatternCount
    );

    CudaCheck(cudaGetLastError());
    CudaCheck(cudaDeviceSynchronize());

    // Measure kernel execution only.
    CudaCheck(cudaEventRecord(StartEvent));

    CircuitKernel<<<Blocks, ThreadsPerBlock>>>(
        DeviceGates,
        static_cast<int>(Compiled.size()),
        DeviceOutputs,
        PatternCount
    );

    CudaCheck(cudaGetLastError());

    CudaCheck(cudaEventRecord(StopEvent));
    CudaCheck(cudaEventSynchronize(StopEvent));

    float GpuMs = 0.0f;

    CudaCheck(cudaEventElapsedTime(
        &GpuMs, StartEvent, StopEvent
    ));

    // Copy results back to the CPU.
    std::vector<uint8_t> GpuOutputs(PatternCount);

    CudaCheck(cudaMemcpy(
        GpuOutputs.data(),
        DeviceOutputs,
        OutputBytes,
        cudaMemcpyDeviceToHost
    ));

    // Check every result.
    int Passed = 0;

    for (int i = 0; i < PatternCount; ++i) {
        if (CpuOutputs[i] == GpuOutputs[i]) {
            Passed++;
        }
    }

    std::cout << "\nA B C | CPU GPU\n";
    std::cout << "----------------\n";

    for (int i = 0; i < 8; ++i) {
        std::cout
            << ((i >> 2) & 1) << " "
            << ((i >> 1) & 1) << " "
            << (i & 1) << " | "
            << int(CpuOutputs[i]) << "   "
            << int(GpuOutputs[i]) << '\n';
    }

    std::cout << "\nVerification: "
              << Passed << "/" << PatternCount << '\n';

    std::cout << "CPU time: " << CpuMs << " ms\n";
    std::cout << "GPU kernel time: " << GpuMs << " ms\n";

    if (GpuMs > 0.0f) {
        std::cout << "Kernel-only speed ratio: "
                  << CpuMs / GpuMs << "x\n";
    }

    CudaCheck(cudaEventDestroy(StartEvent));
    CudaCheck(cudaEventDestroy(StopEvent));

    CudaCheck(cudaFree(DeviceGates));
    CudaCheck(cudaFree(DeviceOutputs));

    return Passed == PatternCount ? EXIT_SUCCESS : EXIT_FAILURE;
};