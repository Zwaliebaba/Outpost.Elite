#include "pch.h"

#include "DataOverlay.h"
#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

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
    rig.Both([](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
             { _pc.Ram().Write8(Elite::DataSegment(_program), Elite::DS.fuel.offset, 0x20); });
    rig.Play("key space; wait 4\ndigest docked");
  }
};

} // namespace GameLogicTests
