// Machine/pch.h
#pragma once

// Standard library only: Machine compiles with MSVC in the solution and with GCC or Clang
// outside it, and includes no Windows header (ADR-004).
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
