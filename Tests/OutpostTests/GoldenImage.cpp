#include "pch.h"

#include "GoldenImage.h"

#include "Repository.h"

#include <objbase.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <system_error>

using Microsoft::VisualStudio::CppUnitTestFramework::Assert;
using Microsoft::VisualStudio::CppUnitTestFramework::Logger;

namespace OutpostTests
{

namespace
{

template <typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

constexpr std::uint32_t BYTES_PER_PIXEL = 3;      // what is written: 24-bit BGR, as a PNG has no use for the fourth byte
constexpr std::uint32_t READ_BYTES_PER_PIXEL = 4; // what is read: 32-bit BGR, which WIC converts any PNG to
constexpr int MOST_CHANNEL_DIFFERENCE = 8;        // a pixel within this of its golden's, in every channel, is the same

// COM on this thread for as long as it lives, as WIC needs it; a thread that already has it keeps it as it was.
class ComScope
{
public:
  ComScope() noexcept
    : m_result(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
  {
  }

  ComScope(const ComScope&) = delete;
  ComScope& operator=(const ComScope&) = delete;

  ~ComScope()
  {
    if (SUCCEEDED(m_result))
    {
      CoUninitialize();
    }
  }

  [[nodiscard]] bool Usable() const noexcept
  {
    return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
  }

private:
  HRESULT m_result;
};

[[nodiscard]] ComPtr<IWICImagingFactory> Factory()
{
  ComPtr<IWICImagingFactory> factory;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
  {
    return nullptr;
  }
  return factory;
}

[[nodiscard]] std::wstring Failure(const wchar_t* _what, HRESULT _result)
{
  return std::format(L"{} failed: 0x{:08X}", _what, static_cast<unsigned long>(_result));
}

[[nodiscard]] std::wstring Widen(std::string_view _text)
{
  return std::wstring(_text.begin(), _text.end());
}

} // namespace

bool GoldenImage::Save(const std::filesystem::path& _path, const Picture& _picture, std::wstring& _error)
{
  const ComScope com;
  const ComPtr<IWICImagingFactory> factory = Factory();
  if (!com.Usable() || factory == nullptr)
  {
    _error = L"WIC is not available";
    return false;
  }
  std::vector<BYTE> bytes(std::size_t{_picture.width} * _picture.height * BYTES_PER_PIXEL);
  for (std::size_t index = 0; index < _picture.pixels.size(); ++index)
  {
    const std::uint32_t pixel = _picture.pixels[index];
    bytes[index * BYTES_PER_PIXEL] = static_cast<BYTE>(pixel & 0xFFu);
    bytes[index * BYTES_PER_PIXEL + 1] = static_cast<BYTE>((pixel >> 8) & 0xFFu);
    bytes[index * BYTES_PER_PIXEL + 2] = static_cast<BYTE>((pixel >> 16) & 0xFFu);
  }
  ComPtr<IWICStream> stream;
  ComPtr<IWICBitmapEncoder> encoder;
  ComPtr<IWICBitmapFrameEncode> frame;
  HRESULT result = factory->CreateStream(&stream);
  if (SUCCEEDED(result))
  {
    result = stream->InitializeFromFilename(_path.c_str(), GENERIC_WRITE);
  }
  if (SUCCEEDED(result))
  {
    result = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  }
  if (SUCCEEDED(result))
  {
    result = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
  }
  if (SUCCEEDED(result))
  {
    result = encoder->CreateNewFrame(&frame, nullptr);
  }
  if (SUCCEEDED(result))
  {
    result = frame->Initialize(nullptr);
  }
  if (SUCCEEDED(result))
  {
    result = frame->SetSize(_picture.width, _picture.height);
  }
  WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
  if (SUCCEEDED(result))
  {
    result = frame->SetPixelFormat(&format);
  }
  if (SUCCEEDED(result) && format != GUID_WICPixelFormat24bppBGR)
  {
    _error = L"the PNG encoder would not take 24-bit BGR";
    return false;
  }
  if (SUCCEEDED(result))
  {
    result = frame->WritePixels(_picture.height, _picture.width * BYTES_PER_PIXEL, static_cast<UINT>(bytes.size()), bytes.data());
  }
  if (SUCCEEDED(result))
  {
    result = frame->Commit();
  }
  if (SUCCEEDED(result))
  {
    result = encoder->Commit();
  }
  if (FAILED(result))
  {
    _error = Failure(L"writing the PNG", result);
    return false;
  }
  return true;
}

std::optional<GoldenImage::Picture> GoldenImage::Load(const std::filesystem::path& _path, std::wstring& _error)
{
  const ComScope com;
  const ComPtr<IWICImagingFactory> factory = Factory();
  if (!com.Usable() || factory == nullptr)
  {
    _error = L"WIC is not available";
    return std::nullopt;
  }
  ComPtr<IWICBitmapDecoder> decoder;
  ComPtr<IWICBitmapFrameDecode> frame;
  ComPtr<IWICFormatConverter> converter;
  Picture picture;
  HRESULT result = factory->CreateDecoderFromFilename(_path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder);
  if (SUCCEEDED(result))
  {
    result = decoder->GetFrame(0, &frame);
  }
  if (SUCCEEDED(result))
  {
    result = frame->GetSize(&picture.width, &picture.height);
  }
  if (SUCCEEDED(result))
  {
    result = factory->CreateFormatConverter(&converter);
  }
  if (SUCCEEDED(result))
  {
    result =
      converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
  }
  std::vector<BYTE> bytes;
  if (SUCCEEDED(result))
  {
    bytes.resize(std::size_t{picture.width} * picture.height * READ_BYTES_PER_PIXEL);
    result = converter->CopyPixels(nullptr, picture.width * READ_BYTES_PER_PIXEL, static_cast<UINT>(bytes.size()), bytes.data());
  }
  if (FAILED(result))
  {
    _error = Failure(L"reading the PNG", result);
    return std::nullopt;
  }
  // 32-bit BGR, the fourth byte unused: each pixel's blue, green and red into 0x00RRGGBB.
  picture.pixels.resize(std::size_t{picture.width} * picture.height);
  for (std::size_t index = 0; index < picture.pixels.size(); ++index)
  {
    const std::size_t at = index * READ_BYTES_PER_PIXEL;
    picture.pixels[index] = std::uint32_t{bytes[at]} | (std::uint32_t{bytes[at + 1]} << 8) | (std::uint32_t{bytes[at + 2]} << 16);
  }
  return picture;
}

std::size_t GoldenImage::Differences(const Picture& _found, const Picture& _golden, int _tolerance)
{
  if (_found.width != _golden.width || _found.height != _golden.height || _found.pixels.size() != _golden.pixels.size())
  {
    return std::max(_found.pixels.size(), _golden.pixels.size());
  }
  std::size_t differences = 0;
  for (std::size_t index = 0; index < _found.pixels.size(); ++index)
  {
    for (int shift = 0; shift <= 16; shift += 8)
    {
      const int found = static_cast<int>((_found.pixels[index] >> shift) & 0xFFu);
      const int golden = static_cast<int>((_golden.pixels[index] >> shift) & 0xFFu);
      if (std::abs(found - golden) > _tolerance)
      {
        ++differences;
        break;
      }
    }
  }
  return differences;
}

void GoldenImage::Check(std::string_view _name, const Picture& _picture)
{
  const std::filesystem::path root = Elite::FindInRepository("Tests").parent_path();
  Assert::IsFalse(root.empty(), L"the repository's root, from its Tests directory");
  const std::wstring name = Widen(_name);
  const std::filesystem::path rendered = root / RENDERED_DIRECTORY / (name + L".png");
  std::error_code ignored;
  std::filesystem::create_directories(rendered.parent_path(), ignored);
  std::wstring error;
  Assert::IsTrue(Save(rendered, _picture, error), (L"writing " + rendered.wstring() + L": " + error).c_str());

  const std::filesystem::path golden = root / GOLDEN_DIRECTORY / (name + L".png");
  if (!std::filesystem::exists(golden))
  {
    // Reported, not failed (D94): the owner commits the golden once they have looked at what was drawn.
    Logger::WriteMessage((L"no golden image " + golden.wstring() + L" yet: look at " + rendered.wstring() +
                          L" (the CI artifact rendered-images) and commit it there once it is right (D94)\n")
                           .c_str());
    return;
  }
  const std::optional<Picture> expected = Load(golden, error);
  if (!expected)
  {
    Assert::Fail((L"reading " + golden.wstring() + L": " + error).c_str());
  }
  const std::size_t differences = Differences(_picture, expected.value_or(Picture{}), MOST_CHANNEL_DIFFERENCE);
  if (differences != 0)
  {
    // Reported, not failed (§6.5): WARP's output can change with the Windows build.
    Logger::WriteMessage(std::format(L"{}: {} pixels differ from the golden image by more than {} in a channel; see {}\n", name,
                                     differences, MOST_CHANNEL_DIFFERENCE, rendered.wstring())
                           .c_str());
  }
}

} // namespace OutpostTests
