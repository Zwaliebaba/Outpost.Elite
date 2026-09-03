#pragma once

/*
 * A runnable stand-in for MSVC's CppUnitTest, so that the repository's own test files execute on
 * a machine without Visual Studio.
 *
 * The point is that NOTHING under Tests/GameLogicTests/ changes. Those files are written against
 * CppUnitTest and built by MSBuild; this header gives the same names different bodies, and the
 * generated runner (see ../generate_runner.py) calls the test methods directly. So there is one
 * set of test sources and two ways to run them, rather than two suites that can disagree.
 *
 * What is deliberately NOT here: fixtures (TEST_METHOD_INITIALIZE and friends), data-driven
 * attributes, the CppUnitTest string formatters, and the automatic registration MSVC does
 * through its section pragmas. The suites do not use any of them, and a shim that pretended to
 * would be a second framework to maintain. If a test ever needs one, the honest fix is to add it
 * here and say so -- not to write the test differently for the two runners.
 *
 * Assertions throw. The generated runner catches, counts and prints, which is why a failing
 * assertion reports one test rather than ending the run.
 */

#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>

/// Distinct from every exception the code under test might throw, so the runner can tell a
/// failed assertion from a genuine crash in the port.
struct ShimFailure : std::runtime_error
{
  using std::runtime_error::runtime_error;
};

/*
 * The suites pass wide literals as assertion messages, because that is what CppUnitTest takes.
 * Narrowing them by hand rather than through <locale> keeps this header free of anything that
 * behaves differently between libstdc++ and the MSVC CRT; the messages are ASCII, and a
 * character that is not says so with a '?' instead of silently disappearing.
 */
inline std::string ShimNarrow(const wchar_t* _wide)
{
  std::string narrow;
  if (_wide == nullptr)
  {
    return narrow;
  }
  for (; *_wide != 0; ++_wide)
  {
    narrow += static_cast<char>(*_wide < 128 ? *_wide : '?');
  }
  return narrow;
}

/*
 * A value in a failure message.
 *
 * Byte-sized types are printed as numbers rather than as characters, which is the whole reason
 * this exists: nearly every assertion in the suite compares std::uint8_t, and `operator<<` would
 * report an expected 10 as a line break.
 */
template <typename T>
inline std::string ShimShow(const T& _value)
{
  std::ostringstream out;
  if constexpr (sizeof(T) == 1 && !std::is_same_v<T, char>)
  {
    out << static_cast<unsigned>(static_cast<unsigned char>(_value));
  }
  else
  {
    out << _value;
  }
  return out.str();
}

namespace Microsoft::VisualStudio::CppUnitTestFramework
{

class Logger
{
public:
  static void WriteMessage(const char* _message)
  {
    std::fputs(_message, stdout);
    std::fputc('\n', stdout);
  }
};

class Assert
{
public:
  template <typename T>
  static void AreEqual(const T& _expected, const T& _actual, const wchar_t* _message = nullptr)
  {
    if (!(_expected == _actual))
    {
      throw ShimFailure("AreEqual: expected " + ShimShow(_expected) + " actual " + ShimShow(_actual) + " -- "
                        + ShimNarrow(_message));
    }
  }

  template <typename T>
  static void AreNotEqual(const T& _expected, const T& _actual, const wchar_t* _message = nullptr)
  {
    if (_expected == _actual)
    {
      throw ShimFailure("AreNotEqual: both " + ShimShow(_expected) + " -- " + ShimNarrow(_message));
    }
  }

  static void IsTrue(bool _condition, const wchar_t* _message = nullptr)
  {
    if (!_condition)
    {
      throw ShimFailure("IsTrue -- " + ShimNarrow(_message));
    }
  }

  static void IsFalse(bool _condition, const wchar_t* _message = nullptr)
  {
    if (_condition)
    {
      throw ShimFailure("IsFalse -- " + ShimNarrow(_message));
    }
  }

  [[noreturn]] static void Fail(const wchar_t* _message = nullptr)
  {
    throw ShimFailure("Fail -- " + ShimNarrow(_message));
  }
};

} // namespace Microsoft::VisualStudio::CppUnitTestFramework

/*
 * MSVC's TEST_CLASS registers the class with the test platform through a linker section. Here it
 * is a plain struct and the generated runner names it explicitly, which is why generate_runner.py
 * has to parse the test files for these two macros.
 */
#define TEST_CLASS(NAME) struct NAME
#define TEST_METHOD(NAME) void NAME()
