#include "CpuSimulator.h"
namespace CircuitForge {
std::vector<uint8_t> SimulateScalar(const Circuit &C, const std::vector<uint8_t> &Inputs, Fault F) {
    if (Inputs.size() != C.Inputs.size())
        throw std::invalid_argument("Input width mismatch");
    std::vector<uint8_t> W(C.WireNames.size());
    for (size_t I = 0; I < Inputs.size(); ++I) {
        if (Inputs[I] > 1)
            throw std::invalid_argument("Nonbinary input");
        W[C.Inputs[I]] = C.Inputs[I] == F.Wire ? F.Value : Inputs[I];
    }
    for (const auto &G : C.Gates) {
        bool A = W[G.InputA] != 0, B = G.InputB >= 0 && W[G.InputB] != 0, R = false;
        switch (G.Type) {
        case GateType::AND:
            R = A && B;
            break;
        case GateType::OR:
            R = A || B;
            break;
        case GateType::XOR:
            R = A != B;
            break;
        case GateType::XNOR:
            R = A == B;
            break;
        case GateType::NAND:
            R = !(A && B);
            break;
        case GateType::NOR:
            R = !(A || B);
            break;
        case GateType::NOT:
            R = !A;
            break;
        case GateType::BUF:
            R = A;
            break;
        }
        W[G.Output] = G.Output == F.Wire ? F.Value : R;
    }
    std::vector<uint8_t> Out;
    for (int Wire : C.Outputs)
        Out.push_back(W[Wire]);
    return Out;
}
std::vector<uint32_t> SimulatePacked(const Circuit &C, const uint32_t *Inputs, Fault F) {
    std::vector<uint32_t> W(C.WireNames.size());
    for (size_t I = 0; I < C.Inputs.size(); ++I)
        W[C.Inputs[I]] = C.Inputs[I] == F.Wire ? (F.Value ? UINT32_MAX : 0) : Inputs[I];
    for (const auto &G : C.Gates) {
        uint32_t A = W[G.InputA], B = G.InputB < 0 ? 0 : W[G.InputB], R = 0;
        switch (G.Type) {
        case GateType::AND:
            R = A & B;
            break;
        case GateType::OR:
            R = A | B;
            break;
        case GateType::XOR:
            R = A ^ B;
            break;
        case GateType::XNOR:
            R = ~(A ^ B);
            break;
        case GateType::NAND:
            R = ~(A & B);
            break;
        case GateType::NOR:
            R = ~(A | B);
            break;
        case GateType::NOT:
            R = ~A;
            break;
        case GateType::BUF:
            R = A;
            break;
        }
        W[G.Output] = G.Output == F.Wire ? (F.Value ? UINT32_MAX : 0) : R;
    }
    std::vector<uint32_t> Out;
    for (int Wire : C.Outputs)
        Out.push_back(W[Wire]);
    return Out;
}
std::vector<uint32_t> CpuMasks(const Circuit &C, const Patterns &P,
                               const std::vector<Fault> &Faults, bool Drop) {
    if (P.InputCount != C.Inputs.size() || P.Words.size() != P.Batches() * P.InputCount)
        throw std::invalid_argument("Invalid packed patterns");
    std::vector<uint32_t> Masks(Faults.size() * P.Batches());
    std::vector<size_t> Active;
    for (size_t F = 0; F < Faults.size(); ++F)
        Active.push_back(F);
    for (size_t B = 0; B < P.Batches(); ++B) {
        const auto *Inputs = P.Words.data() + B * P.InputCount;
        auto Good = SimulatePacked(C, Inputs);
        size_t Remaining = 0;
        for (size_t F : Active) {
            auto Bad = SimulatePacked(C, Inputs, Faults[F]);
            uint32_t Mask = 0;
            for (size_t O = 0; O < Good.size(); ++O)
                Mask |= Good[O] ^ Bad[O];
            Mask &= P.Mask(B);
            Masks[F * P.Batches() + B] = Mask;
            if (!Drop || !Mask)
                Active[Remaining++] = F;
        }
        Active.resize(Remaining);
        if (Active.empty())
            break;
    }
    return Masks;
}
} // namespace CircuitForge
