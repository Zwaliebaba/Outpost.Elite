#include "pch.h"

#include "ComparisonRig.h"
#include "DataOverlay.h"
#include "TwinRig.h"

#include <initializer_list>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint16_t AWARD_ARCHANGEL_TITLE = 0x49E4;
constexpr std::uint16_t FORMAT_FUEL_LIGHT_YEARS = 0x6923;
constexpr std::uint16_t DRAW_DOCKED_FRAME = 0x7C88;
constexpr std::uint8_t TEXT_LAYOUT = 2; // screenLayout while the text page shows

// From the title: the credits, then the status screen.
constexpr std::string_view TO_THE_DOCK = "key space; wait 3.2";

// The status screen ignores its own F9, so F10 first and back.
constexpr std::string_view STATUS_AGAIN = "key F10; wait 0.2\nkey F9; wait 0.2\n";

// Sets the byte _field to _value on both twins.
void SetBoth(TwinRig& _rig, Elite::DataField<std::uint8_t> _field, std::uint8_t _value)
{
  _rig.Both([_field, _value](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
            { _pc.Ram().Write8(Elite::DataSegment(_program), _field.offset, _value); });
}

// Sets the byte at _offset in the data segment to _value on both twins.
void SetBoth(TwinRig& _rig, std::uint16_t _offset, std::uint8_t _value)
{
  SetBoth(_rig, Elite::DataField<std::uint8_t>{_offset}, _value);
}

// The status screen again, then _steps.
std::string StatusAgain(std::string_view _steps)
{
  return std::string(STATUS_AGAIN) + std::string(_steps);
}

} // namespace

// The docked screens that wait, in states no replay reaches (ADR-010 item 5): both twins get the same
// state and keys, and every digest must agree.
TEST_CLASS(DockedTests)
{
public:
  // Cargo held: the sell screen's quantities, the buy screen a second time (quantities already drawn), the
  // inventory's rows; then the status screen in witch space, an offender with the last equipment item
  // fitted, and a fugitive.
  TEST_METHOD(TradeInventoryAndStatusScreensAgreeWithCargoAndARecord)
  {
    TwinRig rig("TwinDockedCargo");
    rig.Play(TO_THE_DOCK);
    SetBoth(rig, Elite::DS.cargoHold.At(0), 5);
    SetBoth(rig, Elite::DS.cargoHold.At(6), 1);
    SetBoth(rig, Elite::DS.cargoHold.At(16), 2);
    SetBoth(rig, Elite::DS.refugeesTonnes, 3);
    rig.Play("key F2; wait 0.2\ndigest sell\nkey F3; wait 0.2\nkey F2; wait 0.2\nkey F3; wait 0.2\ndigest buy-again\n"
             "key F10; wait 0.2\ndigest inventory");
    SetBoth(rig, Elite::DS.legalStatus, 5);
    SetBoth(rig, Elite::DS.witchspaceCountdown, 3);
    SetBoth(rig, Elite::DS.militaryLaserCount, 1);
    rig.Play("key F9; wait 0.2\ndigest offender");
    SetBoth(rig, Elite::DS.legalStatus, 0x30);
    rig.Play(StatusAgain("digest fugitive"));
  }

  // Mission 1, the supernova: a key that is neither Y nor N, then N; the debriefing before the jump (it
  // returns at once) and after it, with no refugees aboard; Y with the large cargo bay; and the debriefing
  // with all 20 tonnes of refugees.
  TEST_METHOD(SupernovaBriefingsAndDebriefingsAgree)
  {
    TwinRig rig("TwinSupernova");
    rig.Play(TO_THE_DOCK);
    SetBoth(rig, Elite::DS.missionNumber, 1);
    SetBoth(rig, Elite::DS.playerDocked, 1);
    rig.Play(StatusAgain("digest briefing\nkey a; wait 0.2\nkey n; wait 0.2\ndigest refused\nkey space; wait 0.2"));
    rig.Play(StatusAgain("digest not-yet"));
    SetBoth(rig, Elite::DS.jumpedSinceBriefing, 1);
    rig.Play(StatusAgain("digest no-refugees\nkey space; wait 0.2"));
    SetBoth(rig, Elite::DS.missionNumber, 1);
    SetBoth(rig, Elite::DS.largeCargoBayFitted, 1);
    rig.Play(StatusAgain("key y; wait 0.2\ndigest accepted\nkey space; wait 0.2"));
    SetBoth(rig, Elite::DS.jumpedSinceBriefing, 1);
    SetBoth(rig, Elite::DS.refugeesTonnes, 0x14);
    rig.Play(StatusAgain("digest rewarded\nkey space; wait 0.2\ndigest after"));
  }

  // Mission 2, the mask ship: the briefing; the debriefing before the ship is destroyed (at once), after it
  // fled, after it was destroyed, and with its masking device recovered. Mission 3, the Thargoid invasion:
  // the briefing, and the debriefing that makes the commander an Archangel.
  TEST_METHOD(MaskShipAndInvasionBriefingsAndDebriefingsAgree)
  {
    TwinRig rig("TwinMaskAndInvasion");
    rig.Play(TO_THE_DOCK);
    SetBoth(rig, Elite::DS.missionNumber, 2);
    SetBoth(rig, Elite::DS.playerDocked, 1);
    rig.Play(StatusAgain("digest mask-briefing\nkey space; wait 0.2"));
    rig.Play(StatusAgain("digest not-yet"));
    SetBoth(rig, Elite::DS.maskShipDestroyed, 1);
    SetBoth(rig, Elite::DS.fledMaskShip, 1);
    rig.Play(StatusAgain("digest fled\nkey space; wait 0.2"));
    for (const bool recovered : {false, true})
    {
      SetBoth(rig, Elite::DS.missionNumber, 2);
      SetBoth(rig, Elite::DS.missionStage, 1);
      SetBoth(rig, Elite::DS.maskShipDestroyed, 1);
      SetBoth(rig, Elite::DS.maskingDeviceRecovered, static_cast<std::uint8_t>(recovered ? 1 : 0));
      rig.Play(StatusAgain("digest destroyed\nkey space; wait 0.2"));
    }
    SetBoth(rig, Elite::DS.missionNumber, 3);
    rig.Play(StatusAgain("digest invasion-briefing\nkey space; wait 0.2"));
    rig.Play(StatusAgain("digest invasion-debriefing\nkey space; wait 0.2\ndigest archangel"));
  }

  // The fuel's text from an empty tank to a full one, and the Archangel's title: the status and inventory screens and the
  // invasion's debriefing call them as value routines since level 5 of the de-assembly (ADR-012 item 12), so only calls like
  // these compare them with the original.
  TEST_METHOD(FuelTextAndArchangelTitleAgree)
  {
    ComparisonRig rig("DockedHelpers");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const std::initializer_list<std::uint8_t> fuels = {0, 1, 35, 36, 100, 254, 255};
    for (const std::uint8_t fuel : fuels)
    {
      ram.Write8(data, Elite::DS.fuel.offset, fuel);
      rig.Call(FORMAT_FUEL_LIGHT_YEARS, {});
    }
    rig.AssertAllAgreed(FORMAT_FUEL_LIGHT_YEARS, fuels.size());
    rig.Call(AWARD_ARCHANGEL_TITLE, {});
    rig.AssertAllAgreed(AWARD_ARCHANGEL_TITLE, 1);
  }

  // The docked screens' frame from each kind of descriptor, with the text page already showing, so that no BIOS call keeps the
  // comparison from undoing it: every docked screen draws it as a value routine since level 5 of the de-assembly (ADR-012 item
  // 12).
  TEST_METHOD(DockedFrameAgreesForEveryScreen)
  {
    ComparisonRig rig("DockedFrame");
    Machine::Memory& ram = rig.Host().Ram();
    const std::uint16_t data = Elite::DataSegment(rig.Program());
    const std::initializer_list<std::uint16_t> frames = {Elite::DS.commanderTitle.offset, Elite::DS.inventoryFrame.offset,
                                                         Elite::DS.emergencyFrame.offset, Elite::DS.discControlFrame.offset,
                                                         Elite::DS.equipShipFrame.offset};
    for (const std::uint16_t frame : frames)
    {
      ram.Write8(data, Elite::DS.screenLayout.offset, TEXT_LAYOUT);
      rig.Call(DRAW_DOCKED_FRAME, {.si = frame});
    }
    rig.AssertAllAgreed(DRAW_DOCKED_FRAME, frames.size());
  }
};

} // namespace GameLogicTests
