#pragma once
#include "FaultGenerator.h"
namespace CircuitForge {
// Representative[original FaultId] is a stable original FaultId.
std::vector<size_t> CollapseFaults(const Circuit &C);
} // namespace CircuitForge
