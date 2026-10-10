// Engine/Presenter.h
#pragma once

#include "Win32.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <span>

namespace Engine
{

/// Shows a picture made of pixels in a window with Direct3D 12 (ADR-009).
///
/// The picture is a fixed grid of source pixels with a display shape of its own: Create is given the grid
/// and the size the whole picture has on screen at scale 1, its logical size. Each frame the picture is
/// drawn at the largest whole-number multiple of that logical size which fits the window, centred, with
/// the rest of the window in the border colour. Inside it each source pixel covers the same rectangle of
/// screen pixels, sharp at its edges: where a source row covers a fractional number of screen rows, only
/// the one screen row on the boundary is blended (a "sharp bilinear" filter), so rows neither blur nor
/// come out of uneven height. A window smaller than the logical size gets the largest fit instead.
///
/// One frame in flight: Present waits for the GPU before it returns. The picture is small and the window
/// is the only thing drawn, so nothing is gained by overlapping frames. Present also paces its caller: it
/// returns at the display's refresh, or after a frame's time when the window is minimised or hidden.
class Presenter
{
public:
  Presenter() noexcept = default;
  Presenter(const Presenter&) = delete;
  Presenter& operator=(const Presenter&) = delete;
  ~Presenter();

  /// Sets up the device, the swap chain and the picture's texture. _sourceWidth x _sourceHeight is the
  /// pixel grid; _logicalWidth x _logicalHeight the picture's size on screen at scale 1. Returns false if
  /// Direct3D 12 is not available.
  [[nodiscard]] bool Create(HWND _window, std::uint32_t _sourceWidth, std::uint32_t _sourceHeight, std::uint32_t _logicalWidth,
                            std::uint32_t _logicalHeight);

  /// Resizes the swap chain to the window's client area. Call it when the window reports a new size.
  [[nodiscard]] bool Resize();

  /// Draws one frame and waits for it. _pixels holds the grid row by row, each pixel 0x00RRGGBB; _border
  /// is the colour around the picture, in the same form. Returns false if the device was lost.
  [[nodiscard]] bool Present(std::span<const std::uint32_t> _pixels, std::uint32_t _border);

  static constexpr std::uint32_t FRAME_COUNT = 2;

private:
  [[nodiscard]] bool CreateRenderTargets();
  [[nodiscard]] bool CreatePipeline();
  [[nodiscard]] bool CreatePictureTexture();
  [[nodiscard]] bool WaitForGpu();

  Microsoft::WRL::ComPtr<IDXGIFactory4> m_factory;
  Microsoft::WRL::ComPtr<ID3D12Device> m_device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_renderTargetHeap;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_shaderResourceHeap;
  std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FRAME_COUNT> m_renderTargets;
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_allocator;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commands;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipeline;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_picture;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_upload;
  Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
  HANDLE m_fenceEvent = nullptr;
  std::uint64_t m_fenceValue = 0;
  HWND m_window = nullptr;
  UINT m_renderTargetStride = 0;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_footprint{};
  std::uint32_t m_sourceWidth = 0;
  std::uint32_t m_sourceHeight = 0;
  std::uint32_t m_logicalWidth = 0;
  std::uint32_t m_logicalHeight = 0;
  std::uint32_t m_clientWidth = 0;
  std::uint32_t m_clientHeight = 0;
};

} // namespace Engine
