#include "pch.h"

#include "ReferenceImage.h"
#include "Sha256.h"

#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The SHA-256 of ELITES.EXE's load image: the 98,080 bytes after its 64-byte MZ header, in the file whose own SHA-256 was
// 18b5076a54733dea2d45b1e3b280fd1377067b6cb1b27166d3873aa7e7744363 (ADR-007). Measured from that file on 2026-10-10, before D28
// took it out of the tree (ADR-020).
constexpr const char* REFERENCE_IMAGE_SHA256 = "248c90724ebf8c4b94c85421638c337f928ffb2ac104001488a5c39381f9d35f";

} // namespace

TEST_CLASS(ReferenceImageTests)
{
public:
  // D3: the checked-in image is ELITES.EXE's, so the hosts load it and nothing reads the file. Since D28 the file is not in the
  // tree (ADR-020), and since D36 nothing generates the image from it, so the image is held to the hash measured from it: a byte
  // edited by hand fails here.
  TEST_METHOD(TheGeneratedImageIsTheReferences)
  {
    Assert::AreEqual(std::size_t{98'080}, Elite::REFERENCE_IMAGE.size(), L"the image's size");
    Assert::AreEqual(REFERENCE_IMAGE_SHA256, Elite::Sha256::ToHex(Elite::Sha256::Of(Elite::REFERENCE_IMAGE)).c_str(), false,
                     L"the image is ELITES.EXE's, byte for byte");
  }
};

} // namespace GameLogicTests
