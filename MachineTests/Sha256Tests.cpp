#include "pch.h"

#include "Sha256.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace MachineTests
{

namespace
{

std::string HexOf(std::string_view _text)
{
  const auto* bytes = reinterpret_cast<const std::uint8_t*>(_text.data());
  return Machine::Sha256::ToHex(Machine::Sha256::Of({bytes, _text.size()}));
}

} // namespace

// The known answers from FIPS 180-4's examples and NIST's SHA-256 test vectors. The 55-, 56- and
// 64-byte messages put the padding on each side of a block boundary.
TEST_CLASS(Sha256Tests)
{
public:
  TEST_METHOD(EmptyMessage)
  {
    Assert::AreEqual("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", HexOf("").c_str());
  }

  TEST_METHOD(OneBlockMessage)
  {
    Assert::AreEqual("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", HexOf("abc").c_str());
  }

  TEST_METHOD(TwoBlockMessage)
  {
    Assert::AreEqual("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
                     HexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
  }

  TEST_METHOD(PaddingAtBlockBoundaries)
  {
    Assert::AreEqual("9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318", HexOf(std::string(55, 'a')).c_str());
    Assert::AreEqual("b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a", HexOf(std::string(56, 'a')).c_str());
    Assert::AreEqual("ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb", HexOf(std::string(64, 'a')).c_str());
  }

  // Fed a piece at a time, in pieces that straddle blocks, it gives the one-piece answer, and
  // Finish() leaves it ready for the next message.
  TEST_METHOD(IncrementalUpdatesMatchOnePiece)
  {
    const std::vector<std::uint8_t> million(1'000'000, std::uint8_t{'a'});
    Machine::Sha256 hash;
    std::size_t offset = 0;
    for (std::size_t piece = 1; offset < million.size(); piece = piece * 3 + 1)
    {
      const std::size_t length = std::min(piece, million.size() - offset);
      hash.Update({million.data() + offset, length});
      offset += length;
    }
    Assert::AreEqual("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", Machine::Sha256::ToHex(hash.Finish()).c_str());

    const auto* abc = reinterpret_cast<const std::uint8_t*>("abc");
    hash.Update({abc, 3});
    Assert::AreEqual("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", Machine::Sha256::ToHex(hash.Finish()).c_str());
  }
};

} // namespace MachineTests
