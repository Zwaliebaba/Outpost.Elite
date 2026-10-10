#include "pch.h"

#include "DataOverlay.h"
#include "TwinRig.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

TEST_CLASS(TwinRigTests)
{
public:
  // The title screen's ship types stepped with F9 and F10, and the commander loaded with space: every digest is the
  // twin's known answer.
  TEST_METHOD(TwinsAgreeFromTheTitleScreenToTheDock)
  {
    TwinRig rig("TwinTitle");
    rig.Play("key F9; wait 1\nkey F10; wait 1\ndigest ships\nkey space; wait 4\ndigest docked");
  }

  // A twin's set-up changes what the game does from there: the commander's fuel set before space loads the commander and
  // docks, and each of its three digests (the boot's, the dock's and the end's) the answer the original gave.
  TEST_METHOD(TwinTakesASetUp)
  {
    TwinRig rig("TwinSetUp");
    rig.Both([](Machine::Pc& _pc, const Machine::LoadedProgram& _program)
             { _pc.Ram().Write8(Elite::DataSegment(_program), Elite::DS.fuel.offset, 0x20); });
    rig.Play("key space; wait 4\ndigest docked");
  }
};

} // namespace GameLogicTests
