#include "PatternGenerator.h"
#include <limits>
#include <random>
namespace CircuitForge {
Patterns GeneratePatterns(size_t Inputs, size_t Count, uint32_t Seed) {
    if (!Inputs || !Count || Count > 16777216 || Inputs > 1000000)
        throw std::invalid_argument("Invalid pattern dimensions");
    Patterns P;
    P.Count = Count;
    P.InputCount = Inputs;
    P.Seed = Seed;
    if (P.Batches() > std::numeric_limits<size_t>::max() / Inputs)
        throw std::overflow_error("Pattern allocation overflow");
    P.Words.resize(P.Batches() * Inputs);
    P.Exhaustive = Inputs < 25 && Count == (size_t(1) << Inputs);
    std::mt19937 Random(Seed);
    for (size_t Batch = 0; Batch < P.Batches(); ++Batch)
        for (size_t Input = 0; Input < Inputs; ++Input) {
            uint32_t Word = 0;
            if (P.Exhaustive) {
                for (size_t Bit = 0; Bit < 32 && Batch * 32 + Bit < Count; ++Bit)
                    Word |= uint32_t(((Batch * 32 + Bit) >> (Inputs - Input - 1)) & 1) << Bit;
            } else
                Word = Random();
            P.Words[Batch * Inputs + Input] = Word & P.Mask(Batch);
        }
    return P;
}
} // namespace CircuitForge
