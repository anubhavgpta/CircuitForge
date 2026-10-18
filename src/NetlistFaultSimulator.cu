
#include <cuda_runtime.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define CudaCheck(Call)                                    \
    do {                                                   \
        cudaError_t Error = (Call);                        \
        if (Error != cudaSuccess) {                        \
            std::cerr << "CUDA error: "                     \
                      << cudaGetErrorString(Error)         \
                      << " at line " << __LINE__ << '\n';   \
            std::exit(EXIT_FAILURE);                       \
        }                                                  \
    } while (0)

constexpr int MaxWires = 64;
constexpr int MaxInputs = 12;
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
    int StuckValue;
};

struct Circuit {
    std::vector<std::string> WireNames;
    std::map<std::string, int> WireIds;
    std::vector<int> Inputs;
    std::vector<int> Outputs;
    std::vector<Gate> Gates;

    int GetWire(const std::string& Name) {
        auto It = WireIds.find(Name);

        if (It != WireIds.end()) {
            return It->second;
        }

        if (WireNames.size() >= MaxWires) {
            throw std::runtime_error("Too many wires");
        }

        int Id = static_cast<int>(WireNames.size());
        WireIds[Name] = Id;
        WireNames.push_back(Name);
        return Id;
    }
};

GateType ParseGateType(const std::string& Name) {
    if (Name == "AND") return GateType::AND;
    if (Name == "OR") return GateType::OR;
    if (Name == "XOR") return GateType::XOR;
    if (Name == "NAND") return GateType::NAND;
    if (Name == "NOR") return GateType::NOR;
    if (Name == "NOT") return GateType::NOT;
    if (Name == "BUF") return GateType::BUF;

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
            "Cannot open netlist: " + Filename
        );
    }

    Circuit Result;
    std::string Line;
    int LineNumber = 0;

    while (std::getline(File, Line)) {
        ++LineNumber;

        // Remove comments.
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

            while (Stream >> Name) {
                int Wire = Result.GetWire(Name);

                auto& List = Command == "INPUT"
                    ? Result.Inputs : Result.Outputs;

                if (std::find(List.begin(), List.end(), Wire)
                    != List.end()) {
                    throw std::runtime_error(
                        "Duplicate declaration on line " +
                        std::to_string(LineNumber)
                    );
                }

                List.push_back(Wire);
                ++Added;
            }

            if (Added == 0) {
                throw std::runtime_error(
                    "Empty declaration on line " +
                    std::to_string(LineNumber)
                );
            }

            continue;
        }

        GateType Type = ParseGateType(Command);

        std::string OutputName;
        std::string AName;
        std::string BName;
        std::string Extra;

        if (!(Stream >> OutputName >> AName)) {
            throw std::runtime_error(
                "Incomplete gate on line " +
                std::to_string(LineNumber)
            );
        }

        bool Unary = IsUnary(Type);

        if (!Unary && !(Stream >> BName)) {
            throw std::runtime_error(
                "Missing second input on line " +
                std::to_string(LineNumber)
            );
        }

        if (Stream >> Extra) {
            throw std::runtime_error(
                "Too many gate arguments on line " +
                std::to_string(LineNumber)
            );
        }

        int Output = Result.GetWire(OutputName);
        int A = Result.GetWire(AName);
        int B = Unary ? -1 : Result.GetWire(BName);

        Result.Gates.push_back({Type, A, B, Output});
    }

    if (Result.Inputs.empty() || Result.Outputs.empty()) {
        throw std::runtime_error(
            "Circuit needs INPUT and OUTPUT declarations"
        );
    }

    if (Result.Inputs.size() > MaxInputs) {
        throw std::runtime_error(
            "Maximum 12 primary inputs in this version"
        );
    }

    return Result;
}

// Validate drivers and place gates in dependency order.
void CompileCircuit(Circuit& C) {
    int WireCount = static_cast<int>(C.WireNames.size());
    int GateCount = static_cast<int>(C.Gates.size());

    std::vector<bool> IsInput(WireCount, false);
    std::vector<int> Producer(WireCount, -1);

    for (int Wire : C.Inputs) {
        IsInput[Wire] = true;
    }

    for (int i = 0; i < GateCount; ++i) {
        int Wire = C.Gates[i].Output;

        if (IsInput[Wire]) {
            throw std::runtime_error(
                "Cannot drive a primary input: " +
                C.WireNames[Wire]
            );
        }

        if (Producer[Wire] != -1) {
            throw std::runtime_error(
                "Multiple drivers on: " + C.WireNames[Wire]
            );
        }

        Producer[Wire] = i;
    }

    // Every non-input wire must have one driver.
    for (int Wire = 0; Wire < WireCount; ++Wire) {
        if (!IsInput[Wire] && Producer[Wire] == -1) {
            throw std::runtime_error(
                "Undriven wire: " + C.WireNames[Wire]
            );
        }
    }

    std::vector<std::vector<int>> Adj(GateCount);
    std::vector<int> InDegree(GateCount, 0);

    for (int i = 0; i < GateCount; ++i) {
        const Gate& G = C.Gates[i];

        int InputWires[2] = {G.InputA, G.InputB};
        int Count = IsUnary(G.Type) ? 1 : 2;

        for (int j = 0; j < Count; ++j) {
            int Parent = Producer[InputWires[j]];

            if (Parent >= 0) {
                Adj[Parent].push_back(i);
                ++InDegree[i];
            }
        }
    }

    std::queue<int> Ready;

    for (int i = 0; i < GateCount; ++i) {
        if (InDegree[i] == 0) {
            Ready.push(i);
        }
    }

    std::vector<Gate> Sorted;

    while (!Ready.empty()) {
        int Current = Ready.front();
        Ready.pop();

        Sorted.push_back(C.Gates[Current]);

        for (int Next : Adj[Current]) {
            if (--InDegree[Next] == 0) {
                Ready.push(Next);
            }
        }
    }

    if (static_cast<int>(Sorted.size()) != GateCount) {
        throw std::runtime_error(
            "Combinational cycle detected"
        );
    }

    C.Gates = std::move(Sorted);
}

// Available on both CPU and GPU.
__host__ __device__
uint8_t EvaluateGate(
    GateType Type, uint8_t A, uint8_t B
) {
    switch (Type) {
        case GateType::AND: return A & B;
        case GateType::OR: return A | B;
        case GateType::XOR: return A ^ B;
        case GateType::NAND: return !(A & B);
        case GateType::NOR: return !(A | B);
        case GateType::NOT: return !A;
        case GateType::BUF: return A;
    }

    return 0;
}

// Return 1 if any primary output differs.
__host__ __device__
uint8_t DetectFault(
    const Gate* Gates,
    int GateCount,
    const int* Inputs,
    int InputCount,
    const int* Outputs,
    int OutputCount,
    int Pattern,
    Fault CurrentFault
) {
    uint8_t Good[MaxWires] = {};
    uint8_t Bad[MaxWires] = {};

    // Decode input pattern.
    for (int i = 0; i < InputCount; ++i) {
        int BitPosition = InputCount - 1 - i;
        uint8_t Value = (Pattern >> BitPosition) & 1;
        int Wire = Inputs[i];

        Good[Wire] = Value;
        Bad[Wire] = Value;

        // Fault on a primary input.
        if (Wire == CurrentFault.Wire) {
            Bad[Wire] =
                static_cast<uint8_t>(CurrentFault.StuckValue);
        }
    }

    // Gates must already be topologically sorted.
    for (int i = 0; i < GateCount; ++i) {
        Gate G = Gates[i];

        uint8_t GoodA = Good[G.InputA];
        uint8_t BadA = Bad[G.InputA];

        uint8_t GoodB = G.InputB >= 0
            ? Good[G.InputB] : 0;

        uint8_t BadB = G.InputB >= 0
            ? Bad[G.InputB] : 0;

        Good[G.Output] =
            EvaluateGate(G.Type, GoodA, GoodB);

        uint8_t Result =
            EvaluateGate(G.Type, BadA, BadB);

        // Fault on a gate output.
        if (G.Output == CurrentFault.Wire) {
            Result =
                static_cast<uint8_t>(CurrentFault.StuckValue);
        }

        Bad[G.Output] = Result;
    }

    for (int i = 0; i < OutputCount; ++i) {
        int Wire = Outputs[i];

        if (Good[Wire] != Bad[Wire]) {
            return 1;
        }
    }

    return 0;
}

// One GPU thread handles one fault-pattern pair.
__global__ void FaultKernel(
    const Gate* Gates,
    int GateCount,
    const int* Inputs,
    int InputCount,
    const int* Outputs,
    int OutputCount,
    const Fault* Faults,
    int PatternCount,
    int TotalWork,
    uint8_t* Detection
) {
    int Index =
        blockIdx.x * blockDim.x + threadIdx.x;

    if (Index >= TotalWork) {
        return;
    }

    int FaultIndex = Index / PatternCount;
    int Pattern = Index % PatternCount;

    Detection[Index] = DetectFault(
        Gates,
        GateCount,
        Inputs,
        InputCount,
        Outputs,
        OutputCount,
        Pattern,
        Faults[FaultIndex]
    );
}

int main(int argc, char** argv) {
    try {
        std::string Filename = argc > 1
            ? argv[1] : "circuits/Example.txt";

        Circuit C = LoadNetlist(Filename);
        CompileCircuit(C);

        int PatternCount =
            1 << static_cast<int>(C.Inputs.size());

        // Generate SA0 and SA1 on every modeled wire.
        std::vector<Fault> Faults;

        for (int Wire = 0;
             Wire < static_cast<int>(C.WireNames.size());
             ++Wire) {
            Faults.push_back({Wire, 0});
            Faults.push_back({Wire, 1});
        }

        int GateCount = static_cast<int>(C.Gates.size());
        int FaultCount = static_cast<int>(Faults.size());
        int TotalWork = FaultCount * PatternCount;

        std::cout << "CircuitForge Netlist Fault Simulator\n";
        std::cout << "File: " << Filename << "\n";
        std::cout << "Wires: " << C.WireNames.size() << "\n";
        std::cout << "Gates: " << GateCount << "\n";
        std::cout << "Patterns: " << PatternCount << "\n";
        std::cout << "Faults: " << FaultCount << "\n";
        std::cout << "Fault-pattern pairs: "
                  << TotalWork << "\n\n";

        Gate* DeviceGates = nullptr;
        int* DeviceInputs = nullptr;
        int* DeviceOutputs = nullptr;
        Fault* DeviceFaults = nullptr;
        uint8_t* DeviceDetection = nullptr;

        size_t GateBytes = C.Gates.size() * sizeof(Gate);
        size_t InputBytes = C.Inputs.size() * sizeof(int);
        size_t OutputBytes = C.Outputs.size() * sizeof(int);
        size_t FaultBytes = Faults.size() * sizeof(Fault);
        size_t ResultBytes = TotalWork * sizeof(uint8_t);

        CudaCheck(cudaMalloc(&DeviceGates, GateBytes));
        CudaCheck(cudaMalloc(&DeviceInputs, InputBytes));
        CudaCheck(cudaMalloc(&DeviceOutputs, OutputBytes));
        CudaCheck(cudaMalloc(&DeviceFaults, FaultBytes));
        CudaCheck(cudaMalloc(&DeviceDetection, ResultBytes));

        CudaCheck(cudaMemcpy(
            DeviceGates, C.Gates.data(), GateBytes,
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
            DeviceFaults, Faults.data(), FaultBytes,
            cudaMemcpyHostToDevice
        ));

        int Blocks =
            (TotalWork + ThreadsPerBlock - 1) /
            ThreadsPerBlock;

        FaultKernel<<<Blocks, ThreadsPerBlock>>>(
            DeviceGates,
            GateCount,
            DeviceInputs,
            static_cast<int>(C.Inputs.size()),
            DeviceOutputs,
            static_cast<int>(C.Outputs.size()),
            DeviceFaults,
            PatternCount,
            TotalWork,
            DeviceDetection
        );

        CudaCheck(cudaGetLastError());
        CudaCheck(cudaDeviceSynchronize());

        std::vector<uint8_t> GpuResults(TotalWork);

        CudaCheck(cudaMemcpy(
            GpuResults.data(),
            DeviceDetection,
            ResultBytes,
            cudaMemcpyDeviceToHost
        ));

        int Correct = 0;
        int DetectedFaults = 0;

        std::cout << "Fault             Detecting patterns\n";
        std::cout << "------------------------------------\n";

        for (int f = 0; f < FaultCount; ++f) {
            int DetectingPatterns = 0;

            for (int p = 0; p < PatternCount; ++p) {
                int Index = f * PatternCount + p;

                uint8_t CpuResult = DetectFault(
                    C.Gates.data(),
                    GateCount,
                    C.Inputs.data(),
                    static_cast<int>(C.Inputs.size()),
                    C.Outputs.data(),
                    static_cast<int>(C.Outputs.size()),
                    p,
                    Faults[f]
                );

                if (CpuResult == GpuResults[Index]) {
                    ++Correct;
                }

                if (GpuResults[Index]) {
                    ++DetectingPatterns;
                }
            }

            if (DetectingPatterns > 0) {
                ++DetectedFaults;
            }

            std::cout
                << C.WireNames[Faults[f].Wire]
                << " SA" << Faults[f].StuckValue
                << "              "
                << DetectingPatterns << "/"
                << PatternCount << "\n";
        }

        double Coverage =
            100.0 * DetectedFaults / FaultCount;

        std::cout << "\nCPU/GPU agreement: "
                  << Correct << "/" << TotalWork << "\n";

        std::cout << "Detected faults: "
                  << DetectedFaults << "/" << FaultCount
                  << "\n";

        std::cout << "Fault coverage: "
                  << Coverage << "%\n";

        CudaCheck(cudaFree(DeviceGates));
        CudaCheck(cudaFree(DeviceInputs));
        CudaCheck(cudaFree(DeviceOutputs));
        CudaCheck(cudaFree(DeviceFaults));
        CudaCheck(cudaFree(DeviceDetection));

        return Correct == TotalWork
            ? EXIT_SUCCESS : EXIT_FAILURE;

    } catch (const std::exception& Error) {
        std::cerr << "Error: " << Error.what() << '\n';
        return EXIT_FAILURE;
    }
}
