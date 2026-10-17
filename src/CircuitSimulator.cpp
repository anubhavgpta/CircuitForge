
#include <iostream>
#include <vector>
#include <queue>
#include <stdexcept>
#include <cstdint>

enum class GateType {
    AND, OR, XOR, NAND, NOR, NOT, BUF
};

struct Gate {
    GateType Type;
    std::vector<int> Inputs;
    int Output;
};

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

    throw std::runtime_error("Unsupported gate");
}

class Circuit {
private:
    int WireCount;
    std::vector<Gate> Gates;
    std::vector<int> EvaluationOrder;

public:
    explicit Circuit(int NumberOfWires)
        : WireCount(NumberOfWires) {
        if (WireCount <= 0) {
            throw std::invalid_argument("Invalid wire count");
        }
    }

    void AddGate(GateType Type,
        std::vector<int> Inputs,
        int Output) {
        int ExpectedInputs =
            (Type == GateType::NOT || Type == GateType::BUF)
            ? 1 : 2;

        if (static_cast<int>(Inputs.size()) != ExpectedInputs) {
            throw std::invalid_argument("Wrong gate input count");
        }

        if (Output < 0 || Output >= WireCount) {
            throw std::out_of_range("Invalid output wire");
        }

        for (int Input : Inputs) {
            if (Input < 0 || Input >= WireCount) {
                throw std::out_of_range("Invalid input wire");
            }
        }

        Gates.push_back({ Type, Inputs, Output });
        EvaluationOrder.clear();
    }

    void Compile() {
        int Count = static_cast<int>(Gates.size());

        std::vector<int> Producer(WireCount, -1);

        // Identify which gate produces each wire.
        for (int i = 0; i < Count; ++i) {
            int Output = Gates[i].Output;

            if (Producer[Output] != -1) {
                throw std::runtime_error(
                    "Multiple gates drive one wire"
                );
            }

            Producer[Output] = i;
        }

        std::vector<std::vector<int>> Adjacency(Count);
        std::vector<int> InDegree(Count, 0);

        // Build the gate dependency graph.
        for (int i = 0; i < Count; ++i) {
            for (int Input : Gates[i].Inputs) {
                int Parent = Producer[Input];

                if (Parent != -1) {
                    Adjacency[Parent].push_back(i);
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

        EvaluationOrder.clear();

        // Kahn's topological sorting algorithm.
        while (!Ready.empty()) {
            int Current = Ready.front();
            Ready.pop();

            EvaluationOrder.push_back(Current);

            for (int Next : Adjacency[Current]) {
                InDegree[Next]--;

                if (InDegree[Next] == 0) {
                    Ready.push(Next);
                }
            }
        }

        if (static_cast<int>(EvaluationOrder.size()) != Count) {
            EvaluationOrder.clear();
            throw std::runtime_error(
                "Combinational cycle detected"
            );
        }
    }

    std::vector<uint8_t> Simulate(
        const std::vector<uint8_t>& InitialWires
    ) const {
        if (InitialWires.size() !=
            static_cast<size_t>(WireCount)) {
            throw std::invalid_argument(
                "Initial wire vector has incorrect size"
            );
        }

        if (EvaluationOrder.size() != Gates.size()) {
            throw std::runtime_error(
                "Circuit must be compiled first"
            );
        }

        for (uint8_t Bit : InitialWires) {
            if (Bit > 1) {
                throw std::invalid_argument(
                    "Only binary logic is supported"
                );
            }
        }

        std::vector<uint8_t> Wires = InitialWires;

        for (int GateIndex : EvaluationOrder) {
            const Gate& Current = Gates[GateIndex];

            uint8_t A = Wires[Current.Inputs[0]];
            uint8_t B = Current.Inputs.size() == 2
                ? Wires[Current.Inputs[1]]
                : 0;

            Wires[Current.Output] =
                EvaluateGate(Current.Type, A, B);
        }

        return Wires;
    }
};

int main() {
    // Wire assignments:
    // A = 0
    // B = 1
    // C = 2
    // D = 3  (A XOR B)
    // E = 4  (NOT C)
    // Y = 5  (D AND E)

    Circuit MyCircuit(6);

    // Deliberately add gates in reverse dependency order.
    // Topological sorting determines the correct execution order.

    MyCircuit.AddGate(GateType::AND, { 3, 4 }, 5);
    MyCircuit.AddGate(GateType::NOT, { 2 }, 4);
    MyCircuit.AddGate(GateType::XOR, { 0, 1 }, 3);

    MyCircuit.Compile();

    std::cout << "Circuit: Y = (A XOR B) AND (NOT C)\n\n";
    std::cout << "A B C | D E | Expected Actual\n";
    std::cout << "-----------------------------\n";

    int Passed = 0;

    for (int Pattern = 0; Pattern < 8; ++Pattern) {
        uint8_t A = (Pattern >> 2) & 1;
        uint8_t B = (Pattern >> 1) & 1;
        uint8_t C = Pattern & 1;

        std::vector<uint8_t> InitialWires = {
            A, B, C, 0, 0, 0
        };

        std::vector<uint8_t> Result =
            MyCircuit.Simulate(InitialWires);

        int D = Result[3];
        int E = Result[4];
        int Y = Result[5];

        int Expected = (A ^ B) & (!C);

        std::cout
            << int(A) << " "
            << int(B) << " "
            << int(C) << " | "
            << D << " "
            << E << " | "
            << Expected << "        "
            << Y << '\n';

        if (Y == Expected) {
            Passed++;
        }
    }

    std::cout << "\nTests Passed: "
        << Passed << "/8\n";

    return Passed == 8 ? 0 : 1;
};
