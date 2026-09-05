#pragma once

#include "SidSynth.h"

#include "Music.h"
#include "SoundEffects.h"

#include <xaudio2.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Outpost
{

  /*
   * The SID, played through XAudio2 (ADR-005 section 2).
   *
   * THE INTERRUPT RUNS OFF THE DAC. On the C64 the sound interrupt fires once a frame because the
   * VIC-II's raster says so; here nothing does, and the two clocks on offer -- the display's vertical
   * sync and the wall clock -- are the wrong rate and the wrong kind respectively. The audio
   * device's own consumption is the right one: it takes samples at exactly the rate they are
   * rendered for, so "the queue is short" IS "it is time for another frame's interrupt". `Pump`
   * asks how many buffers are queued and runs one interrupt, renders one frame of samples, and
   * submits it, until the target depth is reached. Four frames deep is 67 milliseconds of latency,
   * which is under what a player notices and over what a scheduler hiccup costs.
   *
   * ONE FRAME IS 17,095 CYCLES, which is the NTSC VIC-II's 65 cycles a line for 263 lines, and it is
   * cycles rather than a fraction of a second because the chip is clocked in cycles: at 44,100 Hz
   * a frame is 737 samples and a bit, and the bit is carried, so the interrupt's long-run rate is
   * the machine's 59.8 Hz to the sample.
   *
   * WITHOUT A DEVICE THE INTERRUPT STILL RUNS. The buffer's counters are game state -- an effect
   * that has not counted down refuses a lower-priority one, and `NOISE`'s answer to `OUCH` decides
   * which piece of equipment an explosion breaks -- so a machine with no audio must still tick
   * them. It ticks once a `Pump`, which is once a presented frame: the display's rate rather than
   * the chip's, and the closest thing to hand.
   *
   * THE GAME'S OWN WRITES GO THROUGH HERE TOO. `stopat` and `BDENTRY` write the chip from the game
   * rather than from the interrupt, and those writes are queued in `Direct` and applied ahead of the
   * next interrupt's, which is the order they happen in.
   */
  class SoundOutput
  {
  public:
    SoundOutput() noexcept;
    ~SoundOutput();

    SoundOutput(const SoundOutput&) = delete;
    SoundOutput& operator=(const SoundOutput&) = delete;

    /// Whether a device was opened. False is a silent machine, not an error.
    [[nodiscard]] bool Available() const noexcept
    {
      return m_source != nullptr;
    }

    /// Where a game-side write to the chip goes. Applied before the next interrupt's writes.
    [[nodiscard]] Elite::SidWriteLog& Direct() noexcept
    {
      return m_direct;
    }

    /// Run as many sound interrupts as the queue is short by, rendering and submitting each frame.
    void Pump(Elite::SoundBuffer& _buffer, Elite::MusicPlayer& _music) noexcept;

  private:
    void Apply(const Elite::SidWriteLog& _log) noexcept;
    void RunFrame(Elite::SoundBuffer& _buffer, Elite::MusicPlayer& _music) noexcept;
    [[nodiscard]] std::uint32_t QueuedBuffers() noexcept;

    /// 6502: the 6510's clock on the NTSC machine, and the VIC-II's frame in cycles on the same.
    static constexpr std::uint32_t CLOCK_HZ = 1'022'727;
    static constexpr std::uint32_t FRAME_CYCLES = 65 * 263;
    static constexpr std::uint32_t SAMPLE_RATE = 44'100;

    /// A frame is 737 samples and a fraction; the ring's slots hold the ceiling.
    static constexpr std::size_t FRAME_SAMPLES_MAX = FRAME_CYCLES * SAMPLE_RATE / CLOCK_HZ + 1;

    /// How deep the queue is kept, and how many slots there are to keep it from. The slot about to
    /// be written was submitted RING frames ago and at most TARGET are still in flight, so it is free.
    static constexpr std::uint32_t TARGET_QUEUED = 4;
    static constexpr std::size_t RING = 8;
    static_assert(RING > TARGET_QUEUED + 1);

    bool m_comInitialised = false;
    winrt::com_ptr<IXAudio2> m_xaudio;
    IXAudio2MasteringVoice* m_mastering = nullptr;
    IXAudio2SourceVoice* m_source = nullptr;

    SidSynth m_synth{CLOCK_HZ, SAMPLE_RATE};
    Elite::SidWriteLog m_direct;
    Elite::SidWriteLog m_interrupt;

    std::array<std::array<std::int16_t, FRAME_SAMPLES_MAX>, RING> m_ring{};
    std::size_t m_next = 0;
    std::uint32_t m_sampleRemainder = 0; ///< the fraction of a sample a frame leaves behind
  };

} // namespace Outpost
