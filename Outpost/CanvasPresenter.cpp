#include "pch.h"

#include "CanvasPresenter.h"

#include "Canvas.h"

#include <d3dcompiler.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace Outpost
{

namespace
{
/*
 * The whole renderer.
 *
 * `Load` rather than `Sample`, which is why there is no sampler and no filtering state anywhere:
 * an integer load of an integer texture is point sampling by construction, and at an integer
 * scale that is exactly the C64's picture with each pixel repeated.
 *
 * The palette arrives as sixteen root constants -- one RGBA word each, packed by
 * `PaletteAsRgba()` -- so `gPalette[index >> 2][index & 3]` is the colour of index. Sixteen
 * DWORDs is a quarter of the root signature's budget and saves a resource, a descriptor and an
 * upload.
 */
constexpr char SHADER_SOURCE[] = R"(
Texture2D<uint> CanvasTexture : register(t0);

cbuffer PaletteConstants : register(b0)
{
  uint4 gPalette[4];
};

struct Vertex
{
  float4 position : SV_Position;
  float2 uv : TEXCOORD0;
};

Vertex VsMain(uint id : SV_VertexID)
{
  // One triangle covering the viewport twice over: (0,0), (2,0), (0,2) in uv.
  Vertex output;
  output.uv = float2((id << 1) & 2, id & 2);
  output.position = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  return output;
}

float4 PsMain(Vertex input) : SV_Target
{
  int2 texel = int2(input.uv * float2(320.0, 200.0));
  texel = clamp(texel, int2(0, 0), int2(319, 199));

  uint index = CanvasTexture.Load(int3(texel, 0)) & 15u;
  uint packed = gPalette[index >> 2][index & 3];

  return float4(float(packed & 255u), float((packed >> 8) & 255u), float((packed >> 16) & 255u), 255.0) / 255.0;
}
)";

winrt::com_ptr<ID3DBlob> Compile(const char* _entry, const char* _target)
{
  UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#if defined(_DEBUG)
  flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

  winrt::com_ptr<ID3DBlob> code;
  winrt::com_ptr<ID3DBlob> errors;
  const HRESULT result = D3DCompile(SHADER_SOURCE, sizeof(SHADER_SOURCE) - 1, "CanvasPresenter", nullptr,
                                    nullptr, _entry, _target, flags, 0, code.put(), errors.put());
  if (FAILED(result))
  {
    // The compiler's own message is the only useful thing here, so it goes to the debugger's
    // output before the HRESULT is thrown; an HRESULT alone says nothing about a typo in HLSL.
    if (errors)
    {
      OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
    }
    winrt::check_hresult(result);
  }
  return code;
}

D3D12_RESOURCE_BARRIER Transition(ID3D12Resource* _resource, D3D12_RESOURCE_STATES _from,
                                  D3D12_RESOURCE_STATES _to) noexcept
{
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = _resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = _from;
  barrier.Transition.StateAfter = _to;
  return barrier;
}
} // namespace

CanvasPresenter::~CanvasPresenter()
{
  if (m_device)
  {
    // The GPU may still be reading the buffers about to be released, and a shutdown path logs
    // rather than throws (AGENTS.md section 5), so the wait is unconditional and unchecked.
    WaitForGpu();
  }
  if (m_fenceEvent != nullptr)
  {
    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
  }
}

void CanvasPresenter::Create(HWND _window)
{
  UINT factoryFlags = 0;

#if defined(_DEBUG)
  {
    // A capability probe, so it is control flow rather than a checked call: a machine without the
    // graphics tools installed has no debug layer and that is not an error.
    winrt::com_ptr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debug.put()))))
    {
      debug->EnableDebugLayer();
      factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
    }
  }
#endif

  winrt::com_ptr<IDXGIFactory4> factory;
  winrt::check_hresult(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(factory.put())));

  // Feature level 11.0 and the default adapter. Nothing here uses anything newer, and asking for
  // more would refuse to run on hardware that can draw this perfectly well.
  winrt::check_hresult(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(m_device.put())));

  D3D12_COMMAND_QUEUE_DESC queue{};
  queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queue.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  winrt::check_hresult(m_device->CreateCommandQueue(&queue, IID_PPV_ARGS(m_queue.put())));

  DXGI_SWAP_CHAIN_DESC1 chain{};
  chain.BufferCount = FRAME_COUNT;
  chain.Width = 0; // taken from the window
  chain.Height = 0;
  chain.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  chain.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  chain.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  chain.SampleDesc.Count = 1;
  chain.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

  winrt::com_ptr<IDXGISwapChain1> created;
  winrt::check_hresult(
    factory->CreateSwapChainForHwnd(m_queue.get(), _window, &chain, nullptr, nullptr, created.put()));

  // Alt+Enter's built-in fullscreen transition is refused. A 320x200 game has one sensible
  // fullscreen story and it is borderless, which is phase 6; the DXGI one changes the display
  // mode and fights the integer scale.
  winrt::check_hresult(factory->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER));

  m_swapChain = created.as<IDXGISwapChain3>();
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

  D3D12_DESCRIPTOR_HEAP_DESC renderTargets{};
  renderTargets.NumDescriptors = FRAME_COUNT;
  renderTargets.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  winrt::check_hresult(m_device->CreateDescriptorHeap(&renderTargets, IID_PPV_ARGS(m_renderTargetHeap.put())));
  m_renderTargetSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

  D3D12_DESCRIPTOR_HEAP_DESC textures{};
  textures.NumDescriptors = 1;
  textures.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  textures.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  winrt::check_hresult(m_device->CreateDescriptorHeap(&textures, IID_PPV_ARGS(m_textureHeap.put())));

  for (UINT frame = 0; frame < FRAME_COUNT; ++frame)
  {
    winrt::check_hresult(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                          IID_PPV_ARGS(m_allocators[frame].put())));
  }

  // ---- the canvas texture, and one upload buffer per frame in flight -------------------------

  D3D12_HEAP_PROPERTIES defaultHeap{};
  defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

  D3D12_RESOURCE_DESC canvas{};
  canvas.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  canvas.Width = Elite::Canvas::WIDTH;
  canvas.Height = Elite::Canvas::HEIGHT;
  canvas.DepthOrArraySize = 1;
  canvas.MipLevels = 1;
  canvas.Format = DXGI_FORMAT_R8_UINT;
  canvas.SampleDesc.Count = 1;
  canvas.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  canvas.Flags = D3D12_RESOURCE_FLAG_NONE;

  winrt::check_hresult(m_device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &canvas,
                                                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                         nullptr, IID_PPV_ARGS(m_texture.put())));

  /*
   * The upload buffer's rows are padded to 256 bytes, which is why the copy below is a loop
   * rather than one memcpy: 320 bytes of canvas row become 512 bytes of upload row. Asking the
   * device for the footprint rather than computing it is what keeps that alignment a fact rather
   * than an assumption.
   */
  UINT64 uploadBytes = 0;
  UINT64 rowBytes = 0;
  m_device->GetCopyableFootprints(&canvas, 0, 1, 0, &m_footprint, &m_footprintRows, &rowBytes, &uploadBytes);

  D3D12_HEAP_PROPERTIES uploadHeap{};
  uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

  D3D12_RESOURCE_DESC buffer{};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Width = uploadBytes;
  buffer.Height = 1;
  buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1;
  buffer.Format = DXGI_FORMAT_UNKNOWN;
  buffer.SampleDesc.Count = 1;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

  for (UINT frame = 0; frame < FRAME_COUNT; ++frame)
  {
    winrt::check_hresult(m_device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                           D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                           IID_PPV_ARGS(m_uploads[frame].put())));

    // Mapped once and left mapped: it is written every frame and never read, which is what an
    // upload heap is for. The null range says "I read nothing", which the debug layer checks.
    const D3D12_RANGE readsNothing{ 0, 0 };
    void* memory = nullptr;
    winrt::check_hresult(m_uploads[frame]->Map(0, &readsNothing, &memory));
    m_uploadMemory[frame] = static_cast<std::uint8_t*>(memory);
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = DXGI_FORMAT_R8_UINT;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MipLevels = 1;
  m_device->CreateShaderResourceView(m_texture.get(), &view,
                                     m_textureHeap->GetCPUDescriptorHandleForHeapStart());

  // ---- the root signature and the pipeline -----------------------------------------------------

  D3D12_DESCRIPTOR_RANGE range{};
  range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  range.NumDescriptors = 1;
  range.BaseShaderRegister = 0;
  range.RegisterSpace = 0;
  range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[0].DescriptorTable.NumDescriptorRanges = 1;
  parameters[0].DescriptorTable.pDescriptorRanges = &range;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[1].Constants.ShaderRegister = 0;
  parameters[1].Constants.RegisterSpace = 0;
  parameters[1].Constants.Num32BitValues = 16;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC signature{};
  signature.NumParameters = 2;
  signature.pParameters = parameters;
  signature.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

  winrt::com_ptr<ID3DBlob> serialized;
  winrt::com_ptr<ID3DBlob> signatureErrors;
  const HRESULT serialization =
    D3D12SerializeRootSignature(&signature, D3D_ROOT_SIGNATURE_VERSION_1, serialized.put(),
                                signatureErrors.put());
  if (FAILED(serialization))
  {
    if (signatureErrors)
    {
      OutputDebugStringA(static_cast<const char*>(signatureErrors->GetBufferPointer()));
    }
    winrt::check_hresult(serialization);
  }
  winrt::check_hresult(m_device->CreateRootSignature(0, serialized->GetBufferPointer(),
                                                     serialized->GetBufferSize(),
                                                     IID_PPV_ARGS(m_rootSignature.put())));

  const winrt::com_ptr<ID3DBlob> vertexShader = Compile("VsMain", "vs_5_0");
  const winrt::com_ptr<ID3DBlob> pixelShader = Compile("PsMain", "ps_5_0");

  D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
  pipeline.pRootSignature = m_rootSignature.get();
  pipeline.VS = { vertexShader->GetBufferPointer(), vertexShader->GetBufferSize() };
  pipeline.PS = { pixelShader->GetBufferPointer(), pixelShader->GetBufferSize() };

  pipeline.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  pipeline.SampleMask = UINT_MAX;

  pipeline.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
  pipeline.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  pipeline.RasterizerState.DepthClipEnable = TRUE;

  pipeline.DepthStencilState.DepthEnable = FALSE;
  pipeline.DepthStencilState.StencilEnable = FALSE;

  pipeline.InputLayout = { nullptr, 0 };
  pipeline.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  pipeline.NumRenderTargets = 1;
  pipeline.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
  pipeline.SampleDesc.Count = 1;

  winrt::check_hresult(m_device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(m_pipeline.put())));

  winrt::check_hresult(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    m_allocators[m_frameIndex].get(), m_pipeline.get(),
                                                    IID_PPV_ARGS(m_commands.put())));
  winrt::check_hresult(m_commands->Close());

  winrt::check_hresult(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(m_fence.put())));
  m_fenceValues[m_frameIndex] = 1;

  m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (m_fenceEvent == nullptr)
  {
    winrt::throw_last_error();
  }

  m_palette = PaletteAsRgba();
  m_resolved.resize(static_cast<std::size_t>(Elite::Canvas::WIDTH) * Elite::Canvas::HEIGHT);

  CreateRenderTargets();
}

void CanvasPresenter::CreateRenderTargets()
{
  D3D12_CPU_DESCRIPTOR_HANDLE handle = m_renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
  for (UINT frame = 0; frame < FRAME_COUNT; ++frame)
  {
    m_backBuffers[frame] = nullptr;
    winrt::check_hresult(m_swapChain->GetBuffer(frame, IID_PPV_ARGS(m_backBuffers[frame].put())));
    m_device->CreateRenderTargetView(m_backBuffers[frame].get(), nullptr, handle);
    handle.ptr += m_renderTargetSize;
  }

  DXGI_SWAP_CHAIN_DESC1 description{};
  winrt::check_hresult(m_swapChain->GetDesc1(&description));
  m_width = static_cast<int>(description.Width);
  m_height = static_cast<int>(description.Height);
}

void CanvasPresenter::Resize(int _clientWidth, int _clientHeight)
{
  // A minimised window has a zero client area and ResizeBuffers refuses it. Skipping is correct
  // rather than a workaround: there is nothing to present to, and the next real WM_SIZE resizes.
  if (!m_device || _clientWidth <= 0 || _clientHeight <= 0)
  {
    return;
  }
  if (_clientWidth == m_width && _clientHeight == m_height)
  {
    return;
  }

  WaitForGpu();

  for (winrt::com_ptr<ID3D12Resource>& buffer : m_backBuffers)
  {
    buffer = nullptr;
  }

  DXGI_SWAP_CHAIN_DESC1 description{};
  winrt::check_hresult(m_swapChain->GetDesc1(&description));
  winrt::check_hresult(m_swapChain->ResizeBuffers(FRAME_COUNT, static_cast<UINT>(_clientWidth),
                                                  static_cast<UINT>(_clientHeight), description.Format,
                                                  description.Flags));

  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
  CreateRenderTargets();
}

bool CanvasPresenter::Present(const Elite::Canvas& _canvas, int _clientWidth, int _clientHeight)
{
  if (!m_device || _clientWidth <= 0 || _clientHeight <= 0)
  {
    return true;
  }

  const Viewport view = FitCanvas(m_width, m_height);
  _canvas.Resolve(m_resolved);

  winrt::check_hresult(m_allocators[m_frameIndex]->Reset());
  winrt::check_hresult(m_commands->Reset(m_allocators[m_frameIndex].get(), m_pipeline.get()));

  // Row by row, because the upload heap's pitch is padded to 256 bytes and the canvas's is not.
  std::uint8_t* destination = m_uploadMemory[m_frameIndex] + m_footprint.Offset;
  for (UINT row = 0; row < m_footprintRows; ++row)
  {
    std::memcpy(destination + static_cast<std::size_t>(row) * m_footprint.Footprint.RowPitch,
                m_resolved.data() + static_cast<std::size_t>(row) * Elite::Canvas::WIDTH,
                Elite::Canvas::WIDTH);
  }

  D3D12_RESOURCE_BARRIER toCopy = Transition(m_texture.get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                             D3D12_RESOURCE_STATE_COPY_DEST);
  m_commands->ResourceBarrier(1, &toCopy);

  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = m_uploads[m_frameIndex].get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = m_footprint;

  D3D12_TEXTURE_COPY_LOCATION target{};
  target.pResource = m_texture.get();
  target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  target.SubresourceIndex = 0;

  m_commands->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);

  D3D12_RESOURCE_BARRIER toShader = Transition(m_texture.get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
  m_commands->ResourceBarrier(1, &toShader);

  D3D12_RESOURCE_BARRIER toTarget = Transition(m_backBuffers[m_frameIndex].get(),
                                               D3D12_RESOURCE_STATE_PRESENT,
                                               D3D12_RESOURCE_STATE_RENDER_TARGET);
  m_commands->ResourceBarrier(1, &toTarget);

  D3D12_CPU_DESCRIPTOR_HANDLE renderTarget = m_renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
  renderTarget.ptr += static_cast<SIZE_T>(m_frameIndex) * m_renderTargetSize;
  m_commands->OMSetRenderTargets(1, &renderTarget, FALSE, nullptr);

  // The black bars. Clearing the whole target and then drawing into the letterboxed viewport is
  // what puts them there; there is no second draw and no separate bar geometry.
  const float black[4] = { 0.0F, 0.0F, 0.0F, 1.0F };
  m_commands->ClearRenderTargetView(renderTarget, black, 0, nullptr);

  D3D12_VIEWPORT viewport{};
  viewport.TopLeftX = static_cast<float>(view.x);
  viewport.TopLeftY = static_cast<float>(view.y);
  viewport.Width = static_cast<float>(view.width);
  viewport.Height = static_cast<float>(view.height);
  viewport.MinDepth = 0.0F;
  viewport.MaxDepth = 1.0F;
  m_commands->RSSetViewports(1, &viewport);

  /*
   * The scissor is CLAMPED to the client area and the viewport is not, which is the difference
   * that matters when the window is smaller than the canvas: the viewport is then at a negative
   * origin, which Direct3D allows and which is how the picture stays centred while it clips,
   * whereas a scissor rectangle's corners have to be inside the target.
   */
  D3D12_RECT scissor{};
  scissor.left = (view.x > 0) ? view.x : 0;
  scissor.top = (view.y > 0) ? view.y : 0;
  scissor.right = ((view.x + view.width) < m_width) ? (view.x + view.width) : m_width;
  scissor.bottom = ((view.y + view.height) < m_height) ? (view.y + view.height) : m_height;
  m_commands->RSSetScissorRects(1, &scissor);

  ID3D12DescriptorHeap* heaps[] = { m_textureHeap.get() };
  m_commands->SetDescriptorHeaps(1, heaps);
  m_commands->SetGraphicsRootSignature(m_rootSignature.get());
  m_commands->SetGraphicsRootDescriptorTable(0, m_textureHeap->GetGPUDescriptorHandleForHeapStart());
  m_commands->SetGraphicsRoot32BitConstants(1, 16, m_palette.data(), 0);
  m_commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commands->DrawInstanced(3, 1, 0, 0);

  D3D12_RESOURCE_BARRIER toPresent = Transition(m_backBuffers[m_frameIndex].get(),
                                                D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                D3D12_RESOURCE_STATE_PRESENT);
  m_commands->ResourceBarrier(1, &toPresent);

  winrt::check_hresult(m_commands->Close());

  ID3D12CommandList* lists[] = { m_commands.get() };
  m_queue->ExecuteCommandLists(1, lists);

  // Vsync on, and this is the only place the program waits.
  const HRESULT presented = m_swapChain->Present(1, 0);
  if (presented == DXGI_ERROR_DEVICE_REMOVED || presented == DXGI_ERROR_DEVICE_RESET)
  {
    return false;
  }
  winrt::check_hresult(presented);

  MoveToNextFrame();
  return true;
}

void CanvasPresenter::WaitForGpu()
{
  if (!m_queue || !m_fence || m_fenceEvent == nullptr)
  {
    return;
  }

  const UINT64 target = m_fenceValues[m_frameIndex];
  if (FAILED(m_queue->Signal(m_fence.get(), target)))
  {
    return;
  }
  if (SUCCEEDED(m_fence->SetEventOnCompletion(target, m_fenceEvent)))
  {
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
  }

  // Every frame's value settles on the same number, so nothing waits on a value that a resize
  // means will never be signalled.
  for (UINT64& value : m_fenceValues)
  {
    value = target + 1;
  }
}

void CanvasPresenter::MoveToNextFrame()
{
  const UINT64 submitted = m_fenceValues[m_frameIndex];
  winrt::check_hresult(m_queue->Signal(m_fence.get(), submitted));

  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

  // The frame about to be built is the one presented two frames ago, so its command allocator is
  // only reusable once the GPU has finished with it.
  if (m_fence->GetCompletedValue() < m_fenceValues[m_frameIndex])
  {
    winrt::check_hresult(m_fence->SetEventOnCompletion(m_fenceValues[m_frameIndex], m_fenceEvent));
    WaitForSingleObjectEx(m_fenceEvent, INFINITE, FALSE);
  }

  m_fenceValues[m_frameIndex] = submitted + 1;
}

} // namespace Outpost
