#include "AtpgEngine.h"
#include "CircuitCompiler.h"
#include "CpuSimulator.h"
#include "FaultCollapser.h"
#include "GpuSimulator.cuh"
#include "NetlistParser.h"
#include <fstream>
#include <iostream>
#include <random>
using namespace CircuitForge;
void Require(bool Value, const std::string &What) {
    if (!Value)
        throw std::runtime_error(What);
}
Circuit Parse(const std::string &Text) {
    std::ofstream("build-test-netlist.txt") << Text;
    auto C = LoadNetlist("build-test-netlist.txt");
    CompileCircuit(C);
    return C;
}
void Reject(const std::string &Text) {
    bool Failed = false;
    try {
        Parse(Text);
    } catch (const std::exception &) {
        Failed = true;
    }
    Require(Failed, "Expected netlist rejection: " + Text);
}
void Verify(const Circuit &C, size_t Count) {
    auto P = GeneratePatterns(C.Inputs.size(), Count, 42);
    std::vector<Fault> Faults;
    for (int W = 0; W < int(C.WireNames.size()); ++W)
        for (int V = 0; V < 2; ++V)
            Faults.push_back({W, V});
    auto Masks = CpuMasks(C, P, Faults);
    auto Full = SummarizeFaults(Faults, P, Masks),
         Dropped = SummarizeFaults(Faults, P, CpuMasks(C, P, Faults, true));
    for (size_t F = 0; F < Faults.size(); ++F)
        Require(Full[F].Detected == Dropped[F].Detected &&
                    Full[F].FirstPattern == Dropped[F].FirstPattern,
                "Dropping status " + std::to_string(F));
    if (P.Exhaustive)
        for (size_t F = 0; F < Faults.size(); ++F) {
            auto A = GenerateTest(C, Faults[F], 10000);
            Require(A.Status ==
                        (Full[F].Detected ? AtpgStatus::Detected : AtpgStatus::ProvenRedundant),
                    "ATPG completeness " + std::to_string(F));
            if (A.Status == AtpgStatus::Detected)
                Require(SimulateScalar(C, A.Pattern) != SimulateScalar(C, A.Pattern, Faults[F]),
                        "ATPG pattern");
            Require(GenerateTest(C, Faults[F], 0).Status == AtpgStatus::Aborted, "ATPG limit");
        }
    auto Classes = CollapseFaults(C);
    for (size_t F = 0; F < Faults.size(); ++F)
        for (size_t B = 0; B < P.Batches(); ++B)
            Require(Masks[F * P.Batches() + B] == Masks[Classes[F] * P.Batches() + B],
                    "Collapse soundness " + std::to_string(F));
    for (size_t B = 0; B < P.Batches(); ++B)
        for (size_t Bit = 0; Bit < 32 && B * 32 + Bit < Count; ++Bit) {
            std::vector<uint8_t> In;
            for (size_t I = 0; I < P.InputCount; ++I)
                In.push_back((P.Words[B * P.InputCount + I] >> Bit) & 1);
            auto Good = SimulateScalar(C, In);
            auto Packed = SimulatePacked(C, P.Words.data() + B * P.InputCount);
            for (size_t O = 0; O < Good.size(); ++O)
                Require(Good[O] == ((Packed[O] >> Bit) & 1), "Scalar/packed output");
            for (size_t F = 0; F < Faults.size(); ++F)
                Require((Good != SimulateScalar(C, In, Faults[F])) ==
                            bool((Masks[F * P.Batches() + B] >> Bit) & 1),
                        "Detection fault " + std::to_string(F) + " pattern " +
                            std::to_string(B * 32 + Bit));
        }
    for (size_t F = 0; F < Faults.size(); ++F)
        Require((Masks[F * P.Batches() + P.Batches() - 1] & ~P.Mask(P.Batches() - 1)) == 0,
                "Partial batch");
#ifdef CIRCUITFORGE_CUDA
    Require(GpuMasks(C, P, Faults) == Masks, "GPU full masks");
    Require(GpuMasks(C, P, Faults, true) == CpuMasks(C, P, Faults, true), "GPU dropping masks");
#endif
}
int main() {
    try {
        const char *Names[] = {"AND", "OR", "XOR", "NAND", "NOR", "XNOR", "NOT", "BUF"};
        for (int T = 0; T < 8; ++T) {
            auto C = Parse(std::string("INPUT A B\nOUTPUT Z\n") + Names[T] + " Z A" +
                           (T >= 6 ? "" : " B") + "\n");
            for (int A = 0; A < 2; ++A)
                for (int B = 0; B < 2; ++B) {
                    bool Expected[] = {bool(A & B),  bool(A | B),  bool(A ^ B), !bool(A & B),
                                       !bool(A | B), !bool(A ^ B), !bool(A),    bool(A)};
                    Require(SimulateScalar(C, {uint8_t(A), uint8_t(B)})[0] == uint8_t(Expected[T]),
                            Names[T]);
                }
            Verify(C, 4);
            Verify(C, 33);
        }
        for (const char *Name : {"Example", "HalfAdder", "FullAdder"}) {
            auto C = LoadNetlist(std::string("circuits/") + Name + ".txt");
            CompileCircuit(C);
            Verify(C, size_t(1) << C.Inputs.size());
            Verify(C, 65);
        }
        auto Full = LoadNetlist("circuits/FullAdder.txt");
        CompileCircuit(Full);
        for (int A = 0; A < 2; ++A)
            for (int B = 0; B < 2; ++B)
                for (int Ci = 0; Ci < 2; ++Ci) {
                    auto O = SimulateScalar(Full, {uint8_t(A), uint8_t(B), uint8_t(Ci)});
                    Require(O[0] == ((A + B + Ci) % 2) && O[1] == ((A + B + Ci) / 2),
                            "Full adder truth table");
                }
        auto Sorted = Parse("INPUT A B\nOUTPUT Z\nNOT Z T\nAND T A B\n");
        Require(Sorted.Gates[0].Type == GateType::AND && Sorted.Levels[1] == 2,
                "Topology and levels");
        Verify(Parse("INPUT(a)\nINPUT(b)\nOUTPUT(z)\nz = XNOR(a,b)\n"), 31);
        Verify(Parse("INPUT A\nOUTPUT A\n"), 33);
        Reject("INPUT A\nOUTPUT Z\nNOT Z T\nNOT T Z\n");
        Reject("INPUT A\nOUTPUT Z\nAND Z A X\n");
        Reject("INPUT A\nOUTPUT Z\nBUF Z A\nNOT Z A\n");
        Reject("INPUT A\nOUTPUT Z\nDFF Z A\n");
        Reject("INPUT A\nOUTPUT Z\nNOT Z A B\n");
        Reject("INPUT(a)\nOUTPUT(z)\nz=AND(a,)\n");
        Verify(Parse("INPUT A B\nOUTPUT Z\nXOR T A A\nAND Z T B\n"), 4);
        // Exhaustive random DAGs exercise reconvergence, repeated fanins and fanout restrictions.
        std::mt19937 Random(9123);
        for (int Trial = 0; Trial < 30; ++Trial) {
            std::string Text = "INPUT i0 i1 i2\nOUTPUT n7 n6\n";
            std::vector<std::string> W = {"i0", "i1", "i2"};
            for (int G = 0; G < 8; ++G) {
                int T = Random() % 8;
                Text +=
                    std::string(Names[T]) + " n" + std::to_string(G) + " " + W[Random() % W.size()];
                if (T < 6)
                    Text += " " + W[Random() % W.size()];
                Text += "\n";
                W.push_back("n" + std::to_string(G));
            }
            Verify(Parse(Text), 8);
        }
        auto BadRef = Full;
        BadRef.Gates[0].InputA = 99999;
        bool Rejected = false;
        try {
            CompileCircuit(BadRef);
        } catch (const std::exception &) {
            Rejected = true;
        }
        Require(Rejected, "Invalid integer wire reference");
        Require(!GeneratePatterns(3, 65, 1).Exhaustive,
                "Sampling instead of repeated exhaustive cycles");
        Require(GeneratePatterns(40, 65, 1).Words != GeneratePatterns(40, 65, 2).Words,
                "Seed changes patterns");
        std::cout << "Core scalar/packed/parser tests passed\n";
        return 0;
    } catch (const std::exception &E) {
        std::cerr << E.what() << '\n';
        return 1;
    }
}
