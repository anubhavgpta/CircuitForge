#pragma once
#include "Circuit.h"
namespace CircuitForge {
Circuit LoadNetlist(const std::string &Filename);
bool IsUnary(GateType Type);
} // namespace CircuitForge
