#include "pch.h"

#include "Repository.h"
#include "Text.h"
#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

using Commander = Elite::CommanderState;

// From the title: the credits, then the status screen.
constexpr std::string_view TO_THE_DOCK = "key space; wait 3.2";

// The status screen ignores its own F6 (D96), so the disc menu first and back.
constexpr std::string_view STATUS_AGAIN = "key Escape; wait 0.2\nkey F6; wait 0.2\n";

// Sets the byte _member names to _value on the twin.
template <typename Struct> void SetBoth(TwinRig& _rig, DataByte<Struct> _member, std::uint8_t _value)
{
  _rig.Both([_member, _value](Elite::Host&, Elite::GameState& _state) { DataByteIn(_state, _member) = _value; });
}

// Sets the units of product _product held, its lot in cargoHold, to _value on the twin.
void SetHeld(TwinRig& _rig, std::size_t _product, std::uint8_t _value)
{
  _rig.Both([_product, _value](Elite::Host&, Elite::GameState& _state) { _state.Data().commander.cargoHold.lots[_product].held = _value; });
}

// The status screen again, then _steps.
std::string StatusAgain(std::string_view _steps)
{
  return std::string(STATUS_AGAIN) + std::string(_steps);
}

// From the title: the credits, the status screen, the disc menu, and JAMESON loaded.
constexpr std::string_view LOAD_JAMESON = "key space; wait 4\nkey Escape; wait 0.3\nkey l; wait 0.3\n"
                                          "key j; key a; key m; key e; key s; key o; key n; key Return; wait 0.5";

// Replays/_source, a prepared commander, copied into the twin's DOS directory as JAMESON.CDR, as a
// replay's file step copies one (DirectoryFileStore gives a file it did not write attribute 0 and DOS's stamp), and loaded at the
// disc menu as the game loads its own saves.
void LoadCommander(TwinRig& _rig, std::string_view _source)
{
  const std::filesystem::path source = Elite::FindInRepository(std::filesystem::path("Replays") / _source);
  Assert::IsFalse(source.empty(), L"the commander in Replays/");
  std::filesystem::copy_file(source, _rig.Files() / "JAMESON.CDR", std::filesystem::copy_options::overwrite_existing);
  _rig.Play(LOAD_JAMESON);
}

// The cash, in tenths of credits, as the twin holds it.
[[nodiscard]] std::uint32_t TwinCreditsTenths(TwinRig& _rig)
{
  std::uint32_t tenths = 0;
  _rig.Both([&tenths](Elite::Host&, Elite::GameState& _state) { tenths = _state.Data().commander.creditsTenths; });
  return tenths;
}

// The byte _member names, as the twin holds it.
template <typename Struct> [[nodiscard]] std::uint8_t TwinByte(TwinRig& _rig, DataByte<Struct> _member)
{
  std::uint8_t value = 0;
  _rig.Both([&value, _member](Elite::Host&, Elite::GameState& _state) { value = DataByteIn(_state, _member); });
  return value;
}

} // namespace

// The docked screens that wait, in states no replay reaches (ADR-016): every digest is the twin's known answer.
TEST_CLASS(DockedTests)
{
public:
  // Cargo held: the status screen in witch space, an offender with the last equipment item fitted, and a fugitive; then the
  // inventory's rows, in flight, since docked D08 shows the cargo and F10 does nothing (D96, ADR-063). The trade screens it once
  // visited are the market's now (D01, D92).
  TEST_METHOD(TradeInventoryAndStatusScreensAgreeWithCargoAndARecord)
  {
    TwinRig rig("TwinDockedCargo");
    rig.Play(TO_THE_DOCK);
    SetHeld(rig, 0, 5);
    SetHeld(rig, 6, 1);
    SetHeld(rig, 16, 2);
    SetBoth(rig, &Commander::CargoHold::refugeesTonnes, 3);
    SetBoth(rig, &Commander::legalStatus, 5);
    SetBoth(rig, &Commander::witchspaceCountdown, 3);
    SetBoth(rig, &Commander::militaryLaserCount, 1);
    rig.Play(StatusAgain("digest offender"));
    SetBoth(rig, &Commander::legalStatus, 0x30);
    rig.Play(StatusAgain("digest fugitive"));
    SetBoth(rig, &Commander::witchspaceCountdown, 0);
    rig.Play("key F1; wait 2\nkey F10; wait 0.2\ndigest inventory");
  }

  // Mission 1, the supernova: a key that is neither Y nor N, then N; the debriefing before the jump (it
  // returns at once) and after it, with no refugees aboard; Y with the large cargo bay; and the debriefing
  // with all 20 tonnes of refugees.
  TEST_METHOD(SupernovaBriefingsAndDebriefingsAgree)
  {
    TwinRig rig("TwinSupernova");
    rig.Play(TO_THE_DOCK);
    SetBoth(rig, &Commander::missionNumber, 1);
    SetBoth(rig, &Elite::FlightState::playerDocked, 1);
    rig.Play(StatusAgain("digest briefing\nkey a; wait 0.2\nkey n; wait 0.2\ndigest refused\nkey space; wait 0.2"));
    rig.Play(StatusAgain("digest not-yet"));
    SetBoth(rig, &Commander::jumpedSinceBriefing, 1);
    rig.Play(StatusAgain("digest no-refugees\nkey space; wait 0.2"));
    SetBoth(rig, &Commander::missionNumber, 1);
    SetBoth(rig, &Commander::largeCargoBayFitted, 1);
    rig.Play(StatusAgain("key y; wait 0.2\ndigest accepted\nkey space; wait 0.2"));
    SetBoth(rig, &Commander::jumpedSinceBriefing, 1);
    SetBoth(rig, &Commander::CargoHold::refugeesTonnes, 0x14);
    rig.Play(StatusAgain("digest rewarded\nkey space; wait 0.2\ndigest after"));
  }

  // Mission 2, the mask ship: the briefing; the debriefing before the ship is destroyed (at once), after it
  // fled, after it was destroyed, and with its masking device recovered. Mission 3, the Thargoid invasion:
  // the briefing, and the debriefing that makes the commander an Archangel.
  TEST_METHOD(MaskShipAndInvasionBriefingsAndDebriefingsAgree)
  {
    TwinRig rig("TwinMaskAndInvasion");
    rig.Play(TO_THE_DOCK);
    SetBoth(rig, &Commander::missionNumber, 2);
    SetBoth(rig, &Elite::FlightState::playerDocked, 1);
    rig.Play(StatusAgain("digest mask-briefing\nkey space; wait 0.2"));
    rig.Play(StatusAgain("digest not-yet"));
    SetBoth(rig, &Commander::maskShipDestroyed, 1);
    SetBoth(rig, &Commander::fledMaskShip, 1);
    rig.Play(StatusAgain("digest fled\nkey space; wait 0.2"));
    for (const bool recovered : {false, true})
    {
      SetBoth(rig, &Commander::missionNumber, 2);
      SetBoth(rig, &Commander::missionStage, 1);
      SetBoth(rig, &Commander::maskShipDestroyed, 1);
      SetBoth(rig, &Commander::maskingDeviceRecovered, static_cast<std::uint8_t>(recovered ? 1 : 0));
      rig.Play(StatusAgain("digest destroyed\nkey space; wait 0.2"));
    }
    SetBoth(rig, &Commander::missionNumber, 3);
    rig.Play(StatusAgain("digest invasion-briefing\nkey space; wait 0.2"));
    rig.Play(StatusAgain("digest invasion-debriefing\nkey space; wait 0.2\ndigest archangel"));
  }

  // Mission 1 declined, and the 1,400 credits its debriefing pays for a hold without its 20 tonnes of refugees (ADR-008 item 8,
  // D20): supernova-mission.replay with N at the briefing in place of Y. ShowMissionBriefing counts N as taking the mission all
  // the same; ShowMissionDebriefing then waits for a jump after it and a real docking, and pays 1,000 credits only for exactly 20
  // tonnes of refugees. From supernova-mission.cdr: the galactic jump that gives the mission, the escape capsule to the briefing,
  // N; a capsule and a galactic hyperdrive bought, the jump on to galaxy 3, the capsule to its station, which is no docking; and
  // the launch and the docking by hand that launch-and-dock.replay flies, after which the status screen debriefs.
  TEST_METHOD(SupernovaDeclinedPaysFourteenHundred)
  {
    TwinRig rig("TwinSupernovaDeclined");
    LoadCommander(rig, "supernova-mission.cdr");
    rig.Play("key F1; wait 0.5\ndown g; wait 1; up g\ndown h; wait 0.1; up h\nwait 8\ndigest mission-given");
    rig.Play("down c; wait 0.1; up c\nwait 5\ndigest briefing\nkey n; wait 1\ndigest declined\nkey space; wait 1");
    Assert::AreEqual(std::uint8_t{0}, TwinByte(rig, &Commander::CargoHold::refugeesTonnes), L"no refugees aboard");
    // Equip Ship, its cursor on Fuel, which the mission does not sell: seven rows down an escape capsule, four more a galactic
    // hyperdrive.
    rig.Play("key F3; wait 0.8\n"
             "key Down; wait 0.15; key Down; wait 0.15; key Down; wait 0.15; key Down; wait 0.15\n"
             "key Down; wait 0.15; key Down; wait 0.15; key Down; wait 0.15\nkey b; wait 0.3\n"
             "key Down; wait 0.15; key Down; wait 0.15; key Down; wait 0.15; key Down; wait 0.15\nkey b; wait 0.3\ndigest bought");
    // The launch at the moment recorded (ADR-064).
    rig.Play("wait 0.05\nkey F1; wait 0.45\ndown g; wait 1; up g\ndown h; wait 0.1; up h\nwait 8\ndigest galaxy-3");
    rig.Play("down c; wait 0.1; up c\nwait 5\ndigest capsule-docked");
    const std::uint32_t before = TwinCreditsTenths(rig);
    rig.Play("key F1; wait 6\ndown Down; wait 1.17; up Down\nwait 0.5\ndown Right; wait 0.65; up Right\nwait 10\ndigest debriefing");
    rig.Play("key space; wait 1\ndigest rewarded");
    Assert::AreEqual(std::uint32_t{14'000}, TwinCreditsTenths(rig) - before, L"1,400 credits");
  }
};

} // namespace GameLogicTests
