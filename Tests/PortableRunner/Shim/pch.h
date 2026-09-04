#pragma once

/*
 * The test project's precompiled header, as this runner needs it.
 *
 * Every file under test starts with `#include "pch.h"`, and MSVC's version of it pulls in
 * NeuronCore.h and CppUnitTest.h. This does the same, reaching the two shims beside it rather
 * than the real headers -- so the files under test are compiled exactly as written.
 *
 * It is not precompiled. g++ would happily precompile it; at this suite's size the win is
 * seconds and the failure mode -- a stale .gch that silently shadows an edited header -- costs
 * more than that to diagnose.
 */

#include "NeuronCore.h"

#include "CppUnitTest.h"
