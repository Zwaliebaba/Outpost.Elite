#include "pch.h"

#include "SoundOutput.h"

#include <objbase.h>

namespace Outpost
{

  SoundOutput::SoundOutput() noexcept
  {
    /*
     * OPENING THE DEVICE IS A CAPABILITY PROBE, and the rule in AGENTS.md section 5 is that probes
     * are control flow rather than exceptions: a machine with no audio endpoint, or one whose audio
     * service is stopped, plays this game in silence and is told nothing. Every failure below leaves
     * `m_source` null, which is what `Available` reads.
     *
     * XAudio2 wants COM initialised on the calling thread. `S_FALSE` is "already was", and
     * `RPC_E_CHANGED_MODE` is "already was, in the other apartment model", and XAudio2 works in
     * either; only a genuine failure is a reason to stop.
     */
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(com))
    {
      m_comInitialised = true;
    }
    else if (com != RPC_E_CHANGED_MODE)
    {
      return;
    }

    if (FAILED(XAudio2Create(m_xaudio.put(), 0, XAUDIO2_DEFAULT_PROCESSOR)))
    {
      m_xaudio = nullptr;
      return;
    }

    if (FAILED(m_xaudio->CreateMasteringVoice(&m_mastering)))
    {
      m_mastering = nullptr;
      m_xaudio = nullptr;
      return;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 1;
    format.nSamplesPerSec = SAMPLE_RATE;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    IXAudio2SourceVoice* source = nullptr;
    if (FAILED(m_xaudio->CreateSourceVoice(&source, &format)) || FAILED(source->Start(0)))
    {
      if (source != nullptr)
      {
        source->DestroyVoice();
      }
      m_mastering->DestroyVoice();
      m_mastering = nullptr;
      m_xaudio = nullptr;
      return;
    }
    m_source = source;
  }

  SoundOutput::~SoundOutput()
  {
    // Voices are not COM objects: they are destroyed by hand, source before mastering.
    if (m_source != nullptr)
    {
      m_source->Stop(0);
      m_source->DestroyVoice();
      m_source = nullptr;
    }
    if (m_mastering != nullptr)
    {
      m_mastering->DestroyVoice();
      m_mastering = nullptr;
    }
    m_xaudio = nullptr;
    if (m_comInitialised)
    {
      CoUninitialize();
    }
  }

  void SoundOutput::Apply(const Elite::SidWriteLog& _log) noexcept
  {
    for (std::size_t index = 0; index < _log.count; ++index)
    {
      m_synth.Write(_log.writes[index].reg, _log.writes[index].value);
    }
  }

  std::uint32_t SoundOutput::QueuedBuffers() noexcept
  {
    XAUDIO2_VOICE_STATE state{};
    m_source->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return state.BuffersQueued;
  }

  void SoundOutput::RunFrame(Elite::SoundBuffer& _buffer, Elite::MusicPlayer& _music) noexcept
  {
    // 6502: COMIRQ1's second pass -- one frame of the interrupt, and then the chip hears it.
    m_interrupt.Clear();
    Elite::RunSoundInterrupt(_buffer, _music, m_interrupt);
    Apply(m_interrupt);
  }

  void SoundOutput::Pump(Elite::SoundBuffer& _buffer, Elite::MusicPlayer& _music) noexcept
  {
    // The game's own writes since the last pump come first, in the order they were made.
    Apply(m_direct);
    m_direct.Clear();

    if (!Available())
    {
      // No device, so no clock: one interrupt per presented frame keeps the buffer's counters
      // moving, which is what the game can observe of the sound.
      RunFrame(_buffer, _music);
      return;
    }

    while (QueuedBuffers() < TARGET_QUEUED)
    {
      RunFrame(_buffer, _music);

      // How many samples this frame is worth, with the fraction carried: 737 or 738.
      m_sampleRemainder += FRAME_CYCLES * SAMPLE_RATE % CLOCK_HZ;
      std::uint32_t samples = FRAME_CYCLES * SAMPLE_RATE / CLOCK_HZ;
      if (m_sampleRemainder >= CLOCK_HZ)
      {
        m_sampleRemainder -= CLOCK_HZ;
        ++samples;
      }

      std::array<std::int16_t, FRAME_SAMPLES_MAX>& slot = m_ring[m_next];
      m_next = (m_next + 1) % RING;
      m_synth.Render(slot.data(), samples);

      XAUDIO2_BUFFER buffer{};
      buffer.AudioBytes = static_cast<UINT32>(samples * sizeof(std::int16_t));
      buffer.pAudioData = reinterpret_cast<const BYTE*>(slot.data());
      if (FAILED(m_source->SubmitSourceBuffer(&buffer)))
      {
        // A refused buffer is a device on its way out; the interrupt has run, which is what matters.
        return;
      }
    }
  }

} // namespace Outpost
