# StarcraftCombatSim

OpenBW-backed StarCraft combat scenarios with an API shaped like a small combat `GameState`.

The public scenario model is a map, a scripted behavior, and explicitly placed combat units. OpenBW loads the SCX/SCM map for terrain, regions, visibility, collision, iscript, and bullets, while the scenario loader suppresses starting economy units and creates only the units listed in the scenario file.

The OpenBW combat engine headers used by this project are vendored under `src/openbw`; the build does not depend on a sibling `openbw` checkout.

## Build

```powershell
cd StarcraftCombatSim
.\generate_vs.bat
cmake --build build --config Release --target StarcraftCombatSimVisualizer --parallel 8
```

`SFML_DIR` defaults to `C:\dev\libraries\SFML-3.0.1`, matching the local SparCraft convention.

There is also a hand-authored Visual Studio solution at `vs\StarcraftCombatSim.sln`, matching the SparCraft `vs` layout. It contains `StarcraftCombatSimCore` as a static library and `StarcraftCombatSimVisualizer` as the runnable SFML GUI project.

## StarCraft data

This repository does not include the StarCraft data files. To run simulations, copy the three MPQ files from your StarCraft installation into:

```text
bin/starcraft_data/mpq
```

The expected MPQ files are `StarCraft.mpq`, `STARDAT.MPQ`, and `BROODAT.MPQ`. Put SCX/SCM map files in:

```text
bin/starcraft_data/maps
```

## Run

```powershell
.\bin\StarcraftCombatSim.exe
```

In windowed mode, the left-side ImGui panel scans `bin/scenarios` and lets you click a scenario to restart the simulation.

Optional arguments:

```text
StarcraftCombatSim.exe <scenario-file> <starcraft-data-dir> <map-file>
```

The default scenarios live in `bin/scenarios`; the default data directory is `bin/starcraft_data`; MPQs are loaded from `bin/starcraft_data/mpq`; and the default map is `bin/starcraft_data/maps/(4)Python.scx`. A scenario can override the map with `map <file>` and choose behavior with `script AttackClosest`, `script KiteClosest`, or `script None`. A one-token script line applies to every player; `script <player> <name>` overrides just one player, for example `script 0 KiteClosest` and `script 1 AttackClosest`.
