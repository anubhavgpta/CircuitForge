#pragma once
#include "Circuit.h"
namespace CircuitForge {
struct Patterns {
    size_t Count = 0, InputCount = 0;
    bool Exhaustive = false;
    uint32_t Seed = 1;
    std::vector<uint32_t> Words;
    size_t Batches() const {
        return (Count + 31) / 32;
    }
    uint32_t Mask(size_t Batch) const {
        size_t Left = Count - Batch * 32;
        return Left >= 32 ? UINT32_MAX : (uint32_t(1) << Left) - 1;
    }
};
Patterns GeneratePatterns(size_t Inputs, size_t Count, uint32_t Seed = 1);
} // namespace CircuitForge
