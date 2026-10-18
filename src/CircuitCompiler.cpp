#include "CircuitCompiler.h"
#include "NetlistParser.h"
#include <algorithm>
#include <queue>
namespace CircuitForge {
void CompileCircuit(Circuit &C) {
    int N = static_cast<int>(C.WireNames.size());
    int G = static_cast<int>(C.Gates.size());

    if (C.Inputs.empty() || C.Outputs.empty())
        throw std::runtime_error("Inputs and outputs required");
    auto Validate = [&](int Wire) {
        if (Wire < 0 || Wire >= N)
            throw std::runtime_error("Invalid wire reference");
    };
    for (int W : C.Inputs)
        Validate(W);
    for (int W : C.Outputs)
        Validate(W);
    for (const auto &Gate : C.Gates) {
        Validate(Gate.Output);
        Validate(Gate.InputA);
        if (!IsUnary(Gate.Type))
            Validate(Gate.InputB);
    }
    std::vector<int> Producer(N, -1);
    std::vector<bool> PrimaryInput(N, false);

    for (int Wire : C.Inputs) {
        if (PrimaryInput[Wire])
            throw std::runtime_error("Duplicate primary input");
        PrimaryInput[Wire] = true;
    }

    for (int Index = 0; Index < G; ++Index) {
        int Out = C.Gates[Index].Output;

        if (PrimaryInput[Out] || Producer[Out] != -1) {
            throw std::runtime_error("Invalid or multiple driver: " + C.WireNames[Out]);
        }

        Producer[Out] = Index;
    }

    for (int Index = 0; Index < N; ++Index) {
        if (!PrimaryInput[Index] && Producer[Index] == -1) {
            throw std::runtime_error("Undriven wire: " + C.WireNames[Index]);
        }
    }

    std::vector<std::vector<int>> Adj(G);
    std::vector<int> Degree(G, 0);

    for (int Index = 0; Index < G; ++Index) {
        const Gate &Current = C.Gates[Index];

        int Inputs[2] = {Current.InputA, Current.InputB};

        int Count = IsUnary(Current.Type) ? 1 : 2;

        for (int InputIndex = 0; InputIndex < Count; ++InputIndex) {
            int Parent = Producer[Inputs[InputIndex]];

            if (Parent != -1) {
                Adj[Parent].push_back(Index);
                ++Degree[Index];
            }
        }
    }

    std::queue<int> Ready;

    for (int Index = 0; Index < G; ++Index) {
        if (Degree[Index] == 0) {
            Ready.push(Index);
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
        throw std::runtime_error("Combinational cycle detected");
    }

    C.Gates = std::move(Sorted);
    std::vector<int> WireLevels(N, 0);
    C.Levels.clear();
    for (const auto &Current : C.Gates) {
        int Level = 1 + std::max(WireLevels[Current.InputA],
                                 Current.InputB < 0 ? 0 : WireLevels[Current.InputB]);
        WireLevels[Current.Output] = Level;
        C.Levels.push_back(Level);
    }
}

} // namespace CircuitForge
