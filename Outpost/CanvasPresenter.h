#pragma once

#include "Presentation.h"
#include "Window.h"

#include <array>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <dxgi1_4.h>

#include <winrt/base.h>

namespace Elite
{
  class Canvas;
  struct VideoState;
} // namespace Elite

namespace Outpost
{

  /*
   * The canvas on the screen (slice 2e, ADR-005 section 1).
   *
   * The canvas is 320x200 COLOUR INDICES, not pixels, so the texture is `R8_UINT` and the palette
   * lookup happens in the pixel shader. That is one byte per pixel uploaded instead of four, and
   * more to the point it is the only representation in which a change of palette is a change of
   * sixteen root constants rather than a re-upload of the frame.
   *
   * WHAT IS DELIBERATELY NOT HERE. The palette, the letterboxed viewport and the step accumulator
   * are in `Presentation.h`, where the test suite can reach them on a machine with no GPU. What is
   * left in this file is Direct3D and nothing else, so a bug here is a bug in the plumbing.
   *
   * The pipeline is as small as a pipeline gets: no vertex buffer (a fullscreen triangle from
   * `SV_VertexID`), no constant buffer resource (sixteen root constants), no sampler (the shader
   * does an integer `Load`, so point sampling is not a state to set), no depth buffer, one draw of
   * three vertices. The shaders are compiled at runtime from the string at the top of the .cpp,
   * which keeps the project file free of an HLSL build step for twenty lines of shader.
   */
  class CanvasPresenter
  {
  public:
    CanvasPresenter() = default;
    ~CanvasPresenter();

    CanvasPresenter(const CanvasPresenter&) = delete;
    CanvasPresenter& operator=(const CanvasPresenter&) = delete;

    /// Throws through `winrt::check_hresult`; the composition root catches and reports.
    void Create(HWND _window);

    /*
     * Resolve the canvas, upload it, draw it letterboxed and present on the vertical blank.
     *
     * THIS IS WHERE THE LOOP WAITS. `Present(1, 0)` blocks until the display is ready for the
     * frame, which is what paces the whole program -- there is no timer and no sleep anywhere in
     * the shell. Returns false if the device has been removed, which is not recoverable here.
     */
    /*
     * `_video` is the VIC-II sprite registers, or null for the bitmap alone.
     *
     * A POINTER rather than a reference, because "no sprites" is a real state and not a missing
     * argument: the title screen and every docked screen present before a flight session exists,
     * and a default-constructed `VideoState` would be a lie about registers nothing has written.
     */
    [[nodiscard]] bool Present(const Elite::Canvas& _canvas, const Elite::VideoState* _video, int _clientWidth, int _clientHeight);

    /// The client area changed. Cheap and idempotent; a zero-sized client (a minimised window) is
    /// ignored rather than resized to, because `ResizeBuffers` rejects it.
    void Resize(int _clientWidth, int _clientHeight);

    [[nodiscard]] bool Ready() const noexcept
    {
      return m_device != nullptr;
    }

    /*
     * Wait for the GPU and release everything, without needing the destructor to run.
     *
     * THE SHELL ENDS THE PROCESS WHERE IT STANDS when the window closes (`GameShell::Abandon`, and
     * `Shell.h` argues for why), so on the ordinary way out of a docked game -- the player clicking
     * the X while the game is blocked in `TT217` -- no destructor anywhere runs. That is fine for
     * memory, which the OS reclaims, and not fine for Direct3D: the debug layer reports every
     * unreleased object at process exit, so quitting printed forty live objects and three live DXGI
     * ones every time. They were never a leak, but a warning nobody can act on is a warning
     * everybody learns to skip past, and the next real one goes with it.
     *
     * Idempotent, and safe on a presenter that was never created.
     */
    void Destroy() noexcept;

  private:
    /// Two, which is the minimum a flip-model chain allows and enough for a program that never
    /// runs ahead: one frame is being presented while the next is being built.
    static constexpr UINT FRAME_COUNT = 2;

    void CreateRenderTargets();
    void WaitForGpu();
    void MoveToNextFrame();

    winrt::com_ptr<ID3D12Device> m_device;
    winrt::com_ptr<ID3D12CommandQueue> m_queue;
    winrt::com_ptr<IDXGISwapChain3> m_swapChain;

    winrt::com_ptr<ID3D12DescriptorHeap> m_renderTargetHeap;
    winrt::com_ptr<ID3D12DescriptorHeap> m_textureHeap;
    UINT m_renderTargetSize = 0;

    std::array<winrt::com_ptr<ID3D12Resource>, FRAME_COUNT> m_backBuffers;
    std::array<winrt::com_ptr<ID3D12CommandAllocator>, FRAME_COUNT> m_allocators;
    std::array<winrt::com_ptr<ID3D12Resource>, FRAME_COUNT> m_uploads;
    std::array<std::uint8_t*, FRAME_COUNT> m_uploadMemory = {};
    std::array<UINT64, FRAME_COUNT> m_fenceValues = {};

    winrt::com_ptr<ID3D12GraphicsCommandList> m_commands;
    winrt::com_ptr<ID3D12RootSignature> m_rootSignature;
    winrt::com_ptr<ID3D12PipelineState> m_pipeline;
    winrt::com_ptr<ID3D12Resource> m_texture;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_footprint = {};
    UINT m_footprintRows = 0;

    winrt::com_ptr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    UINT m_frameIndex = 0;

    int m_width = 0;
    int m_height = 0;

    std::array<std::uint32_t, 16> m_palette = {};
    std::vector<std::uint8_t> m_resolved;
  };

} // namespace Outpost
