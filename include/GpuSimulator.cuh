#pragma once
#include "CpuSimulator.h"
namespace CircuitForge {
struct GpuMetrics {
    double KernelMs = 0, H2DMs = 0, D2HMs = 0, EndToEndMs = 0;
    size_t DeviceBytes = 0;
};
std::vector<uint32_t> GpuMasks(const Circuit &C, const Patterns &P,
                               const std::vector<Fault> &Faults, bool Drop = false,
                               int BlockSize = 128, GpuMetrics *Metrics = nullptr);
} // namespace CircuitForge
