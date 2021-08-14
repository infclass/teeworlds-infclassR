# Changelog

## InfclassR v1.3.0 - 2021-xx-xx (unreleased)

General:
- Added initial support for the Entities View (Infclass game tiles converted to DDNet tiles)
- Added a broadcast message to Class Menu on a disabled class hovered (suggested by ipoopi)
- Updated Hero help page (by ipoopi)
- Spider feet are now colored in dark red on web hook length limit reached
- Base HP increase on a ninja killed a target replaced with overall HP increase (now ninja can get an armor)
- Fixed sniper position unlock on a jump
- Fixed utf8 ban reasons (from DDNet)
- Fixed InfClass zones sensitivity (only the right top point of the Tee was checked previously)
- Fixed the player skin sometimes showed as default if the character is not in the game world
- Fixed Scientist ammo wasting on not allowed teleportation attempts

Maps:
- infc_headquarter: The graphics cleaned up (no gamelayer changes)
- infc_k9f_small: Removed an invisible hookable tile on the bottom left
- infc_floatingislands: Update to a remapped version (by FluffyTee)

Maintenance:
- Added `sci` shortcut for the Scientist class
- In case of internal failure the server process now will return the actual error code (port from DDNet)
- Now the server will refuse to generate maps if the needed skins are missing
- Data files moved from `bin/data` to the correct `data/` dir
- Removed `bam` build support. CMake is the only option now.
- CMake: Implemented installation target
- CMake: Minimum version fixed to 3.7 (needed for FindICU module)
- CMake: Removed unused requirements
- CMake: `storage.cfg` now installed as `storage.cfg.example` to simplify data customization

## InfclassR v1.2.1 - 2021-07-14

General:
- Medics grenade now makes the targets happy only if they're actually healed
- Fixed looper color in class menu
- Fixed compatibility with teeworlds-0.6.5 (from DDNet)

Maps:
- infc_skull: Added green background to the infection zone
- infc_warehouse: Updated the infection zones highlight
- infc_damascus: Slightly optimized for better client performance

Maintenance:
- Added `sv_suggest_more_rounds`
- Added `sv_vote_time` (from DDNet)
- Added `sv_vote_delay` (from DDNet)
- Added `inf_spider_web_hook_length`

- Fixed `git_revision.cpp` updating for out-of-tree builds (from DDNet)
- Fixed memory leak in console (from DDNet)
- Improved CMake build support
- CMake: Added support for build with `geolocation`
- Fixed some builds (a C++ template was not available at the point of instantiation; no idea how this worked before).
- A lot of internal refactoring moving the project toward DDNet-based server codebase

## InfclassR v1.2.0 - 2021-03-10

Changes since [yavl](https://github.com/yavl/teeworlds-infclassR) fork:

- Fixed looper grenades regeneration on hero found a flag (https://github.com/yavl/teeworlds-infclassR/issues/155)
- Count hooker as a killer if the weapon is World (https://github.com/bretonium/my-infclass-server/pull/2)
- Update hookers score on the hook target infected (by zone) (https://github.com/bretonium/my-infclass-server/pull/2)
- Hook to a death tile now counted as a kill (https://github.com/bretonium/my-infclass-server/pull/3)
- Fixed SnapID leaks (https://github.com/bretonium/my-infclass-server/pull/12)
- Ghoul souls now disappear on round ended (https://github.com/bretonium/my-infclass-server/pull/18)
- Bat color changed to green (https://github.com/bretonium/my-infclass-server/pull/19)
- Added a new (laser) weapon for the mercenary (https://github.com/bretonium/my-infclass-server/pull/46)
- Turrets do not try to fire through walls anymore
- Fixed hero could use a hammer as a usual weapon
- Fixed hero still having a hammer after the last turret placed

Changes since [breton](https://github.com/bretonium/my-infclass-server) fork:

- Applied the fix for scientist kills counter (https://github.com/yavl/teeworlds-infclassR/pull/151)
- Added maps: infc_headquarter, infc_floatingislands, infc_canyon, infc_k9f_small
- Imported Turrets for Hero class (https://github.com/yavl/teeworlds-infclassR/pull/79)

Changes since both forks:

- Send kill message on a player infected himself (fixes https://github.com/necropotame/teeworlds-infclass/issues/166)
- Restore both health and armor for a lonely zombie (in infection zone)
- Fixed zombies count calculation (affects different aspects)
- A spectator can not become a called witch anymore
- Witches excluded from the list of candidates on a witch call
- A slug now can refresh the placed slime (note: this doesn't affect the client performance)
- Fixed conditions for becoming a spectator
- Bat now is able to jump right on infection
- Tuned settings for many maps (timelimit, rounds per map, initial infected, min players, fun rounds...)
- Credits info moved to own /credits chat command
- Fixed "Kicked (is probably a dummy)" for builds with disabled SQL
- Added broadcast help message for a mercenary with laser
- Added broadcast help message for a medic with laser
- Default Scientist weapon changed to the rifle (to prevent instant death on 'fire' of a just revived scientist)
- Added a check if the class menu is clickable before processing (fixes rare issue with class selection)
- If a dead character was frozen, count the freezer as the killer
- Give score points to the killer when "undead is finally dead"
- Merc (poisoning) grenades explosion does not consume extra ammo anymore
- Fixed NOAMMO sound for merc and medic grenade launchers

Improvements for Fun Rounds:
- The player class is now set immediately (without a menu)
- Mercs now have no rifle (the weapon does nothing during fun rounds)
- The fun round now can be configured via console or specified on a per-map basis

Maps:
- Added infc_headquarter (by Armadillo)
- Added infc_floatingislands (v2 by FluffyTee)
- Added infc_canyon (by ipoopi)
- Added infc_k9f_small (by FluffyTee)
- Some minor changes to a few maps (adjusted limits, minplayers, bonus zone moved on `infc_spacelab`, infection zone colored red on some other maps)

Maintenance:
- Added a reset.cfg file called on map reload
- Allow a simpler syntax for start_special_fun_round
- Added `inf_first_infected_limit` variable to override the number of initially infected players (used for some maps)
- Added `inf_anti_fire_time` to configure antifire duration (suppress fire after character spawned and/or class selected)
- Added CMakeLists (bam is still recommended for building the production binary)
- Added support for a reset file to execute on map change or reload
- mapinfo files replaced by executable <map>.cfg
- Added commands:
  - `add_map` to add a map to the rotation list
  - `queue_map` to queue the next map
  - `clear_fun_rounds` to clear the fun rounds configuration
  - `add_fun_round` to add a possible fun round configuration to the list
  - `sv_shutdown_when_empty 1` to shut down the server when all players quit
  - `version` command to get the info about the server build
  - `dump_variables` to get all variables
  - `get` command to get a variable value
  - `adjust` command to increase/decrease an int var (e.g. `adjust sv_rounds_per_map +2`)
