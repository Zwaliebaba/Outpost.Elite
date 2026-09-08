#include "pch.h"

#include "Oracle.h"

#include <algorithm>
#include <fstream>
#include <string>

namespace Elite::Testing
{

  namespace
  {

    /// A process-wide seam, because `Cpu6502` is created by value all over the suite and a test
    /// never names its oracle. The runner installs one for the whole run or none at all.
    Oracle* g_oracle = nullptr;

    /// The fixture's own header, so a stale or foreign file says so rather than being read as
    /// records. The version moves when the layout below does, and a fixture is never re-recorded
    /// (rule 1), so it moves exactly once per format change.
    constexpr const char* FIXTURE_MAGIC = "OUTPOST-ORACLE\n";
    constexpr std::uint32_t FIXTURE_VERSION = 1;

    constexpr std::uint64_t FNV_OFFSET = 1469598103934665603ull;
    constexpr std::uint64_t FNV_PRIME = 1099511628211ull;

    inline void Fold(std::uint64_t& _digest, const void* _bytes, std::size_t _count) noexcept
    {
      const std::uint8_t* at = static_cast<const std::uint8_t*>(_bytes);
      for (std::size_t index = 0; index < _count; ++index)
      {
        _digest ^= at[index];
        _digest *= FNV_PRIME;
      }
    }

    template <typename Value>
    inline void FoldValue(std::uint64_t& _digest, const Value& _value) noexcept
    {
      Fold(_digest, &_value, sizeof(Value));
    }

    /// The bytes a record costs on disk, in the format `RecordedOracle` will read: a fixed header
    /// and then the three variable lists.
    constexpr std::size_t RECORD_HEADER_BYTES = 8   // the key
                                                + 4 // how many memory writes
                                                + 4 // how many I/O writes
                                                + 4 // how many trap hits
                                                + 4 // how many store hits
                                                + 6 // a, x, y, sp, status and the two result flags
                                                + 2 // pc
                                                + 8 // cycles
                                                + 4 // instructions
                                                + 2 // stoppedAt
      ;
    constexpr std::size_t MEMORY_WRITE_BYTES = 3;                  // address and value
    constexpr std::size_t TRAP_HIT_BYTES = 2 + 3 + 1 + Cpu6502::WATCH_SLOTS; // address, AXY, carry, watched
    constexpr std::size_t STORE_HIT_BYTES = 3;                     // address and value

  } // namespace

  std::size_t CallRecord::Bytes() const noexcept
  {
    return RECORD_HEADER_BYTES + (memory.size() + io.size()) * MEMORY_WRITE_BYTES + trapHits.size() * TRAP_HIT_BYTES +
           stores.size() * STORE_HIT_BYTES;
  }

  Oracle* Oracle::Current() noexcept
  {
    return g_oracle;
  }

  void Oracle::Install(Oracle* _oracle) noexcept
  {
    g_oracle = _oracle;
  }

  /*
   * Everything the call can READ, and nothing it produces.
   *
   * The whole 64K goes in rather than the bytes a fixture seeded, because nothing says which those
   * are: a test scribbles wherever it likes on top of the assembled game, and a routine reads its
   * own code as well as its data. `pc` is left out on purpose -- `CallSubroutine` overwrites it
   * with the entry address -- and so is `cycles`, which is an output a caller zeroes when it wants
   * a measurement. `trapHits` and `stores` go in by SIZE, because the call appends to them and the
   * record has to know how many were already there.
   */
  std::uint64_t CallDigest(const Cpu6502& _cpu, std::uint16_t _address, std::uint32_t _maxInstructions,
                           std::uint16_t _stopAddress) noexcept
  {
    std::uint64_t digest = FNV_OFFSET;
    FoldValue(digest, _address);
    FoldValue(digest, _maxInstructions);
    FoldValue(digest, _stopAddress);

    Fold(digest, _cpu.memory.data(), _cpu.memory.size());
    Fold(digest, _cpu.io.data(), _cpu.io.size());
    Fold(digest, _cpu.keysDown.data(), _cpu.keysDown.size());

    FoldValue(digest, _cpu.a);
    FoldValue(digest, _cpu.x);
    FoldValue(digest, _cpu.y);
    FoldValue(digest, _cpu.sp);
    const std::uint8_t status = _cpu.StatusByte();
    FoldValue(digest, status);

    const std::uint32_t traps = static_cast<std::uint32_t>(_cpu.traps.size());
    FoldValue(digest, traps);
    for (const Cpu6502::Trap& trap : _cpu.traps)
    {
      FoldValue(digest, trap.address);
      const std::uint8_t exit = static_cast<std::uint8_t>(trap.exit);
      FoldValue(digest, exit);
    }
    Fold(digest, _cpu.watch.data(), _cpu.watch.size() * sizeof(std::uint16_t));

    FoldValue(digest, _cpu.storeLogLow);
    FoldValue(digest, _cpu.storeLogHigh);

    const std::uint32_t hits = static_cast<std::uint32_t>(_cpu.trapHits.size());
    const std::uint32_t written = static_cast<std::uint32_t>(_cpu.stores.size());
    FoldValue(digest, hits);
    FoldValue(digest, written);
    return digest;
  }

  RunResult RecordingOracle::Call(Cpu6502& _cpu, std::uint16_t _address, std::uint32_t _maxInstructions,
                                  std::uint16_t _stopAddress) noexcept
  {
    const std::uint64_t key = CallDigest(_cpu, _address, _maxInstructions, _stopAddress);

    // The pre-image, so the record can be the difference. Two arrays and 68K, which is the price of
    // a fixture that can be replayed into a machine the test built rather than one it was handed.
    const std::array<std::uint8_t, 65536> beforeMemory = _cpu.memory;
    const std::array<std::uint8_t, 0x1000> beforeIo = _cpu.io;
    const std::uint64_t beforeCycles = _cpu.cycles;
    const std::size_t beforeHits = _cpu.trapHits.size();
    const std::size_t beforeStores = _cpu.stores.size();

    const RunResult result = _cpu.Interpret(_address, _maxInstructions, _stopAddress);

    CallRecord record;
    for (std::size_t at = 0; at < beforeMemory.size(); ++at)
    {
      if (_cpu.memory[at] != beforeMemory[at])
      {
        record.memory.emplace_back(static_cast<std::uint16_t>(at), _cpu.memory[at]);
      }
    }
    for (std::size_t at = 0; at < beforeIo.size(); ++at)
    {
      if (_cpu.io[at] != beforeIo[at])
      {
        record.io.emplace_back(static_cast<std::uint16_t>(at), _cpu.io[at]);
      }
    }
    record.a = _cpu.a;
    record.x = _cpu.x;
    record.y = _cpu.y;
    record.sp = _cpu.sp;
    record.pc = _cpu.pc;
    record.status = _cpu.StatusByte();
    record.cycles = _cpu.cycles - beforeCycles;
    record.completed = result.completed;
    record.illegalOpcode = result.illegalOpcode;
    record.instructions = result.instructions;
    record.stoppedAt = result.stoppedAt;
    record.trapHits.assign(_cpu.trapHits.begin() + static_cast<std::ptrdiff_t>(beforeHits), _cpu.trapHits.end());
    record.stores.assign(_cpu.stores.begin() + static_cast<std::ptrdiff_t>(beforeStores), _cpu.stores.end());

    ++m_totals.calls;
    m_totals.memoryWrites += record.memory.size() + record.io.size();

    /*
     * A digest of the ANSWER as well as of the question, and it is not belt and braces.
     *
     * The key is 64 bits of the whole machine, so two different calls sharing one is a
     * one-in-eighteen-quintillion accident -- but if it ever happened, the fixture would answer the
     * second call with the first one's memory and the test would fail somewhere unrelated. This
     * counts them instead, and a run that reports any is a run whose fixture must not be committed.
     */
    std::uint64_t answer = FNV_OFFSET;
    FoldValue(answer, record.a);
    FoldValue(answer, record.x);
    FoldValue(answer, record.y);
    FoldValue(answer, record.sp);
    FoldValue(answer, record.pc);
    FoldValue(answer, record.status);
    FoldValue(answer, record.instructions);
    FoldValue(answer, record.stoppedAt);
    for (const auto& [at, value] : record.memory)
    {
      FoldValue(answer, at);
      FoldValue(answer, value);
    }
    for (const auto& [at, value] : record.io)
    {
      FoldValue(answer, at);
      FoldValue(answer, value);
    }

    const auto seen = m_answers.find(key);
    if (seen == m_answers.end())
    {
      ++m_totals.distinct;
      const std::size_t bytes = record.Bytes();
      m_totals.bytes += bytes;
      m_totals.largest = std::max<std::uint64_t>(m_totals.largest, bytes);

      std::size_t bucket = 0;
      for (std::size_t edge = SMALLEST_BUCKET; bucket + 1u < BUCKETS && bytes > edge; edge *= 2u)
      {
        ++bucket;
      }
      ++m_totals.bucketCalls[bucket];
      m_totals.bucketBytes[bucket] += bytes;

      m_answers.emplace(key, answer);
      if (m_mode == Mode::Keep && (m_threshold == 0u || bytes <= m_threshold))
      {
        m_records.emplace(key, std::move(record));
      }
    }
    else if (seen->second != answer)
    {
      ++m_totals.collisions;
    }

    return result;
  }

  namespace
  {

    inline void PutByte(std::vector<std::uint8_t>& _out, std::uint8_t _value)
    {
      _out.push_back(_value);
    }
    inline void PutWord(std::vector<std::uint8_t>& _out, std::uint16_t _value)
    {
      _out.push_back(static_cast<std::uint8_t>(_value & 0xFFu));
      _out.push_back(static_cast<std::uint8_t>(_value >> 8));
    }
    inline void PutLong(std::vector<std::uint8_t>& _out, std::uint32_t _value)
    {
      for (int shift = 0; shift < 32; shift += 8)
      {
        _out.push_back(static_cast<std::uint8_t>((_value >> shift) & 0xFFu));
      }
    }
    inline void PutQuad(std::vector<std::uint8_t>& _out, std::uint64_t _value)
    {
      for (int shift = 0; shift < 64; shift += 8)
      {
        _out.push_back(static_cast<std::uint8_t>((_value >> shift) & 0xFFu));
      }
    }

  } // namespace

  bool WriteFixture(const RecordingOracle& _recorder, const char* _path, std::string& _error)
  {
    std::vector<std::uint64_t> keys;
    keys.reserve(_recorder.Records().size());
    for (const auto& [key, record] : _recorder.Records())
    {
      static_cast<void>(record);
      keys.push_back(key);
    }
    std::sort(keys.begin(), keys.end());

    std::vector<std::uint8_t> bytes;
    bytes.reserve(_recorder.Read().bytes + 32u);
    for (const char* letter = FIXTURE_MAGIC; *letter != '\0'; ++letter)
    {
      PutByte(bytes, static_cast<std::uint8_t>(*letter));
    }
    PutLong(bytes, FIXTURE_VERSION);
    PutLong(bytes, static_cast<std::uint32_t>(keys.size()));

    for (const std::uint64_t key : keys)
    {
      const CallRecord& record = _recorder.Records().at(key);
      PutQuad(bytes, key);
      PutByte(bytes, static_cast<std::uint8_t>((record.completed ? 1u : 0u) | (record.illegalOpcode ? 2u : 0u)));
      PutByte(bytes, record.a);
      PutByte(bytes, record.x);
      PutByte(bytes, record.y);
      PutByte(bytes, record.sp);
      PutByte(bytes, record.status);
      PutWord(bytes, record.pc);
      PutQuad(bytes, record.cycles);
      PutLong(bytes, record.instructions);
      PutWord(bytes, record.stoppedAt);
      PutLong(bytes, static_cast<std::uint32_t>(record.memory.size()));
      PutLong(bytes, static_cast<std::uint32_t>(record.io.size()));
      PutLong(bytes, static_cast<std::uint32_t>(record.trapHits.size()));
      PutLong(bytes, static_cast<std::uint32_t>(record.stores.size()));
      for (const auto& [at, value] : record.memory)
      {
        PutWord(bytes, at);
        PutByte(bytes, value);
      }
      for (const auto& [at, value] : record.io)
      {
        PutWord(bytes, at);
        PutByte(bytes, value);
      }
      for (const Cpu6502::TrapHit& hit : record.trapHits)
      {
        PutWord(bytes, hit.address);
        PutByte(bytes, hit.a);
        PutByte(bytes, hit.x);
        PutByte(bytes, hit.y);
        PutByte(bytes, hit.carry ? 1u : 0u);
        for (const std::uint8_t watched : hit.watched)
        {
          PutByte(bytes, watched);
        }
      }
      for (const Cpu6502::StoreHit& hit : record.stores)
      {
        PutWord(bytes, hit.address);
        PutByte(bytes, hit.value);
      }
    }

    std::ofstream file(_path, std::ios::binary | std::ios::trunc);
    if (!file.is_open())
    {
      _error = std::string("cannot write ") + _path;
      return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file.good())
    {
      _error = std::string("the write to ") + _path + " failed";
      return false;
    }
    return true;
  }

} // namespace Elite::Testing
