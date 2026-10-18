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

/// A stroke across a scene's inset (ADR-035): a capsule from (fromX, fromY) to (toX, toY) in its layers' pixels, x right and y
/// down from the inset's top left corner, a pixel's middle half a pixel in, fromWidth across at its start and toWidth at its end,
/// in color, 0x00RRGGBB, display-encoded, times intensity, which past 1 blooms.
struct SceneStroke
{
  float fromX = 0.0f;
  float fromY = 0.0f;
  float toX = 0.0f;
  float toY = 0.0f;
  float fromWidth = 1.0f;
  float toWidth = 1.0f;
  std::uint32_t color = 0;
  float intensity = 1.0f;
};

/// The sky behind a scene (ADR-035), fixed in the scene's frame: stars, starDensity of the sky's cells holding one, and a nebula
/// of two colours, nebulaDensity its strength, laid out by seed. It is drawn where the scene and its under layer hold nothing.
struct SceneSky
{
  bool shown = false;
  std::uint32_t seed = 0;
  std::array<std::uint32_t, 2> nebula{}; ///< 0x00RRGGBB, display-encoded
  float nebulaDensity = 0.0f;            ///< 0 to 1
  float starDensity = 0.0f;              ///< 0 to 1
};

/// Where a scene is drawn and how (ADR-033): an inset rectangle of the target, in design units when the frame draws a canvas
/// (ADR-034), or in the target's own pixels when it draws none (ADR-070); the camera's projection onto it; the light; the four
/// palette colours its models and layers are drawn in; and two layers of the inset's own pixels, under and over the scene, each a
/// palette index or SCENE_LAYER_CLEAR.
///
/// The camera looks along its z, x right and y down: a point (x, y, z) of the camera's frame is drawn centerX + focalX x / z
/// of the inset's units right of the inset's left edge and centerY + focalY y / z below its top, and nothing nearer than nearUnits.
/// The scene's frame is SceneSnapshot's: its camera orientation takes a direction of the scene into the camera's frame.
struct SceneInset
{
  float left = 0.0f; ///< in design units under a canvas, or in the target's pixels
  float top = 0.0f;
  float width = 0.0f;
  float height = 0.0f;
  float focalX = 0.0f; ///< the inset's units per unit of x / z
  float focalY = 0.0f;
  float centerX = 0.0f; ///< where the camera's axis meets the inset, in its units from its top left corner
  float centerY = 0.0f;
  float nearUnits = 1.0f;
  Vector3 towardsLight{.x = 0.0f, .y = 0.0f, .z = -1.0f}; ///< a unit vector towards the light, in the scene's frame
  std::array<std::uint32_t, 4> palette{};                 ///< 0x00RRGGBB
  std::uint32_t layerWidth = 0;                           ///< the layers' size in their own pixels
  std::uint32_t layerHeight = 0;
  float layersLeft = 0.0f; ///< where the layers lie in the inset, in its units from its top left; a width of 0 for all of it (D69)
  float layersTop = 0.0f;
  float layersWidth = 0.0f;
  float layersHeight = 0.0f;
  std::span<const std::uint8_t> under; ///< row by row: under the scene, where it draws nothing
  std::span<const std::uint8_t> over;  ///< row by row: over the scene
  std::span<const SceneStroke> dust;   ///< over the spheres and under the models (ADR-035)
  std::span<const SceneStroke> beams;  ///< over everything the scene draws
  SceneSky sky;
  float bloom = 0.0f; ///< how much of the scene's light past 1 is spread round it, 0 for none
};

/// A scene to draw: what is in it and where it goes.
struct SceneFrame
{
  const SceneSnapshot* scene = nullptr;
  SceneInset inset;
};

/// Draws the game with Direct3D 12 (ADR-031, after D47): a target of one background colour; over it, when a frame has one, a
/// scene in an inset of the target (ADR-033, ADR-035), drawn in a linear FP16 target with 4x MSAA and a D32_FLOAT depth buffer in reversed Z
/// at the inset's size on screen: its spheres, traced through each pixel, and the light round them; its dust; its models, flat
/// and lit; and its beams. It is resolved, its light past 1 bloomed at a quarter of its size, and laid between the inset's two
/// layers, with the sky behind the under one; and over both, when a frame has one, a canvas (ADR-034).
///
/// It draws into a window's swap chain (AttachWindow, Present), or, with no window, into a target of its own that it reads
/// back (Capture), on any device, WARP included, which is how the tests see what it draws. Two frames are in flight: the CPU
/// records one while the GPU draws the other, each with its own allocator and upload buffer. The swap chain holds a frame
/// latency of one, and Present waits on it before it records, so a frame is never more than one refresh old when it is shown.
class Renderer
{
public:
  /// The device to draw on.
  struct Desc
  {
    bool warp = false; ///< WARP, Direct3D 12's software device, rather than the hardware's
  };

  Renderer() = default; // not noexcept: a debug std::map allocates as it is made
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  ~Renderer();

  /// Sets up the device, the queue, the frames and the pipelines. Returns false if Direct3D 12 is not available.
  [[nodiscard]] bool Create(const Desc& _desc);

  /// A swap chain on _window's client area, which Present draws into. Returns false if Windows refuses it.
  [[nodiscard]] bool AttachWindow(HWND _window);

  /// Resizes the swap chain to the window's client area. Call it when the window reports a new size.
  [[nodiscard]] bool Resize();

  /// Model _model of the scenes this renderer draws, as _desc makes it; a model of that number already added is replaced, after
  /// the GPU is done with it. An object of a scene whose model was never added is not drawn. Returns false if the device fails.
  [[nodiscard]] bool AddModel(std::uint32_t _model, const SceneModelDesc& _desc);

  /// Draws one frame into the window and shows it: the target cleared to _background, 0x00RRGGBB; _scene, when there is one,
  /// over it; and _canvas (ADR-034), when there is one, over both, the scene's inset then in design units, else in the target's
  /// pixels. It waits for the swap chain before it records, so it paces its caller at the display's refresh, or at
  /// a frame's time when the window is minimised or hidden. Returns false if the device was lost.
  [[nodiscard]] bool Present(std::uint32_t _background, const SceneFrame* _scene = nullptr, const Canvas* _canvas = nullptr);

  /// Draws one frame as Present would into a window whose client area is _width x _height, and returns it, row by row, each
  /// pixel 0x00RRGGBB; empty if the device fails. It needs no window, and waits for the GPU before it returns.
  [[nodiscard]] std::vector<std::uint32_t> Capture(std::uint32_t _background, std::uint32_t _width, std::uint32_t _height,
                                                   const SceneFrame* _scene = nullptr, const Canvas* _canvas = nullptr);

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
  [[nodiscard]] bool CreateResourceHeap();
  [[nodiscard]] bool CreateScenePipelines();
  [[nodiscard]] bool CreateSpherePipelines(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& _modelDesc);
  [[nodiscard]] bool CreateBloomPipeline(const D3D12_GRAPHICS_PIPELINE_STATE_DESC& _compositeDesc);
  [[nodiscard]] bool CreateCanvasPipeline();
  [[nodiscard]] bool CreateWindowTargets();
  // The scene's targets at _width x _height, made again only when the size changes, after the GPU is done with the old.
  [[nodiscard]] bool SizeSceneTargets(std::uint32_t _width, std::uint32_t _height);
  // The frame's allocator reset and its commands begun, and _scene's layers uploaded.
  [[nodiscard]] bool BeginFrame(Frame& _frame, const SceneFrame* _scene);
  [[nodiscard]] bool UploadLayers(Frame& _frame, const SceneFrame& _scene);
  // What Present and Capture draw into _target, a target of _width x _height in _targetState, which it is left in:
  // _targetView its view and _srgbView its sRGB view. The background, the scene and the canvas.
  [[nodiscard]] bool DrawFrame(Frame& _frame, D3D12_CPU_DESCRIPTOR_HANDLE _targetView, D3D12_CPU_DESCRIPTOR_HANDLE _srgbView,
                               std::uint32_t _width, std::uint32_t _height, std::uint32_t _background, const SceneFrame* _scene,
                               const Canvas* _canvas, ID3D12Resource* _target, D3D12_RESOURCE_STATES _targetState);
  // _canvas into _srgbView, over what is there, its text laid out and its shapes uploaded through _frame.
  [[nodiscard]] bool DrawCanvas(Frame& _frame, D3D12_CPU_DESCRIPTOR_HANDLE _srgbView, std::uint32_t _width, std::uint32_t _height,
                                const Canvas& _canvas);
  // The inset's place on a target of _width x _height, in whole screen pixels: in design units when _design, else in the
  // target's pixels.
  [[nodiscard]] ScreenRect InsetPlace(const SceneInset& _inset, std::uint32_t _width, std::uint32_t _height, bool _design) const noexcept;
  // _scene into the scene's targets, resolved and bloomed, and then laid with its layers into _targetView's inset, _place.
  [[nodiscard]] bool DrawScene(D3D12_CPU_DESCRIPTOR_HANDLE _targetView, const ScreenRect& _place, const SceneFrame& _scene);
  void DrawModels(const SceneFrame& _scene, bool _lines);
  // _scene's spheres, in the pipeline set: their surfaces, or the light round them.
  void DrawSpheres(const SceneFrame& _scene);
  void DrawStrokes(const SceneFrame& _scene, std::span<const SceneStroke> _strokes);
  // The resolved scene's light past 1, at a quarter of its size, blurred, into the bloom's first target.
  void DrawBloom();
  void DrawComposite(D3D12_CPU_DESCRIPTOR_HANDLE _targetView, const ScreenRect& _place, const SceneFrame& _scene);
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

  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_shaderResourceHeap; // the scene's, its layers', the bloom's, the atlas's, the shapes'

  // The scene (ADR-033).
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_sceneSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_trianglePipeline;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_linePipeline;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_compositeSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_compositePipeline;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_sphereSignature; // and its glow (ADR-035)
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_spherePipeline;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_glowPipeline;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_strokeSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_strokePipeline;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_bloomSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_bloomPipeline;
  std::vector<Model> m_models;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneColor;    // FP16, 4x MSAA
  Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneDepth;    // D32_FLOAT, 4x MSAA, reversed Z
  Microsoft::WRL::ComPtr<ID3D12Resource> m_sceneResolved; // FP16, what the composite reads
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_sceneTargetHeap;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_sceneDepthHeap;
  std::uint32_t m_sceneWidth = 0;
  std::uint32_t m_sceneHeight = 0;
  std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2> m_bloomTargets; // FP16, a quarter of the scene's size: the bloom, and its blur
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_bloomTargetHeap;
  std::uint32_t m_bloomWidth = 0;
  std::uint32_t m_bloomHeight = 0;
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

  // Waits, while frames tear, until the next refresh's start: one refresh past the last frame's, or now if that has gone by.
  void PaceToRefresh() noexcept;

  HWND m_window = nullptr;
  Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swapChain;
  HANDLE m_frameLatencyWaitable = nullptr;
  // Frames tear where the system allows it (D69), with no vertical sync, one a refresh at most: the output's refresh rate,
  // looked up again now and then, the timer that waits for the next, and when that is due, in the performance counter's ticks.
  bool m_tearing = false;
  UINT m_swapChainFlags = 0;
  HANDLE m_paceTimer = nullptr;
  std::uint32_t m_refreshHertz = 0;
  std::int64_t m_refreshCheckedTicks = 0;
  std::int64_t m_nextFrameTicks = 0;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_renderTargetHeap;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srgbTargetHeap; // the back buffers' sRGB views, for the canvas
  std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, FRAME_COUNT> m_backBuffers;
  UINT m_renderTargetStride = 0;
  std::uint32_t m_clientWidth = 0;
  std::uint32_t m_clientHeight = 0;
};

} // namespace Engine
