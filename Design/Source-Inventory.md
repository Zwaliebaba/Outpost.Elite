# Source Inventory — where every original routine goes

**Status:** Proposed · 2026-09-02. This is the coverage ledger: every library file the 13
masters include is in exactly one row, and `tools/inventory.py` (slice 0c) fails the build
when a file is missing from it or when a row's *Disposition* is still *Pending* after its
slice has closed.

**How to read it.** *Labels* are the original 6502 labels (the include file names, which
Moxon derives from the label). *Home* is the C++ file in `GameLogic/` (or elsewhere when
stated). *Disposition* is one of:

- **Port** — the routine becomes C++ with the same behaviour, oracle-tested where pure.
- **Data** — bytes extracted by `tools/extract_tables.py` into a generated `.cpp`.
- **Replace** — the original is hardware- or OS-specific; the port has something that produces
  the same *effect* by other means, named in the row.
- **Drop** — no counterpart in the port; the reason is stated.

Counts are per group from the masters' `INCLUDE "library/..."` lists (source 626, data 57,
loader 22, sprites 5 — 710 includes, all distinct; verified mechanically 2026-09-02).

---

## 1. `elite-source.asm` — the game (626 library includes, ELTA–ELTK)

### 1.1 Workspaces, configuration, boot

| Labels | Files | Home | Disposition | Notes |
|---|---|---|---|---|
| `zp`, `xx3`, `k_per_cent` (`K%`), `up`, `wp`, `option_variables`, `tgint` | 7 | `ZeroPage.h`, `Workspace.h` | Port | Structs with the original field names. `K%` is the ship-slot arena (`NOSH` × `NI%` bytes). |
| `s_per_cent` (`S%`), `g_per_cent` (`G%`), `deeor`, `deeors`, `f_per_cent` (`F%`) | 5 | — | Drop | Boot vectors, code decryption (`KEY1`/`KEY2` scrambling), end-of-code marker. The port has no encrypted code. |
| `doentry`, `brkbk-cold`, `cold`, `startup`, `nmipissoff`, `putback`, `kernalsetup`, `backtonormal`, `swappzero`, `swappzero2`, `mvblockk` | 11 | `Game.cpp` (entry) | Replace | C64 memory-bank switching, NMI/BRK vectors, Kernal setup, zero-page swapping between the game and the Kernal. `Game::Reset` does the state part; the rest has no meaning on Windows. |
| `checksum`, `chk`, `chk2`, `chk3` | 4 | `SaveBlock.cpp` | Port | Commander block checksums — needed for save compatibility. `checksum` (code integrity) is dropped. |
| `na_per_cent` (`NA%`), `na_per_cent-na2_per_cent`, `s1_per_cent`, `jameson`, `dfault-qu5` | 5 | `Commander.cpp`, `CommanderData.cpp` | Data + Port | The default commander and the last-saved copy. |

### 1.2 Main loops and top-level flow

| Labels | Files | Home | Disposition |
|---|---|---|---|
| `main_flight_loop_part_1_of_16` … `part_16_of_16`, `spin` | 17 | `FlightLoop.cpp` | Port |
| `main_game_loop_part_1_of_6` … `part_6_of_6` | 6 | `GameLoop.cpp`, `Spawn.cpp` | Port |
| `br1_part_1_of_2`, `br1_part_2_of_2`, `bay`, `tt170`, `begin`, `reset`, `res2`, `zinf`, `title`, `check`, `check2`, `tt102`, `tt110`, `tt114`, `tt18`, `me2`, `ze` | 17 | `Game.cpp`, `TitleScreen.cpp`, `Bubble.cpp` | Port |
| `death`, `death2`, `spasto`, `bad`, `farof`, `farof2`, `mas4` | 7 | `Game.cpp`, `FlightLoop.cpp` | Port |
| `pause`, `pause2`, `pas1`, `mt23`, `mt29`, `delay`, `cldelay`, `ping`, `ginf` | 9 | `Game.cpp`, `Bubble.cpp` | Port (`delay`/`cldelay` become step counts, not busy loops) |
| `yetanotherrts`, `r_per_cent` (`R%`) | 2 | — | Drop (a shared `RTS`; a code-region marker) |

### 1.3 Text: recursive tokens, extended tokens, printing

| Labels | Files | Home | Disposition |
|---|---|---|---|
| ✅ `tt27`, `tt42`, `tt41`, `tt43`, `tt45`, `tt46`, `tt74`, `qw`, `ex` (with `tt47`, `tt48`–`tt51`, `tt59`) | 9 | `Tokens.cpp` | **Ported 2026-09-03** (slice 1c-a). 243 of 250 tokens compared character for character against the shipped printer in all five capitalisation states, through a trap on the character routine. The other 7 embed value tokens and are counted, not skipped silently. |
| `tt68`, `tt73`, `csh`, `plf`, `spc`, `tt162`, `tt160`, `tt161`, `tt16a`, `tt163` | 10 | `Tokens.cpp`, `Screens.cpp` | Port. Each prints a value or moves the cursor, so they land with the commander and canvas work they depend on. |
| ✅ `qq16` (the two-letter token pairs) | 1 | `TokenTables.cpp` | **Extracted 2026-09-03** (slice 1c-a), byte-compared against the oracle image. |
| `tkn2`, `jmtb` | 2 | `ExtendedTokens.cpp` | Data / Port — with slice 1c-b. |
| ✅ `detok`, `detok2`, `detok3` | 3 | `ExtendedTokens.cpp` | **Ported 2026-09-03** (slice 1c-b). 199 of 255 tokens and 21 system overrides compared character for character in three case states. |
| `mt1`, `mt2`, `mt5`, `mt6`, `mt8`, `mt9`, `mt13`, `mt14`, `mt15`, `mt16`, `mt17`, `mt18`, `mt19`, `mt26`, `mt27`, `mt28`, `vowel`, `whitetext`, `rline` | 19 | `ExtendedTokens.cpp` | Port — **scoped 2026-09-03 from `JMTB` in the binary, not built** (slice 1c-c-b). Eleven are two or three stores to `DTW1`–`DTW8` or `QQ17`, or a cursor move, and the machinery they need now exists. Nine do not: 9 and 11 need `TT66` and `NLIN4`, 16 is reached through an operand that 27 and 28 self-modify, and 17, 18 and the mission codes reach `DETOK` with state phase 2 owns. `mt26`/`rline` take their characters from `InputFrame`. |
| ✅ `mtin` | 1 | `ExtendedTokenTables.cpp` | **Extracted 2026-09-03**, byte-compared against the oracle image. |
| ✅ `bprnt`, `tt11`, `pr2`, `pr5`, `pr6`, `tens` | 6 | `TextPrint.cpp`, `ScreenTables.cpp` | **Ported 2026-09-03** (slice 1c-c-a). 308 numbers compared character for character through a trap on `DASC`, across every digit width and both settings of the carry that decides the decimal point; `TT11` swept over sixteen bits and `pr2` over every byte. |
| `dtw1`–`dtw6`, `dtw8`, `feed`, `dtw*` justification state | 8 | `TextPrint.cpp` | Port — with 1c-c-b. |
| ✅ `tt26`, `tt26-chpr`, `rr4s`, `setxc`, `setxc-doxc`, `setyc`, `setyc-doyc` | 7 | `TextPrint.h/.cpp` | **Ported 2026-09-03** (slice 1d-b). **5,376 printable characters** compared by whole-screen byte compare at seven columns and four rows in two cell colours, plus the cursor, the cell colour and the returned character; the 31 control codes at nine cursor positions; and the QQ17 = 255 suppression over every code. `r5` (the bell) and `clss` (clear and retry) are declared seams, counted rather than skipped. |
| `chpr2`, `tt67`, `tt67-tt67k`, `tt69`, `ttx69`, `tt70`, `tt60`, `tt146`, `tt11`, `bprnt`, `pr2`, `pr5`, `pr6`, `prq`, `tens`, `tnpr`, `tnpr1`, `incyc`, `clss`, `r5`, `wscan`, `newosrdch`, `setvdu19-dovdu19`, `docol`, `dohfx`, `dosvn` | 26 | `TextPrint.cpp`, `Canvas.cpp` | Port (`wscan` — wait for raster — is a no-op; `setvdu19` — palette change — becomes a canvas palette-slot write). The number printers land with 1c-c. |
| ✅ `tt20`, `tt54`, `tt24`, `cpl`, `tt81`, `tt111`, `Ghy`'s `G1` | 7 | `Universe.h/.cpp` | **Ported 2026-09-03** (slice 2a, the generator). **All 2,048 systems in all eight galaxies compared on every field** — economy, government, technology, population and productivity — and **all 2,048 names compared character for character** through a trap on the character routine. The seed twist swept over 4,007 states including the corners; the galaxy rotation over every value a byte can hold in every position. Galaxy 1 system 0 comes out TIBEDIED. `TT111` compared over 864 searches across all eight galaxies and a crosshair grid — the seeds it settles on, the index, both coordinates, the distance, and the `TT24` it jumps into rather than returning from. |
| `pdesc`, `cmn`, `ypl`, `tal`, `fwl`, `tt25`, `tt22`, `tt23`, `tt15`, `tt14`, `tt128`, `tt16`, `tt103`, `tt123`, `tt105`, `tt81`, `tt111`, `tt147`, `tt151`, `tt152`, `tt167`, `tt213`, `tt214`, `tt219`, `tt210`, `tt208`, `gnum`, `nwdav4`, `tt217`, `var`, `gvl`, `status`, `plf2`, `eqshp`, `dn`, `dn2`, `eq`, `prx`, `prxs`, `qv`, `hm`, `refund`, `lcash`, `mcash`, `gcash`, `gc2`, `rdli`, `tt17`, `yesno`, `trademode`, `qq23`, `item`, `ex`, `tt20`, `tt54`, `hyp`, `hyp1`, `hy6-docked`, `ww`, `ttx110`, `ghy`, `jmp`, `ee3`, `tt66`, `ttx66-ttx66k`, `ttx66k`, `tt66simp`, `box2`, `boxs`, `boxs2`, `blueband`, `bluebands`, `wantdials`, `zonkscanners`, `nosprites`, `setl1`, `l1m`, `clyns`, `zes1k`, `zes2k`, `zesnew`, `zes1`, `zes2`, `zero`, `zebc` | 86 | `Universe.cpp`, `SystemData.cpp`, `Charts.cpp`, `Market.cpp`, `Equipment.cpp`, `Screens.cpp`, `StatusScreen.cpp`, `MarketScreen.cpp`, `ChartScreens.cpp`, `Hyperspace.cpp`, `Canvas.cpp` | Port. `nosprites`/`wantdials`/`zonkscanners`/`blueband` (VIC-II sprite and raster-split control when switching between the space view and a text screen) become canvas-mode flags with the same visible effect. |

### 1.4 Arithmetic

| Labels | Files | Home | Disposition |
|---|---|---|---|
| `dornd` | 1 | `Rng.cpp` | Port |
| ✅ `multu`, `mu11`, `mu1`, `mult1`, `mult12`, `squa`, `squa2`, `mlu2`, `add` (with `mu8`, `mu9`) | 9 | `Arith.cpp` | **Ported 2026-09-03** (slice 1b-a). Every multiply verified exhaustively against the shipped routine over all 65,536 input pairs; the addition over a 200,000-case sweep plus the sign-magnitude edges. |
| ✅ `mad`, `mu5`, `mu6`, `mls1`'s `mults` body, `mltu2`, `tis1` (with `dvid96`), `tis2`, `dvidt` | 8 | `Arith.cpp` | **Ported 2026-09-03** (slice 1b-b). The multiply-accumulate, the scaled and wide multiplies, and the three divisions. Exhaustive where the input space is 16 bits, 150,000-case sweeps above that. |
| ✅ `ll5` | 1 | `Arith.cpp` | **Ported 2026-09-03** with slice 2a, which needs it for the distance calculation. Compared against the shipped routine over **all 65,536 radicands**, and against real integer arithmetic as well. It was grouped with the state-dependent helpers below and deferred to 3a; it is not state-dependent — it takes R and Q and leaves Q — which is the third time a ledger row has named a routine without its dependencies being checked. |
| `mult3`, `mls1`/`mls2`'s setup, `mlu1`, `mut1`, `mut2`, `mut3`, `tas3`, `tis3`, `dv41`, `dv42`, `cntr`, `bump2`, `redu2`, `norm` | 14 | `Arith.cpp`, `ShipMove.cpp` | Port. Each reads game state the port has not defined yet — ship slots (`INWK`), the rotation angles (`ALP1`), the stardust arrays or the damping flag — so they land with the workspace they belong to rather than with the kernel. |
| ✅ `fmltu`, `ll28`, `ll38`, `arctan` (with `ars1`) | 4 | `Arith.cpp` | **Ported 2026-09-03** (slice 1b-d). The multiply, the divide and the angle verified over all 65,536 input pairs each; the combine over a 200,000-case sweep. The divide's returned carry is compared too, since callers branch on it. |
| ✅ `fmltu2`, `dvid4` (with the unlabelled `LL28` copy it falls into) | 2 | `Arith.cpp` | **Ported 2026-09-03** (slice 1b-d, completed). Both compared against the shipped routines over all 65,536 input pairs. `DVID4` has **no `RTS`**: it runs on into a second, unlabelled copy of `LL28`'s body, and both of its callers get it, so the port is the whole path — an 8.8 fixed-point divide leaving the whole part in `P` and the fraction in `R`. |
| `dvid3b2`, `ll51` | 2 | `Arith.cpp`, `ShipMove.cpp` | Port — **deferred to 3a, 2026-09-03**, for the reason 1b-c was: `dvid3b2` reads `INWK+6..8` and `ll51` reads `XX15`/`XX16`, and neither workspace exists yet. `DVID3B`, the state-free entry `dvid3b2` falls into, goes with them. |
| ✅ `log`, `logl`, `antilog-alogh`, `antilogodd` | 4 | `LogTables.cpp` | **Extracted 2026-09-03** (slice 1a), byte-compared against the oracle image by `TableTests.cpp`. |
| ✅ `ylookupl`, `ylookuph`, `celllookl`, `celllookh`, `twos`, `dtwos`, `ctwos2`, `twos2`, `twfl`, `twfr` | 10 | `LookupTables.h`, `ScreenTables.cpp` | **Extracted 2026-09-03** (slice 1a, the screen half), byte-compared against the oracle image. Lengths come from what indexes each table, not from the next label (plan §6.8). `ylookup`/`celllook` are bitmap-address tables — the canvas computes those addresses directly, and a test proves the two agree for all 256 rows. |
| `ctwos`, `tens` | 2 | `LookupTables.h` | Data — deferred. `ctwos`'s only C64 consumer is `DIL2`, a dashboard routine (slice 3d), so its index range cannot be established from a phase-1 caller; `tens` belongs with `BPRNT` in 1c-c. |
| `mvt3`, `mvs5`, `mas1`, `mas2`, `mas3`, `mvt1`, `mvs4`, `mvt6`, `mv40`, `tidy`, `tas1`, `tas2`, `tas4`, `tas6`, `dcs1`, `sps1`–`sps4`, `sp1`, `sp2` | 21 | `ShipMove.cpp`, `Orientation.cpp`, `Dashboard.cpp` (compass parts) | Port |

### 1.5 Drawing primitives

| Labels | Files | Home | Disposition |
|---|---|---|---|
| ✅ `hloin`, `pixel`, `pixel2`, `pix1`, `cpix4`, `cpix2-cpixk` | 6 | `Canvas.h`, `Lines.cpp` | **Ported 2026-09-03** (slice 1d-a). Each compared against the shipped routine by **whole-screen byte compare** — all 0x2800 bytes after every call — so a routine that draws the right pixel and scribbles elsewhere fails. `PIXEL` over every x at eight distances; `PIXEL2` over all 65,536 coordinate pairs; `CPIX2`/`CPIX4` over every x in five colours; `HLOIN` over both ends across three cells. |
| ✅ `loin_part_1_of_7` … `part_7_of_7`, `lijt1`–`lijt8` | 15 | `Lines.cpp` | **Ported 2026-09-03** (slice 1d-a). **3,528 lines compared byte for byte** across both gradients, both directions on each axis, the swapped and unswapped entries and the degenerate spans. The shipped code unrolls two loops into thirty-two copies reached through self-modifying jumps patched from `lijt*`; the port is the two loops with the starting bit as a variable, and the screen pointer kept as two bytes because the carry between them feeds the next iteration's accumulator. |
| `hloin2`, `nlin`, `nlin2`, `nlin3`, `nlin4`, `dot` | 6 | `Lines.cpp`, `Canvas.cpp` | Port. `nlin*` draw a screen's title underline and go with the text screens; `dot` reads the compass workspace and goes with the dashboard in 3d. |
| `bline`, `circle`, `circle2`, `flip`, `stars`, `stars1`, `stars2`, `stars6`, `nwstars` | 9 | `Circles.cpp`, `Stardust.cpp` | Port — **`circle`/`circle2`/`bline` moved from 1d-c to 3c, 2026-09-03.** They were scoped into phase 1 as canvas work and are not: `circle` opens with `CHKON`, and `bline` builds the sun line heap (`LSX2`/`LSY2`/`LSP`) and clips through `LL145`. None of that exists before slice 3c, and forcing it would have meant porting the heap into a slice that has no use for it. |
| `lsx2`, `lsy2`, `shppt`, `ll9_part_1_of_12` … `part_12_of_12`, `ll61`, `ll62`, `ll145_part_1_of_4` … `part_4_of_4`, `ll118`, `ll120`, `ll123`, `ll129`, `proj`, `pl2`, `edges`, `chkon`, `plut-pu1` | 28 | `ShipDraw.cpp`, `LineHeap.h` | Port |
| `planet`, `pl9_part_1_of_3` … `part_3_of_3`, `pls1`, `pls2`, `pls22`, `pls3`–`pls6`, `wpls2`, `wp1`, `wpls`, `pl21`, `sun_part_1_of_4` … `part_4_of_4`, `solar`, `nwq`, `wpshps`, `flflls`, `sos1` | 24 | `Planet.cpp`, `Sun.cpp` | Port |
| `doexp`, `exlook`, `ptcls2`, `det1`, `shd`, `dengy` | 6 | `Explosion.cpp`, `FlightLoop.cpp` | Port |
| `univ`, `scacol`, `sightcol` | 3 | `Bubble.cpp`, `ShipBlueprintData.cpp`, `Dashboard.cpp` | Data / Port |

### 1.6 Dashboard, scanner, messages

| Labels | Files | Home | Disposition |
|---|---|---|---|
| `dials_part_1_of_4` … `part_4_of_4`, `pzw`, `dilx`, `dil2`, `compas`, `msbar`, `ecblb`, `ecblb2`, `spblb-dobulb`, `escape`, `hme2`, `scan`, `look1`, `sight`, `lasli` | 18 | `Dashboard.cpp`, `Scanner.cpp`, `Lasers.cpp` | Port. `scan` positions VIC-II sprites for the blips and the `sight` crosshair; the port draws the sprite pixels onto the canvas at the same coordinates (`spritp` data). |
| `mess`, `me1`, `mes9`, `ouch`, `ou2`, `ou3` | 6 | `Dashboard.cpp` | Port |
| `mvtribs`, `tribdir`, `tribdirh`, `spmask`, `tribta`, `tribma` | 6 | `Trumbles.cpp` | Port (sprite motion becomes canvas draws) |

### 1.7 Ships, motion, AI, combat

| Labels | Files | Home | Disposition |
|---|---|---|---|
| `mveit_part_1_of_9` … `part_9_of_9` | 9 | `ShipMove.cpp` | Port |
| `tactics_part_1_of_7` … `part_7_of_7`, `dockit`, `vcsu1`, `vcsub`, `hitch`, `frs1`, `frmis`, `sfrmis`, `angry`, `fr1`, `sescp`, `sfs1`, `sfs2`, `ecmof`, `oops`, `exno`, `exno2`, `exno3`, `bomboff` | 25 | `Tactics.cpp`, `Missiles.cpp`, `Ecm.cpp` | Port |
| `nwsps`, `nwshp`, `nws1`, `abort`, `abort2`, `killshp`, `ks1`–`ks4`, `there`, `msblob`, `nwstars` (see 1.5) | 12 | `Bubble.cpp`, `Spawn.cpp` | Port |
| `ll164`, `laun`, `hfs2`, `warp`, `mjp`, `gthg` | 6 | `Hyperspace.cpp`, `FlightLoop.cpp` | Port |
| `brief`, `brief2`, `brief3`, `brp`, `bris`, `debrief`, `debrief2`, `tbrief` | 8 | `Missions.cpp` | Port |

### 1.8 Input

| Labels | Files | Home | Disposition |
|---|---|---|---|
| `keylook`, `rdkey`, `kytb-ikns`, `ctrl`, `dks4-dks5`, `dksanykey`, `dks2`, `dks3`, `dkj1`, `u_per_cent` (`U%`), `dokey`, `dk4`, `flkb`, `ktran`, `trantable-trtb_per_cent`, `mutokch` | 16 | `KeyPoll.cpp`, `EliteKeys.h`, and `KeyMap.cpp` in `Outpost/` | Replace. The CIA keyboard-matrix scan and the C64 internal key numbers are replaced by `InputFrame` bits; `kytb` (the flight key table) defines the bit order so the original polling logic in `dokey`/`ctrl` ports unchanged. `dkj1` (joystick) maps to the same bits. |

### 1.9 Disk and Kernal

| Labels | Files | Home | Disposition |
|---|---|---|---|
| `sve`, `lod`, `thislong`, `oldlong`, `gtdrv`, `filesys`, `tapeerror`, `zektran`, `filepr`, `otherfilepr`, `trnme`, `tr1`, `gtnme-gtnmew`, `catf` | 13 | `SaveBlock.cpp` (block layout, name handling, checksums) + `SaveStore.cpp` in `Outpost/` (files) | Replace — Kernal `SETLFS`/`SETNAM`/`LOAD`/`SAVE` become file I/O in the executable; the byte layout is kept so original saves import. |

### 1.10 Sound and music

| Labels | Files | Home | Disposition |
|---|---|---|---|
| `beep`, `noise`, `noise2`, `noiseoff`, `hypnoise`, `soflush`, `sound_variables`, `sevens`, `sfxpr`, `sfxcnt`, `sfxfq`, `sfxcr`, `sfxatk`, `sfxsus`, `sfxfrch`, `sfxvch`, `rasct`, `zebop` | 18 | `SoundEffects.cpp`, `SoundTables.cpp` | Port / Data — emits `SoundEvent`s |
| `comirq1`, `soint`, `coffee` | 3 | `SoundEffects.cpp`, `Music.cpp` | Replace — the raster interrupt handler (it also flips the dashboard palette mid-frame: the *effect* is a canvas palette region, the SID half is the per-tick player) |
| `music_variables`, `bdirqhere`, `bdro1`–`bdro15`, `bdlab1`, `bdlab3`–`bdlab8`, `bdlab19`, `bdlab21`, `bdlab23`, `bdlab24`, `bdentry`, `bdjmptbl`, `bdjmptbh`, `comudat`, `startat`, `startbd`, `stopbd` | 31 | `Music.cpp` | Port — the player as a state machine emitting register writes |
| `abraxas`, `innersec`, `shango`, `moonflower`, `caravanserai`, `santana`, `lotus`, `welcome`, `soul3b` | 9 | `TuneData.cpp` | Data |

### 1.11 Miscellany

| Labels | Files | Home | Disposition |
|---|---|---|---|
| `bell` | 1 | `SoundEffects.cpp` | Port |

The per-row file counts above were made by hand from the include list and are approximate;
`tools/inventory.py` (slice 0c) is what reconciles them mechanically against the 627 includes
and replaces the hand count.

---

## 2. `elite-data.asm` — data (57 library includes + one `INCBIN`)

| Labels | Files | Home | Disposition |
|---|---|---|---|
| ✅ `qq23` | 1 | `MarketTable.cpp` | **Extracted 2026-09-03** (slice 2c) — four bytes each for seventeen goods, byte-compared against the oracle image. |
| `char`, `twok`, `cont`, `rtok` (macros) | 4 | `tools/extract_tables.py` | Replace — the macros encode tokens; the extractor reproduces the encoding (or reads the assembled `WORDS.bin`) |
| ✅ `qq18` | 1 | `TokenTables.cpp` | **Extracted 2026-09-03** (slice 1c-a) — the recursive token table, byte-compared against the oracle image. |
| ✅ `sne`, `act` | 2 | `SineTable.cpp`, `ArctanTable.cpp` | **Extracted 2026-09-03** (slice 1a), byte-compared against the oracle image. |
| `ejmp`, `echr`, `etok`, `etwo`, `ernd`, `tokn` (macros) | 6 | `tools/extract_tables.py` | Replace |
| `tkn1`, `rupla`, `rugal`, `rutok` | 4 | `TokenTables.cpp` | Data — extended tokens and the per-system description overrides |
| `xx21`, `e_per_cent` (`E%`), `kwl_per_cent`, `kwh_per_cent` | 4 | `ShipBlueprintData.cpp`, `LookupTables.h` | Data |
| `vertex`, `edge`, `face` (macros) | 3 | `tools/extract_tables.py` | Replace |
| `ship_missile`, `ship_coriolis`, `ship_escape_pod`, `ship_plate`, `ship_canister`, `ship_boulder`, `ship_asteroid`, `ship_splinter`, `ship_shuttle`, `ship_transporter`, `ship_cobra_mk_3`, `ship_python`, `ship_boa`, `ship_anaconda`, `ship_rock_hermit`, `ship_viper`, `ship_sidewinder`, `ship_mamba`, `ship_krait`, `ship_adder`, `ship_gecko`, `ship_cobra_mk_1`, `ship_worm`, `ship_cobra_mk_3_p`, `ship_asp_mk_2`, `ship_python_p`, `ship_fer_de_lance`, `ship_moray`, `ship_thargoid`, `ship_thargon`, `ship_constrictor`, `ship_cougar`, `ship_dodo` | 33 | `ShipBlueprintData.cpp` | Data — 33 blueprints (the `NTY = 33` ship types) |
| ✅ `C.FONT.bin` (`INCBIN`) | 1 | `Font.cpp` | **Extracted 2026-09-03** (slice 1a) — 768 bytes, 96 characters of eight rows, byte-compared against the oracle image. |

---

## 3. `elite-loader.asm` — loader (22 library includes)

| Labels | Files | Home | Disposition |
|---|---|---|---|
| `zp` (loader), `w_per_cent`, `lodata`, `ships`, `x_per_cent`, `frin`, `v_per_cent` | 7 | — | Drop — loader workspace and file tables |
| `elite_loader_part_1_of_7` … `part_7_of_7`, `deeors`, `mvblock`, `mvsm` | 10 | — | Drop — memory moves, decryption, VIC-II bank and mode setup |
| `sdump`, `cdump` | 2 | `Palette.cpp` (in `GameLogic`, since the game reads them) | Data — the screen-RAM and colour-RAM maps that give the dashboard its colours |
| `dials` | 1 | `DashboardImage.cpp` | Data — the dashboard bitmap (`DLOC%`) |
| `spritp` | 1 | `SpriteData.cpp` | Data — sprite bitmaps |
| `date` | 1 | `Game.cpp` | Data — the version string shown on the title screen |

Loader bytes marked "unused workspace noise" in the masters are dropped.

---

## 4. `elite-sprites.asm` (5 includes)

| Labels | Files | Home | Disposition |
|---|---|---|---|
| `sprite2`, `sprite2_byte`, `sprite4`, `sprite4_byte` (macros) | 4 | `tools/extract_tables.py` | Replace |
| `spritp` | 1 | `SpriteData.cpp` | Data (same file as §3) |

---

## 5. The other masters (no includes beyond build options)

`elite-gma1.asm`, `elite-gma2.asm`, `elite-gma3.asm`, `elite-firebird.asm`, `elite-send.asm`,
`elite-checksum.asm`, `elite-readme.asm`: **Drop**, all of them. They are the disk loader,
copy protection, autoboot, the development transfer tool, a reference copy of the build-time
checksum routine, and the on-disk README. None of it runs while the game is played.

`elite-build-options.asm`: the five options become `constexpr` in `EliteConfig.h`
(`VARIANT = Gma85Ntsc`, `MAX_COMMANDER = false`) so that a PAL variant or a maxed commander is a
build or config switch rather than a fork (ADR-001 §2).

---

## 6. Constants from the masters' preambles

Everything in `elite-source.asm` lines 79–486 that is not an `INCLUDE` — ship type numbers
(`MSL`, `SST`, `ESC` … `DOD`), `JL`/`JH` junk range, `PACK`, laser powers (`POW`, `Mlas`,
`Armlas`), `NI%`, `X`/`Y`, `conhieght`, key numbers, the multicolour pixel constants
(`RED`, `YELLOW`, `GREEN`, `WHITE`), the screen-RAM palette bytes (`RED2` … `BULBCOL`), the
sixteen sound numbers, `NRU%`, `RE`/`VE`, `LL`, and the memory-map addresses — go to
`EliteConfig.h` as `constexpr` values with the original names in comments. The memory-map
addresses (`SCBASE`, `DLOC%`, `ECELL` …) are kept for the oracle fixture only; the canvas
does not use them.
