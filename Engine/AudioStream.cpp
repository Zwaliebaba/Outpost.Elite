#include "pch.h"

#include "AudioStream.h"

#include <algorithm>

namespace Engine
{

AudioStream::~AudioStream()
{
  if (m_source != nullptr)
  {
    m_source->DestroyVoice();
  }
  if (m_master != nullptr)
  {
    m_master->DestroyVoice();
  }
}

bool AudioStream::Create(std::uint32_t _sampleRate)
{
  if (FAILED(XAudio2Create(&m_engine, 0, XAUDIO2_DEFAULT_PROCESSOR)) || FAILED(m_engine->CreateMasteringVoice(&m_master)))
  {
    return false;
  }
  WAVEFORMATEX format{};
  format.wFormatTag = WAVE_FORMAT_PCM;
  format.nChannels = 1;
  format.nSamplesPerSec = _sampleRate;
  format.wBitsPerSample = 16;
  format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
  format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
  if (FAILED(m_engine->CreateSourceVoice(&m_source, &format)))
  {
    m_source = nullptr;
    return false;
  }
  m_silence.assign(std::size_t{_sampleRate} * LEAD_MILLISECONDS / 1000, 0);
  m_mostQueuedSamples = std::uint64_t{_sampleRate} * MOST_QUEUED_MILLISECONDS / 1000;
  Queue(m_silence);
  return SUCCEEDED(m_source->Start());
}

void AudioStream::Submit(std::span<const std::int16_t> _samples)
{
  if (m_source == nullptr || _samples.empty())
  {
    return;
  }
  XAUDIO2_VOICE_STATE state{};
  m_source->GetState(&state, 0);
  if (state.BuffersQueued >= BUFFER_COUNT)
  {
    return;
  }
  if (state.BuffersQueued == 0)
  {
    // The device ran dry: its clock is faster than the caller's, or the caller stalled. Start again with
    // the same slack as at the start.
    Queue(m_silence);
  }
  // The device's clock and the caller's drift apart. When the caller runs ahead, its oldest samples go, so
  // that the sound never lags the picture by more than MOST_QUEUED_MILLISECONDS.
  const std::uint64_t queued = m_samplesSubmitted - state.SamplesPlayed;
  if (queued >= m_mostQueuedSamples)
  {
    return;
  }
  Queue(_samples.last(static_cast<std::size_t>(std::min<std::uint64_t>(m_mostQueuedSamples - queued, _samples.size()))));
}

void AudioStream::Queue(std::span<const std::int16_t> _samples)
{
  std::vector<std::int16_t>& buffer = m_buffers[m_next];
  m_next = (m_next + 1) % BUFFER_COUNT;
  buffer.assign(_samples.begin(), _samples.end());
  XAUDIO2_BUFFER submission{};
  submission.AudioBytes = static_cast<UINT32>(buffer.size() * sizeof(std::int16_t));
  submission.pAudioData = reinterpret_cast<const BYTE*>(buffer.data());
  if (SUCCEEDED(m_source->SubmitSourceBuffer(&submission)))
  {
    m_samplesSubmitted += buffer.size();
  }
}

} // namespace Engine
