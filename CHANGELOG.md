# Changelog

## InfclassR v1.4.1 - 2022-03-23

- Fixed poison grenades 'no healing' effect
- Fixed a memory leak on map client map generation (AddEmbeddedImage())
- Fixed uninitialized memory access (Teleports layer in CCollision)
- Fixed a possible crash on map rotation (CInfClassInfected::GetDefaultEmote())
- Fixed high CPU load from thread pool processing (CEngine and CJob backported from DDNet)

## InfclassR v1.4.0 - 2022-03-13

General:
- New blinding laser is given to Ninja
- Merc bombs are now fully deterministic
- Merc grenades now disable healing for the infected
- Medic grenades now explode instantly on fired
- Ghoul flying points now depend on the death type
- Early infected now fall in love
- Early infected now reliably killed on round start
- 'Love effect' now used to animate lovers attacks
- The 'lovers' can't hook humans anymore
- The 'lovers' now apply less force on humans
- Slug in love now can't place a slime
- HAPPY is now the default emote for lovers
- Poison effect now can be replenished
- Scientist tele now respects the ammo amount
- Freezed characters now can cry
- Added self-kill protection for humans
- Humans inactivity time limit increased to 180 seconds
- Class help pages now have shortcuts (e.g. '/help sci' and '/help merc')
- Added a sound on character healed
- Added support for teleports (map entities)
- Added support for ENTITIES_OFF DDNet tiles (map entities)
- Added a taxi mode with disabled ammo regen
- Invisible ghosts now ignore infected zone emotions
- DEATH_TILE as a kill reason mapped to WEAPON_NINJA
- Taxi passengers can't hook anymore
- 'Lock position' now disabled for taxi-passenger Sniper
- Human class now kept on reconnect or joining spec
- Added Portuguese (Brazil) translation (thanks fahh)
- Updated German translation (thanks Emrldy)
- Fixed Engineer Wall interactions with undead
- Fixed a rare crash on a disconnect between rounds
- Fixed compatibility with teeworlds-0.6.x
- Fixed merc grenades poison effect duration
- Fixed ninja katana collisions on higher speeds
- Fixed round started up during sv_warmup
- Fixed slug slime effect applied too often
- Fixed spider web hook (not) catching humans
- Fixed voodoo death with active spawn protection
- Fixed zones tesselation
- Fixed laser clipping

Server-side features for Infclass Client:
- Implemented damage type
- Implemented kill assistance
- Implemented infclass object info
- Implemented Kills/Deaths/Assists statistic
- Added FORCED_TO_SPECTATE camera mode
- Boomer camera now follows BFed target

Maps:
- Added infc_half_provence
- Fixed some small graphics issues on some maps
- infc_hardcorepit timelimit reduced from 2 minutes to 90 seconds

Maintenance:
- Added 'sv_timelimit_in_seconds' conf variable
- Added 'sv_info_change_delay' (the same as in DDNet)
- Added 'inf_inactive_humans_kick_time'
- Added 'inf_inactive_infected_kick_time'
- Added 'inf_taxi' (0 = disabled, 1 = enabled (without passengers ammo regen), 2 = enabled)
- Added 'inf_initial_infection_delay'
- Added 'inf_merc_bomb_max_damage'
- Added 'inf_slime_poison_damage' (replaces 'inf_slime_poison_duration')
- Added 'inf_poison_duration'
- Added 'inf_converter_id'
- Added 'inf_converter_force_regeneration'
- Added 'inf_event' (similar to 'events' in DDNet)
- Added 'inf_infzone_freeze_duration'
- Added 'inf_blindness_duration'
- Added 'inf_revival_damage'

## InfclassR v1.3.1 - 2021-12-09

General:
- Improved Taxi responsiveness
- Merc laser now collides only with the owner bomb
- Returned the notorious 'whoosh' sound effect for "vs witches" fun rounds
- Slug slime effect reworked to appear approximately 4x less times (should improve the performance).
- Added missing NetworkClipping to a number of 'traps' (biologist mine, looper wall, scientist mine, turret, white hole)

Maintenance:
- Fixed a crash on a new round if a Voodoo finished the previous round on a death tile
- CMake: Add missing CONF_DEBUG processing

## InfclassR v1.3.0 - 2021-11-27

General:
- Added initial support for the Entities View (Infclass game tiles converted to DDNet tiles)
- Added a broadcast message to Class Menu on a disabled class hovered (suggested by ipoopi)
- Added a message on an infected hooked human to inf zone
- Added a message on a human (self) infected by the zone
- Added a message for the infected player ("You have been infected by...")
- Added /lang shortcut for /language chat command
- Added freeze indicator
- Updated help pages (big thanks to ipoopi)
- Updated translation
- Updated conditions for joining specs
- Updated `/changelog` implementation
- `/help` now shows `/help game` page
- Added a reference to `/help` to the welcome message
- Improved indirect killer detection (added some causality)
- Implemented DDNet-specific /w support
- Implemented a chat filter for !<msg> messages (!me, !best, etc...)
- Re-enabled infected hammer force effect on Soldier (noticeable on Bat damage)
- Spider feet are now colored in dark red on web hook length limit reached
- Base HP increase on a ninja killed a target replaced with overall HP increase (now ninja can get an armor)
- Undeads and Voodoos now reduce Engineer Wall lifespan
- Undead now can not be healed/unfreezed on an Engineer Wall
- Ghoul leveling rebalanced (effectively capped at the value of previous 50%)
- Removed Ghouls bonus to hook damage
- Removed (random) stun grenades from Soldier and Looper arsenal
- Removed 'witch portal' game feature
- Slug slime now heals the slugs
- Slug slime now heals up to 12 of total HP
- Bat class excluded from the classes available for the first infected
- Ninjas are now invincible for hammers during the split second of katana attack
- Infection 'by the game' (e.g. the initial infection and the 'unfair' infection) now kills the characters
- Infection spawn protection (1 second) now actually given on infection spawned (on a spawn point)
- Activity check is now disabled if the player is alone
- The indirect killer lookup now applied to self killers
- Hero gift now includes ammo for the Gun
- Witch death speciality turned off for fun rounds
- Stunned characters now express EMOTE_BLINK
- Fixed sniper position unlock on a jump
- Fixed UTF-8 ban reasons (from DDNet)
- Fixed InfClass zones sensitivity (only the right top point of the Tee was checked previously)
- Fixed the player skin sometimes showed as default if the character is not in the game world
- Fixed Scientist ammo wasting on not allowed teleportation attempts
- Fixed Scientist kills during a white hole effect allowed to place another white hole
- Fixed Scientist broadcast message for the case with active white hole and mines
- Fixed the lonely infection HP bonus (sometimes given by a mistake)
- Fixed ninja (freezer) reward if the player ID is 0
- Fixed joining specs with 3 first infected
- Fixed missing hook protection until the player class is set
- Fixed ServerInfo compatibility with DDNet 15.5+
- Fixed spawn delay on Voodoo selfkill
- Fixed Ninja target update on the target revival
- Fixed Mercenary (laser rifle) reward on kills
- Fixed '/alwaysrandom 1' armor bonus (big thanks to breton)
- Fixed died passenger still spawns on the taxi driver

Maps:
- infc_headquarter: The graphics cleaned up (no gamelayer changes)
- infc_k9f_small: Removed an invisible hookable tile on the bottom left
- infc_floatingislands: Updated to a remapped version (by FluffyTee), fixed flags position
- infc_warehouse2: Fixed the bottom 'death' tiles

Maintenance:
- Added `sci` shortcut for the Scientist class
- Added `inf_last_enforcer_time_ms` variable to adjust the causality
- Added `sv_changelog_file`
- Added `sv_filter_chat_commands` to toggle the filter of !me and similar messages
- Added various `about` config variables so an admin don't need to hack the server to change contacts:
  - `about_source_url`
  - `about_translation_url`
  - `about_contacts_discord`
  - `about_contacts_telegram`
  - `about_contacts_matrix`
- Deprecated variables:
- `inf_stun_grenade_minimal_kills` (does nothing, remove it)
- `inf_stun_grenade_probability` (does nothing, remove it)
- `queue_map` is verbose now
- In case of internal failure the server process now will return the actual error code (port from DDNet)
- Now the server will refuse to generate maps if the needed skins are missing
- Data files moved from `bin/data` to the correct `data/` dir
- Removed `bam` build support. CMake is the only option now.
- CMake: Implemented installation target
- CMake: Minimum version fixed to 3.7 (needed for FindICU module)
- CMake: Removed unused requirements
- CMake: `storage.cfg` now installed as `storage.cfg.example` to simplify data customization
- CMake: MaxMindDB is a soft dependency now

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
