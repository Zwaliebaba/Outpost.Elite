// Engine/Renderer.h
#pragma once

#include "Canvas.h"
#include "GlyphAtlas.h"
#include "SceneSnapshot.h"
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

/// Where design units go on a target (D48): scale screen pixels to a unit, the smaller of the target's width over 1920 and its
/// height over 1080, with design (0, 0) at (left, top), so that the 1920 x 1080 design is centred.
struct DesignPlacement
{
  float scale = 1.0f;
  float left = 0.0f;
  float top = 0.0f;
};

/// The design units' placement on a target of _width x _height.
[[nodiscard]] DesignPlacement PlaceDesign(std::uint32_t _width, std::uint32_t _height) noexcept;

/// A vertex of a scene model (ADR-033), in the model's own frame and the scene's axes: x right, y down and z ahead. Each belongs to
/// a face, whose normal and a point on whose plane it carries: the face is drawn, every triangle and line of it, exactly when
/// its normal points towards the camera. Its colour is a mix of the scene's four palette colours, by weights that sum to 1. Its
/// layer orders what lies in one plane: a face's triangles of a higher layer are drawn over those of a lower one, as a painter
/// would paint them in turn (SCENE_LAYERS).
struct SceneVertex
{
  Vector3 position;
  Vector3 normal;    ///< the face's, a unit vector out of the model
  Vector3 facePoint; ///< a point of the face's plane
  std::array<float, 4> paletteWeights{};
  float layer = 0.0f; ///< 0 and up, below SCENE_LAYERS
};

/// The layers a scene vertex may have. A triangle of layer n is drawn as if n / 8192 of its distance nearer than it is, so that it
/// covers a lower layer in its plane; at most 31 / 8192, under 0.4%. A line is drawn 1 / 128 nearer, over every layer of its face.
inline constexpr std::uint32_t SCENE_LAYERS = 32;

/// What a scene model is made of: its faces' triangles, three vertices each, and lines over its faces, two each.
struct SceneModelDesc
{
  std::span<const SceneVertex> triangles;
  std::span<const SceneVertex> lines;
};

/// A pixel of a scene layer that holds nothing (SceneInset).
inline constexpr std::uint8_t SCENE_LAYER_CLEAR = 0xFF;

/// Where a scene is drawn over the picture and how (ADR-033): an inset rectangle of the picture, in its source pixels, or of the
/// design, in design units, when the frame draws a canvas in place of the picture (ADR-034), which the scene replaces; the camera's projection onto it; the light; the four palette colours its models and layers are drawn in; and
/// two layers of the inset's own pixels, under and over the scene, each a palette index or SCENE_LAYER_CLEAR.
///
/// The camera looks along its z, x right and y down: a point (x, y, z) of the camera's frame is drawn centerX + focalX x / z
/// source pixels right of the inset's left edge and centerY + focalY y / z below its top, and nothing nearer than nearUnits.
/// The scene's frame is SceneSnapshot's: its camera orientation takes a direction of the scene into the camera's frame.
struct SceneInset
{
  float left = 0.0f; ///< in the picture's source pixels, or in design units under a canvas
  float top = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float focalX = 0.0f; ///< source pixels per unit of x / z
  float focalY = 0.0f;
  float centerX = 0.0f; ///< where the camera's axis meets the inset, in source pixels from its top left corner
  float centerY = 0.0f;
  float nearUnits = 1.0f;
  Vector3 towardsLight{.x = 0.0f, .y = 0.0f, .z = -1.0f}; ///< a unit vector towards the light, in the scene's frame
  std::array<std::uint32_t, 4> palette{};                 ///< 0x00RRGGBB, as the picture's pixels
  std::uint32_t layerWidth = 0;                           ///< the layers' size in their own pixels, which cover the inset
  std::uint32_t layerHeight = 0;
  std::span<const std::uint8_t> under; ///< row by row: under the scene, where it draws nothing
  std::span<const std::uint8_t> over;  ///< row by row: over the scene
};

/// A scene to draw over the picture: what is in it and where it goes.
struct SceneFrame
{
  const SceneSnapshot* scene = nullptr;
  SceneInset inset;
};

/// Draws the game with Direct3D 12 (ADR-031, after D47): the picture, a fixed grid of source pixels with a display shape of its
/// own, at the place PlacePicture gives it, on a border of one colour. Inside the picture each source pixel covers the same
/// rectangle of screen pixels, sharp at its edges (ADR-009's sharp bilinear filter). Over it, when a frame has one, a scene in an
/// inset of the picture (ADR-033): its models drawn in a linear FP16 target with 4x MSAA and a D32_FLOAT depth buffer in reversed
/// Z, flat and lit, at the inset's size on screen, resolved and laid between the inset's two layers.
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

  Renderer() = default; // not noexcept: a debug std::map allocates as it is made
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  ~Renderer();

  /// Sets up the device, the queue, the frames and the picture. Returns false if Direct3D 12 is not available.
  [[nodiscard]] bool Create(const Desc& _desc);

  /// A swap chain on _window's client area, which Present draws into. Returns false if Windows refuses it.
  [[nodiscard]] bool AttachWindow(HWND _window);

  /// Resizes the swap chain to the window's client area. Call it when the window reports a new size.
  [[nodiscard]] bool Resize();

  /// Model _model of the scenes this renderer draws, as _desc makes it; a model of that number already added is replaced, after
  /// the GPU is done with it. An object of a scene whose model was never added is not drawn. Returns false if the device fails.
  [[nodiscard]] bool AddModel(std::uint32_t _model, const SceneModelDesc& _desc);

  /// Draws one frame into the window and shows it. _pixels holds the grid row by row, each pixel 0x00RRGGBB; _border is the
  /// colour around the picture, in the same form; _scene, when there is one, is drawn over it. With _canvas (ADR-034) the
  /// picture is not drawn: the target is _border, the scene's inset is in design units, and the canvas is drawn over both, so
  /// _pixels may be empty. It waits for the swap chain before it records, so it paces its caller at the display's refresh, or at
  /// a frame's time when the window is minimised or hidden. Returns false if the device was lost.
  [[nodiscard]] bool Present(std::span<const std::uint32_t> _pixels, std::uint32_t _border, const SceneFrame* _scene = nullptr,
                             const Canvas* _canvas = nullptr);

  /// Draws one frame as Present would into a window whose client area is _width x _height, and returns it, row by row, each
  /// pixel 0x00RRGGBB; empty if the device fails. It needs no window, and waits for the GPU before it returns.
  [[nodiscard]] std::vector<std::uint32_t> Capture(std::span<const std::uint32_t> _pixels, std::uint32_t _border, std::uint32_t _width,
                                                   std::uint32_t _height, const SceneFrame* _scene = nullptr,
                                                   const Canvas* _canvas = nullptr);

  /// The width of _text as a canvas draws it, in design units (GlyphAtlas::Measure).
  [[nodiscard]] float MeasureText(const CanvasText& _text);

  /// Whether the canvas's face, Bahnschrift, is installed (D39); without it DirectWrite draws text in another.
  [[nodiscard]] bool HasBahnschrift() const noexcept
  {
    return m_glyphs.HasBahnschrift();
  }

  static constexpr std::uint32_t FRAME_COUNT = 2;

private:
  // What each frame in flight owns: the CPU writes one while the GPU may still read the other.
  struct Frame
  {
    Microsoft::WRL::ComPtr<ID3D12CommandAllocator> allocator;
    Microsoft::WRL::ComPtr<ID3D12Resource> upload;
    Microsoft::WRL::ComPtr<ID3D12Resource> layerUpload; // the scene's layers, once a scene is drawn
    Microsoft::WRL::ComPtr<ID3D12Resource> shapeUpload; // the canvas's shapes, as many as it has held at most
    std::size_t shapeCapacity = 0;
    Microsoft::WRL::ComPtr<ID3D12Resource> atlasUpload; // the glyph atlas, when it changed
    std::uint64_t fenceValue = 0;
  };

  // A scene model's vertices, in a buffer of their own.
  struct Model
  {
    Microsoft::WRL::ComPtr<ID3D12Resource> vertices;
    D3D12_VERTEX_BUFFER_VIEW triangles{};
    D3D12_VERTEX_BUFFER_VIEW lines{};
    UINT triangleVertices = 0;
    UINT lineVertices = 0;
  };

  [[nodiscard]] bool CreateDevice(bool _warp);
  [[nodiscard]] bool CreatePipeline();
  [[nodiscard]] bool CreateScenePipelines();
  [[nodiscard]] bool CreateCanvasPipeline();
  [[nodiscard]] bool CreatePicture();
  [[nodiscard]] bool CreateWindowTargets();
  // The scene's targets at _width x _height, made again only when the size changes, after the GPU is done with the old.
  [[nodiscard]] bool SizeSceneTargets(std::uint32_t _width, std::uint32_t _height);
  // The frame's allocator reset and its commands begun: the picture's pixels copied when _picture, and _scene's layers.
  [[nodiscard]] bool BeginFrame(Frame& _frame, std::span<const std::uint32_t> _pixels, const SceneFrame* _scene, bool _picture);
  [[nodiscard]] bool CopyPicture(Frame& _frame);
  [[nodiscard]] bool UploadLayers(Frame& _frame, const SceneFrame& _scene);
  // What Present and Capture draw into _target, a target of _width x _height in _targetState, which it is left in:
  // _targetView its view and _srgbView its sRGB view. The picture and the scene, or the border, the scene and the canvas.
  [[nodiscard]] bool DrawFrame(Frame& _frame, D3D12_CPU_DESCRIPTOR_HANDLE _targetView, D3D12_CPU_DESCRIPTOR_HANDLE _srgbView,
                               std::uint32_t _width, std::uint32_t _height, std::uint32_t _border, const SceneFrame* _scene,
                               const Canvas* _canvas, std::span<const std::uint32_t> _pixels, ID3D12Resource* _target,
                               D3D12_RESOURCE_STATES _targetState);
  // _canvas into _srgbView, over what is there, its text laid out and its shapes uploaded through _frame.
  [[nodiscard]] bool DrawCanvas(Frame& _frame, D3D12_CPU_DESCRIPTOR_HANDLE _srgbView, std::uint32_t _width, std::uint32_t _height,
                                const Canvas& _canvas);
  // The border and the picture into _targetView, a target of _width x _height in the render-target state.
  void DrawPicture(D3D12_CPU_DESCRIPTOR_HANDLE _targetView, std::uint32_t _width, std::uint32_t _height, std::uint32_t _border);
  // The inset's place on a target of _width x _height, in whole screen pixels: in the picture's source pixels, or in design
  // units when _design.
  [[nodiscard]] ScreenRect InsetPlace(const SceneInset& _inset, std::uint32_t _width, std::uint32_t _height, bool _design) const noexcept;
  // _scene's models into the scene's targets, resolved, and then laid with its layers into _targetView's inset, _place.
  [[nodiscard]] bool DrawScene(D3D12_CPU_DESCRIPTOR_HANDLE _targetView, const ScreenRect& _place, const SceneFrame& _scene);
  void DrawModels(const SceneFrame& _scene, bool _lines);
  // Whether _scene can be drawn: an inset of some size and layers that cover it.
  [[nodiscard]] static bool Drawable(const SceneFrame* _scene) noexcept;
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

  // The scene (ADR-033).
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_sceneSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_trianglePipeline;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_linePipeline;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_compositeSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_compositePipeline;
  std::vector<Model> m_models;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneColor;    // FP16, 4x MSAA
  Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneDepth;    // D32_FLOAT, 4x MSAA, reversed Z
  Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneResolved; // FP16, what the composite reads
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_sceneTargetHeap;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_sceneDepthHeap;
  std::uint32_t m_sceneWidth = 0;
  std::uint32_t m_sceneHeight = 0;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_layers; // R8G8_UINT: under and over
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_layerFootprint{};
  std::uint32_t m_layerWidth = 0;
  std::uint32_t m_layerHeight = 0;
  UINT m_resourceStride = 0;

  // The 2D layer (ADR-034).
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_canvasSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_canvasPipeline;
  GlyphAtlas m_glyphs;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_atlas; // R8_UNORM, GlyphAtlas::SIZE_PIXELS square
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT m_atlasFootprint{};
  std::uint64_t m_atlasUploadBytes = 0;
  std::vector<CanvasShape> m_drawnShapes; // a frame's shapes, its text turned to glyphs
  std::vector<PlacedGlyph> m_placedGlyphs;

  HWND m_window = nullptr;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
  HANDLE m_frameLatencyWaitable = nullptr;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_renderTargetHeap;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srgbTargetHeap; // the back buffers' sRGB views, for the canvas
  std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FRAME_COUNT> m_backBuffers;
  UINT m_renderTargetStride = 0;
  std::uint32_t m_clientWidth = 0;
  std::uint32_t m_clientHeight = 0;
};

} // namespace Engine
