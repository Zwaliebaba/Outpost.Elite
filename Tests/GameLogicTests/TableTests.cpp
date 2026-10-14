#include "pch.h"

#include "GalaxyTables.h"
#include "Scene.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// What a blueprint indexes (RenderBlueprint, DrawVisibleFaces): vertexBuffer's 42 vertices of six bytes, the four-byte point
// read at a vertex byte, and faceFillPatterns' 16 words.
constexpr std::size_t VERTEX_BUFFER_BYTES = std::size_t{42} * 6;
constexpr std::size_t POINT_BYTES = 4;
constexpr std::uint8_t VERTEX_INDEX_BITS = 0x3F;
constexpr std::uint16_t EDGE_POINT_BITS = 0xFCFC;
constexpr std::uint8_t FACE_TRIANGLE = 0x80;
constexpr std::size_t FILL_PATTERN_BYTES = std::size_t{16} * 2;
constexpr int TRIANGLE_ITEM_BYTES = 4;
constexpr std::size_t SHIP_TYPES = 30;

// ExpandDescriptionText's codes: control codes 1 to 6, and phrase codes from 80h, one for each of descriptionPhraseLists' lists.
constexpr std::uint8_t LAST_CONTROL_CODE = 6;
constexpr std::uint8_t FIRST_PRINTABLE = 0x20;
constexpr std::uint8_t PHRASE_CODE = 0x80;

// Whether a point read at the vertex byte _vertex lies within vertexBuffer.
[[nodiscard]] bool PointInBuffer(std::size_t _vertex) noexcept
{
  return _vertex + POINT_BYTES <= VERTEX_BUFFER_BYTES;
}

// What is wrong with ship type _kind's blueprint, or nothing: an index past what it indexes, or a face that does not end where its
// count says, or a face list that does not end with its last face.
[[nodiscard]] std::string BlueprintFault(std::uint8_t _kind)
{
  const Elite::Blueprint& blueprint = Elite::BlueprintOf(_kind);
  for (const std::uint8_t operation : blueprint.vertexProgram)
  {
    if (6 * std::size_t{static_cast<std::uint8_t>(operation & VERTEX_INDEX_BITS)} + 6 > VERTEX_BUFFER_BYTES)
    {
      return "a vertex";
    }
  }
  const std::size_t faceEdges = blueprint.faceEdges.size() / 2;
  for (std::size_t edge = 0; edge < faceEdges; ++edge)
  {
    const auto points =
      static_cast<std::uint16_t>((blueprint.faceEdges[2 * edge] | (blueprint.faceEdges[2 * edge + 1] << 8)) & EDGE_POINT_BITS);
    if (!PointInBuffer(std::max<std::size_t>(points & 0xFF, points >> 8)))
    {
      return "an edge's point";
    }
  }
  const std::span<const std::uint8_t> list = blueprint.faceList;
  std::size_t at = 0;
  for (std::uint8_t face = 0; face < blueprint.faces; ++face)
  {
    if (at + 4 > list.size() || !PointInBuffer(*std::max_element(list.begin() + at, list.begin() + at + 3)))
    {
      return "a face's point";
    }
    if (std::any_of(list.begin() + at, list.begin() + at + 3, [](std::uint8_t _byte) { return _byte % 2 != 0; }))
    {
      return "a face's point between two of vertexBuffer's words";
    }
    int left = list[at + 3];
    at += 4;
    while (left > 0)
    {
      if (at >= list.size())
      {
        return "a face past the list";
      }
      const std::uint8_t item = list[at];
      ++at;
      if ((item & FACE_TRIANGLE) != 0)
      {
        const std::size_t fill = item & static_cast<std::uint8_t>(~FACE_TRIANGLE);
        if (fill % 2 != 0 || fill + 2 > FILL_PATTERN_BYTES)
        {
          return "a fill pattern";
        }
        if (at + 3 > list.size())
        {
          return "a triangle past the list";
        }
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
          if (!PointInBuffer(static_cast<std::uint8_t>(list[at + corner] << 1)))
          {
            return "a triangle's point";
          }
        }
        at += 3;
        left -= TRIANGLE_ITEM_BYTES;
      }
      else
      {
        if (std::size_t{item} + 2 > 2 * faceEdges)
        {
          return "an edge item";
        }
        --left;
      }
      if (left < 0)
      {
        return "a face that does not end where its count says";
      }
    }
  }
  return at == list.size() ? std::string() : std::string("a face list that runs on past its last face");
}

// Whether _text holds only what ExpandDescriptionText expands: characters, control codes 1 to 6, and a list's phrase code.
[[nodiscard]] bool HoldsOnlyItsCodes(std::string_view _text) noexcept
{
  return std::ranges::all_of(_text,
                             [](char _character)
                             {
                               const auto code = static_cast<std::uint8_t>(_character);
                               return !(code > LAST_CONTROL_CODE && code < FIRST_PRINTABLE) &&
                                      code < PHRASE_CODE + Elite::DESCRIPTION_PHRASE_LISTS.size();
                             });
}

} // namespace

// The game's tables are source (Phase 7, step 5): what the tool that wrote them from the original's image checked of them, these check,
// so that an edit cannot take the code that reads them past their ends.
TEST_CLASS(TableTests)
{
public:
  // Every blueprint's vertex program, edges and faces index within vertexBuffer, faceFillPatterns and the blueprint's own edges,
  // a face's points at the start of one of vertexBuffer's words, and every face ends where its count says, the last where the face
  // list does (Scene.cpp relies on it).
  TEST_METHOD(EveryBlueprintStaysWithinWhatItIndexes)
  {
    for (std::size_t kind = 0; kind < SHIP_TYPES; ++kind)
    {
      const std::string fault = BlueprintFault(static_cast<std::uint8_t>(kind));
      Assert::IsTrue(fault.empty(),
                     (L"ship type " + std::to_wstring(kind) + L"'s blueprint: " + std::wstring(fault.begin(), fault.end())).c_str());
    }
  }

  // Every phrase and the description's template hold only the codes ExpandDescriptionText expands, and a byte indexes every
  // phrase (Galaxy.cpp relies on it: a code 7 to 31 would run into the words after its handlers).
  TEST_METHOD(EveryDescriptionTextHoldsOnlyItsCodes)
  {
    Assert::IsTrue(Elite::DESCRIPTION_PHRASES.size() <= 0x100, L"a byte indexes every phrase");
    for (const std::string_view phrase : Elite::DESCRIPTION_PHRASES)
    {
      Assert::IsTrue(HoldsOnlyItsCodes(phrase), L"a phrase holds only its codes");
    }
    Assert::IsTrue(HoldsOnlyItsCodes(Elite::DESCRIPTION_TEMPLATE), L"the template holds only its codes");
  }
};

} // namespace GameLogicTests
