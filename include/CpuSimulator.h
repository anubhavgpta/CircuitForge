#pragma once
#include "PatternGenerator.h"
namespace CircuitForge {
std::vector<uint8_t> SimulateScalar(const Circuit &C, const std::vector<uint8_t> &Inputs,
                                    Fault F = {-1, 0});
std::vector<uint32_t> SimulatePacked(const Circuit &C, const uint32_t *Inputs, Fault F = {-1, 0});
std::vector<uint32_t> CpuMasks(const Circuit &C, const Patterns &P,
                               const std::vector<Fault> &Faults, bool Drop = false);
} // namespace CircuitForge
