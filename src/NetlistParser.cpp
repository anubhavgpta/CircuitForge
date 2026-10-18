#include "NetlistParser.h"
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>
namespace CircuitForge {
GateType ParseType(const std::string &Name) {
    if (Name == "AND")
        return GateType::AND;
    if (Name == "OR")
        return GateType::OR;
    if (Name == "XOR")
        return GateType::XOR;
    if (Name == "NAND")
        return GateType::NAND;
    if (Name == "NOR")
        return GateType::NOR;
    if (Name == "XNOR")
        return GateType::XNOR;
    if (Name == "NOT")
        return GateType::NOT;
    if (Name == "BUF")
        return GateType::BUF;

    throw std::runtime_error("Unknown gate: " + Name);
}

bool IsUnary(GateType Type) {
    return Type == GateType::NOT || Type == GateType::BUF;
}

Circuit LoadNetlist(const std::string &Filename) {
    std::ifstream File(Filename);

    if (!File) {
        throw std::runtime_error("Cannot open: " + Filename);
    }

    Circuit C;
    std::string Line;
    int LineNumber = 0;

    while (std::getline(File, Line)) {
        try {
            ++LineNumber;

            size_t Comment = Line.find('#');

            if (Comment != std::string::npos) {
                Line.erase(Comment);
            }

            if (Line.find('(') != std::string::npos) {
                std::smatch Match;
                if (std::regex_match(
                        Line, Match,
                        std::regex(R"(\s*(INPUT|OUTPUT)\s*\(\s*([A-Za-z0-9_.$]+)\s*\)\s*)"))) {
                    Line = Match[1].str() + " " + Match[2].str();
                } else if (std::regex_match(
                               Line, Match,
                               std::regex(
                                   R"(\s*([A-Za-z0-9_.$]+)\s*=\s*([A-Z]+)\s*\(([^()]*)\)\s*)"))) {
                    std::string Args = Match[3];
                    if (!std::regex_match(
                            Args, std::regex(R"(\s*[A-Za-z0-9_.$]+\s*(,\s*[A-Za-z0-9_.$]+\s*)*)")))
                        throw std::runtime_error("Malformed input list");
                    std::replace(Args.begin(), Args.end(), ',', ' ');
                    Line = Match[2].str() + " " + Match[1].str() + " " + Args;
                } else
                    throw std::runtime_error("Malformed bench syntax");
            }
            std::istringstream Stream(Line);
            std::string Command;

            if (!(Stream >> Command)) {
                continue;
            }

            if (Command == "INPUT" || Command == "OUTPUT") {
                std::string Name;
                int Added = 0;

                auto &List = Command == "INPUT" ? C.Inputs : C.Outputs;

                while (Stream >> Name) {
                    if (!std::regex_match(Name, std::regex(R"([A-Za-z0-9_.$]+)")))
                        throw std::runtime_error("Invalid wire name: " + Name);
                    int Wire = C.GetWire(Name);

                    if (std::find(List.begin(), List.end(), Wire) != List.end()) {
                        throw std::runtime_error("Duplicate declaration at line " +
                                                 std::to_string(LineNumber));
                    }

                    List.push_back(Wire);
                    ++Added;
                }

                if (Added == 0) {
                    throw std::runtime_error("Empty declaration at line " +
                                             std::to_string(LineNumber));
                }

                continue;
            }

            GateType Type = ParseType(Command);

            std::string Out, A, B, Extra;

            if (!(Stream >> Out >> A)) {
                throw std::runtime_error("Incomplete gate at line " + std::to_string(LineNumber));
            }

            bool Unary = IsUnary(Type);

            if (!Unary && !(Stream >> B)) {
                throw std::runtime_error("Missing gate input at line " +
                                         std::to_string(LineNumber));
            }

            if (Stream >> Extra) {
                throw std::runtime_error("Extra gate argument at line " +
                                         std::to_string(LineNumber));
            }

            for (const auto &Name : {Out, A, Unary ? A : B})
                if (!std::regex_match(Name, std::regex(R"([A-Za-z0-9_.$]+)")))
                    throw std::runtime_error("Invalid wire name: " + Name);
            int Output = C.GetWire(Out);
            int InputA = C.GetWire(A);
            int InputB = Unary ? -1 : C.GetWire(B);

            C.Gates.push_back({Type, InputA, InputB, Output});
        } catch (const std::exception &Error) {
            throw std::runtime_error("line " + std::to_string(LineNumber) + ": " + Error.what());
        }
    }

    if (C.Inputs.empty() || C.Outputs.empty()) {
        throw std::runtime_error("INPUT and OUTPUT declarations required");
    }

    return C;
}

} // namespace CircuitForge
