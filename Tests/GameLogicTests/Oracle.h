#pragma once

#include "Cpu6502.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

/*
 * What answers a call to the original (Design/Modernize.md section 4.10, slice M6-a-2).
 *
 * Every oracle test has the same shape: build a 6502 state, call a routine, read memory back,
 * compare with the port. `Cpu6502::CallSubroutine` is the one place all of them go through, and
 * that is the seam the detachment uses -- THE TESTS DO NOT CHANGE SHAPE; WHAT ANSWERS THEM DOES.
 *
 * THE PLAN DREW THE SEAM AS `State Call(label, const State&)` AND THAT IS NOT THIS TREE'S SHAPE.
 * A test does not hand the oracle a state and take one back: it takes a whole machine from
 * `OracleImage::Fresh()`, scribbles in its 64K, arms traps and a store log, runs a routine and then
 * reads whatever it likes back out of the machine. Nothing names which bytes are the inputs and
 * nothing names which are the outputs, so a record keyed by "the inputs" cannot be built without
 * rewriting every fixture -- which is what section 4.10 promises not to do. So the record is keyed
 * by a digest of the WHOLE machine as the call found it, and holds the DIFFERENCE the call made:
 * content-addressed, order-independent, and the same on any machine that runs the suite.
 *
 * The three oracles, in the order the phase installs them:
 *
 *   LiveOracle       the interpreter, named. Installing it is the same as installing nothing.
 *   RecordingOracle  runs the interpreter and writes down what it answered (M6-a).
 *   RecordedOracle   serves the fixture, with no interpreter in the tree at all (M6-b).
 */

namespace Elite::Testing
{

  class Oracle
  {
  public:
    Oracle() = default;
    Oracle(const Oracle&) = delete;
    Oracle& operator=(const Oracle&) = delete;
    virtual ~Oracle() = default;

    virtual RunResult Call(Cpu6502& _cpu, std::uint16_t _address, std::uint32_t _maxInstructions,
                           std::uint16_t _stopAddress) noexcept = 0;

    /// What `CallSubroutine` goes to. Null is the interpreter, which is the default and what every
    /// test has always had.
    [[nodiscard]] static Oracle* Current() noexcept;
    static void Install(Oracle* _oracle) noexcept;
  };

  /// The interpreter, named so that a run can say which oracle answered it.
  class LiveOracle final : public Oracle
  {
  public:
    RunResult Call(Cpu6502& _cpu, std::uint16_t _address, std::uint32_t _maxInstructions, std::uint16_t _stopAddress) noexcept override
    {
      return _cpu.Interpret(_address, _maxInstructions, _stopAddress);
    }
  };

  /*
   * The difference one call made to the machine, which is everything a fixture has to be able to
   * put back.
   *
   * Memory and the I/O page are stored as the bytes that CHANGED, because a routine touches tens of
   * bytes out of sixty-eight thousand. The registers and the flags are stored whole because they
   * are eleven bytes. The trap hits and the store log are APPENDED rather than replaced: a fixture
   * that clears them would lose the hits a test accumulated across several calls, which several do.
   */
  struct CallRecord
  {
    std::vector<std::pair<std::uint16_t, std::uint8_t>> memory; ///< RAM the call wrote
    std::vector<std::pair<std::uint16_t, std::uint8_t>> io;     ///< the I/O page it wrote

    std::uint8_t a = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    std::uint8_t sp = 0;
    std::uint16_t pc = 0;
    std::uint8_t status = 0;      ///< the six flags as `StatusByte` packs them
    std::uint64_t cycles = 0;     ///< what the call ADDED, so a caller's own count is untouched

    bool completed = false;
    bool illegalOpcode = false;
    std::uint32_t instructions = 0;
    std::uint16_t stoppedAt = 0;

    std::vector<Cpu6502::TrapHit> trapHits; ///< appended to whatever the caller was holding
    std::vector<Cpu6502::StoreHit> stores;  ///< the same

    /// Bytes this record costs in the fixture file, which is what M6-a's threshold is measured in.
    [[nodiscard]] std::size_t Bytes() const noexcept;
  };

  /// A 64-bit FNV-1a of everything about the machine a call can read, which is the record's key.
  [[nodiscard]] std::uint64_t CallDigest(const Cpu6502& _cpu, std::uint16_t _address, std::uint32_t _maxInstructions,
                                         std::uint16_t _stopAddress) noexcept;

  /*
   * Runs the interpreter and writes down what it answered (M6-a).
   *
   * `Measure` is the first half of the row's acceptance and the reason this class has a mode: the
   * threshold section 4.10 asks for cannot be chosen before the corpus is measured, and measuring
   * it means running the whole suite through a recorder that keeps sizes rather than records. It
   * costs one pass and no memory.
   */
  class RecordingOracle final : public Oracle
  {
  public:
    enum class Mode
    {
      Measure, ///< count the calls and add up what they would cost; keep nothing
      Keep,    ///< keep every distinct call, to be written out at the end
    };

    explicit RecordingOracle(Mode _mode) noexcept
      : m_mode(_mode)
    {
    }

    RunResult Call(Cpu6502& _cpu, std::uint16_t _address, std::uint32_t _maxInstructions, std::uint16_t _stopAddress) noexcept override;

    /*
     * The record-size histogram, in powers of two from 64 bytes: bucket 0 is a record of 64 bytes
     * or fewer, bucket 1 up to 128, and the last is everything above 64K.
     *
     * It is here because section 4.10's threshold cannot be guessed. The question the buckets
     * answer is which records the corpus is MADE of -- if the bytes are in a few enormous ones, a
     * threshold buys a lot; if they are spread evenly, it buys nothing.
     */
    static constexpr std::size_t BUCKETS = 11;
    static constexpr std::size_t SMALLEST_BUCKET = 64;

    struct Totals
    {
      std::uint64_t calls = 0;    ///< every `CallSubroutine` the suite made
      std::uint64_t distinct = 0; ///< how many of them had an input no earlier call had
      std::uint64_t bytes = 0;    ///< what the distinct ones would cost as records
      std::uint64_t largest = 0;  ///< the biggest single record
      std::uint64_t memoryWrites = 0;
      std::uint64_t collisions = 0; ///< distinct inputs that produced different answers: must be 0
      std::uint64_t bucketCalls[BUCKETS] = {};
      std::uint64_t bucketBytes[BUCKETS] = {};

      /*
       * The read census (M6-b-1), which sizes the tail §1 R-g accepts. `imageReads` counts data
       * reads of a byte nothing had written, so a record keyed on the write set does not name it;
       * `callsOnImage` counts the calls that made at least one, which is the number that says how
       * much of the corpus answers from the original rather than from its inputs. `callsPure` is
       * its complement and is the fraction a write-set key covers outright.
       */
      std::uint64_t reads = 0;
      std::uint64_t imageReads = 0;
      std::uint64_t imageContentReads = 0; ///< of those, the ones where the image's byte is not zero
      std::uint64_t callsOnImage = 0;
      std::uint64_t callsOnContent = 0;
      std::uint64_t callsPure = 0;
    };

    /// The record size at which `Keep` stops keeping. Zero keeps everything, which is what the
    /// measuring pass runs with.
    void KeepUnder(std::size_t _bytes) noexcept
    {
      m_threshold = _bytes;
    }

    [[nodiscard]] const Totals& Read() const noexcept
    {
      return m_totals;
    }

    /// Every distinct call, in digest order, which is what makes two recording runs produce
    /// identical files. Empty in `Measure` mode.
    [[nodiscard]] const std::unordered_map<std::uint64_t, CallRecord>& Records() const noexcept
    {
      return m_records;
    }

  private:
    Mode m_mode;
    std::size_t m_threshold = 0;
    Totals m_totals;
    std::unordered_map<std::uint64_t, CallRecord> m_records;
    std::unordered_map<std::uint64_t, std::uint64_t> m_answers; ///< digest -> a digest of the answer
  };

  /*
   * The fixture file: `Tests/Fixtures/Oracle.fixture` (M6-a-2).
   *
   * ONE FILE AND NOT ONE PER SUITE, which is where this parts from section 4.10's sketch. A record
   * is addressed by what the call ASKED, not by which test asked it, so the same question asked by
   * two suites is one record -- and the recorder has no way to know whose test it is in anyway:
   * MSVC's test platform does not tell a fixture its own name, and inventing a way to tell it would
   * be the shape change section 4.10 promises not to make. Keying by content also makes the file
   * REPRODUCIBLE: the records are written in key order, so two recording runs, on two machines, in
   * two compilers, produce the same bytes.
   *
   * Little-endian throughout and written a byte at a time, so the file does not depend on the
   * machine that wrote it.
   */
  [[nodiscard]] bool WriteFixture(const RecordingOracle& _recorder, const char* _path, std::string& _error);

} // namespace Elite::Testing
