#include "GpuSimulator.cuh"
#include <algorithm>
#include <chrono>
#include <cuda_runtime.h>
namespace CircuitForge {
namespace {
void Check(cudaError_t Error) {
    if (Error != cudaSuccess)
        throw std::runtime_error(cudaGetErrorString(Error));
}
template <class T> struct Buffer {
    T *Data = nullptr;
    size_t Count;
    explicit Buffer(size_t N) : Count(N) {
        if (N)
            Check(cudaMalloc(reinterpret_cast<void **>(&Data), N * sizeof(T)));
    }
    ~Buffer() {
        if (Data)
            cudaFree(Data);
    }
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    void Upload(const T *Source) {
        if (Count)
            Check(cudaMemcpy(Data, Source, Count * sizeof(T), cudaMemcpyHostToDevice));
    }
};
struct Event {
    cudaEvent_t Value;
    Event() {
        Check(cudaEventCreate(&Value));
    }
    ~Event() {
        cudaEventDestroy(Value);
    }
};
__device__ uint32_t Eval(GateType Type, uint32_t A, uint32_t B) {
    switch (Type) {
    case GateType::AND:
        return A & B;
    case GateType::OR:
        return A | B;
    case GateType::XOR:
        return A ^ B;
    case GateType::XNOR:
        return ~(A ^ B);
    case GateType::NAND:
        return ~(A & B);
    case GateType::NOR:
        return ~(A | B);
    case GateType::NOT:
        return ~A;
    case GateType::BUF:
        return A;
    }
    return 0;
}
__global__ void EvaluateGood(const Gate *Gates, int GateCount, const int *Inputs, int InputCount,
                             const int *Outputs, int OutputCount, const uint32_t *Words,
                             uint32_t *Wires, uint32_t *Good) {
    if (threadIdx.x || blockIdx.x)
        return;
    for (int I = 0; I < InputCount; ++I)
        Wires[Inputs[I]] = Words[I];
    for (int I = 0; I < GateCount; ++I) {
        Gate G = Gates[I];
        Wires[G.Output] = Eval(G.Type, Wires[G.InputA], G.InputB < 0 ? 0 : Wires[G.InputB]);
    }
    for (int O = 0; O < OutputCount; ++O)
        Good[O] = Wires[Outputs[O]];
}
// Wire-major scratch coalesces accesses across faults and avoids per-thread local arrays.
__global__ void Evaluate(const Gate *Gates, int GateCount, const int *Inputs, int InputCount,
                         const int *Outputs, int OutputCount, const Fault *Faults, int FaultCount,
                         const uint32_t *Words, const uint32_t *Good, uint32_t *Scratch,
                         uint32_t *Masks, uint32_t Valid) {
    int Task = blockIdx.x * blockDim.x + threadIdx.x;
    if (Task >= FaultCount)
        return;
    Fault F = Faults[Task];
    for (int I = 0; I < InputCount; ++I)
        Scratch[size_t(Inputs[I]) * FaultCount + Task] =
            Inputs[I] == F.Wire ? (F.Value ? ~0U : 0U) : Words[I];
    for (int I = 0; I < GateCount; ++I) {
        Gate G = Gates[I];
        uint32_t A = Scratch[size_t(G.InputA) * FaultCount + Task];
        uint32_t B = G.InputB < 0 ? 0 : Scratch[size_t(G.InputB) * FaultCount + Task];
        Scratch[size_t(G.Output) * FaultCount + Task] =
            G.Output == F.Wire ? (F.Value ? ~0U : 0U) : Eval(G.Type, A, B);
    }
    uint32_t Mask = 0;
    for (int O = 0; O < OutputCount; ++O)
        Mask |= Good[O] ^ Scratch[size_t(Outputs[O]) * FaultCount + Task];
    Masks[Task] = Mask & Valid;
}
} // namespace
std::vector<uint32_t> GpuMasks(const Circuit &C, const Patterns &P,
                               const std::vector<Fault> &Faults, bool Drop, int BlockSize,
                               GpuMetrics *Metrics) {
    if (BlockSize < 32 || BlockSize > 1024 || BlockSize % 32)
        throw std::invalid_argument("Block size must be a multiple of 32 in 32..1024");
    if (P.InputCount != C.Inputs.size() || P.Words.size() != P.Batches() * P.InputCount)
        throw std::invalid_argument("Invalid packed patterns");
    auto Start = std::chrono::steady_clock::now();
    GpuMetrics M;
    std::vector<uint32_t> Result(Faults.size() * P.Batches());
    if (Faults.empty())
        return Result;
    // Bound global scratch to 128 MiB; process tiles if the fault set is larger.
    size_t Capacity =
        std::min(Faults.size(), std::max(size_t(1), (size_t(128) << 20) /
                                                        (C.WireNames.size() * sizeof(uint32_t))));
    Buffer<Gate> Gates(C.Gates.size());
    Buffer<int> Inputs(C.Inputs.size()), Outputs(C.Outputs.size());
    Buffer<Fault> DeviceFaults(Capacity);
    Buffer<uint32_t> Words(P.InputCount), Good(C.Outputs.size()),
        Scratch(Capacity * C.WireNames.size()), Masks(Capacity), GoodScratch(C.WireNames.size());
    M.DeviceBytes = C.Gates.size() * sizeof(Gate) +
                    (C.Inputs.size() + C.Outputs.size()) * sizeof(int) + Capacity * sizeof(Fault) +
                    (P.InputCount + C.Outputs.size() + Capacity * C.WireNames.size() + Capacity +
                     C.WireNames.size()) *
                        4;
    Event Begin, End;
    auto Timed = [&](auto Action) {
        Check(cudaEventRecord(Begin.Value));
        Action();
        Check(cudaEventRecord(End.Value));
        Check(cudaEventSynchronize(End.Value));
        float Ms = 0;
        Check(cudaEventElapsedTime(&Ms, Begin.Value, End.Value));
        return double(Ms);
    };
    M.H2DMs += Timed([&] {
        Gates.Upload(C.Gates.data());
        Inputs.Upload(C.Inputs.data());
        Outputs.Upload(C.Outputs.data());
    });
    std::vector<size_t> Active;
    for (size_t F = 0; F < Faults.size(); ++F)
        Active.push_back(F);
    for (size_t B = 0; B < P.Batches() && !Active.empty(); ++B) {
        const auto *HostWords = P.Words.data() + B * P.InputCount;
        M.H2DMs += Timed([&] { Words.Upload(HostWords); });
        M.KernelMs += Timed([&] {
            EvaluateGood<<<1, 32>>>(Gates.Data, int(C.Gates.size()), Inputs.Data,
                                    int(C.Inputs.size()), Outputs.Data, int(C.Outputs.size()),
                                    Words.Data, GoodScratch.Data, Good.Data);
            Check(cudaGetLastError());
        });
        std::vector<size_t> Next;
        for (size_t Offset = 0; Offset < Active.size(); Offset += Capacity) {
            size_t Count = std::min(Capacity, Active.size() - Offset);
            std::vector<Fault> Tile;
            std::vector<uint32_t> HostMasks(Count);
            for (size_t I = 0; I < Count; ++I)
                Tile.push_back(Faults[Active[Offset + I]]);
            // Measurements show transfer/launch overhead dominates small circuits.
            // A single non-dropping tile has an invariant fault list: upload it once.
            if (Drop || B == 0 || Active.size() > Capacity) {
                M.H2DMs += Timed([&] {
                    Check(cudaMemcpy(DeviceFaults.Data, Tile.data(), Count * sizeof(Fault),
                                     cudaMemcpyHostToDevice));
                });
            }
            M.KernelMs += Timed([&] {
                Evaluate<<<(Count + BlockSize - 1) / BlockSize, BlockSize>>>(
                    Gates.Data, int(C.Gates.size()), Inputs.Data, int(C.Inputs.size()),
                    Outputs.Data, int(C.Outputs.size()), DeviceFaults.Data, int(Count), Words.Data,
                    Good.Data, Scratch.Data, Masks.Data, P.Mask(B));
                Check(cudaGetLastError());
            });
            M.D2HMs += Timed([&] {
                Check(cudaMemcpy(HostMasks.data(), Masks.Data, Count * 4, cudaMemcpyDeviceToHost));
            });
            for (size_t I = 0; I < Count; ++I) {
                size_t F = Active[Offset + I];
                Result[F * P.Batches() + B] = HostMasks[I];
                if (!Drop || !HostMasks[I])
                    Next.push_back(F);
            }
        }
        Active.swap(Next);
    }
    M.EndToEndMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    if (Metrics)
        *Metrics = M;
    return Result;
}
} // namespace CircuitForge
