#include "FaultCollapser.h"
#include <algorithm>
#include <numeric>
namespace CircuitForge {
std::vector<size_t> CollapseFaults(const Circuit &C) {
    std::vector<size_t> Parent(C.WireNames.size() * 2);
    std::iota(Parent.begin(), Parent.end(), 0);
    auto Find = [&](size_t X) {
        while (Parent[X] != X) {
            Parent[X] = Parent[Parent[X]];
            X = Parent[X];
        }
        return X;
    };
    auto Merge = [&](size_t A, size_t B) {
        A = Find(A);
        B = Find(B);
        Parent[std::max(A, B)] = std::min(A, B);
    };
    std::vector<int> Uses(C.WireNames.size());
    for (const auto &G : C.Gates) {
        ++Uses[G.InputA];
        if (G.InputB >= 0)
            ++Uses[G.InputB];
    }
    for (int W : C.Outputs)
        ++Uses[W]; // A primary-output observation is another fanout branch.
    for (const auto &G : C.Gates) {
        for (int W : {G.InputA, G.InputB}) {
            if (W < 0 || Uses[W] != 1)
                continue;
            if (G.Type == GateType::BUF || G.Type == GateType::NOT) {
                for (int V = 0; V < 2; ++V)
                    Merge(size_t(W) * 2 + V,
                          size_t(G.Output) * 2 + (V ^ (G.Type == GateType::NOT)));
            } else if (G.Type == GateType::AND || G.Type == GateType::NAND)
                Merge(size_t(W) * 2, size_t(G.Output) * 2 + (G.Type == GateType::NAND));
            else if (G.Type == GateType::OR || G.Type == GateType::NOR)
                Merge(size_t(W) * 2 + 1, size_t(G.Output) * 2 + (G.Type == GateType::OR));
        }
    }
    for (size_t I = 0; I < Parent.size(); ++I)
        Parent[I] = Find(I);
    return Parent;
}
} // namespace CircuitForge
