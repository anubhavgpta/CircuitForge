#include "AtpgEngine.h"
#include "Benchmark.h"
#include "CircuitCompiler.h"
#include "FaultCollapser.h"
#include "NetlistParser.h"
#include <cctype>
#include <fstream>
#include <iostream>
#include <set>
using namespace CircuitForge;
size_t Number(const std::string &Text, size_t Max) {
    if (Text.empty())
        throw std::invalid_argument("Empty number");
    for (unsigned char C : Text)
        if (!std::isdigit(C))
            throw std::invalid_argument("Invalid number: " + Text);
    size_t N = std::stoull(Text);
    if (N > Max)
        throw std::invalid_argument("Number out of range");
    return N;
}
int main(int argc, char **argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) {
            std::cout
                << "CircuitForge <simulate|faults|collapse|atpg|benchmark> <netlist> [--cpu|--gpu] "
                   "[--patterns N] [--seed N] [--block-size N] [--fault-dropping] [--output "
                   "report.csv] [--search-limit N] [--repetitions odd-N]\n";
            return 0;
        }
        if (argc < 3)
            throw std::invalid_argument("Command and netlist required");
        std::string Command = argv[1];
        if (Command != "simulate" && Command != "faults" && Command != "collapse" &&
            Command != "atpg" && Command != "benchmark")
            throw std::invalid_argument("Unknown command: " + Command);
        size_t Count = 0, Limit = 100000;
        uint32_t Seed = 1;
        bool Gpu = false, Drop = false, BackendSet = false;
        int Block = 128, Reps = 3;
        std::string Output;
        for (int I = 3; I < argc; ++I) {
            std::string Arg = argv[I];
            if (Arg == "--cpu" || Arg == "--gpu") {
                if (BackendSet)
                    throw std::invalid_argument("Specify one backend");
                BackendSet = true;
                Gpu = Arg == "--gpu";
            } else if (Arg == "--fault-dropping")
                Drop = true;
            else {
                if (I + 1 >= argc)
                    throw std::invalid_argument("Missing option value: " + Arg);
                std::string Value = argv[++I];
                if (Arg == "--output")
                    Output = Value;
                else if (Arg == "--patterns") {
                    Count = Number(Value, 16777216);
                    if (!Count)
                        throw std::invalid_argument("Patterns must be positive");
                } else if (Arg == "--seed")
                    Seed = uint32_t(Number(Value, UINT32_MAX));
                else if (Arg == "--block-size")
                    Block = int(Number(Value, 1024));
                else if (Arg == "--search-limit")
                    Limit = Number(Value, 1000000000);
                else if (Arg == "--repetitions")
                    Reps = int(Number(Value, 101));
                else
                    throw std::invalid_argument("Unknown option: " + Arg);
            }
        }
        if (Block < 32 || Block % 32)
            throw std::invalid_argument("Block size must be a multiple of 32 in 32..1024");
        if (Reps < 1 || Reps % 2 == 0)
            throw std::invalid_argument("Repetitions must be odd and positive");
        if ((Command == "collapse" || Command == "atpg" || Command == "simulate") && (Gpu || Drop))
            throw std::invalid_argument(
                "This command uses CPU and does not support fault dropping");
        auto C = LoadNetlist(argv[2]);
        CompileCircuit(C);
        auto Faults = GenerateFaults(C);
        std::ofstream File;
        if (!Output.empty()) {
            File.open(Output);
            if (!File)
                throw std::runtime_error("Cannot open output file");
        }
        std::ostream &Out = Output.empty() ? std::cout : File;
        if (Command == "collapse") {
            auto Map = CollapseFaults(C);
            Out << "fault_id,wire,stuck,representative\n";
            for (size_t F = 0; F < Map.size(); ++F)
                Out << F << ',' << C.WireNames[Faults[F].Wire] << ',' << Faults[F].Value << ','
                    << Map[F] << '\n';
        } else if (Command == "atpg") {
            Out << "fault_id,wire,stuck,status,nodes,pattern\n";
            for (size_t F = 0; F < Faults.size(); ++F) {
                auto R = GenerateTest(C, Faults[F], Limit);
                Out << F << ',' << C.WireNames[Faults[F].Wire] << ',' << Faults[F].Value << ','
                    << (R.Status == AtpgStatus::Detected          ? "Detected"
                        : R.Status == AtpgStatus::ProvenRedundant ? "ProvenRedundant"
                                                                  : "Aborted")
                    << ',' << R.Nodes << ',';
                for (auto Bit : R.Pattern)
                    Out << int(Bit);
                Out << '\n';
            }
        } else {
            if (!Count)
                Count = C.Inputs.size() <= 16 ? (size_t(1) << C.Inputs.size()) : 1024;
            auto P = GeneratePatterns(C.Inputs.size(), Count, Seed);
            if (Command == "benchmark") {
                Benchmark(C, P, Faults, Gpu, Drop, Block, Reps, Out);
            } else if (Command == "simulate") {
                Out << "pattern";
                for (int W : C.Outputs)
                    Out << ',' << C.WireNames[W];
                Out << '\n';
                for (size_t B = 0; B < P.Batches(); ++B) {
                    auto Values = SimulatePacked(C, P.Words.data() + B * P.InputCount);
                    for (size_t Bit = 0; Bit < 32 && B * 32 + Bit < P.Count; ++Bit) {
                        Out << B * 32 + Bit;
                        for (uint32_t V : Values)
                            Out << ',' << ((V >> Bit) & 1);
                        Out << '\n';
                    }
                }
            } else {
                auto Masks = CpuMasks(C, P, Faults, Drop);
#ifdef CIRCUITFORGE_CUDA
                if (Gpu) {
                    auto Actual = GpuMasks(C, P, Faults, Drop, Block);
                    if (Actual != Masks)
                        throw std::runtime_error("CPU/GPU mask mismatch");
                    Masks = std::move(Actual);
                }
#else
                if (Gpu)
                    throw std::runtime_error("CUDA backend not built");
#endif
                auto Status = SummarizeFaults(Faults, P, Masks);
                Out << "fault_id,wire,stuck,detected,first_pattern,"
                    << (Drop ? "observed_detecting_patterns" : "detecting_patterns") << "\n";
                size_t Detected = 0;
                for (const auto &S : Status) {
                    Detected += S.Detected;
                    Out << S.FaultId << ',' << C.WireNames[S.Site.Wire] << ',' << S.Site.Value
                        << ',' << S.Detected << ',';
                    if (S.Detected)
                        Out << S.FirstPattern;
                    Out << ',' << S.DetectingPatterns << '\n';
                }
                std::cerr << "Coverage: " << Detected << '/' << Faults.size() << " ("
                          << 100.0 * Detected / Faults.size() << "%)\n";
            }
        }
        if (!Out)
            throw std::runtime_error("Output write failed");
        return 0;
    } catch (const std::exception &E) {
        std::cerr << "Error: " << E.what() << '\n';
        return 1;
    }
}
