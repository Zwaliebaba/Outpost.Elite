#include "pch.h"

#include "Renderer.h"

#include "CompiledShader/PresentPS.h"
#include "CompiledShader/PresentVS.h"

#include <algorithm>
#include <bit>
#include <climits>
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
  return CreateDevice(_desc.warp) && CreatePipeline() && CreatePicture();
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
  resourceHeap.NumDescriptors = 1;
  resourceHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(m_device->CreateDescriptorHeap(&resourceHeap, IID_PPV_ARGS(&m_shaderResourceHeap))))
  {
    return false;
  }
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

bool Renderer::Present(std::span<const std::uint32_t> _pixels, std::uint32_t _border)
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
  if (!BeginFrame(frame, _pixels))
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
                                             std::uint32_t _height)
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
  if (!BeginFrame(frame, _pixels))
  {
    return {};
  }
  DrawPicture(targetView, _width, _height, _border);
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

bool Renderer::BeginFrame(Frame& _frame, std::span<const std::uint32_t> _pixels)
{
  // The frame's allocator and upload buffer are the GPU's until the frame it last drew is done.
  if (!WaitForFrame(_frame))
  {
    return false;
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
