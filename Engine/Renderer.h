// Engine/Renderer.h
#pragma once

#include "Win32.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace Engine
{

/// A rectangle of the screen in physical pixels: its top-left corner and its size.
struct ScreenRect
{
  std::uint32_t left = 0;
  std::uint32_t top = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  friend bool operator==(const ScreenRect&, const ScreenRect&) = default;
};

/// Where a picture of _logicalWidth x _logicalHeight at scale 1 goes on a target of _targetWidth x _targetHeight physical
/// pixels (D46): the largest whole-number multiple of its logical size that fits, centred, the odd pixel right of and below
/// it; or, on a target smaller than the logical size, the largest fit that keeps its shape. The logical size is not 0.
[[nodiscard]] ScreenRect PlacePicture(std::uint32_t _targetWidth, std::uint32_t _targetHeight, std::uint32_t _logicalWidth,
                                      std::uint32_t _logicalHeight) noexcept;

/// Draws the game with Direct3D 12 (ADR-031, after D47): today the picture, a fixed grid of source pixels with a display shape
/// of its own, at the place PlacePicture gives it, on a border of one colour. Inside the picture each source pixel covers the
/// same rectangle of screen pixels, sharp at its edges (ADR-009's sharp bilinear filter).
///
/// It draws into a window's swap chain (AttachWindow, Present), or, with no window, into a target of its own that it reads
/// back (Capture), on any device, WARP included, which is how the tests see what it draws. Two frames are in flight: the CPU
/// records one while the GPU draws the other, each with its own allocator and upload buffer. The swap chain holds a frame
/// latency of one, and Present waits on it before it records, so a frame is never more than one refresh old when it is shown.
class Renderer
{
public:
  /// The device to draw on, and the picture's grid and logical size.
  struct Desc
  {
    bool warp = false;             ///< WARP, Direct3D 12's software device, rather than the hardware's
    std::uint32_t sourceWidth = 0; ///< the picture's grid, in source pixels
    std::uint32_t sourceHeight = 0;
    std::uint32_t logicalWidth = 0; ///< the picture's size on screen at scale 1
    std::uint32_t logicalHeight = 0;
  };

  Renderer() noexcept = default;
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  ~Renderer();

  /// Sets up the device, the queue, the frames and the picture. Returns false if Direct3D 12 is not available.
  [[nodiscard]] bool Create(const Desc& _desc);

  /// A swap chain on _window's client area, which Present draws into. Returns false if Windows refuses it.
  [[nodiscard]] bool AttachWindow(HWND _window);

  /// Resizes the swap chain to the window's client area. Call it when the window reports a new size.
  [[nodiscard]] bool Resize();

  /// Draws one frame into the window and shows it. _pixels holds the grid row by row, each pixel 0x00RRGGBB; _border is the
  /// colour around the picture, in the same form. It waits for the swap chain before it records, so it paces its caller at the
  /// display's refresh, or at a frame's time when the window is minimised or hidden. Returns false if the device was lost.
  [[nodiscard]] bool Present(std::span<const std::uint32_t> _pixels, std::uint32_t _border);

  /// Draws one frame as Present would into a window whose client area is _width x _height, and returns it, row by row, each
  /// pixel 0x00RRGGBB; empty if the device fails. It needs no window, and waits for the GPU before it returns.
  [[nodiscard]] std::vector<std::uint32_t> Capture(std::span<const std::uint32_t> _pixels, std::uint32_t _border, std::uint32_t _width,
                                                   std::uint32_t _height);

  static constexpr std::uint32_t FRAME_COUNT = 2;

private:
  // What each frame in flight owns: the CPU writes one while the GPU may still read the other.
  struct Frame
  {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    std::uint64_t fenceValue = 0;
  };

  [[nodiscard]] bool CreateDevice(bool _warp);
  [[nodiscard]] bool CreatePipeline();
  [[nodiscard]] bool CreatePicture();
  [[nodiscard]] bool CreateWindowTargets();
  [[nodiscard]] bool BeginFrame(Frame& _frame, std::span<const std::uint32_t> _pixels);
  // The border and the picture into _targetView, a target of _width x _height in the render-target state.
  void DrawPicture(D3D12_CPU_DESCRIPTOR_HANDLE _targetView, std::uint32_t _width, std::uint32_t _height, std::uint32_t _border);
  [[nodiscard]] bool Submit(Frame& _frame);
  [[nodiscard]] bool WaitForFrame(const Frame& _frame);
  [[nodiscard]] bool WaitForGpu();

  Microsoft::WRL::ComPtr<IDXGIFactory4> m_factory;
  Microsoft::WRL::ComPtr<ID3D12Device> m_device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commands;
  Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
  HANDLE m_fenceEvent = nullptr;
  std::uint64_t m_lastFenceValue = 0;
  std::array<Frame, FRAME_COUNT> m_frames;
  std::uint32_t m_nextFrame = 0;

  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipeline;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_shaderResourceHeap;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_picture;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_footprint{};
  std::uint64_t m_uploadBytes = 0;
  std::uint32_t m_sourceWidth = 0;
  std::uint32_t m_sourceHeight = 0;
  std::uint32_t m_logicalWidth = 0;
  std::uint32_t m_logicalHeight = 0;

  HWND m_window = nullptr;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
  HANDLE m_frameLatencyWaitable = nullptr;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_renderTargetHeap;
  std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FRAME_COUNT> m_backBuffers;
  UINT m_renderTargetStride = 0;
  std::uint32_t m_clientWidth = 0;
  std::uint32_t m_clientHeight = 0;
};

} // namespace Engine
