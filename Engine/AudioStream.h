// Engine/AudioStream.h
#pragma once

#include "Win32.h"

#include <wrl/client.h>
#include <xaudio2.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Engine
{

/// A mono 16-bit stream played through XAudio2 (ADR-009). The caller produces samples at the stream's
/// rate as time passes and hands them over with Submit; the stream copies them into one of a ring of
/// buffers, so the caller's memory can go at once. LEAD_MILLISECONDS of silence is queued first, as slack
/// for the caller's uneven timing, and queued again whenever the device runs dry. COM must be initialised
/// on the calling thread.
class AudioStream
{
public:
  AudioStream() noexcept = default;
  AudioStream(const AudioStream&) = delete;
  AudioStream& operator=(const AudioStream&) = delete;
  ~AudioStream();

  /// Opens the default output device. Returns false if there is none, in which case Submit does nothing.
  [[nodiscard]] bool Create(std::uint32_t _sampleRate);

  /// Queues _samples to play after what is already queued. Samples that would put more than
  /// MOST_QUEUED_MILLISECONDS in the queue are dropped, oldest first: the caller is running ahead of the
  /// device.
  void Submit(std::span<const std::int16_t> _samples);

  static constexpr std::size_t BUFFER_COUNT = 16;
  static constexpr std::uint32_t LEAD_MILLISECONDS = 60;
  static constexpr std::uint32_t MOST_QUEUED_MILLISECONDS = 90;

private:
  void Queue(std::span<const std::int16_t> _samples);

  Microsoft::WRL::ComPtr<IXAudio2> m_engine;
  IXAudio2MasteringVoice* m_master = nullptr; // owned by m_engine; destroyed in the destructor
  IXAudio2SourceVoice* m_source = nullptr;    // likewise
  std::array<std::vector<std::int16_t>, BUFFER_COUNT> m_buffers;
  std::size_t m_next = 0;
  std::vector<std::int16_t> m_silence;
  std::uint64_t m_samplesSubmitted = 0;
  std::uint64_t m_mostQueuedSamples = 0;
};

} // namespace Engine
