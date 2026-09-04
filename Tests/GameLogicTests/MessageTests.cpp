#include "pch.h"

#include "Cpu6502.h"
#include "OracleImage.h"

#include "Canvas.h"
#include "ExtendedTokens.h"
#include "Messages.h"
#include "Rng.h"
#include "TextPrint.h"
#include "Tokens.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using Elite::Testing::Cpu6502;
using Elite::Testing::OracleImage;

/*
 * In-flight messages (slice 3d-c).
 *
 * Compared on the BITMAP, because `MESS` is a printing routine and where the characters land is
 * the whole of what it decides -- the row, the centred column, and whether " DESTROYED" follows.
 * The message counters are compared alongside, because the next frame's erase depends on them.
 */
namespace GameLogicTests
{

namespace
{
bool OracleMissing()
{
  const OracleImage& oracle = OracleImage::Instance();
  if (oracle.Available())
  {
    return false;
  }
  Logger::WriteMessage(("SKIPPED -- oracle absent: " + oracle.Reason()).c_str());
  return true;
}

std::wstring Widen(const std::string& _text)
{
  return std::wstring(_text.begin(), _text.end());
}

/// The whole text stack the port prints through, wired the way `ChartTests` wires it.
struct PortText
{
  PortText()
    : screen(canvas, text)
    , characters(screen)
    , printer(characters)
    , extended(characters, printer, rng)
  {
  }

  Elite::Canvas canvas;
  Elite::TextState text;
  Elite::Rng rng;
  Elite::TextPrinter screen;
  Elite::CharacterPrinter characters;
  Elite::TokenPrinter printer;
  Elite::ExtendedTokenPrinter extended;
  Elite::MessageState message;
};

struct Labels
{
  std::uint16_t mess = 0, dly = 0, de = 0, mch = 0, messXC = 0, qq11 = 0;
  std::uint16_t yc = 0, xc = 0, qq17 = 0, dtw2 = 0, dtw4 = 0, dtw5 = 0, col2 = 0;
  std::uint16_t rand = 0, screen = 0;

  explicit Labels(const OracleImage& _oracle)
  {
    mess = _oracle.Label("MESS");
    dly = _oracle.Label("DLY");
    de = _oracle.Label("de");
    mch = _oracle.Label("MCH");
    messXC = _oracle.Label("messXC");
    qq11 = _oracle.Label("QQ11");
    yc = _oracle.Label("YC");
    xc = _oracle.Label("XC");
    qq17 = _oracle.Label("QQ17");
    dtw2 = _oracle.Label("DTW2");
    dtw4 = _oracle.Label("DTW4");
    dtw5 = _oracle.Label("DTW5");
    col2 = _oracle.Label("COL2");
    rand = _oracle.Label("RAND");

    const Cpu6502 image = _oracle.Fresh();
    screen = static_cast<std::uint16_t>(
      (image.memory[_oracle.Label("ylookupl")] | (image.memory[_oracle.Label("ylookuph")] << 8))
      - 0x20);
  }
};

std::uint32_t CompareScreens(const Cpu6502& _cpu, std::uint16_t _base, const Elite::Canvas& _canvas,
                             const std::wstring& _context)
{
  const std::span<const std::uint8_t> ours = _canvas.Screen();
  std::uint32_t drawn = 0;

  for (std::uint16_t offset = 0; offset < Elite::Canvas::SCREEN_SIZE; ++offset)
  {
    const std::uint8_t expected = _cpu.memory[static_cast<std::uint16_t>(_base + offset)];
    if (expected != ours[offset])
    {
      Assert::Fail((_context + L": screen differs at offset " + std::to_wstring(offset)
                    + L" -- game has " + std::to_wstring(expected) + L", port has "
                    + std::to_wstring(ours[offset]))
                     .c_str());
    }
    drawn += (ours[offset] != 0u) ? 1u : 0u;
  }

  return drawn;
}
} // namespace


TEST_CLASS(InFlightMessages)
{
public:
  /*
   * 6502: MESS, me1 and mes9 -- the whole chain, over the tokens the game actually sends it.
   *
   * Every view, both states of `de`, and both states of `DLY` -- because a non-zero `DLY` is what
   * sends `MESS` through `me1` to erase the message already up, which is the second pass through
   * the same instructions and the case a port is most likely to write as a straight line.
   */
  TEST_METHOD(ShowingAMessageMatchesMESS)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Labels at(oracle);

    /*
     * The tokens the game sends: "ENERGY LOW", "RIGHT ON COMMANDER", "GALACTIC HYPERSPACE",
     * "INCOMING MISSILE", "FUEL SCOOPS ON", "TARGET LOST", and a plain letter.
     *
     * `DTW4` bit 6 -- which suppresses the flush a form feed would otherwise cause -- is not
     * distinguishable here, and that is a measurement rather than an omission: dropping it
     * survives this sweep, and the sweep is every token the game sends `MESS`. Only tokens 4,
     * 65, 95, 126 and 132 contain a form feed and the game sends none of them, so bit 6 is
     * unobservable for the routine's real inputs (§6.68).
     */
    const std::uint8_t TOKENS[] = { 100, 101, 116, 120, 160, 200, 'A' };

    std::uint32_t compared = 0;
    std::uint32_t drawn = 0;
    std::uint32_t erased = 0;

    for (const std::uint8_t token : TOKENS)
    {
      for (const std::uint8_t view : { 0u, 1u, 128u })
      {
        for (const std::uint8_t destroyed : { 0u, 1u })
        {
          for (const std::uint8_t already : { 0u, 20u })
          {
            Cpu6502 cpu = oracle.Fresh();
            PortText port;

            const std::uint8_t SEED[4] = { 0x71, 0xC3, 0x49, 0x2B };
            for (std::size_t byte = 0; byte < 4u; ++byte)
            {
              cpu.memory[static_cast<std::uint16_t>(at.rand + byte)] = SEED[byte];
            }
            port.rng.SetState({ SEED[0], SEED[1], SEED[2], SEED[3] });

            cpu.memory[at.qq11] = view;
            cpu.memory[at.dly] = already;
            cpu.memory[at.de] = destroyed;
            cpu.memory[at.mch] = 101u; // whatever was on screen before
            cpu.memory[at.messXC] = 9u;
            cpu.memory[at.yc] = 5u;
            cpu.memory[at.xc] = 7u;
            cpu.memory[at.qq17] = 0x80u;
            cpu.memory[at.dtw2] = 0xFFu;
            cpu.memory[at.dtw4] = 0u;
            cpu.memory[at.dtw5] = 0u;
            cpu.memory[at.col2] = 0x40u;

            port.message.delay = already;
            port.message.append = destroyed;
            port.message.token = 101u;
            port.message.column = 9u;
            port.text.row = 5u;
            port.text.column = 7u;
            port.text.caseFlags = 0x80u;
            port.text.cellColour = 0x40u;
            port.characters.state.sentenceStart = 0xFFu;
            port.printer.SetCaseFlags(0x80u);

            cpu.a = token;
            const Elite::Testing::RunResult run = cpu.CallSubroutine(at.mess, 400'000);
            Assert::IsTrue(run.completed, L"MESS returned");

            Elite::ShowMessage(port.canvas, port.printer, port.text, port.characters.state,
                               port.message, token, view);

            const std::wstring where =
              Widen("MESS(token " + std::to_string(token) + ", view " + std::to_string(view)
                    + ", de " + std::to_string(destroyed) + ", DLY " + std::to_string(already)
                    + ")");

            drawn += CompareScreens(cpu, at.screen, port.canvas, where);

            Assert::AreEqual(cpu.memory[at.dly], port.message.delay, (where + L": DLY").c_str());
            Assert::AreEqual(cpu.memory[at.de], port.message.append, (where + L": de").c_str());
            Assert::AreEqual(cpu.memory[at.mch], port.message.token, (where + L": MCH").c_str());
            Assert::AreEqual(cpu.memory[at.messXC], port.message.column,
                             (where + L": messXC").c_str());
            Assert::AreEqual(cpu.memory[at.yc], port.text.row, (where + L": YC").c_str());
            Assert::AreEqual(cpu.memory[at.xc], port.text.column, (where + L": XC").c_str());
            Assert::AreEqual(cpu.memory[at.dtw4], port.characters.state.justify,
                             (where + L": DTW4").c_str());
            Assert::AreEqual(cpu.memory[at.dtw5], port.characters.state.bufferLength,
                             (where + L": DTW5").c_str());

            erased += (already != 0u) ? 1u : 0u;
            ++compared;
          }
        }
      }
    }

    Assert::AreEqual<std::uint32_t>(7u * 3u * 2u * 2u, compared, L"the whole sweep ran");
    Assert::IsTrue(erased > 0u, L"some cases had a message to erase first");
    Assert::IsTrue(drawn > 0u, L"and messages were actually printed");
    Logger::WriteMessage(("MESS: " + std::to_string(compared) + " messages, "
                          + std::to_string(erased) + " with one already up")
                           .c_str());
  }
};


TEST_CLASS(TheTokenPrinterOnMessageTokens)
{
public:
  /*
   * 6502: TT27 alone, over the recursive tokens `A` reaches directly.
   *
   * Here to tell a `MESS` defect from a `TT27` one. `A` between 96 and 127 is a recursive token
   * number and nothing else, so this is the same stack the message printer runs on with the
   * message printer taken away -- and five of the tokens in the table contain a FORM FEED, which
   * is the character the two disagree about.
   */
  TEST_METHOD(EveryDirectTokenMatchesTT27)
  {
    if (OracleMissing())
    {
      return;
    }

    const OracleImage& oracle = OracleImage::Instance();
    const Labels at(oracle);
    const std::uint16_t tt27 = oracle.Label("TT27");

    std::uint32_t compared = 0;
    std::uint32_t drawn = 0;

    /*
     * Three of the thirty-two are left out and the reason is the FIXTURE rather than the port:
     * tokens 119, 125 and 126 expand to control codes 0, 2 and 5 -- the cash, the current
     * system's name and the fuel -- which reach the `ValueTokens` seam, and this fixture has
     * none. Wiring one means a `StateTokens` with a commander, a name and two seed sets matched
     * on both sides, which belongs in a test about value tokens rather than one about messages.
     *
     * They were excluded only after being run: the first version of this swept all thirty-two,
     * failed on all three, and the decoded token table is what said the cause was a missing seam
     * and not a defect.
     */
    const auto reachesValueTokens = [](std::uint32_t _token) {
      return _token == 119u || _token == 125u || _token == 126u;
    };

    for (std::uint32_t token = 96; token <= 127; ++token)
    {
      if (reachesValueTokens(token))
      {
        continue;
      }

      Cpu6502 cpu = oracle.Fresh();
      PortText port;

      cpu.memory[at.yc] = 16u;
      cpu.memory[at.xc] = 1u;
      cpu.memory[at.qq17] = 0u;
      cpu.memory[at.dtw2] = 0xFFu;
      cpu.memory[at.dtw4] = 0u;
      cpu.memory[at.dtw5] = 0u;
      cpu.memory[at.col2] = 0x40u;
      cpu.memory[at.qq11] = 0u;

      port.text.row = 16u;
      port.text.column = 1u;
      port.text.caseFlags = 0u;
      port.text.cellColour = 0x40u;
      port.characters.state.sentenceStart = 0xFFu;
      port.printer.SetCaseFlags(0u);

      cpu.a = static_cast<std::uint8_t>(token);
      const Elite::Testing::RunResult run = cpu.CallSubroutine(tt27, 400'000);
      Assert::IsTrue(run.completed, L"TT27 returned");

      port.printer.Print(static_cast<std::uint8_t>(token));

      const std::wstring where = Widen("TT27(" + std::to_string(token) + ")");
      drawn += CompareScreens(cpu, at.screen, port.canvas, where);
      Assert::AreEqual(cpu.memory[at.yc], port.text.row, (where + L": YC").c_str());
      Assert::AreEqual(cpu.memory[at.xc], port.text.column, (where + L": XC").c_str());
      ++compared;
    }

    Assert::AreEqual<std::uint32_t>(29u, compared, L"every directly reachable token with no value seam");
    Assert::IsTrue(drawn > 0u, L"and they printed something");
  }
};

} // namespace GameLogicTests
