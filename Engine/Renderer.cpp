#include "pch.h"

#include "Renderer.h"

#include "CompiledShader/CompositePS.h"
#include "CompiledShader/PresentPS.h"
#include "CompiledShader/PresentVS.h"
#include "CompiledShader/ScenePS.h"
#include "CompiledShader/SceneVS.h"

#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <cstring>

namespace Engine
{

namespace
{

constexpr DXGI_FORMAT TARGET_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT PICTURE_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM; // a 0x00RRGGBB word, little-endian
constexpr UINT PRESENT_SYNC_INTERVAL = 1;
constexpr UINT FRAME_LATENCY = 1;
constexpr UINT SWAP_CHAIN_FLAGS = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
constexpr DWORD UNSEEN_FRAME_MILLISECONDS = 16;         // a frame at 60 Hz, when there is no display to wait for
constexpr DWORD LONGEST_FRAME_WAIT_MILLISECONDS = 1000; // a swap chain that never signals does not hang the game
constexpr UINT ROOT_PARAMETER_PICTURE = 0;
constexpr UINT ROOT_PARAMETER_SCALE = 1;
constexpr UINT SCALE_CONSTANT_COUNT = 4;

// The scene (ADR-033).
constexpr DXGI_FORMAT SCENE_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;
constexpr DXGI_FORMAT SCENE_DEPTH_FORMAT = DXGI_FORMAT_D32_FLOAT;
constexpr DXGI_FORMAT LAYER_FORMAT = DXGI_FORMAT_R8G8_UINT; // under, then over
constexpr UINT SCENE_SAMPLES = 4;
constexpr float FARTHEST_DEPTH = 0.0f;                  // reversed Z: 1 at the near plane, 0 at infinity
constexpr float LINE_DEPTH_LIFT = 1.0f + 1.0f / 128.0f; // a line drawn a 128th nearer than its face, over every layer of it
constexpr UINT ROOT_PARAMETER_FRAME = 0;
constexpr UINT ROOT_PARAMETER_OBJECT = 1;
constexpr UINT ROOT_PARAMETER_PALETTE = 2;
constexpr UINT FRAME_CONSTANT_COUNT = 8;
constexpr UINT OBJECT_CONSTANT_COUNT = 12;
constexpr UINT PALETTE_CONSTANT_COUNT = 16;
constexpr UINT ROOT_PARAMETER_COMPOSITE_TEXTURES = 0;
constexpr UINT ROOT_PARAMETER_COMPOSITE_CONSTANTS = 1;
constexpr UINT COMPOSITE_CONSTANT_COUNT = 20;
constexpr UINT DESCRIPTOR_SCENE = 1; // then the layers, in the composite's table
constexpr UINT DESCRIPTOR_LAYERS = 2;
constexpr UINT DESCRIPTOR_COUNT = 3;
constexpr std::size_t LAYER_TEXEL_BYTES = 2;
constexpr std::size_t PALETTE_COLORS = 4;

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* _resource, D3D12_RESOURCE_STATES _before, D3D12_RESOURCE_STATES _after) noexcept
{
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = _resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = _before;
  barrier.Transition.StateAfter = _after;
  return barrier;
}

D3D12_HEAP_PROPERTIES HeapOfType(D3D12_HEAP_TYPE _type) noexcept
{
  return D3D12_HEAP_PROPERTIES{.Type = _type,
                               .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                               .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
                               .CreationNodeMask = 1,
                               .VisibleNodeMask = 1};
}

D3D12_RESOURCE_DESC BufferOfBytes(std::uint64_t _bytes) noexcept
{
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Width = _bytes;
  desc.Height = 1;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  return desc;
}

D3D12_RESOURCE_DESC TextureOfSize(std::uint32_t _width, std::uint32_t _height, DXGI_FORMAT _format, D3D12_RESOURCE_FLAGS _flags) noexcept
{
  D3D12_RESOURCE_DESC desc{};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = _width;
  desc.Height = _height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = _format;
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = _flags;
  return desc;
}

// A description whose enumerations have no zero value states every one of them: zero is not a default
// there but an invalid value.
constexpr D3D12_RENDER_TARGET_BLEND_DESC REPLACE_TARGET{.BlendEnable = FALSE,
                                                        .LogicOpEnable = FALSE,
                                                        .SrcBlend = D3D12_BLEND_ONE,
                                                        .DestBlend = D3D12_BLEND_ZERO,
                                                        .BlendOp = D3D12_BLEND_OP_ADD,
                                                        .SrcBlendAlpha = D3D12_BLEND_ONE,
                                                        .DestBlendAlpha = D3D12_BLEND_ZERO,
                                                        .BlendOpAlpha = D3D12_BLEND_OP_ADD,
                                                        .LogicOp = D3D12_LOGIC_OP_NOOP,
                                                        .RenderTargetWriteMask = static_cast<UINT8>(D3D12_COLOR_WRITE_ENABLE_ALL)};

constexpr D3D12_DEPTH_STENCILOP_DESC KEEP_STENCIL{.StencilFailOp = D3D12_STENCIL_OP_KEEP,
                                                  .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
                                                  .StencilPassOp = D3D12_STENCIL_OP_KEEP,
                                                  .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS};

float Channel(std::uint32_t _color, int _shift) noexcept
{
  return static_cast<float>((_color >> _shift) & 0xFFu) / 255.0f;
}

// A display-encoded channel, sRGB, in linear light.
float LinearChannel(std::uint32_t _color, int _shift) noexcept
{
  const float channel = Channel(_color, _shift);
  return channel <= 0.04045f ? channel / 12.92f : std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

Quaternion Product(const Quaternion& _a, const Quaternion& _b) noexcept
{
  return Quaternion{.x = _a.w * _b.x + _a.x * _b.w + _a.y * _b.z - _a.z * _b.y,
                    .y = _a.w * _b.y - _a.x * _b.z + _a.y * _b.w + _a.z * _b.x,
                    .z = _a.w * _b.z + _a.x * _b.y - _a.y * _b.x + _a.z * _b.w,
                    .w = _a.w * _b.w - _a.x * _b.x - _a.y * _b.y - _a.z * _b.z};
}

Quaternion Conjugate(const Quaternion& _q) noexcept
{
  return Quaternion{.x = -_q.x, .y = -_q.y, .z = -_q.z, .w = _q.w};
}

// _vector turned by _rotation, a unit quaternion.
Vector3 Rotate(const Quaternion& _rotation, const Vector3& _vector) noexcept
{
  const Vector3 twice{.x = 2.0f * (_rotation.y * _vector.z - _rotation.z * _vector.y),
                      .y = 2.0f * (_rotation.z * _vector.x - _rotation.x * _vector.z),
                      .z = 2.0f * (_rotation.x * _vector.y - _rotation.y * _vector.x)};
  return Vector3{.x = _vector.x + _rotation.w * twice.x + (_rotation.y * twice.z - _rotation.z * twice.y),
                 .y = _vector.y + _rotation.w * twice.y + (_rotation.z * twice.x - _rotation.x * twice.z),
                 .z = _vector.z + _rotation.w * twice.z + (_rotation.x * twice.y - _rotation.y * twice.x)};
}

template <std::size_t Count> std::array<std::uint32_t, Count> Bits(const std::array<float, Count>& _values) noexcept
{
  std::array<std::uint32_t, Count> bits{};
  std::ranges::transform(_values, bits.begin(), [](float _value) { return std::bit_cast<std::uint32_t>(_value); });
  return bits;
}

D3D12_RESOURCE_DESC MultisampledOfSize(std::uint32_t _width, std::uint32_t _height, DXGI_FORMAT _format,
                                       D3D12_RESOURCE_FLAGS _flags) noexcept
{
  D3D12_RESOURCE_DESC desc = TextureOfSize(_width, _height, _format, _flags);
  desc.SampleDesc.Count = SCENE_SAMPLES;
  return desc;
}

// The scene's vertex, as SceneVS reads it.
constexpr std::array<D3D12_INPUT_ELEMENT_DESC, 5> SCENE_VERTEX_LAYOUT = {{
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SceneVertex, position), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SceneVertex, normal), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(SceneVertex, facePoint), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(SceneVertex, paletteWeights), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, offsetof(SceneVertex, layer), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
}};
static_assert(sizeof(SceneVertex) == 14 * sizeof(float), "SceneVertex is SceneVS's vertex, packed");

[[nodiscard]] D3D12_ROOT_PARAMETER Constants(UINT _register, UINT _count, D3D12_SHADER_VISIBILITY _visibility) noexcept
{
  D3D12_ROOT_PARAMETER parameter{};
  parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameter.Constants.ShaderRegister = _register;
  parameter.Constants.Num32BitValues = _count;
  parameter.ShaderVisibility = _visibility;
  return parameter;
}

[[nodiscard]] bool CreateRootSignature(ID3D12Device* _device, const D3D12_ROOT_SIGNATURE_DESC& _desc,
                                       Microsoft::WRL::ComPtr<ID3D12RootSignature>& _signature)
{
  Microsoft::WRL::ComPtr<ID3DBlob> serialized;
  Microsoft::WRL::ComPtr<ID3DBlob> errors;
  return SUCCEEDED(D3D12SerializeRootSignature(&_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors)) &&
         SUCCEEDED(_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&_signature)));
}

} // namespace

ScreenRect PlacePicture(std::uint32_t _targetWidth, std::uint32_t _targetHeight, std::uint32_t _logicalWidth,
                        std::uint32_t _logicalHeight) noexcept
{
  const std::uint32_t scale = std::min(_targetWidth / _logicalWidth, _targetHeight / _logicalHeight);
  std::uint32_t width = _logicalWidth * scale;
  std::uint32_t height = _logicalHeight * scale;
  if (scale == 0)
  {
    // The largest fit, by whichever side runs out first, the other side rounded down.
    if (std::uint64_t{_targetWidth} * _logicalHeight <= std::uint64_t{_targetHeight} * _logicalWidth)
    {
      width = _targetWidth;
      height = static_cast<std::uint32_t>(std::uint64_t{_targetWidth} * _logicalHeight / _logicalWidth);
    }
    else
    {
      height = _targetHeight;
      width = static_cast<std::uint32_t>(std::uint64_t{_targetHeight} * _logicalWidth / _logicalHeight);
    }
  }
  return ScreenRect{.left = (_targetWidth - width) / 2, .top = (_targetHeight - height) / 2, .width = width, .height = height};
}

Renderer::~Renderer()
{
  if (m_queue && m_fence)
  {
    (void)WaitForGpu();
  }
  if (m_fenceEvent != nullptr)
  {
    CloseHandle(m_fenceEvent);
  }
  if (m_frameLatencyWaitable != nullptr)
  {
    CloseHandle(m_frameLatencyWaitable);
  }
}

bool Renderer::Create(const Desc& _desc)
{
  m_sourceWidth = _desc.sourceWidth;
  m_sourceHeight = _desc.sourceHeight;
  m_logicalWidth = _desc.logicalWidth;
  m_logicalHeight = _desc.logicalHeight;
  return CreateDevice(_desc.warp) && CreatePipeline() && CreateScenePipelines() && CreatePicture();
}

bool Renderer::CreateDevice(bool _warp)
{
  UINT factoryFlags = 0;
#ifdef _DEBUG
  Microsoft::WRL::ComPtr<ID3D12Debug> debug;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
  {
    debug->EnableDebugLayer();
    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif
  if (FAILED(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory))))
  {
    return false;
  }
  Microsoft::WRL::ComPtr<IDXGIAdapter> adapter; // none: the hardware's default
  if (_warp && FAILED(m_factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))))
  {
    return false;
  }
  if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
  {
    return false;
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue))))
  {
    return false;
  }
  for (Frame& frame : m_frames)
  {
    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&frame.allocator))))
    {
      return false;
    }
  }
  if (FAILED(
        m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_frames[0].allocator.Get(), nullptr, IID_PPV_ARGS(&m_commands))) ||
      FAILED(m_commands->Close()))
  {
    return false;
  }
  if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
  {
    return false;
  }
  m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  return m_fenceEvent != nullptr;
}

bool Renderer::CreatePipeline()
{
  D3D12_DESCRIPTOR_RANGE pictureRange{};
  pictureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  pictureRange.NumDescriptors = 1;
  pictureRange.BaseShaderRegister = 0;
  pictureRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  std::array<D3D12_ROOT_PARAMETER, 2> parameters{};
  parameters[ROOT_PARAMETER_PICTURE].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[ROOT_PARAMETER_PICTURE].DescriptorTable.NumDescriptorRanges = 1;
  parameters[ROOT_PARAMETER_PICTURE].DescriptorTable.pDescriptorRanges = &pictureRange;
  parameters[ROOT_PARAMETER_PICTURE].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[ROOT_PARAMETER_SCALE].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[ROOT_PARAMETER_SCALE].Constants.ShaderRegister = 0;
  parameters[ROOT_PARAMETER_SCALE].Constants.Num32BitValues = SCALE_CONSTANT_COUNT;
  parameters[ROOT_PARAMETER_SCALE].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  const D3D12_STATIC_SAMPLER_DESC sampler{.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
                                          .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                          .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                          .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                          .MipLODBias = 0.0f,
                                          .MaxAnisotropy = 1,
                                          .ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
                                          .BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
                                          .MinLOD = 0.0f,
                                          .MaxLOD = D3D12_FLOAT32_MAX,
                                          .ShaderRegister = 0,
                                          .RegisterSpace = 0,
                                          .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL};

  D3D12_ROOT_SIGNATURE_DESC rootDesc{};
  rootDesc.NumParameters = static_cast<UINT>(parameters.size());
  rootDesc.pParameters = parameters.data();
  rootDesc.NumStaticSamplers = 1;
  rootDesc.pStaticSamplers = &sampler;
  rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  Microsoft::WRL::ComPtr<ID3DBlob> serialized;
  Microsoft::WRL::ComPtr<ID3DBlob> errors;
  if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors)) ||
      FAILED(m_device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature))))
  {
    return false;
  }

  const D3D12_GRAPHICS_PIPELINE_STATE_DESC pipelineDesc{
    .pRootSignature = m_rootSignature.Get(),
    .VS = {g_PresentVS, sizeof(g_PresentVS)},
    .PS = {g_PresentPS, sizeof(g_PresentPS)},
    .DS = {},
    .HS = {},
    .GS = {},
    .StreamOutput = {},
    .BlendState = {.AlphaToCoverageEnable = FALSE,
                   .IndependentBlendEnable = FALSE,
                   .RenderTarget = {REPLACE_TARGET, REPLACE_TARGET, REPLACE_TARGET, REPLACE_TARGET, REPLACE_TARGET, REPLACE_TARGET,
                                    REPLACE_TARGET, REPLACE_TARGET}},
    .SampleMask = UINT_MAX,
    .RasterizerState = {.FillMode = D3D12_FILL_MODE_SOLID,
                        .CullMode = D3D12_CULL_MODE_NONE,
                        .FrontCounterClockwise = FALSE,
                        .DepthBias = 0,
                        .DepthBiasClamp = 0.0f,
                        .SlopeScaledDepthBias = 0.0f,
                        .DepthClipEnable = TRUE,
                        .MultisampleEnable = FALSE,
                        .AntialiasedLineEnable = FALSE,
                        .ForcedSampleCount = 0,
                        .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF},
    .DepthStencilState = {.DepthEnable = FALSE,
                          .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO,
                          .DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS,
                          .StencilEnable = FALSE,
                          .StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
                          .StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
                          .FrontFace = KEEP_STENCIL,
                          .BackFace = KEEP_STENCIL},
    .InputLayout = {},
    .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .NumRenderTargets = 1,
    .RTVFormats = {TARGET_FORMAT},
    .DSVFormat = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .NodeMask = 0,
    .CachedPSO = {},
    .Flags = D3D12_PIPELINE_STATE_FLAG_NONE};
  return SUCCEEDED(m_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&m_pipeline)));
}

bool Renderer::CreateScenePipelines()
{
  // The models: three sets of constants, for the vertex shader alone.
  const std::array<D3D12_ROOT_PARAMETER, 3> sceneParameters = {Constants(0, FRAME_CONSTANT_COUNT, D3D12_SHADER_VISIBILITY_VERTEX),
                                                               Constants(1, OBJECT_CONSTANT_COUNT, D3D12_SHADER_VISIBILITY_VERTEX),
                                                               Constants(2, PALETTE_CONSTANT_COUNT, D3D12_SHADER_VISIBILITY_VERTEX)};
  D3D12_ROOT_SIGNATURE_DESC sceneDesc{};
  sceneDesc.NumParameters = static_cast<UINT>(sceneParameters.size());
  sceneDesc.pParameters = sceneParameters.data();
  sceneDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  if (!CreateRootSignature(m_device.Get(), sceneDesc, m_sceneSignature))
  {
    return false;
  }
  D3D12_GRAPHICS_PIPELINE_STATE_DESC modelDesc{
    .pRootSignature = m_sceneSignature.Get(),
    .VS = {g_SceneVS, sizeof(g_SceneVS)},
    .PS = {g_ScenePS, sizeof(g_ScenePS)},
    .DS = {},
    .HS = {},
    .GS = {},
    .StreamOutput = {},
    .BlendState = {.AlphaToCoverageEnable = FALSE,
                   .IndependentBlendEnable = FALSE,
                   .RenderTarget = {REPLACE_TARGET, REPLACE_TARGET, REPLACE_TARGET, REPLACE_TARGET, REPLACE_TARGET, REPLACE_TARGET,
                                    REPLACE_TARGET, REPLACE_TARGET}},
    .SampleMask = UINT_MAX,
    // No culling by winding: a face is culled by its normal, in SceneVS, as the game culls it.
    .RasterizerState = {.FillMode = D3D12_FILL_MODE_SOLID,
                        .CullMode = D3D12_CULL_MODE_NONE,
                        .FrontCounterClockwise = FALSE,
                        .DepthBias = 0,
                        .DepthBiasClamp = 0.0f,
                        .SlopeScaledDepthBias = 0.0f,
                        .DepthClipEnable = TRUE,
                        .MultisampleEnable = TRUE,
                        .AntialiasedLineEnable = FALSE,
                        .ForcedSampleCount = 0,
                        .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF},
    .DepthStencilState = {.DepthEnable = TRUE,
                          .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL,
                          .DepthFunc = D3D12_COMPARISON_FUNC_GREATER_EQUAL,
                          .StencilEnable = FALSE,
                          .StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
                          .StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
                          .FrontFace = KEEP_STENCIL,
                          .BackFace = KEEP_STENCIL},
    .InputLayout = {SCENE_VERTEX_LAYOUT.data(), static_cast<UINT>(SCENE_VERTEX_LAYOUT.size())},
    .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .NumRenderTargets = 1,
    .RTVFormats = {SCENE_FORMAT},
    .DSVFormat = SCENE_DEPTH_FORMAT,
    .SampleDesc = {.Count = SCENE_SAMPLES, .Quality = 0},
    .NodeMask = 0,
    .CachedPSO = {},
    .Flags = D3D12_PIPELINE_STATE_FLAG_NONE};
  if (FAILED(m_device->CreateGraphicsPipelineState(&modelDesc, IID_PPV_ARGS(&m_trianglePipeline))))
  {
    return false;
  }
  modelDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
  if (FAILED(m_device->CreateGraphicsPipelineState(&modelDesc, IID_PPV_ARGS(&m_linePipeline))))
  {
    return false;
  }

  // The composite: the resolved scene and the layers, read by the pixel shader, and its constants.
  D3D12_DESCRIPTOR_RANGE textures{};
  textures.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  textures.NumDescriptors = DESCRIPTOR_COUNT - DESCRIPTOR_SCENE;
  textures.BaseShaderRegister = 0;
  textures.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  std::array<D3D12_ROOT_PARAMETER, 2> compositeParameters{};
  compositeParameters[ROOT_PARAMETER_COMPOSITE_TEXTURES].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  compositeParameters[ROOT_PARAMETER_COMPOSITE_TEXTURES].DescriptorTable.NumDescriptorRanges = 1;
  compositeParameters[ROOT_PARAMETER_COMPOSITE_TEXTURES].DescriptorTable.pDescriptorRanges = &textures;
  compositeParameters[ROOT_PARAMETER_COMPOSITE_TEXTURES].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  compositeParameters[ROOT_PARAMETER_COMPOSITE_CONSTANTS] = Constants(0, COMPOSITE_CONSTANT_COUNT, D3D12_SHADER_VISIBILITY_PIXEL);
  D3D12_ROOT_SIGNATURE_DESC compositeDesc{};
  compositeDesc.NumParameters = static_cast<UINT>(compositeParameters.size());
  compositeDesc.pParameters = compositeParameters.data();
  compositeDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  if (!CreateRootSignature(m_device.Get(), compositeDesc, m_compositeSignature))
  {
    return false;
  }
  const D3D12_GRAPHICS_PIPELINE_STATE_DESC compositePipelineDesc{
    .pRootSignature = m_compositeSignature.Get(),
    .VS = {g_PresentVS, sizeof(g_PresentVS)},
    .PS = {g_CompositePS, sizeof(g_CompositePS)},
    .DS = {},
    .HS = {},
    .GS = {},
    .StreamOutput = {},
    .BlendState = modelDesc.BlendState,
    .SampleMask = UINT_MAX,
    .RasterizerState = {.FillMode = D3D12_FILL_MODE_SOLID,
                        .CullMode = D3D12_CULL_MODE_NONE,
                        .FrontCounterClockwise = FALSE,
                        .DepthBias = 0,
                        .DepthBiasClamp = 0.0f,
                        .SlopeScaledDepthBias = 0.0f,
                        .DepthClipEnable = TRUE,
                        .MultisampleEnable = FALSE,
                        .AntialiasedLineEnable = FALSE,
                        .ForcedSampleCount = 0,
                        .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF},
    .DepthStencilState = {.DepthEnable = FALSE,
                          .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO,
                          .DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS,
                          .StencilEnable = FALSE,
                          .StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
                          .StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
                          .FrontFace = KEEP_STENCIL,
                          .BackFace = KEEP_STENCIL},
    .InputLayout = {},
    .IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED,
    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    .NumRenderTargets = 1,
    .RTVFormats = {TARGET_FORMAT},
    .DSVFormat = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .NodeMask = 0,
    .CachedPSO = {},
    .Flags = D3D12_PIPELINE_STATE_FLAG_NONE};
  if (FAILED(m_device->CreateGraphicsPipelineState(&compositePipelineDesc, IID_PPV_ARGS(&m_compositePipeline))))
  {
    return false;
  }

  D3D12_DESCRIPTOR_HEAP_DESC targetHeap{};
  targetHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  targetHeap.NumDescriptors = 1;
  D3D12_DESCRIPTOR_HEAP_DESC depthHeap{};
  depthHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  depthHeap.NumDescriptors = 1;
  return SUCCEEDED(m_device->CreateDescriptorHeap(&targetHeap, IID_PPV_ARGS(&m_sceneTargetHeap))) &&
         SUCCEEDED(m_device->CreateDescriptorHeap(&depthHeap, IID_PPV_ARGS(&m_sceneDepthHeap)));
}

bool Renderer::CreatePicture()
{
  const D3D12_RESOURCE_DESC textureDesc = TextureOfSize(m_sourceWidth, m_sourceHeight, PICTURE_FORMAT, D3D12_RESOURCE_FLAG_NONE);
  const D3D12_HEAP_PROPERTIES defaultHeap = HeapOfType(D3D12_HEAP_TYPE_DEFAULT);
  if (FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&m_picture))))
  {
    return false;
  }

  UINT64 uploadBytes = 0;
  m_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &m_footprint, nullptr, nullptr, &uploadBytes);
  m_uploadBytes = uploadBytes;
  const D3D12_RESOURCE_DESC bufferDesc = BufferOfBytes(m_uploadBytes);
  const D3D12_HEAP_PROPERTIES uploadHeap = HeapOfType(D3D12_HEAP_TYPE_UPLOAD);
  for (Frame& frame : m_frames)
  {
    if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&frame.upload))))
    {
      return false;
    }
  }

  D3D12_DESCRIPTOR_HEAP_DESC resourceHeap{};
  resourceHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  resourceHeap.NumDescriptors = DESCRIPTOR_COUNT; // the picture, then the scene and its layers
  resourceHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(m_device->CreateDescriptorHeap(&resourceHeap, IID_PPV_ARGS(&m_shaderResourceHeap))))
  {
    return false;
  }
  m_resourceStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
  D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc{};
  viewDesc.Format = PICTURE_FORMAT;
  viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  viewDesc.Texture2D.MipLevels = 1;
  m_device->CreateShaderResourceView(m_picture.Get(), &viewDesc, m_shaderResourceHeap->GetCPUDescriptorHandleForHeapStart());
  return true;
}

bool Renderer::AttachWindow(HWND _window)
{
  m_window = _window;
  RECT client{};
  GetClientRect(_window, &client);
  m_clientWidth = static_cast<std::uint32_t>(std::max<LONG>(client.right - client.left, 1));
  m_clientHeight = static_cast<std::uint32_t>(std::max<LONG>(client.bottom - client.top, 1));

  DXGI_SWAP_CHAIN_DESC1 swapDesc{};
  swapDesc.Width = m_clientWidth;
  swapDesc.Height = m_clientHeight;
  swapDesc.Format = TARGET_FORMAT;
  swapDesc.SampleDesc.Count = 1;
  swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapDesc.BufferCount = FRAME_COUNT;
  swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapDesc.Flags = SWAP_CHAIN_FLAGS;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
  if (FAILED(m_factory->CreateSwapChainForHwnd(m_queue.Get(), _window, &swapDesc, nullptr, nullptr, &swapChain)) ||
      FAILED(swapChain.As(&m_swapChain)) || FAILED(m_swapChain->SetMaximumFrameLatency(FRAME_LATENCY)))
  {
    return false;
  }
  m_frameLatencyWaitable = m_swapChain->GetFrameLatencyWaitableObject();
  if (m_frameLatencyWaitable == nullptr)
  {
    return false;
  }
  m_factory->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER);

  D3D12_DESCRIPTOR_HEAP_DESC targetHeap{};
  targetHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  targetHeap.NumDescriptors = FRAME_COUNT;
  if (FAILED(m_device->CreateDescriptorHeap(&targetHeap, IID_PPV_ARGS(&m_renderTargetHeap))))
  {
    return false;
  }
  m_renderTargetStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  return CreateWindowTargets();
}

bool Renderer::CreateWindowTargets()
{
  D3D12_CPU_DESCRIPTOR_HANDLE handle = m_renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
  for (UINT index = 0; index < FRAME_COUNT; ++index)
  {
    if (FAILED(m_swapChain->GetBuffer(index, IID_PPV_ARGS(&m_backBuffers[index]))))
    {
      return false;
    }
    m_device->CreateRenderTargetView(m_backBuffers[index].Get(), nullptr, handle);
    handle.ptr += m_renderTargetStride;
  }
  return true;
}

bool Renderer::Resize()
{
  RECT client{};
  GetClientRect(m_window, &client);
  const auto width = static_cast<std::uint32_t>(client.right - client.left);
  const auto height = static_cast<std::uint32_t>(client.bottom - client.top);
  if (width == 0 || height == 0)
  {
    return true; // minimised: keep the old buffers until the window comes back
  }
  if (!WaitForGpu())
  {
    return false;
  }
  for (Microsoft::WRL::ComPtr<ID3D12Resource>& buffer : m_backBuffers)
  {
    buffer.Reset();
  }
  if (FAILED(m_swapChain->ResizeBuffers(FRAME_COUNT, width, height, DXGI_FORMAT_UNKNOWN, SWAP_CHAIN_FLAGS)))
  {
    return false;
  }
  m_clientWidth = width;
  m_clientHeight = height;
  return CreateWindowTargets();
}

bool Renderer::AddModel(std::uint32_t _model, const SceneModelDesc& _desc)
{
  if (_model < m_models.size() && m_models[_model].vertices && !WaitForGpu())
  {
    return false;
  }
  if (_model >= m_models.size())
  {
    m_models.resize(std::size_t{_model} + 1);
  }
  Model& model = m_models[_model];
  model = Model{};
  const std::size_t triangleBytes = _desc.triangles.size_bytes();
  const std::size_t bytes = triangleBytes + _desc.lines.size_bytes();
  if (bytes == 0)
  {
    return true;
  }
  // An upload heap the GPU reads in place: a model is small and written once.
  const D3D12_HEAP_PROPERTIES uploadHeap = HeapOfType(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_RESOURCE_DESC bufferDesc = BufferOfBytes(bytes);
  if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&model.vertices))))
  {
    return false;
  }
  void* mapped = nullptr;
  const D3D12_RANGE nothingRead{0, 0};
  if (FAILED(model.vertices->Map(0, &nothingRead, &mapped)))
  {
    return false;
  }
  std::memcpy(mapped, _desc.triangles.data(), triangleBytes);
  std::memcpy(static_cast<std::byte*>(mapped) + triangleBytes, _desc.lines.data(), _desc.lines.size_bytes());
  model.vertices->Unmap(0, nullptr);
  const D3D12_GPU_VIRTUAL_ADDRESS start = model.vertices->GetGPUVirtualAddress();
  model.triangles = D3D12_VERTEX_BUFFER_VIEW{start, static_cast<UINT>(triangleBytes), sizeof(SceneVertex)};
  model.lines = D3D12_VERTEX_BUFFER_VIEW{start + triangleBytes, static_cast<UINT>(_desc.lines.size_bytes()), sizeof(SceneVertex)};
  model.triangleVertices = static_cast<UINT>(_desc.triangles.size());
  model.lineVertices = static_cast<UINT>(_desc.lines.size());
  return true;
}

bool Renderer::SizeSceneTargets(std::uint32_t _width, std::uint32_t _height)
{
  if (m_sceneColor && m_sceneWidth == _width && m_sceneHeight == _height)
  {
    return true;
  }
  if (m_sceneColor && !WaitForGpu())
  {
    return false;
  }
  m_sceneColor.Reset();
  m_sceneDepth.Reset();
  m_sceneResolved.Reset();
  const D3D12_HEAP_PROPERTIES defaultHeap = HeapOfType(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC colorDesc = MultisampledOfSize(_width, _height, SCENE_FORMAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  D3D12_CLEAR_VALUE clearColor{};
  clearColor.Format = SCENE_FORMAT;
  const D3D12_RESOURCE_DESC depthDesc = MultisampledOfSize(_width, _height, SCENE_DEPTH_FORMAT, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);
  D3D12_CLEAR_VALUE clearDepth{};
  clearDepth.Format = SCENE_DEPTH_FORMAT;
  clearDepth.DepthStencil.Depth = FARTHEST_DEPTH;
  const D3D12_RESOURCE_DESC resolvedDesc = TextureOfSize(_width, _height, SCENE_FORMAT, D3D12_RESOURCE_FLAG_NONE);
  // Between frames the scene's colour is where the resolve left it, and the resolved copy where the composite read it.
  if (FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &colorDesc, D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
                                               &clearColor, IID_PPV_ARGS(&m_sceneColor))) ||
      FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDesc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                               &clearDepth, IID_PPV_ARGS(&m_sceneDepth))) ||
      FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &resolvedDesc,
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&m_sceneResolved))))
  {
    m_sceneColor.Reset();
    return false;
  }
  m_device->CreateRenderTargetView(m_sceneColor.Get(), nullptr, m_sceneTargetHeap->GetCPUDescriptorHandleForHeapStart());
  m_device->CreateDepthStencilView(m_sceneDepth.Get(), nullptr, m_sceneDepthHeap->GetCPUDescriptorHandleForHeapStart());
  D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc{};
  viewDesc.Format = SCENE_FORMAT;
  viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  viewDesc.Texture2D.MipLevels = 1;
  D3D12_CPU_DESCRIPTOR_HANDLE view = m_shaderResourceHeap->GetCPUDescriptorHandleForHeapStart();
  view.ptr += std::size_t{DESCRIPTOR_SCENE} * m_resourceStride;
  m_device->CreateShaderResourceView(m_sceneResolved.Get(), &viewDesc, view);
  m_sceneWidth = _width;
  m_sceneHeight = _height;
  return true;
}

bool Renderer::Drawable(const SceneFrame* _scene) noexcept
{
  if (_scene == nullptr || _scene->scene == nullptr)
  {
    return false;
  }
  const SceneInset& inset = _scene->inset;
  const std::size_t layerPixels = std::size_t{inset.layerWidth} * inset.layerHeight;
  return inset.width > 0.0f && inset.height > 0.0f && layerPixels != 0 && inset.under.size() >= layerPixels &&
         inset.over.size() >= layerPixels;
}

ScreenRect Renderer::InsetPlace(const SceneInset& _inset, std::uint32_t _width, std::uint32_t _height) const noexcept
{
  const ScreenRect picture = PlacePicture(_width, _height, m_logicalWidth, m_logicalHeight);
  const float across = static_cast<float>(picture.width) / static_cast<float>(m_sourceWidth);
  const float down = static_cast<float>(picture.height) / static_cast<float>(m_sourceHeight);
  const auto edge = [](std::uint32_t _origin, float _at, float _scale)
  { return _origin + static_cast<std::uint32_t>(std::lround(std::max(_at, 0.0f) * _scale)); };
  const std::uint32_t left = edge(picture.left, _inset.left, across);
  const std::uint32_t top = edge(picture.top, _inset.top, down);
  const std::uint32_t right = std::min(edge(picture.left, _inset.left + _inset.width, across), picture.left + picture.width);
  const std::uint32_t bottom = std::min(edge(picture.top, _inset.top + _inset.height, down), picture.top + picture.height);
  return ScreenRect{.left = left, .top = top, .width = right > left ? right - left : 0, .height = bottom > top ? bottom - top : 0};
}

bool Renderer::Present(std::span<const std::uint32_t> _pixels, std::uint32_t _border, const SceneFrame* _scene)
{
  if (_pixels.size() < std::size_t{m_sourceWidth} * m_sourceHeight)
  {
    return true;
  }
  if (IsIconic(m_window))
  {
    Sleep(UNSEEN_FRAME_MILLISECONDS); // nothing to wait for the display on, so the caller is paced here
    return true;
  }
  // The swap chain signals when it can take a frame: at most FRAME_LATENCY queued, so what is drawn now is shown next.
  WaitForSingleObjectEx(m_frameLatencyWaitable, LONGEST_FRAME_WAIT_MILLISECONDS, TRUE);

  Frame& frame = m_frames[m_nextFrame];
  const SceneFrame* scene = Drawable(_scene) ? _scene : nullptr;
  if (scene != nullptr)
  {
    const ScreenRect inset = InsetPlace(scene->inset, m_clientWidth, m_clientHeight);
    if (inset.width == 0 || inset.height == 0)
    {
      scene = nullptr;
    }
    else if (!SizeSceneTargets(inset.width, inset.height))
    {
      return false;
    }
  }
  if (!BeginFrame(frame, _pixels, scene))
  {
    return false;
  }
  const UINT index = m_swapChain->GetCurrentBackBufferIndex();
  ID3D12Resource* target = m_backBuffers[index].Get();
  const D3D12_RESOURCE_BARRIER toTarget = Transition(target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_commands->ResourceBarrier(1, &toTarget);
  D3D12_CPU_DESCRIPTOR_HANDLE targetView = m_renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
  targetView.ptr += std::size_t{index} * m_renderTargetStride;
  DrawPicture(targetView, m_clientWidth, m_clientHeight, _border);
  if (scene != nullptr && !DrawScene(targetView, m_clientWidth, m_clientHeight, *scene))
  {
    return false;
  }
  const D3D12_RESOURCE_BARRIER toPresent = Transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
  m_commands->ResourceBarrier(1, &toPresent);
  if (FAILED(m_commands->Close()))
  {
    return false;
  }
  ID3D12CommandList* lists[] = {m_commands.Get()};
  m_queue->ExecuteCommandLists(1, lists);
  const HRESULT presented = m_swapChain->Present(PRESENT_SYNC_INTERVAL, 0);
  if (FAILED(presented) || !Submit(frame))
  {
    return false;
  }
  if (presented == DXGI_STATUS_OCCLUDED)
  {
    Sleep(UNSEEN_FRAME_MILLISECONDS);
  }
  return true;
}

std::vector<std::uint32_t> Renderer::Capture(std::span<const std::uint32_t> _pixels, std::uint32_t _border, std::uint32_t _width,
                                             std::uint32_t _height, const SceneFrame* _scene)
{
  if (_pixels.size() < std::size_t{m_sourceWidth} * m_sourceHeight || _width == 0 || _height == 0)
  {
    return {};
  }
  // A target of the window's size, in the swap chain's format, and a buffer to read it back through.
  const D3D12_RESOURCE_DESC targetDesc = TextureOfSize(_width, _height, TARGET_FORMAT, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
  const D3D12_HEAP_PROPERTIES defaultHeap = HeapOfType(D3D12_HEAP_TYPE_DEFAULT);
  Microsoft::WRL::ComPtr<ID3D12Resource> target;
  if (FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &targetDesc, D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr,
                                               IID_PPV_ARGS(&target))))
  {
    return {};
  }
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 readbackBytes = 0;
  m_device->GetCopyableFootprints(&targetDesc, 0, 1, 0, &footprint, nullptr, nullptr, &readbackBytes);
  const D3D12_RESOURCE_DESC bufferDesc = BufferOfBytes(readbackBytes);
  const D3D12_HEAP_PROPERTIES readbackHeap = HeapOfType(D3D12_HEAP_TYPE_READBACK);
  Microsoft::WRL::ComPtr<ID3D12Resource> readback;
  if (FAILED(m_device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&readback))))
  {
    return {};
  }
  D3D12_DESCRIPTOR_HEAP_DESC targetHeapDesc{};
  targetHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  targetHeapDesc.NumDescriptors = 1;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> targetHeap;
  if (FAILED(m_device->CreateDescriptorHeap(&targetHeapDesc, IID_PPV_ARGS(&targetHeap))))
  {
    return {};
  }
  const D3D12_CPU_DESCRIPTOR_HANDLE targetView = targetHeap->GetCPUDescriptorHandleForHeapStart();
  m_device->CreateRenderTargetView(target.Get(), nullptr, targetView);

  Frame& frame = m_frames[m_nextFrame];
  const SceneFrame* scene = Drawable(_scene) ? _scene : nullptr;
  if (scene != nullptr)
  {
    const ScreenRect inset = InsetPlace(scene->inset, _width, _height);
    if (inset.width == 0 || inset.height == 0)
    {
      scene = nullptr;
    }
    else if (!SizeSceneTargets(inset.width, inset.height))
    {
      return {};
    }
  }
  if (!BeginFrame(frame, _pixels, scene))
  {
    return {};
  }
  DrawPicture(targetView, _width, _height, _border);
  if (scene != nullptr && !DrawScene(targetView, _width, _height, *scene))
  {
    return {};
  }
  const D3D12_RESOURCE_BARRIER toSource = Transition(target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
  m_commands->ResourceBarrier(1, &toSource);
  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = readback.Get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = target.Get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  source.SubresourceIndex = 0;
  m_commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  if (FAILED(m_commands->Close()))
  {
    return {};
  }
  ID3D12CommandList* lists[] = {m_commands.Get()};
  m_queue->ExecuteCommandLists(1, lists);
  if (!Submit(frame) || !WaitForGpu())
  {
    return {};
  }

  void* mapped = nullptr;
  const D3D12_RANGE everything{0, static_cast<SIZE_T>(readbackBytes)};
  if (FAILED(readback->Map(0, &everything, &mapped)))
  {
    return {};
  }
  // Each pixel of the target is R, G, B and A, a byte each.
  std::vector<std::uint32_t> pixels(std::size_t{_width} * _height);
  const auto* rows = static_cast<const std::uint8_t*>(mapped) + footprint.Offset;
  for (std::uint32_t row = 0; row < _height; ++row)
  {
    const std::uint8_t* bytes = rows + std::size_t{row} * footprint.Footprint.RowPitch;
    for (std::uint32_t column = 0; column < _width; ++column)
    {
      const std::uint8_t* pixel = bytes + std::size_t{column} * 4;
      pixels[std::size_t{row} * _width + column] = (std::uint32_t{pixel[0]} << 16) | (std::uint32_t{pixel[1]} << 8) | pixel[2];
    }
  }
  const D3D12_RANGE nothingWritten{0, 0};
  readback->Unmap(0, &nothingWritten);
  return pixels;
}

bool Renderer::BeginFrame(Frame& _frame, std::span<const std::uint32_t> _pixels, const SceneFrame* _scene)
{
  // The frame's allocator and upload buffer are the GPU's until the frame it last drew is done.
  if (!WaitForFrame(_frame))
  {
    return false;
  }
  if (_scene != nullptr && (!m_layers || m_layerWidth != _scene->inset.layerWidth || m_layerHeight != _scene->inset.layerHeight))
  {
    // The layers' texture, and each frame's buffer to fill it from, at the layers' size, once the GPU is done with the old.
    if (m_layers && !WaitForGpu())
    {
      return false;
    }
    m_layerWidth = _scene->inset.layerWidth;
    m_layerHeight = _scene->inset.layerHeight;
    const D3D12_RESOURCE_DESC layerDesc = TextureOfSize(m_layerWidth, m_layerHeight, LAYER_FORMAT, D3D12_RESOURCE_FLAG_NONE);
    const D3D12_HEAP_PROPERTIES defaultHeap = HeapOfType(D3D12_HEAP_TYPE_DEFAULT);
    m_layers.Reset();
    if (FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &layerDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&m_layers))))
    {
      return false;
    }
    UINT64 layerBytes = 0;
    m_device->GetCopyableFootprints(&layerDesc, 0, 1, 0, &m_layerFootprint, nullptr, nullptr, &layerBytes);
    const D3D12_RESOURCE_DESC bufferDesc = BufferOfBytes(layerBytes);
    const D3D12_HEAP_PROPERTIES uploadHeap = HeapOfType(D3D12_HEAP_TYPE_UPLOAD);
    for (Frame& frame : m_frames)
    {
      frame.layerUpload.Reset();
      if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                   nullptr, IID_PPV_ARGS(&frame.layerUpload))))
      {
        return false;
      }
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC viewDesc{};
    viewDesc.Format = LAYER_FORMAT;
    viewDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    viewDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    viewDesc.Texture2D.MipLevels = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE view = m_shaderResourceHeap->GetCPUDescriptorHandleForHeapStart();
    view.ptr += std::size_t{DESCRIPTOR_LAYERS} * m_resourceStride;
    m_device->CreateShaderResourceView(m_layers.Get(), &viewDesc, view);
  }
  void* mapped = nullptr;
  const D3D12_RANGE nothingRead{0, 0};
  if (FAILED(_frame.upload->Map(0, &nothingRead, &mapped)))
  {
    return false;
  }
  auto* rows = static_cast<std::byte*>(mapped) + m_footprint.Offset;
  for (std::uint32_t row = 0; row < m_sourceHeight; ++row)
  {
    std::memcpy(rows + std::size_t{row} * m_footprint.Footprint.RowPitch, _pixels.data() + std::size_t{row} * m_sourceWidth,
                std::size_t{m_sourceWidth} * sizeof(std::uint32_t));
  }
  _frame.upload->Unmap(0, nullptr);

  if (FAILED(_frame.allocator->Reset()) || FAILED(m_commands->Reset(_frame.allocator.Get(), m_pipeline.Get())))
  {
    return false;
  }
  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = m_picture.Get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  destination.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = _frame.upload.Get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = m_footprint;
  m_commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
  const D3D12_RESOURCE_BARRIER toRead =
    Transition(m_picture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_commands->ResourceBarrier(1, &toRead);
  if (_scene == nullptr)
  {
    return true;
  }

  // The layers, each pixel's under and over side by side.
  if (FAILED(_frame.layerUpload->Map(0, &nothingRead, &mapped)))
  {
    return false;
  }
  auto* layerRows = static_cast<std::uint8_t*>(mapped) + m_layerFootprint.Offset;
  for (std::uint32_t row = 0; row < m_layerHeight; ++row)
  {
    std::uint8_t* texel = layerRows + std::size_t{row} * m_layerFootprint.Footprint.RowPitch;
    for (std::uint32_t column = 0; column < m_layerWidth; ++column)
    {
      const std::size_t pixel = std::size_t{row} * m_layerWidth + column;
      texel[0] = _scene->inset.under[pixel];
      texel[1] = _scene->inset.over[pixel];
      texel += LAYER_TEXEL_BYTES;
    }
  }
  _frame.layerUpload->Unmap(0, nullptr);
  D3D12_TEXTURE_COPY_LOCATION layerDestination{};
  layerDestination.pResource = m_layers.Get();
  layerDestination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  layerDestination.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION layerSource{};
  layerSource.pResource = _frame.layerUpload.Get();
  layerSource.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  layerSource.PlacedFootprint = m_layerFootprint;
  m_commands->CopyTextureRegion(&layerDestination, 0, 0, 0, &layerSource, nullptr);
  const D3D12_RESOURCE_BARRIER layersToRead =
    Transition(m_layers.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_commands->ResourceBarrier(1, &layersToRead);
  return true;
}

void Renderer::DrawPicture(D3D12_CPU_DESCRIPTOR_HANDLE _targetView, std::uint32_t _width, std::uint32_t _height, std::uint32_t _border)
{
  m_commands->OMSetRenderTargets(1, &_targetView, FALSE, nullptr);
  const std::array<float, 4> border = {Channel(_border, 16), Channel(_border, 8), Channel(_border, 0), 1.0f};
  m_commands->ClearRenderTargetView(_targetView, border.data(), 0, nullptr);

  const ScreenRect place = PlacePicture(_width, _height, m_logicalWidth, m_logicalHeight);
  D3D12_VIEWPORT viewport{};
  viewport.TopLeftX = static_cast<float>(place.left);
  viewport.TopLeftY = static_cast<float>(place.top);
  viewport.Width = static_cast<float>(place.width);
  viewport.Height = static_cast<float>(place.height);
  viewport.MaxDepth = 1.0f;
  const D3D12_RECT scissor{static_cast<LONG>(place.left), static_cast<LONG>(place.top), static_cast<LONG>(place.left + place.width),
                           static_cast<LONG>(place.top + place.height)};
  m_commands->RSSetViewports(1, &viewport);
  m_commands->RSSetScissorRects(1, &scissor);

  m_commands->SetGraphicsRootSignature(m_rootSignature.Get());
  ID3D12DescriptorHeap* heaps[] = {m_shaderResourceHeap.Get()};
  m_commands->SetDescriptorHeaps(1, heaps);
  m_commands->SetGraphicsRootDescriptorTable(ROOT_PARAMETER_PICTURE, m_shaderResourceHeap->GetGPUDescriptorHandleForHeapStart());
  const std::array<std::uint32_t, SCALE_CONSTANT_COUNT> constants = {
    std::bit_cast<std::uint32_t>(static_cast<float>(m_sourceWidth)), std::bit_cast<std::uint32_t>(static_cast<float>(m_sourceHeight)),
    std::bit_cast<std::uint32_t>(viewport.Width / static_cast<float>(m_sourceWidth)),
    std::bit_cast<std::uint32_t>(viewport.Height / static_cast<float>(m_sourceHeight))};
  m_commands->SetGraphicsRoot32BitConstants(ROOT_PARAMETER_SCALE, SCALE_CONSTANT_COUNT, constants.data(), 0);
  m_commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commands->DrawInstanced(3, 1, 0, 0);

  const D3D12_RESOURCE_BARRIER toWrite =
    Transition(m_picture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  m_commands->ResourceBarrier(1, &toWrite);
}

bool Renderer::DrawScene(D3D12_CPU_DESCRIPTOR_HANDLE _targetView, std::uint32_t _width, std::uint32_t _height, const SceneFrame& _scene)
{
  const SceneInset& inset = _scene.inset;
  const ScreenRect place = InsetPlace(inset, _width, _height);

  // The models, into the scene's targets.
  const std::array<D3D12_RESOURCE_BARRIER, 1> toTarget = {
    Transition(m_sceneColor.Get(), D3D12_RESOURCE_STATE_RESOLVE_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET)};
  m_commands->ResourceBarrier(static_cast<UINT>(toTarget.size()), toTarget.data());
  const D3D12_CPU_DESCRIPTOR_HANDLE sceneView = m_sceneTargetHeap->GetCPUDescriptorHandleForHeapStart();
  const D3D12_CPU_DESCRIPTOR_HANDLE depthView = m_sceneDepthHeap->GetCPUDescriptorHandleForHeapStart();
  m_commands->OMSetRenderTargets(1, &sceneView, FALSE, &depthView);
  const std::array<float, 4> nothing = {0.0f, 0.0f, 0.0f, 0.0f};
  m_commands->ClearRenderTargetView(sceneView, nothing.data(), 0, nullptr);
  m_commands->ClearDepthStencilView(depthView, D3D12_CLEAR_FLAG_DEPTH, FARTHEST_DEPTH, 0, 0, nullptr);
  D3D12_VIEWPORT viewport{};
  viewport.Width = static_cast<float>(m_sceneWidth);
  viewport.Height = static_cast<float>(m_sceneHeight);
  viewport.MaxDepth = 1.0f;
  const D3D12_RECT scissor{0, 0, static_cast<LONG>(m_sceneWidth), static_cast<LONG>(m_sceneHeight)};
  m_commands->RSSetViewports(1, &viewport);
  m_commands->RSSetScissorRects(1, &scissor);
  m_commands->SetGraphicsRootSignature(m_sceneSignature.Get());
  std::array<float, PALETTE_CONSTANT_COUNT> palette{};
  for (std::size_t color = 0; color < PALETTE_COLORS; ++color)
  {
    palette[4 * color] = LinearChannel(inset.palette[color], 16);
    palette[4 * color + 1] = LinearChannel(inset.palette[color], 8);
    palette[4 * color + 2] = LinearChannel(inset.palette[color], 0);
  }
  m_commands->SetGraphicsRoot32BitConstants(ROOT_PARAMETER_PALETTE, PALETTE_CONSTANT_COUNT, Bits(palette).data(), 0);
  m_commands->SetPipelineState(m_trianglePipeline.Get());
  m_commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  DrawModels(_scene, false);
  m_commands->SetPipelineState(m_linePipeline.Get());
  m_commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
  DrawModels(_scene, true);

  // Resolved, for the composite to read.
  const std::array<D3D12_RESOURCE_BARRIER, 2> toResolve = {
    Transition(m_sceneColor.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
    Transition(m_sceneResolved.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RESOLVE_DEST)};
  m_commands->ResourceBarrier(static_cast<UINT>(toResolve.size()), toResolve.data());
  m_commands->ResolveSubresource(m_sceneResolved.Get(), 0, m_sceneColor.Get(), 0, SCENE_FORMAT);
  const std::array<D3D12_RESOURCE_BARRIER, 1> toRead = {
    Transition(m_sceneResolved.Get(), D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)};
  m_commands->ResourceBarrier(static_cast<UINT>(toRead.size()), toRead.data());

  // The composite, into the inset.
  m_commands->OMSetRenderTargets(1, &_targetView, FALSE, nullptr);
  D3D12_VIEWPORT insetViewport{};
  insetViewport.TopLeftX = static_cast<float>(place.left);
  insetViewport.TopLeftY = static_cast<float>(place.top);
  insetViewport.Width = static_cast<float>(place.width);
  insetViewport.Height = static_cast<float>(place.height);
  insetViewport.MaxDepth = 1.0f;
  const D3D12_RECT insetScissor{static_cast<LONG>(place.left), static_cast<LONG>(place.top), static_cast<LONG>(place.left + place.width),
                                static_cast<LONG>(place.top + place.height)};
  m_commands->RSSetViewports(1, &insetViewport);
  m_commands->RSSetScissorRects(1, &insetScissor);
  m_commands->SetPipelineState(m_compositePipeline.Get());
  m_commands->SetGraphicsRootSignature(m_compositeSignature.Get());
  ID3D12DescriptorHeap* heaps[] = {m_shaderResourceHeap.Get()};
  m_commands->SetDescriptorHeaps(1, heaps);
  D3D12_GPU_DESCRIPTOR_HANDLE textures = m_shaderResourceHeap->GetGPUDescriptorHandleForHeapStart();
  textures.ptr += std::uint64_t{DESCRIPTOR_SCENE} * m_resourceStride;
  m_commands->SetGraphicsRootDescriptorTable(ROOT_PARAMETER_COMPOSITE_TEXTURES, textures);
  std::array<float, COMPOSITE_CONSTANT_COUNT> constants{};
  for (std::size_t color = 0; color < PALETTE_COLORS; ++color)
  {
    constants[4 * color] = Channel(inset.palette[color], 16);
    constants[4 * color + 1] = Channel(inset.palette[color], 8);
    constants[4 * color + 2] = Channel(inset.palette[color], 0);
    constants[4 * color + 3] = 1.0f;
  }
  constants[16] = static_cast<float>(m_layerWidth);
  constants[17] = static_cast<float>(m_layerHeight);
  constants[18] = static_cast<float>(m_sceneWidth);
  constants[19] = static_cast<float>(m_sceneHeight);
  m_commands->SetGraphicsRoot32BitConstants(ROOT_PARAMETER_COMPOSITE_CONSTANTS, COMPOSITE_CONSTANT_COUNT, Bits(constants).data(), 0);
  m_commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commands->DrawInstanced(3, 1, 0, 0);

  const D3D12_RESOURCE_BARRIER layersToWrite =
    Transition(m_layers.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
  m_commands->ResourceBarrier(1, &layersToWrite);
  return true;
}

void Renderer::DrawModels(const SceneFrame& _scene, bool _lines)
{
  const SceneInset& inset = _scene.inset;
  // The projection onto the inset's normalised coordinates, as Scene.hlsli's Frame reads it.
  const std::array<float, FRAME_CONSTANT_COUNT> frame = {2.0f * inset.focalX / inset.width,
                                                         2.0f * inset.centerX / inset.width - 1.0f,
                                                         2.0f * inset.focalY / inset.height,
                                                         1.0f - 2.0f * inset.centerY / inset.height,
                                                         inset.nearUnits,
                                                         _lines ? LINE_DEPTH_LIFT : 1.0f,
                                                         0.0f,
                                                         0.0f};
  m_commands->SetGraphicsRoot32BitConstants(ROOT_PARAMETER_FRAME, FRAME_CONSTANT_COUNT, Bits(frame).data(), 0);
  const SceneSnapshot& scene = *_scene.scene;
  for (const SceneObject& object : scene.objects)
  {
    if (object.model >= m_models.size())
    {
      continue;
    }
    const Model& model = m_models[object.model];
    const UINT vertices = _lines ? model.lineVertices : model.triangleVertices;
    if (vertices == 0)
    {
      continue;
    }
    // The model's frame into the camera's, its origin there, and the light in its own frame.
    const Quaternion rotation = Product(scene.cameraOrientation, object.orientation);
    const Vector3 offset{.x = object.position.x - scene.cameraPosition.x,
                         .y = object.position.y - scene.cameraPosition.y,
                         .z = object.position.z - scene.cameraPosition.z};
    const Vector3 at = Rotate(scene.cameraOrientation, offset);
    const Vector3 light = Rotate(Conjugate(object.orientation), inset.towardsLight);
    const std::array<float, OBJECT_CONSTANT_COUNT> constants = {rotation.x, rotation.y,           rotation.z, rotation.w, at.x,    at.y,
                                                                at.z,       _lines ? 0.0f : 1.0f, light.x,    light.y,    light.z, 0.0f};
    m_commands->SetGraphicsRoot32BitConstants(ROOT_PARAMETER_OBJECT, OBJECT_CONSTANT_COUNT, Bits(constants).data(), 0);
    m_commands->IASetVertexBuffers(0, 1, _lines ? &model.lines : &model.triangles);
    m_commands->DrawInstanced(vertices, 1, 0, 0);
  }
}

bool Renderer::Submit(Frame& _frame)
{
  // The frame's resources are free again once the queue passes this value; the next frame takes the other set.
  ++m_lastFenceValue;
  if (FAILED(m_queue->Signal(m_fence.Get(), m_lastFenceValue)))
  {
    return false;
  }
  _frame.fenceValue = m_lastFenceValue;
  m_nextFrame = (m_nextFrame + 1) % FRAME_COUNT;
  return true;
}

bool Renderer::WaitForFrame(const Frame& _frame)
{
  if (m_fence->GetCompletedValue() >= _frame.fenceValue)
  {
    return true;
  }
  if (FAILED(m_fence->SetEventOnCompletion(_frame.fenceValue, m_fenceEvent)))
  {
    return false;
  }
  WaitForSingleObject(m_fenceEvent, INFINITE);
  return true;
}

bool Renderer::WaitForGpu()
{
  ++m_lastFenceValue;
  if (FAILED(m_queue->Signal(m_fence.Get(), m_lastFenceValue)))
  {
    return false;
  }
  if (m_fence->GetCompletedValue() < m_lastFenceValue)
  {
    if (FAILED(m_fence->SetEventOnCompletion(m_lastFenceValue, m_fenceEvent)))
    {
      return false;
    }
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }
  return true;
}

} // namespace Engine
