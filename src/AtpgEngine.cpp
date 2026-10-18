#include "AtpgEngine.h"
#include <algorithm>
namespace CircuitForge {
namespace {
// -1 is unknown. Ternary implication is conservative even with reconvergence.
int Imply(GateType Type, int A, int B) {
    switch (Type) {
    case GateType::BUF:
        return A;
    case GateType::NOT:
        return A < 0 ? -1 : 1 - A;
    case GateType::AND:
    case GateType::NAND: {
        int R = (A == 0 || B == 0) ? 0 : (A == 1 && B == 1 ? 1 : -1);
        return R < 0 ? -1 : (Type == GateType::NAND ? 1 - R : R);
    }
    case GateType::OR:
    case GateType::NOR: {
        int R = (A == 1 || B == 1) ? 1 : (A == 0 && B == 0 ? 0 : -1);
        return R < 0 ? -1 : (Type == GateType::NOR ? 1 - R : R);
    }
    case GateType::XOR:
    case GateType::XNOR:
        return A < 0 || B < 0 ? -1 : (A ^ B ^ (Type == GateType::XNOR));
    }
    return -1;
}
} // namespace
AtpgResult GenerateTest(const Circuit &C, Fault Target, size_t NodeLimit) {
    if (Target.Wire < 0 || size_t(Target.Wire) >= C.WireNames.size() ||
        (Target.Value != 0 && Target.Value != 1))
        throw std::invalid_argument("Invalid ATPG fault");
    AtpgResult Result;
    std::vector<int> Assignment(C.Inputs.size(), -1);
    std::vector<int> Branch(C.Inputs.size(), 0);
    size_t Depth = 0;
    // Iterative PI backtracking avoids a recursion limit on large input sets.
    for (;;) {
        if (Result.Nodes >= NodeLimit)
            return Result;
        ++Result.Nodes;
        std::vector<int> Good(C.WireNames.size(), -1), Bad(Good);
        for (size_t I = 0; I < C.Inputs.size(); ++I) {
            int W = C.Inputs[I];
            Good[W] = Assignment[I];
            Bad[W] = W == Target.Wire ? Target.Value : Assignment[I];
        }
        for (const auto &G : C.Gates) {
            Good[G.Output] = Imply(G.Type, Good[G.InputA], G.InputB < 0 ? -1 : Good[G.InputB]);
            Bad[G.Output] = G.Output == Target.Wire
                                ? Target.Value
                                : Imply(G.Type, Bad[G.InputA], G.InputB < 0 ? -1 : Bad[G.InputB]);
        }
        bool Activated = Good[Target.Wire] >= 0 && Good[Target.Wire] != Target.Value;
        bool Observable = false, Possible = false;
        for (int W : C.Outputs) {
            Observable |= Good[W] >= 0 && Bad[W] >= 0 && Good[W] != Bad[W];
            Possible |= Good[W] < 0 || Bad[W] < 0 || Good[W] != Bad[W];
        }
        if (Activated && Observable) {
            for (int Value : Assignment)
                Result.Pattern.push_back(uint8_t(std::max(0, Value)));
            if (SimulateScalar(C, Result.Pattern) == SimulateScalar(C, Result.Pattern, Target))
                throw std::logic_error("ATPG verification failed");
            Result.Status = AtpgStatus::Detected;
            return Result;
        }
        bool Blocked = Good[Target.Wire] == Target.Value || !Possible;
        if (!Blocked && Depth < Assignment.size()) {
            Branch[Depth] = 0;
            Assignment[Depth] = 0;
            ++Depth;
            continue;
        }
        bool Advanced = false;
        while (Depth) {
            --Depth;
            if (Branch[Depth] == 0) {
                Branch[Depth] = 1;
                Assignment[Depth] = 1;
                ++Depth;
                Advanced = true;
                break;
            }
            Assignment[Depth] = -1;
        }
        if (!Advanced) {
            Result.Status = AtpgStatus::ProvenRedundant;
            return Result;
        }
    }
}
} // namespace CircuitForge
