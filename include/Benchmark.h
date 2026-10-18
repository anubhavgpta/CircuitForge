#pragma once
#include "GpuSimulator.cuh"
#include <ostream>
namespace CircuitForge {
void Benchmark(const Circuit &C, const Patterns &P, const std::vector<Fault> &Faults, bool Gpu,
               bool Drop, int BlockSize, int Repetitions, std::ostream &Out);
}
