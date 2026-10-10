#include "pch.h"

#include "Presenter.h"

#include "CompiledShader/PresentPS.h"
#include "CompiledShader/PresentVS.h"

#include <algorithm>
#include <bit>
#include <climits>
#include <cmath>
#include <cstring>

namespace Engine
{

namespace
{

constexpr DXGI_FORMAT BACK_BUFFER_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT PICTURE_FORMAT = DXGI_FORMAT_B8G8R8A8_UNORM; // a 0x00RRGGBB word, little-endian
constexpr UINT PRESENT_SYNC_INTERVAL = 1;
constexpr DWORD UNSEEN_FRAME_MILLISECONDS = 16; // a frame at 60 Hz, when there is no display to wait for
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

Presenter::~Presenter()
{
  if (m_queue && m_fence)
  {
    (void)WaitForGpu();
  }
  if (m_fenceEvent != nullptr)
  {
    CloseHandle(m_fenceEvent);
  }
}

bool Presenter::Create(HWND _window, std::uint32_t _sourceWidth, std::uint32_t _sourceHeight, std::uint32_t _logicalWidth,
                       std::uint32_t _logicalHeight)
{
  m_window = _window;
  m_sourceWidth = _sourceWidth;
  m_sourceHeight = _sourceHeight;
  m_logicalWidth = _logicalWidth;
  m_logicalHeight = _logicalHeight;

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
  if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&m_device))))
  {
    return false;
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  if (FAILED(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue))))
  {
    return false;
  }

  RECT client{};
  GetClientRect(_window, &client);
  m_clientWidth = static_cast<std::uint32_t>(std::max<LONG>(client.right - client.left, 1));
  m_clientHeight = static_cast<std::uint32_t>(std::max<LONG>(client.bottom - client.top, 1));

  DXGI_SWAP_CHAIN_DESC1 swapDesc{};
  swapDesc.Width = m_clientWidth;
  swapDesc.Height = m_clientHeight;
  swapDesc.Format = BACK_BUFFER_FORMAT;
  swapDesc.SampleDesc.Count = 1;
  swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapDesc.BufferCount = FRAME_COUNT;
  swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain;
  if (FAILED(m_factory->CreateSwapChainForHwnd(m_queue.Get(), _window, &swapDesc, nullptr, nullptr, &swapChain)) ||
      FAILED(swapChain.As(&m_swapChain)))
  {
    return false;
  }
  m_factory->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER);

  D3D12_DESCRIPTOR_HEAP_DESC targetHeap{};
  targetHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  targetHeap.NumDescriptors = FRAME_COUNT;
  D3D12_DESCRIPTOR_HEAP_DESC resourceHeap{};
  resourceHeap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  resourceHeap.NumDescriptors = 1;
  resourceHeap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if (FAILED(m_device->CreateDescriptorHeap(&targetHeap, IID_PPV_ARGS(&m_renderTargetHeap))) ||
      FAILED(m_device->CreateDescriptorHeap(&resourceHeap, IID_PPV_ARGS(&m_shaderResourceHeap))))
  {
    return false;
  }
  m_renderTargetStride = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_allocator))) ||
      FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_allocator.Get(), nullptr, IID_PPV_ARGS(&m_commands))) ||
      FAILED(m_commands->Close()))
  {
    return false;
  }
  if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence))))
  {
    return false;
  }
  m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (m_fenceEvent == nullptr)
  {
    return false;
  }
  return CreateRenderTargets() && CreatePipeline() && CreatePictureTexture();
}

bool Presenter::CreateRenderTargets()
{
  D3D12_CPU_DESCRIPTOR_HANDLE handle = m_renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
  for (UINT index = 0; index < FRAME_COUNT; ++index)
  {
    if (FAILED(m_swapChain->GetBuffer(index, IID_PPV_ARGS(&m_renderTargets[index]))))
    {
      return false;
    }
    m_device->CreateRenderTargetView(m_renderTargets[index].Get(), nullptr, handle);
    handle.ptr += m_renderTargetStride;
  }
  return true;
}

bool Presenter::CreatePipeline()
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
    .RTVFormats = {BACK_BUFFER_FORMAT},
    .DSVFormat = DXGI_FORMAT_UNKNOWN,
    .SampleDesc = {.Count = 1, .Quality = 0},
    .NodeMask = 0,
    .CachedPSO = {},
    .Flags = D3D12_PIPELINE_STATE_FLAG_NONE};
  return SUCCEEDED(m_device->CreateGraphicsPipelineState(&pipelineDesc, IID_PPV_ARGS(&m_pipeline)));
}

bool Presenter::CreatePictureTexture()
{
  D3D12_RESOURCE_DESC textureDesc{};
  textureDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  textureDesc.Width = m_sourceWidth;
  textureDesc.Height = m_sourceHeight;
  textureDesc.DepthOrArraySize = 1;
  textureDesc.MipLevels = 1;
  textureDesc.Format = PICTURE_FORMAT;
  textureDesc.SampleDesc.Count = 1;
  textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  const D3D12_HEAP_PROPERTIES defaultHeap = HeapOfType(D3D12_HEAP_TYPE_DEFAULT);
  if (FAILED(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &textureDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&m_picture))))
  {
    return false;
  }

  UINT64 uploadBytes = 0;
  m_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &m_footprint, nullptr, nullptr, &uploadBytes);
  D3D12_RESOURCE_DESC bufferDesc{};
  bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  bufferDesc.Width = uploadBytes;
  bufferDesc.Height = 1;
  bufferDesc.DepthOrArraySize = 1;
  bufferDesc.MipLevels = 1;
  bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
  bufferDesc.SampleDesc.Count = 1;
  bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  const D3D12_HEAP_PROPERTIES uploadHeap = HeapOfType(D3D12_HEAP_TYPE_UPLOAD);
  if (FAILED(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                               IID_PPV_ARGS(&m_upload))))
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

bool Presenter::Resize()
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
  for (Microsoft::WRL::ComPtr<ID3D12Resource>& target : m_renderTargets)
  {
    target.Reset();
  }
  if (FAILED(m_swapChain->ResizeBuffers(FRAME_COUNT, width, height, DXGI_FORMAT_UNKNOWN, 0)))
  {
    return false;
  }
  m_clientWidth = width;
  m_clientHeight = height;
  return CreateRenderTargets();
}

bool Presenter::Present(std::span<const std::uint32_t> _pixels, std::uint32_t _border)
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

  void* mapped = nullptr;
  const D3D12_RANGE nothingRead{0, 0};
  if (FAILED(m_upload->Map(0, &nothingRead, &mapped)))
  {
    return false;
  }
  auto* rows = static_cast<std::byte*>(mapped) + m_footprint.Offset;
  for (std::uint32_t row = 0; row < m_sourceHeight; ++row)
  {
    std::memcpy(rows + std::size_t{row} * m_footprint.Footprint.RowPitch, _pixels.data() + std::size_t{row} * m_sourceWidth,
                std::size_t{m_sourceWidth} * sizeof(std::uint32_t));
  }
  m_upload->Unmap(0, nullptr);

  if (FAILED(m_allocator->Reset()) || FAILED(m_commands->Reset(m_allocator.Get(), m_pipeline.Get())))
  {
    return false;
  }
  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = m_picture.Get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  destination.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = m_upload.Get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = m_footprint;
  m_commands->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

  const UINT frame = m_swapChain->GetCurrentBackBufferIndex();
  ID3D12Resource* target = m_renderTargets[frame].Get();
  const std::array<D3D12_RESOURCE_BARRIER, 2> before = {
    Transition(m_picture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE),
    Transition(target, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET)};
  m_commands->ResourceBarrier(static_cast<UINT>(before.size()), before.data());

  D3D12_CPU_DESCRIPTOR_HANDLE targetView = m_renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
  targetView.ptr += std::size_t{frame} * m_renderTargetStride;
  m_commands->OMSetRenderTargets(1, &targetView, FALSE, nullptr);
  const std::array<float, 4> border = {Channel(_border, 16), Channel(_border, 8), Channel(_border, 0), 1.0f};
  m_commands->ClearRenderTargetView(targetView, border.data(), 0, nullptr);

  // The largest whole multiple of the logical size that fits, or the largest fit if none does.
  const std::uint32_t scale = std::min(m_clientWidth / m_logicalWidth, m_clientHeight / m_logicalHeight);
  float width = static_cast<float>(m_logicalWidth * scale);
  float height = static_cast<float>(m_logicalHeight * scale);
  if (scale == 0)
  {
    const float fit = std::min(static_cast<float>(m_clientWidth) / static_cast<float>(m_logicalWidth),
                               static_cast<float>(m_clientHeight) / static_cast<float>(m_logicalHeight));
    width = static_cast<float>(m_logicalWidth) * fit;
    height = static_cast<float>(m_logicalHeight) * fit;
  }
  D3D12_VIEWPORT viewport{};
  viewport.TopLeftX = std::floor((static_cast<float>(m_clientWidth) - width) / 2.0f);
  viewport.TopLeftY = std::floor((static_cast<float>(m_clientHeight) - height) / 2.0f);
  viewport.Width = width;
  viewport.Height = height;
  viewport.MaxDepth = 1.0f;
  const D3D12_RECT scissor{static_cast<LONG>(viewport.TopLeftX), static_cast<LONG>(viewport.TopLeftY),
                           static_cast<LONG>(viewport.TopLeftX + width), static_cast<LONG>(viewport.TopLeftY + height)};
  m_commands->RSSetViewports(1, &viewport);
  m_commands->RSSetScissorRects(1, &scissor);

  m_commands->SetGraphicsRootSignature(m_rootSignature.Get());
  ID3D12DescriptorHeap* heaps[] = {m_shaderResourceHeap.Get()};
  m_commands->SetDescriptorHeaps(1, heaps);
  m_commands->SetGraphicsRootDescriptorTable(ROOT_PARAMETER_PICTURE, m_shaderResourceHeap->GetGPUDescriptorHandleForHeapStart());
  const std::array<std::uint32_t, SCALE_CONSTANT_COUNT> constants = {
    std::bit_cast<std::uint32_t>(static_cast<float>(m_sourceWidth)), std::bit_cast<std::uint32_t>(static_cast<float>(m_sourceHeight)),
    std::bit_cast<std::uint32_t>(width / static_cast<float>(m_sourceWidth)),
    std::bit_cast<std::uint32_t>(height / static_cast<float>(m_sourceHeight))};
  m_commands->SetGraphicsRoot32BitConstants(ROOT_PARAMETER_SCALE, SCALE_CONSTANT_COUNT, constants.data(), 0);
  m_commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commands->DrawInstanced(3, 1, 0, 0);

  const std::array<D3D12_RESOURCE_BARRIER, 2> after = {
    Transition(target, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT),
    Transition(m_picture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST)};
  m_commands->ResourceBarrier(static_cast<UINT>(after.size()), after.data());
  if (FAILED(m_commands->Close()))
  {
    return false;
  }
  ID3D12CommandList* lists[] = {m_commands.Get()};
  m_queue->ExecuteCommandLists(1, lists);
  const HRESULT presented = m_swapChain->Present(PRESENT_SYNC_INTERVAL, 0);
  if (FAILED(presented))
  {
    return false;
  }
  if (presented == DXGI_STATUS_OCCLUDED)
  {
    Sleep(UNSEEN_FRAME_MILLISECONDS);
  }
  return WaitForGpu();
}

bool Presenter::WaitForGpu()
{
  ++m_fenceValue;
  if (FAILED(m_queue->Signal(m_fence.Get(), m_fenceValue)))
  {
    return false;
  }
  if (m_fence->GetCompletedValue() < m_fenceValue)
  {
    if (FAILED(m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent)))
    {
      return false;
    }
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }
  return true;
}

} // namespace Engine
