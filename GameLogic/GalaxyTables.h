// GameLogic/GalaxyTables.h
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace Elite
{

// The tables and constants this subsystem reads, which nothing writes (D33, D38): source since the original's image went (Phase 7,
// step 5).
// Galaxy.cpp alone includes this, so they are in an unnamed namespace (D38).
namespace
{

/// economyNames: each pointer's text, up to its NUL.
constexpr std::array<std::string_view, 8> ECONOMY_NAMES = {
  std::string_view{"Poor Agricultural"},    // [0]
  std::string_view{"Average Agricultural"}, // [1]
  std::string_view{"Rich Agricultural"},    // [2]
  std::string_view{"Mainly Agricultural"},  // [3]
  std::string_view{"Mainly Industrial"},    // [4]
  std::string_view{"Poor Industrial"},      // [5]
  std::string_view{"Average Industrial"},   // [6]
  std::string_view{"Rich Industrial"},      // [7]
};

/// governmentNames: each pointer's text, up to its NUL.
constexpr std::array<std::string_view, 8> GOVERNMENT_NAMES = {
  std::string_view{"Anarchy"},          // [0]
  std::string_view{"Feudal"},           // [1]
  std::string_view{"Multi-Government"}, // [2]
  std::string_view{"Dictatorship"},     // [3]
  std::string_view{"Communist"},        // [4]
  std::string_view{"Confederacy"},      // [5]
  std::string_view{"Democracy"},        // [6]
  std::string_view{"Corporate State"},  // [7]
};

/// speciesSizeNames: each pointer's text, up to its NUL.
constexpr std::array<std::string_view, 8> SPECIES_SIZE_NAMES = {
  std::string_view{"Large "},  // [0]
  std::string_view{"Fierce "}, // [1]
  std::string_view{"Small "},  // [2]
  std::string_view{},          // [3]
  std::string_view{},          // [4]
  std::string_view{},          // [5]
  std::string_view{},          // [6]
  std::string_view{},          // [7]
};

/// speciesColorNames: each pointer's text, up to its NUL.
constexpr std::array<std::string_view, 8> SPECIES_COLOR_NAMES = {
  std::string_view{"Green "},    // [0]
  std::string_view{"Red "},      // [1]
  std::string_view{"Yellow "},   // [2]
  std::string_view{"Blue "},     // [3]
  std::string_view{"Black "},    // [4]
  std::string_view{"Harmless "}, // [5]
  std::string_view{},            // [6]
  std::string_view{},            // [7]
};

/// speciesTraitNames: each pointer's text, up to its NUL.
constexpr std::array<std::string_view, 8> SPECIES_TRAIT_NAMES = {
  std::string_view{"Slimy "},    // [0]
  std::string_view{"Bug-Eyed "}, // [1]
  std::string_view{"Horned "},   // [2]
  std::string_view{"Bony "},     // [3]
  std::string_view{"Fat "},      // [4]
  std::string_view{"Furry "},    // [5]
  std::string_view{},            // [6]
  std::string_view{},            // [7]
};

/// speciesTypeNames: each pointer's text, up to its NUL.
constexpr std::array<std::string_view, 8> SPECIES_TYPE_NAMES = {
  std::string_view{"Rodents)"},   // [0]
  std::string_view{"Frogs)"},     // [1]
  std::string_view{"Lizards)"},   // [2]
  std::string_view{"Lobsters)"},  // [3]
  std::string_view{"Birds)"},     // [4]
  std::string_view{"Humanoids)"}, // [5]
  std::string_view{"Felines)"},   // [6]
  std::string_view{"Insects)"},   // [7]
};

constexpr std::string_view ECONOMY_LABEL_TEXT = "Economy:      ";

constexpr std::string_view GOVERNMENT_LABEL_TEXT = "Government:   ";

constexpr std::string_view TECH_LEVEL_LABEL_TEXT = "Tech. Level:  ";

constexpr std::string_view DISTANCE_LABEL_TEXT = "Distance:     ";

constexpr std::string_view LIGHT_YEARS_TEXT = " Light Years";

constexpr std::string_view POPULATION_LABEL_TEXT = "Population:   ";

constexpr std::string_view POPULATION_UNIT_TEXT = " Billion";

constexpr std::string_view SPECIES_OPEN_TEXT = "(";

constexpr std::string_view HUMAN_COLONIALS_TEXT = "Human Colonials)";

constexpr std::string_view PRODUCTIVITY_LABEL_TEXT = "Productivity: ";

constexpr std::string_view PRODUCTIVITY_UNIT_TEXT = " M CR";

constexpr std::string_view RADIUS_UNIT_TEXT = " km";

/// Every phrase descriptionPhraseLists' lists point at, in the order of their addresses: its text up to its NUL, which
/// may hold the control codes (1-31) and phrase codes (80h on) ExpandDescriptionText expands.
inline constexpr std::array<std::string_view, 195> DESCRIPTION_PHRASES = {
  std::string_view{"fabled "},                                          // 0
  std::string_view{"notable "},                                         // 1
  std::string_view{"well known "},                                      // 2
  std::string_view{"famous "},                                          // 3
  std::string_view{"noted "},                                           // 4
  std::string_view{"very "},                                            // 5
  std::string_view{"mildly "},                                          // 6
  std::string_view{"most "},                                            // 7
  std::string_view{"reasonably "},                                      // 8
  std::string_view{},                                                   // 9
  std::string_view{"ancient "},                                         // 10
  std::string_view{"\227"},                                             // 11
  std::string_view{"great "},                                           // 12
  std::string_view{"vast "},                                            // 13
  std::string_view{"pink "},                                            // 14
  std::string_view{"\005\234 \233\006plantations "},                    // 15
  std::string_view{"mountains "},                                       // 16
  std::string_view{"\232"},                                             // 17
  std::string_view{"\245forests "},                                     // 18
  std::string_view{"oceans "},                                          // 19
  std::string_view{"\246"},                                             // 20
  std::string_view{"mountain "},                                        // 21
  std::string_view{"edible "},                                          // 22
  std::string_view{"tree "},                                            // 23
  std::string_view{"spotted "},                                         // 24
  std::string_view{"\235"},                                             // 25
  std::string_view{"\236"},                                             // 26
  std::string_view{"\206\004oid "},                                     // 27
  std::string_view{"\244"},                                             // 28
  std::string_view{"\243"},                                             // 29
  std::string_view{"walking \215"},                                     // 30
  std::string_view{"crab "},                                            // 31
  std::string_view{"bat "},                                             // 32
  std::string_view{"lobst "},                                           // 33
  std::string_view{"\003"},                                             // 34
  std::string_view{"ancient "},                                         // 35
  std::string_view{"exceptional "},                                     // 36
  std::string_view{"eccentric "},                                       // 37
  std::string_view{"ingrained "},                                       // 38
  std::string_view{"\227"},                                             // 39
  std::string_view{"shyness "},                                         // 40
  std::string_view{"silliness "},                                       // 41
  std::string_view{"mating traditions "},                               // 42
  std::string_view{"loathing of \211"},                                 // 43
  std::string_view{"love for \211"},                                    // 44
  std::string_view{"food blenders "},                                   // 45
  std::string_view{"tourists "},                                        // 46
  std::string_view{"poetry "},                                          // 47
  std::string_view{"discos "},                                          // 48
  std::string_view{"\221"},                                             // 49
  std::string_view{"its \202\203"},                                     // 50
  std::string_view{"the \002\204\205"},                                 // 51
  std::string_view{"its inhabitants' \207\210"},                        // 52
  std::string_view{"\237\006"},                                         // 53
  std::string_view{"its \220\221"},                                     // 54
  std::string_view{"beset "},                                           // 55
  std::string_view{"plagued "},                                         // 56
  std::string_view{"ravaged "},                                         // 57
  std::string_view{"cursed "},                                          // 58
  std::string_view{"scourged "},                                        // 59
  std::string_view{"\226civil war "},                                   // 60
  std::string_view{"\215\204\205\004s "},                               // 61
  std::string_view{"a \215disease "},                                   // 62
  std::string_view{"\226earth quakes "},                                // 63
  std::string_view{"\226solar activity "},                              // 64
  std::string_view{"killer "},                                          // 65
  std::string_view{"deadly "},                                          // 66
  std::string_view{"evil "},                                            // 67
  std::string_view{"lethal "},                                          // 68
  std::string_view{"vicious "},                                         // 69
  std::string_view{"juice "},                                           // 70
  std::string_view{"brandy "},                                          // 71
  std::string_view{"water "},                                           // 72
  std::string_view{"brew "},                                            // 73
  std::string_view{"gargle blasters "},                                 // 74
  std::string_view{"\003"},                                             // 75
  std::string_view{"\002\205"},                                         // 76
  std::string_view{"\002\003"},                                         // 77
  std::string_view{"\002\215"},                                         // 78
  std::string_view{"\215\003"},                                         // 79
  std::string_view{"fabulous "},                                        // 80
  std::string_view{"exotic "},                                          // 81
  std::string_view{"hoopy "},                                           // 82
  std::string_view{"unusual "},                                         // 83
  std::string_view{"exciting "},                                        // 84
  std::string_view{"cuisine "},                                         // 85
  std::string_view{"night life "},                                      // 86
  std::string_view{"casinos "},                                         // 87
  std::string_view{"sit coms "},                                        // 88
  std::string_view{"\237\006"},                                         // 89
  std::string_view{"\001"},                                             // 90
  std::string_view{"The planet \001"},                                  // 91
  std::string_view{"The world \001"},                                   // 92
  std::string_view{"This planet "},                                     // 93
  std::string_view{"This world "},                                      // 94
  std::string_view{"\201\200for \212"},                                 // 95
  std::string_view{"\201\200for \212and \212"},                         // 96
  std::string_view{"\213by \214"},                                      // 97
  std::string_view{"\201\200for \212but \213by \214"},                  // 98
  std::string_view{"a \224\225"},                                       // 99
  std::string_view{"\004n unremarkable "},                              // 100
  std::string_view{"boring "},                                          // 101
  std::string_view{"dull "},                                            // 102
  std::string_view{"tedious "},                                         // 103
  std::string_view{"revolting "},                                       // 104
  std::string_view{"planet "},                                          // 105
  std::string_view{"world "},                                           // 106
  std::string_view{"place "},                                           // 107
  std::string_view{"little planet "},                                   // 108
  std::string_view{"dump "},                                            // 109
  std::string_view{"frequent "},                                        // 110
  std::string_view{"occasional "},                                      // 111
  std::string_view{"unpredictable "},                                   // 112
  std::string_view{"dreadful "},                                        // 113
  std::string_view{"deadly "},                                          // 114
  std::string_view{"funny "},                                           // 115
  std::string_view{"wierd "},                                           // 116
  std::string_view{"unusual "},                                         // 117
  std::string_view{"strange "},                                         // 118
  std::string_view{"peculiar "},                                        // 119
  std::string_view{"son of a bitch "},                                  // 120
  std::string_view{"scoundrel "},                                       // 121
  std::string_view{"blackguard "},                                      // 122
  std::string_view{"rogue "},                                           // 123
  std::string_view{"whoreson beetle headed flap ear'd knave "},         // 124
  std::string_view{"I hear a \227looking ship appeared at Errius"},     // 125
  std::string_view{"Yeah, I hear a \227ship left Errius a while back"}, // 126
  std::string_view{"Get your iron ass over to Errius"},                 // 127
  std::string_view{"Some \230new ship was seen at Errius"},             // 128
  std::string_view{"Try Errius"},                                       // 129
  std::string_view{"parking meters "},                                  // 130
  std::string_view{"dust clouds "},                                     // 131
  std::string_view{"ice bergs "},                                       // 132
  std::string_view{"rock formations "},                                 // 133
  std::string_view{"volcanoes "},                                       // 134
  std::string_view{"plant "},                                           // 135
  std::string_view{"tulip "},                                           // 136
  std::string_view{"banana "},                                          // 137
  std::string_view{"corn "},                                            // 138
  std::string_view{"weed "},                                            // 139
  std::string_view{"\003"},                                             // 140
  std::string_view{"\002\003"},                                         // 141
  std::string_view{"\002\215"},                                         // 142
  std::string_view{"inhabitant "},                                      // 143
  std::string_view{"\002\003"},                                         // 144
  std::string_view{"shrew "},                                           // 145
  std::string_view{"beast "},                                           // 146
  std::string_view{"bison "},                                           // 147
  std::string_view{"snake "},                                           // 148
  std::string_view{"wolf "},                                            // 149
  std::string_view{"leopard "},                                         // 150
  std::string_view{"cat "},                                             // 151
  std::string_view{"monkey "},                                          // 152
  std::string_view{"goat "},                                            // 153
  std::string_view{"fish "},                                            // 154
  std::string_view{"\005\217\216"},                                     // 155
  std::string_view{"\002\005\235\240"},                                 // 156
  std::string_view{"its \005\220\236\240"},                             // 157
  std::string_view{"\241\242"},                                         // 158
  std::string_view{"\005\217\216"},                                     // 159
  std::string_view{"meat "},                                            // 160
  std::string_view{"cutlet "},                                          // 161
  std::string_view{"steak "},                                           // 162
  std::string_view{"burgers "},                                         // 163
  std::string_view{"soup "},                                            // 164
  std::string_view{"ice "},                                             // 165
  std::string_view{"mud "},                                             // 166
  std::string_view{"zero-G "},                                          // 167
  std::string_view{"vacuum "},                                          // 168
  std::string_view{"\002ultra "},                                       // 169
  std::string_view{"hockey "},                                          // 170
  std::string_view{"cricket "},                                         // 171
  std::string_view{"karate "},                                          // 172
  std::string_view{"polo "},                                            // 173
  std::string_view{"tennis "},                                          // 174
  std::string_view{"wasp "},                                            // 175
  std::string_view{"moth "},                                            // 176
  std::string_view{"grub "},                                            // 177
  std::string_view{"ant "},                                             // 178
  std::string_view{"\003"},                                             // 179
  std::string_view{"poet "},                                            // 180
  std::string_view{"arts graduate "},                                   // 181
  std::string_view{"yak "},                                             // 182
  std::string_view{"snail "},                                           // 183
  std::string_view{"slug "},                                            // 184
  std::string_view{"dense "},                                           // 185
  std::string_view{"lush "},                                            // 186
  std::string_view{"rain "},                                            // 187
  std::string_view{"bamboo "},                                          // 188
  std::string_view{"deciduous "},                                       // 189
  std::string_view{"green "},                                           // 190
  std::string_view{"black "},                                           // 191
  std::string_view{"yellow stripey "},                                  // 192
  std::string_view{"pinky grey "},                                      // 193
  std::string_view{"white "},                                           // 194
};

/// descriptionPhraseLists: for each phrase code from 80h, the five phrases it picks from, as
/// indexes into DESCRIPTION_PHRASES.
inline constexpr std::array<std::array<std::uint8_t, 5>, 39> DESCRIPTION_PHRASE_LISTS = {{
  {0, 1, 2, 3, 4},           // 80h
  {5, 6, 7, 8, 9},           // 81h
  {10, 11, 12, 13, 14},      // 82h
  {15, 16, 17, 18, 19},      // 83h
  {20, 21, 22, 23, 24},      // 84h
  {25, 26, 27, 28, 29},      // 85h
  {30, 31, 32, 33, 34},      // 86h
  {35, 36, 37, 38, 39},      // 87h
  {40, 41, 42, 43, 44},      // 88h
  {45, 46, 47, 48, 49},      // 89h
  {50, 51, 52, 53, 54},      // 8Ah
  {55, 56, 57, 58, 59},      // 8Bh
  {60, 61, 62, 63, 64},      // 8Ch
  {65, 66, 67, 68, 69},      // 8Dh
  {70, 71, 72, 73, 74},      // 8Eh
  {75, 76, 77, 78, 79},      // 8Fh
  {80, 81, 82, 83, 84},      // 90h
  {85, 86, 87, 88, 89},      // 91h
  {90, 91, 92, 93, 94},      // 92h
  {95, 96, 97, 98, 99},      // 93h
  {100, 101, 102, 103, 104}, // 94h
  {105, 106, 107, 108, 109}, // 95h
  {110, 111, 112, 113, 114}, // 96h
  {115, 116, 117, 118, 119}, // 97h
  {120, 121, 122, 123, 124}, // 98h
  {125, 126, 127, 128, 129}, // 99h
  {130, 131, 132, 133, 134}, // 9Ah
  {135, 136, 137, 138, 139}, // 9Bh
  {140, 141, 142, 143, 144}, // 9Ch
  {145, 146, 147, 148, 149}, // 9Dh
  {150, 151, 152, 153, 154}, // 9Eh
  {155, 156, 157, 158, 159}, // 9Fh
  {160, 161, 162, 163, 164}, // A0h
  {165, 166, 167, 168, 169}, // A1h
  {170, 171, 172, 173, 174}, // A2h
  {175, 176, 177, 178, 179}, // A3h
  {180, 181, 182, 183, 184}, // A4h
  {185, 186, 187, 188, 189}, // A5h
  {190, 191, 192, 193, 194}, // A6h
}};

/// galaxySeeds: 6-byte seed; system 0 of each galaxy; the first is 5A4A 0248 B753
constexpr std::array<std::array<std::uint8_t, 6>, 8> GALAXY_SEEDS = {{
  {0x4A, 0x5A, 0x48, 0x02, 0x53, 0xB7}, // [0]
  {0x94, 0xB4, 0x90, 0x04, 0xA6, 0x6F}, // [1]
  {0x29, 0x69, 0x21, 0x08, 0x4D, 0xDE}, // [2]
  {0x52, 0xD2, 0x42, 0x10, 0x9A, 0xBD}, // [3]
  {0xA4, 0xA5, 0x84, 0x20, 0x35, 0x7B}, // [4]
  {0x49, 0x4B, 0x09, 0x40, 0x6A, 0xF6}, // [5]
  {0x92, 0x96, 0x12, 0x80, 0xD4, 0xED}, // [6]
  {0x25, 0x2D, 0x24, 0x01, 0xA9, 0xDB}, // [7]
}};

/// blankChartLine: 27 spaces and a NUL; ClearChartTextLines prints it twice
constexpr std::string_view BLANK_CHART_LINE = "                           ";

/// nameLinePadding: 12 spaces printed after a system name
constexpr std::string_view NAME_LINE_PADDING = "            ";

/// findPrompt: 'FIND: Which System ?'
constexpr std::string_view FIND_PROMPT = "FIND: Which System ?";

/// systemNameDigrams: 2 characters; entry 0 is two spaces, which add nothing
constexpr std::array<std::array<std::uint8_t, 2>, 32> SYSTEM_NAME_DIGRAMS = {{
  {0x20, 0x20}, // [0]
  {0x4C, 0x45}, // [1]
  {0x58, 0x45}, // [2]
  {0x47, 0x45}, // [3]
  {0x5A, 0x41}, // [4]
  {0x43, 0x45}, // [5]
  {0x42, 0x49}, // [6]
  {0x53, 0x4F}, // [7]
  {0x55, 0x53}, // [8]
  {0x45, 0x53}, // [9]
  {0x41, 0x52}, // [10]
  {0x4D, 0x41}, // [11]
  {0x49, 0x4E}, // [12]
  {0x44, 0x49}, // [13]
  {0x52, 0x45}, // [14]
  {0x41, 0x20}, // [15]
  {0x45, 0x52}, // [16]
  {0x41, 0x54}, // [17]
  {0x45, 0x4E}, // [18]
  {0x42, 0x45}, // [19]
  {0x52, 0x41}, // [20]
  {0x4C, 0x41}, // [21]
  {0x56, 0x45}, // [22]
  {0x54, 0x49}, // [23]
  {0x45, 0x44}, // [24]
  {0x4F, 0x52}, // [25]
  {0x51, 0x55}, // [26]
  {0x41, 0x4E}, // [27]
  {0x54, 0x45}, // [28]
  {0x49, 0x53}, // [29]
  {0x52, 0x49}, // [30]
  {0x4F, 0x4E}, // [31]
}};

/// shortRangeChartTitle: 'SHORT RANGE CHART'
constexpr std::string_view SHORT_RANGE_CHART_TITLE = "SHORT RANGE CHART";

/// galacticChartTitle: 'GALACTIC CHART ' then galacticChartNumber
constexpr std::string_view GALACTIC_CHART_TITLE{"GALACTIC CHART ", 15};

/// galacticChartTitleEnd: the space and the NUL that end the galactic chart's title after galacticChartNumber (change 3c
/// names it)
constexpr std::string_view GALACTIC_CHART_TITLE_END = " ";

/// findErrorPrefix: 'ERROR: '
constexpr std::string_view FIND_ERROR_PREFIX = "ERROR: ";

/// findErrorSuffix: ' not on map!'
constexpr std::string_view FIND_ERROR_SUFFIX = " not on map!";

/// systemDataFrame: frame bytes, then "DATA ON "
constexpr std::string_view SYSTEM_DATA_FRAME = "\033\037DATA ON ";

/// radiusLabel: 'Radius (Av): ', which runs on into radiusDigits (change 3c names it)
constexpr std::string_view RADIUS_LABEL{"Radius (Av): ", 13};

/// descriptionTemplate: code 6, phrase 0x92, "is ", phrase 0x93, code 4, "."
constexpr std::string_view DESCRIPTION_TEMPLATE = "\006\222is \223\004.";

/// adjectiveSuffix: ian
constexpr std::array<std::uint8_t, 5> ADJECTIVE_SUFFIX = {
  0x69, 0x61, 0x6E, 0x20, 0x00, // [0]
};

} // namespace

} // namespace Elite
