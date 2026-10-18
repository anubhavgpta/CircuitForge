
#include <cuda_runtime.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#define CudaCheck(Call)                                      \
    do {                                                     \
        cudaError_t Error = (Call);                          \
        if (Error != cudaSuccess) {                          \
            std::cerr << "CUDA: "                             \
                      << cudaGetErrorString(Error)            \
                      << " line " << __LINE__ << '\n';        \
            std::exit(EXIT_FAILURE);                         \
        }                                                    \
    } while (0)

constexpr int MaxWires = 64;
constexpr int MaxInputs = 24;
constexpr int BitsPerWord = 32;
constexpr int ThreadsPerBlock = 256;

enum class GateType : int {
    AND, OR, XOR, NAND, NOR, NOT, BUF
};

struct Gate {
    GateType Type;
    int InputA;
    int InputB;
    int Output;
};

struct Fault {
    int Wire;
    int Value;
};

struct Circuit {
    std::vector<std::string> WireNames;
    std::unordered_map<std::string, int> WireIds;
    std::vector<int> Inputs;
    std::vector<int> Outputs;
    std::vector<Gate> Gates;

    int GetWire(const std::string& Name) {
        auto It = WireIds.find(Name);

        if (It != WireIds.end()) {
            return It->second;
        }

        if (WireNames.size() >= MaxWires) {
            throw std::runtime_error("Exceeded MaxWires");
        }

        int Id = static_cast<int>(WireNames.size());

        WireNames.push_back(Name);
        WireIds[Name] = Id;

        return Id;
    }
};

GateType ParseType(const std::string& Name) {
    if (Name == "AND")  return GateType::AND;
    if (Name == "OR")   return GateType::OR;
    if (Name == "XOR")  return GateType::XOR;
    if (Name == "NAND") return GateType::NAND;
    if (Name == "NOR")  return GateType::NOR;
    if (Name == "NOT")  return GateType::NOT;
    if (Name == "BUF")  return GateType::BUF;

    throw std::runtime_error("Unknown gate: " + Name);
}

bool IsUnary(GateType Type) {
    return Type == GateType::NOT ||
           Type == GateType::BUF;
}

Circuit LoadNetlist(const std::string& Filename) {
    std::ifstream File(Filename);

    if (!File) {
        throw std::runtime_error(
            "Cannot open: " + Filename
        );
    }

    Circuit C;
    std::string Line;
    int LineNumber = 0;

    while (std::getline(File, Line)) {
        ++LineNumber;

        size_t Comment = Line.find('#');

        if (Comment != std::string::npos) {
            Line.erase(Comment);
        }

        std::istringstream Stream(Line);
        std::string Command;

        if (!(Stream >> Command)) {
            continue;
        }

        if (Command == "INPUT" || Command == "OUTPUT") {
            std::string Name;
            int Added = 0;

            auto& List = Command == "INPUT"
                ? C.Inputs : C.Outputs;

            while (Stream >> Name) {
                int Wire = C.GetWire(Name);

                if (std::find(
                    List.begin(), List.end(), Wire
                ) != List.end()) {
                    throw std::runtime_error(
                        "Duplicate declaration at line " +
                        std::to_string(LineNumber)
                    );
                }

                List.push_back(Wire);
                ++Added;
            }

            if (Added == 0) {
                throw std::runtime_error(
                    "Empty declaration at line " +
                    std::to_string(LineNumber)
                );
            }

            continue;
        }

        GateType Type = ParseType(Command);

        std::string Out, A, B, Extra;

        if (!(Stream >> Out >> A)) {
            throw std::runtime_error(
                "Incomplete gate at line " +
                std::to_string(LineNumber)
            );
        }

        bool Unary = IsUnary(Type);

        if (!Unary && !(Stream >> B)) {
            throw std::runtime_error(
                "Missing gate input at line " +
                std::to_string(LineNumber)
            );
        }

        if (Stream >> Extra) {
            throw std::runtime_error(
                "Extra gate argument at line " +
                std::to_string(LineNumber)
            );
        }

        int Output = C.GetWire(Out);
        int InputA = C.GetWire(A);
        int InputB = Unary ? -1 : C.GetWire(B);

        C.Gates.push_back({
            Type, InputA, InputB, Output
        });
    }

    if (C.Inputs.empty() || C.Outputs.empty()) {
        throw std::runtime_error(
            "INPUT and OUTPUT declarations required"
        );
    }

    if (C.Inputs.size() > MaxInputs) {
        throw std::runtime_error("Too many inputs");
    }

    return C;
}

void CompileCircuit(Circuit& C) {
    int N = static_cast<int>(C.WireNames.size());
    int G = static_cast<int>(C.Gates.size());

    std::vector<int> Producer(N, -1);
    std::vector<bool> PrimaryInput(N, false);

    for (int Wire : C.Inputs) {
        PrimaryInput[Wire] = true;
    }

    for (int i = 0; i < G; ++i) {
        int Out = C.Gates[i].Output;

        if (PrimaryInput[Out] || Producer[Out] != -1) {
            throw std::runtime_error(
                "Invalid or multiple driver: " +
                C.WireNames[Out]
            );
        }

        Producer[Out] = i;
    }

    for (int i = 0; i < N; ++i) {
        if (!PrimaryInput[i] && Producer[i] == -1) {
            throw std::runtime_error(
                "Undriven wire: " + C.WireNames[i]
            );
        }
    }

    std::vector<std::vector<int>> Adj(G);
    std::vector<int> Degree(G, 0);

    for (int i = 0; i < G; ++i) {
        const Gate& Current = C.Gates[i];

        int Inputs[2] = {
            Current.InputA, Current.InputB
        };

        int Count = IsUnary(Current.Type) ? 1 : 2;

        for (int j = 0; j < Count; ++j) {
            int Parent = Producer[Inputs[j]];

            if (Parent != -1) {
                Adj[Parent].push_back(i);
                ++Degree[i];
            }
        }
    }

    std::queue<int> Ready;

    for (int i = 0; i < G; ++i) {
        if (Degree[i] == 0) {
            Ready.push(i);
        }
    }

    std::vector<Gate> Sorted;

    while (!Ready.empty()) {
        int Current = Ready.front();
        Ready.pop();

        Sorted.push_back(C.Gates[Current]);

        for (int Next : Adj[Current]) {
            if (--Degree[Next] == 0) {
                Ready.push(Next);
            }
        }
    }

    if (static_cast<int>(Sorted.size()) != G) {
        throw std::runtime_error(
            "Combinational cycle detected"
        );
    }

    C.Gates = std::move(Sorted);
}

// One bit per independent test pattern.
__host__ __device__
uint32_t EvaluateGate(
    GateType Type,
    uint32_t A,
    uint32_t B
) {
    switch (Type) {
        case GateType::AND:  return A & B;
        case GateType::OR:   return A | B;
        case GateType::XOR:  return A ^ B;
        case GateType::NAND: return ~(A & B);
        case GateType::NOR:  return ~(A | B);
        case GateType::NOT:  return ~A;
        case GateType::BUF:  return A;
    }

    return 0;
}

// Deterministic integer mixing for test generation.
uint32_t Mix32(uint32_t Value) {
    Value ^= Value >> 16;
    Value *= 0x7feb352dU;
    Value ^= Value >> 15;
    Value *= 0x846ca68bU;
    Value ^= Value >> 16;
    return Value;
}

// Generate input words arranged by batch, then input.
std::vector<uint32_t> GeneratePatterns(
    int InputCount,
    int PatternCount
) {
    int BatchCount =
        (PatternCount + BitsPerWord - 1) / BitsPerWord;

    std::vector<uint32_t> Packed(
        static_cast<size_t>(BatchCount) * InputCount,
        0
    );

    for (int Pattern = 0;
         Pattern < PatternCount;
         ++Pattern) {

        uint32_t Value;

        // Exhaustive patterns for small circuits.
        if (InputCount <= 12) {
            Value = static_cast<uint32_t>(Pattern) &
                    ((1U << InputCount) - 1U);
        } else {
            // Reproducible pseudo-random patterns.
            Value = Mix32(
                static_cast<uint32_t>(Pattern) +
                0x9e3779b9U
            ) & ((1U << InputCount) - 1U);
        }

        int Batch = Pattern / BitsPerWord;
        int Bit = Pattern % BitsPerWord;

        for (int Input = 0;
             Input < InputCount;
             ++Input) {

            int Position = InputCount - Input - 1;

            if ((Value >> Position) & 1U) {
                Packed[
                    static_cast<size_t>(Batch) *
                    InputCount + Input
                ] |= (1U << Bit);
            }
        }
    }

    return Packed;
}

// Produces a 32-bit fault detection mask.
__host__ __device__
uint32_t SimulateFaultBatch(
    const Gate* Gates,
    int GateCount,
    const int* Inputs,
    int InputCount,
    const int* Outputs,
    int OutputCount,
    const uint32_t* InputWords,
    Fault CurrentFault
) {
    uint32_t Good[MaxWires] = {};
    uint32_t Bad[MaxWires] = {};

    for (int i = 0; i < InputCount; ++i) {
        int Wire = Inputs[i];
        uint32_t Value = InputWords[i];

        Good[Wire] = Value;
        Bad[Wire] = Value;

        if (Wire == CurrentFault.Wire) {
            Bad[Wire] = CurrentFault.Value
                ? 0xffffffffU : 0U;
        }
    }

    for (int i = 0; i < GateCount; ++i) {
        Gate G = Gates[i];

        uint32_t GoodB = G.InputB >= 0
            ? Good[G.InputB] : 0U;

        uint32_t BadB = G.InputB >= 0
            ? Bad[G.InputB] : 0U;

        Good[G.Output] = EvaluateGate(
            G.Type, Good[G.InputA], GoodB
        );

        uint32_t Result = EvaluateGate(
            G.Type, Bad[G.InputA], BadB
        );

        if (G.Output == CurrentFault.Wire) {
            Result = CurrentFault.Value
                ? 0xffffffffU : 0U;
        }

        Bad[G.Output] = Result;
    }

    uint32_t Detection = 0U;

    for (int i = 0; i < OutputCount; ++i) {
        int Wire = Outputs[i];

        Detection |= Good[Wire] ^ Bad[Wire];
    }

    return Detection;
}

__global__ void BitParallelFaultKernel(
    const Gate* Gates,
    int GateCount,
    const int* Inputs,
    int InputCount,
    const int* Outputs,
    int OutputCount,
    const Fault* Faults,
    const uint32_t* PackedInputs,
    int BatchCount,
    int PatternCount,
    int TotalTasks,
    uint32_t* DetectionMasks
) {
    int Index =
        blockIdx.x * blockDim.x + threadIdx.x;

    if (Index >= TotalTasks) {
        return;
    }

    int FaultIndex = Index / BatchCount;
    int BatchIndex = Index % BatchCount;

    const uint32_t* InputWords =
        PackedInputs +
        static_cast<size_t>(BatchIndex) * InputCount;

    uint32_t Mask = SimulateFaultBatch(
        Gates,
        GateCount,
        Inputs,
        InputCount,
        Outputs,
        OutputCount,
        InputWords,
        Faults[FaultIndex]
    );

    // Discard unused bits in the final batch.
    int Remaining = PatternCount -
                    BatchIndex * BitsPerWord;

    if (Remaining < BitsPerWord) {
        Mask &= (1U << Remaining) - 1U;
    }

    DetectionMasks[Index] = Mask;
}

int CountBits(uint32_t Value) {
    int Count = 0;

    while (Value != 0) {
        Value &= Value - 1;
        ++Count;
    }

    return Count;
}

int main(int argc, char** argv) {
    try {
        std::string Filename = argc > 1
            ? argv[1] : "circuits/Example.txt";

        int PatternCount = argc > 2
            ? std::stoi(argv[2]) : 65536;

        if (PatternCount <= 0 || PatternCount > 1048576) {
            throw std::runtime_error(
                "Pattern count must be 1..1048576"
            );
        }

        Circuit C = LoadNetlist(Filename);
        CompileCircuit(C);

        int InputCount = static_cast<int>(C.Inputs.size());
        int OutputCount = static_cast<int>(C.Outputs.size());
        int GateCount = static_cast<int>(C.Gates.size());

        int BatchCount =
            (PatternCount + BitsPerWord - 1) / BitsPerWord;

        std::vector<Fault> Faults;

        for (int Wire = 0;
             Wire < static_cast<int>(C.WireNames.size());
             ++Wire) {
            Faults.push_back({Wire, 0});
            Faults.push_back({Wire, 1});
        }

        int FaultCount = static_cast<int>(Faults.size());

        size_t TotalTasks =
            static_cast<size_t>(FaultCount) * BatchCount;

        std::vector<uint32_t> PackedInputs =
            GeneratePatterns(InputCount, PatternCount);

        std::vector<uint32_t> CpuMasks(TotalTasks);
        std::vector<uint32_t> GpuMasks(TotalTasks);

        std::cout << "CircuitForge Bit-Parallel CUDA\n";
        std::cout << "Netlist: " << Filename << "\n";
        std::cout << "Wires: " << C.WireNames.size() << "\n";
        std::cout << "Gates: " << GateCount << "\n";
        std::cout << "Inputs: " << InputCount << "\n";
        std::cout << "Patterns: " << PatternCount << "\n";
        std::cout << "Faults: " << FaultCount << "\n";
        std::cout << "GPU tasks: " << TotalTasks << "\n\n";

        auto CpuStart = std::chrono::steady_clock::now();

        for (int f = 0; f < FaultCount; ++f) {
            for (int b = 0; b < BatchCount; ++b) {
                size_t Index =
                    static_cast<size_t>(f) * BatchCount + b;

                uint32_t Mask = SimulateFaultBatch(
                    C.Gates.data(),
                    GateCount,
                    C.Inputs.data(),
                    InputCount,
                    C.Outputs.data(),
                    OutputCount,
                    PackedInputs.data() +
                        static_cast<size_t>(b) * InputCount,
                    Faults[f]
                );

                int Remaining =
                    PatternCount - b * BitsPerWord;

                if (Remaining < BitsPerWord) {
                    Mask &= (1U << Remaining) - 1U;
                }

                CpuMasks[Index] = Mask;
            }
        }

        auto CpuEnd = std::chrono::steady_clock::now();

        double CpuMs =
            std::chrono::duration<double, std::milli>(
                CpuEnd - CpuStart
            ).count();

        Gate* DeviceGates = nullptr;
        Fault* DeviceFaults = nullptr;
        int* DeviceInputs = nullptr;
        int* DeviceOutputs = nullptr;
        uint32_t* DevicePatterns = nullptr;
        uint32_t* DeviceMasks = nullptr;

        size_t GateBytes = C.Gates.size() * sizeof(Gate);
        size_t FaultBytes = Faults.size() * sizeof(Fault);
        size_t InputBytes = C.Inputs.size() * sizeof(int);
        size_t OutputBytes = C.Outputs.size() * sizeof(int);
        size_t PatternBytes =
            PackedInputs.size() * sizeof(uint32_t);
        size_t MaskBytes =
            GpuMasks.size() * sizeof(uint32_t);

        auto GpuStart = std::chrono::steady_clock::now();

        CudaCheck(cudaMalloc(&DeviceGates, GateBytes));
        CudaCheck(cudaMalloc(&DeviceFaults, FaultBytes));
        CudaCheck(cudaMalloc(&DeviceInputs, InputBytes));
        CudaCheck(cudaMalloc(&DeviceOutputs, OutputBytes));
        CudaCheck(cudaMalloc(&DevicePatterns, PatternBytes));
        CudaCheck(cudaMalloc(&DeviceMasks, MaskBytes));

        CudaCheck(cudaMemcpy(
            DeviceGates, C.Gates.data(), GateBytes,
            cudaMemcpyHostToDevice
        ));

        CudaCheck(cudaMemcpy(
            DeviceFaults, Faults.data(), FaultBytes,
            cudaMemcpyHostToDevice
        ));

        CudaCheck(cudaMemcpy(
            DeviceInputs, C.Inputs.data(), InputBytes,
            cudaMemcpyHostToDevice
        ));

        CudaCheck(cudaMemcpy(
            DeviceOutputs, C.Outputs.data(), OutputBytes,
            cudaMemcpyHostToDevice
        ));

        CudaCheck(cudaMemcpy(
            DevicePatterns, PackedInputs.data(),
            PatternBytes, cudaMemcpyHostToDevice
        ));

        int Blocks =
            (static_cast<int>(TotalTasks) +
             ThreadsPerBlock - 1) / ThreadsPerBlock;

        auto LaunchKernel = [&]() {
            BitParallelFaultKernel<<<Blocks, ThreadsPerBlock>>>(
                DeviceGates,
                GateCount,
                DeviceInputs,
                InputCount,
                DeviceOutputs,
                OutputCount,
                DeviceFaults,
                DevicePatterns,
                BatchCount,
                PatternCount,
                static_cast<int>(TotalTasks),
                DeviceMasks
            );

            CudaCheck(cudaGetLastError());
        };

        // Warmup before kernel benchmarking.
        LaunchKernel();
        CudaCheck(cudaDeviceSynchronize());

        constexpr int Repetitions = 50;

        cudaEvent_t StartEvent, StopEvent;

        CudaCheck(cudaEventCreate(&StartEvent));
        CudaCheck(cudaEventCreate(&StopEvent));

        CudaCheck(cudaEventRecord(StartEvent));

        for (int i = 0; i < Repetitions; ++i) {
            LaunchKernel();
        }

        CudaCheck(cudaEventRecord(StopEvent));
        CudaCheck(cudaEventSynchronize(StopEvent));

        float TotalKernelMs = 0.0f;

        CudaCheck(cudaEventElapsedTime(
            &TotalKernelMs, StartEvent, StopEvent
        ));

        double AverageKernelMs =
            TotalKernelMs / Repetitions;

        CudaCheck(cudaMemcpy(
            GpuMasks.data(),
            DeviceMasks,
            MaskBytes,
            cudaMemcpyDeviceToHost
        ));

        auto GpuEnd = std::chrono::steady_clock::now();

        double GpuEndToEndMs =
            std::chrono::duration<double, std::milli>(
                GpuEnd - GpuStart
            ).count();

        // Verify all packed masks, not just final coverage.
        size_t Correct = 0;

        for (size_t i = 0; i < TotalTasks; ++i) {
            if (CpuMasks[i] == GpuMasks[i]) {
                ++Correct;
            }
        }

        int DetectedFaults = 0;

        std::cout << "Fault detection report\n";
        std::cout << "----------------------\n";

        for (int f = 0; f < FaultCount; ++f) {
            int DetectedPatterns = 0;

            for (int b = 0; b < BatchCount; ++b) {
                size_t Index =
                    static_cast<size_t>(f) * BatchCount + b;

                DetectedPatterns += CountBits(GpuMasks[Index]);
            }

            if (DetectedPatterns > 0) {
                ++DetectedFaults;
            }

            std::cout
                << C.WireNames[Faults[f].Wire]
                << " SA" << Faults[f].Value
                << " : " << DetectedPatterns
                << "/" << PatternCount << "\n";
        }

        double Coverage =
            100.0 * DetectedFaults / FaultCount;

        double Evaluations =
            static_cast<double>(FaultCount) * PatternCount;

        std::cout << std::fixed << std::setprecision(4);

        std::cout << "\nVerification: "
                  << Correct << "/" << TotalTasks
                  << " packed masks\n";

        std::cout << "Fault coverage: "
                  << Coverage << "%\n";

        std::cout << "CPU reference: "
                  << CpuMs << " ms\n";

        std::cout << "Average GPU kernel: "
                  << AverageKernelMs << " ms\n";

        std::cout << "GPU measured sequence: "
                  << GpuEndToEndMs << " ms\n";

        std::cout << "Kernel throughput: "
                  << Evaluations /
                     (AverageKernelMs / 1000.0) / 1e6
                  << " M fault-pattern evaluations/s\n";

        std::cout << "\nNote: GPU measured sequence includes "
                     "allocations, warmup, 50 kernel runs, "
                     "and transfers.\n";

        CudaCheck(cudaEventDestroy(StartEvent));
        CudaCheck(cudaEventDestroy(StopEvent));

        CudaCheck(cudaFree(DeviceGates));
        CudaCheck(cudaFree(DeviceFaults));
        CudaCheck(cudaFree(DeviceInputs));
        CudaCheck(cudaFree(DeviceOutputs));
        CudaCheck(cudaFree(DevicePatterns));
        CudaCheck(cudaFree(DeviceMasks));

        return Correct == TotalTasks
            ? EXIT_SUCCESS : EXIT_FAILURE;

    } catch (const std::exception& Error) {
        std::cerr << "Error: " << Error.what() << '\n';
        return EXIT_FAILURE;
    }
}
