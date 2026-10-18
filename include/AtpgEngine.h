#pragma once
#include "CpuSimulator.h"
namespace CircuitForge {
enum class AtpgStatus { Detected, ProvenRedundant, Aborted };
struct AtpgResult {
    AtpgStatus Status = AtpgStatus::Aborted;
    std::vector<uint8_t> Pattern;
    size_t Nodes = 0;
};
AtpgResult GenerateTest(const Circuit &C, Fault Target, size_t NodeLimit = 100000);
} // namespace CircuitForge
