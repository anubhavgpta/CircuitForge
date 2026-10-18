#pragma once
#include "Circuit.h"
#include "PatternGenerator.h"
namespace CircuitForge {
struct FaultStatus {
    size_t FaultId;
    Fault Site;
    bool Detected = false;
    size_t FirstPattern = SIZE_MAX, DetectingPatterns = 0;
};
std::vector<Fault> GenerateFaults(const Circuit &C);
std::vector<FaultStatus> SummarizeFaults(const std::vector<Fault> &Faults, const Patterns &P,
                                         const std::vector<uint32_t> &Masks);
} // namespace CircuitForge
