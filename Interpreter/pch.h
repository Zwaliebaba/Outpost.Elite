// Interpreter/pch.h
#pragma once

// Standard library only: the interpreter compiles with MSVC in the solution and with GCC or Clang
// outside it, and includes no Windows header (ADR-004).
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>
