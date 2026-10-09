#include "pch.h"

#include "TestReader.h"
#include "TestRunner.h"

#include <charconv>
#include <cstdio>
#include <exception>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Runs the SingleStepTests 8088 suite against Machine's interpreter (ADR-003, ADR-004). The suite is
// fetched and converted by Tools/CpuConformance.py; this executable reads the converted form.
//
//   CpuConformance [--failures N] FILE...
//
// For each file it prints the first N failing tests in detail (default 5), then one line
//   RESULT <tab> <file> <tab> <passed> <tab> <total>
// which the script collects. Exit status: 0 every test passed, 1 failures, 2 usage or file errors.

namespace
{

constexpr char USAGE[] = "usage: CpuConformance [--failures N] FILE...\n";

void Print(const std::string& _text)
{
  std::fputs(_text.c_str(), stdout);
}

int Run(int _argc, char** _argv)
{
  std::size_t failureLimit = 5;
  std::vector<std::string> files;
  for (int index = 1; index < _argc; ++index)
  {
    const std::string_view argument = _argv[index];
    if (argument == "--failures" && index + 1 < _argc)
    {
      const std::string_view value = _argv[++index];
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), failureLimit);
      if (error != std::errc{} || end != value.data() + value.size())
      {
        std::fputs(USAGE, stderr);
        return 2;
      }
    }
    else if (argument.starts_with("--"))
    {
      std::fputs(USAGE, stderr);
      return 2;
    }
    else
    {
      files.emplace_back(argument);
    }
  }
  if (files.empty())
  {
    std::fputs(USAGE, stderr);
    return 2;
  }

  // The runner holds 1 MiB of address space and a 1 MiB zero page; keep them off the stack.
  const auto runner = std::make_unique<CpuConformance::TestRunner>();
  bool allPassed = true;
  bool readError = false;
  for (const std::string& file : files)
  {
    CpuConformance::TestReader reader(file);
    if (!reader.IsOpen())
    {
      Print(std::format("ERROR\t{}\tcannot open\n", file));
      readError = true;
      continue;
    }
    std::size_t passed = 0;
    std::size_t total = 0;
    CpuConformance::TestCase test;
    while (reader.Next(test))
    {
      ++total;
      const std::vector<std::string> mismatches = runner->Run(test);
      if (mismatches.empty())
      {
        ++passed;
        continue;
      }
      if (total - passed <= failureLimit)
      {
        Print(std::format("FAIL\t{}\t#{} {} [{}]\n", file, test.index, test.name, test.bytes));
        for (const std::string& mismatch : mismatches)
        {
          Print(std::format("  {}\n", mismatch));
        }
      }
    }
    if (!reader.Error().empty())
    {
      Print(std::format("ERROR\t{}\t{}\n", file, reader.Error()));
      readError = true;
    }
    Print(std::format("RESULT\t{}\t{}\t{}\n", file, passed, total));
    std::fflush(stdout);
    allPassed = allPassed && passed == total;
  }
  if (readError)
  {
    return 2;
  }
  return allPassed ? 0 : 1;
}

} // namespace

int main(int _argc, char** _argv)
{
  try
  {
    return Run(_argc, _argv);
  }
  catch (const std::exception& error)
  {
    std::fprintf(stderr, "CpuConformance: %s\n", error.what());
  }
  catch (...)
  {
    std::fputs("CpuConformance: unexpected exception\n", stderr);
  }
  return 2;
}
