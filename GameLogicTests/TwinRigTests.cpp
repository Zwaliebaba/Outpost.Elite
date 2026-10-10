#include "pch.h"

#include "DataOverlay.h"
#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

// The commander's fuel set on every machine of _twin before space loads the commander and docks: TwinSetUp's scenario,
// written once for the TwinRig and for the NativeTwin that takes its place at D7.
template <typename Twin> void LoadTheCommanderAfterASetUp(Twin& _twin)
{
  _twin.Both([](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
             { _pc.Ram().Write8(Elite::DataSegment(_program), Elite::DS.fuel.offset, 0x20); });
  _twin.Play("key space; wait 4\ndigest docked");
}

} // namespace

TEST_CLASS(TwinRigTests)
{
public:
  // The title screen's ship types stepped with F9 and F10, and the commander loaded with space: the two
  // machines agree on every digest and end in one state.
  TEST_METHOD(TwinsAgreeFromTheTitleScreenToTheDock)
  {
    TwinRig rig("TwinTitle");
    rig.Play("key F9; wait 1\nkey F10; wait 1\ndigest ships\nkey space; wait 4\ndigest docked");
  }

  // The native twin changes nothing the interpreted one does not: memory set up on both stays equal.
  TEST_METHOD(TwinsTakeTheSameSetUp)
  {
    TwinRig rig("TwinSetUp");
    LoadTheCommanderAfterASetUp(rig);
  }

  // A twin as D7 leaves it, run today (D20): TwinSetUp's scenario on the native machine alone, on a Dispatcher, each of
  // its three digests (the boot's, the dock's and the end's) checked against the answers the interpreted original
  // recorded for TwinSetUp, with no original to compare it with.
  TEST_METHOD(NativeTwinAloneMeetsTheKnownAnswers)
  {
    NativeTwin twin("TwinSetUp");
    LoadTheCommanderAfterASetUp(twin);
  }
};

} // namespace GameLogicTests
