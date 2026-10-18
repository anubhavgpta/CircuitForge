#include "Benchmark.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
namespace CircuitForge {
namespace {
using Clock = std::chrono::steady_clock;
double Ms(Clock::time_point Start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - Start).count();
}
double Median(std::vector<double> Values) {
    std::sort(Values.begin(), Values.end());
    return Values[Values.size() / 2];
}
std::vector<uint32_t> ScalarMasks(const Circuit &C, const Patterns &P,
                                  const std::vector<Fault> &Faults, bool Drop) {
    std::vector<uint32_t> Masks(Faults.size() * P.Batches());
    std::vector<bool> Detected(Faults.size());
    // Drop at batch boundaries so the scalar reference performs the same work as packed engines.
    for (size_t B = 0; B < P.Batches(); ++B) {
        std::vector<bool> BatchDetected(Faults.size());
        for (size_t Bit = 0; Bit < 32 && B * 32 + Bit < P.Count; ++Bit) {
            std::vector<uint8_t> In;
            for (size_t I = 0; I < P.InputCount; ++I)
                In.push_back((P.Words[B * P.InputCount + I] >> Bit) & 1);
            auto Good = SimulateScalar(C, In);
            for (size_t F = 0; F < Faults.size(); ++F)
                if (!Drop || !Detected[F])
                    if (Good != SimulateScalar(C, In, Faults[F])) {
                        Masks[F * P.Batches() + B] |= uint32_t(1) << Bit;
                        BatchDetected[F] = true;
                    }
        }
        for (size_t F = 0; F < Faults.size(); ++F)
            Detected[F] = Detected[F] || BatchDetected[F];
    }
    return Masks;
}
} // namespace
void Benchmark(const Circuit &C, const Patterns &P, const std::vector<Fault> &Faults, bool Gpu,
               bool Drop, int BlockSize, int Repetitions, std::ostream &Out) {
    auto Expected = CpuMasks(C, P, Faults, Drop);
    auto ScalarWarmup = ScalarMasks(C, P, Faults, Drop);
    if (ScalarWarmup != Expected)
        throw std::runtime_error("Scalar benchmark mismatch");
#ifdef CIRCUITFORGE_CUDA
    if (Gpu && GpuMasks(C, P, Faults, Drop, BlockSize) != Expected)
        throw std::runtime_error("GPU warmup mismatch");
#else
    if (Gpu)
        throw std::runtime_error("CUDA backend not built");
#endif
    std::vector<double> CpuTimes, ScalarTimes, GpuTimes, Kernel, H2D, D2H;
    size_t Bytes = 0;
    for (int Run = 0; Run < Repetitions; ++Run) {
        auto Start = Clock::now();
        auto Scalar = ScalarMasks(C, P, Faults, Drop);
        ScalarTimes.push_back(Ms(Start));
        if (Scalar != Expected)
            throw std::runtime_error("Scalar mismatch");
        Start = Clock::now();
        auto Actual = CpuMasks(C, P, Faults, Drop);
        CpuTimes.push_back(Ms(Start));
        if (Actual != Expected)
            throw std::runtime_error("CPU mismatch");
#ifdef CIRCUITFORGE_CUDA
        if (Gpu) {
            GpuMetrics M;
            Start = Clock::now();
            auto ActualGpu = GpuMasks(C, P, Faults, Drop, BlockSize, &M);
            GpuTimes.push_back(Ms(Start));
            if (ActualGpu != Expected)
                throw std::runtime_error("GPU benchmark mismatch");
            Kernel.push_back(M.KernelMs);
            H2D.push_back(M.H2DMs);
            D2H.push_back(M.D2HMs);
            Bytes = M.DeviceBytes;
        }
#endif
    }
    size_t Evaluations = 0;
    for (size_t F = 0; F < Faults.size(); ++F)
        for (size_t B = 0; B < P.Batches(); ++B) {
            Evaluations += std::min(size_t(32), P.Count - B * 32);
            if (Drop && Expected[F * P.Batches() + B])
                break;
        }
    double Cpu = Median(CpuTimes), Total = Gpu ? Median(GpuTimes) : Cpu;
    Out << "wires,gates,patterns,pattern_mode,seed,faults,dropping,block_size,repetitions,scalar_"
           "ms,cpu_packed_ms,gpu_kernel_ms,h2d_ms,d2h_ms,end_to_end_ms,evaluations_per_s,patterns_"
           "per_s,device_bytes,cpu_over_gpu\n";
    Out << std::setprecision(10) << C.WireNames.size() << ',' << C.Gates.size() << ',' << P.Count
        << ',' << (P.Exhaustive ? "exhaustive" : "sampled") << ',' << P.Seed << ',' << Faults.size()
        << ',' << Drop << ',' << BlockSize << ',' << Repetitions << ',' << Median(ScalarTimes)
        << ',' << Cpu << ',';
    if (Gpu)
        Out << Median(Kernel) << ',' << Median(H2D) << ',' << Median(D2H);
    else
        Out << ",,";
    Out << ',' << Total << ',' << Evaluations * 1000.0 / Total << ',' << P.Count * 1000.0 / Total
        << ',' << Bytes << ',';
    if (Gpu)
        Out << Cpu / Total;
    Out << '\n';
}
} // namespace CircuitForge
