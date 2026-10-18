#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
namespace CircuitForge {
enum class GateType : int { AND, OR, XOR, NAND, NOR, XNOR, NOT, BUF };

struct Gate {
    GateType Type;
    int InputA;
    int InputB;
    int Output;
};

struct Fault {
    int Wire;
    int Value;
};

struct Circuit {
    std::vector<std::string> WireNames;
    std::unordered_map<std::string, int> WireIds;
    std::vector<int> Inputs;
    std::vector<int> Outputs;
    std::vector<Gate> Gates;
    std::vector<int> Levels;

    int GetWire(const std::string &Name) {
        auto It = WireIds.find(Name);

        if (It != WireIds.end()) {
            return It->second;
        }

        if (WireNames.size() >= 1000000) {
            throw std::runtime_error("Exceeded MaxWires");
        }

        int Id = static_cast<int>(WireNames.size());

        WireNames.push_back(Name);
        WireIds[Name] = Id;

        return Id;
    }
};

} // namespace CircuitForge
