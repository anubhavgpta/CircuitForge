#include "FaultGenerator.h"
namespace CircuitForge {
std::vector<Fault> GenerateFaults(const Circuit &C) {
    std::vector<Fault> Result;
    for (int W = 0; W < int(C.WireNames.size()); ++W)
        for (int V = 0; V < 2; ++V)
            Result.push_back({W, V});
    return Result;
}
std::vector<FaultStatus> SummarizeFaults(const std::vector<Fault> &Faults, const Patterns &P,
                                         const std::vector<uint32_t> &Masks) {
    if (Masks.size() != Faults.size() * P.Batches())
        throw std::invalid_argument("Mask dimensions");
    std::vector<FaultStatus> Result;
    for (size_t F = 0; F < Faults.size(); ++F) {
        FaultStatus S{size_t(Faults[F].Wire) * 2 + Faults[F].Value, Faults[F]};
        for (size_t B = 0; B < P.Batches(); ++B) {
            uint32_t M = Masks[F * P.Batches() + B] & P.Mask(B);
            for (size_t Bit = 0; Bit < 32; ++Bit)
                if ((M >> Bit) & 1) {
                    if (!S.Detected)
                        S.FirstPattern = B * 32 + Bit;
                    S.Detected = true;
                    ++S.DetectingPatterns;
                }
        }
        Result.push_back(S);
    }
    return Result;
}
} // namespace CircuitForge
