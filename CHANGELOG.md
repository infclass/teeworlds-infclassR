# Changelog

## InfclassR v1.5.1 - 2024-07-11

General:
- Implemented tracking of taxi passengers kills caused by the driver hooked to death tiles
- Soldier Bomb explosion now prevented for 140ms after the bomb placed (workaround for 'double click' issue)

Balance:
- Slime poisoning damage reduces from 5 to 3
- Slime poisoning interval increased from 1.0 to 1.5 sec

Maps:
- Updated `infc_warehouse`: the hidden path is now visible

Maintenance:
- Added logging of vote start/stop
- Fixed server messages logging
- Fixed server registration for OpenSSL-enabled builds

## InfclassR v1.5.0 - 2024-03-22

General:
- Added 'timeout code' support
- Implemented DDNet masterserver registration
- Enabled DDRace HUD
- Ghost spawn events now not visible for humans
- Ghost visibility for spectators now depends on `sv_strict_spectate_mode` config option
- "Forced spectator" mode reworked for compatibility with DDNet client
- Added `/me` chat command
- Enabled freeze state for DDNet-17.3+
- Dynamically toggle the turrets enablement during the round
- Added `/prefer_class <classname>` chat command
- Weapon change now allowed during ammo reload
- Added broadcast message about coming infection
- Just-killed human's camera now follows the killer
- Balancing infection now delayed
- Turret destruction score reduced from 3 to 1 sp
- Biologist laser now does NoAmmo sound on fire failed
- Biologist mine is now kept on a new placement failed
- Scientist died from teleportation now gives score for the involved infected
- Implemented entities animation
- Added health and armor pickups support
- Added damage zone type (deals a certain damage)
- Added a broadcast warning about kick for inactivity
- Taxi passengers now collide with solid ground
- Sniper position re-lock now enabled if 1+ seconds remaining
- Spider hook limit indicator now visible only for the team
- Spider hook changed from web to pulling one after grab
- Added slime (visual) effect on poison damage taken
- Introduced 'smart' map rotation
- Names are now checked for confusing with existing ones
- "By-hammer senders" are now considered as killer helpers
- Healers are now considered as killer helpers
- Added training mode with `/save_position`, `/load_position`, and `/set_class` commands
- Added `/sp`, `/lp` aliases for the save/load position commands
- Hero flag indicator now properly animated for standing heroes
- Merc bomb self-damage now dealt on behalf of the player who triggered the explosion
- Player last infection time now saved in session (now it is expected that if you reconnect you won't be 'first infected' if it is not your turn yet)
- 'Last picked class' now also remembered across maps
- Previously picked/given human class won't be ever given randomly again
- Spectators now can see players personal pickups
- Added hint messages
- Medic now see Hero healing icons if grenade launcher is active
- Hammer hit events now suppressed if the character is 'in love'

Balance:
- Bat's hook lifestealing replaced with hammer lifestealing (+2 HP per hit)
- Medic now gets a grenade on enemy killed
- Scientists now get the superweapon after 15 kills
- Scientists now get 0.5 kill progression per assisting
- Scientist white hole spawning and final explosion now does not push the owner
- White hole is disabled for games with less than 8 players
- Biologist mine lasers number reduced from 15 to 12
- Biologist is now immune to *continuous* poisoning effect
- Biologist now can use the hammer to stop the poisoning effect on humans
- Biologist now see a Heart icon above poisoned humans
- Looper wall effect is now applied continuously
- Engineer and Looper walls now can't be built on spawns
- Ninja targets are now set individually
- `inf_merc_bombs` reduced from 16 to 15 (restored the value used before v1.4.0_alpha6)
- Slime now gives the healing effect for 2 seconds
- Slime poisoning effect now ignores the armor (and takes health points)
- Slime poisoning damage reduces from 5 to 4
- Slime poisoning interval increased from 1.0 to 1.25 sec
- Frozen witch now can not spawn the infected
- The game now tries to pick the witch among callers
- Boomer healing effect now depends on the distance
- New same-round infection now restores the previous infected class
- `inf_shock_wave_affect_humans` now disabled by default (frustrating)

Fixes:
- Fixed Hero flag indicator on the class picked
- Fixed 'undead is finally dead' on player left the game
- Fixed spawn protection
- Fixed a few bugs in votes
- Fixed sound on Biologist laser fired with ammo < 10
- Fixed Biologist shotgun could hit through walls
- Fixed (Biologist) bouncing bullets disappeared if shoot right into the wall
- Fixed langs list in /lang description
- Fixed translation for some Merc text strings
- Fixed missing help for messaging commands
- Fixed taxi behavior on character team changes
- Fixed spider hook for player id=0
- Fixed Tele layer processing (for maps with teleports)
- Fixed hook visibility for distant characters
- Fixed '(connecting)' player infection
- Fixed Sniper position immediately unlocked if jump was pressed before the lock
- Fixed Bat and Sniper help pages
- Fixed 'Fun Round' settings kept for other rounds (in some cases)
- Fixed blinding effect on the player revived (now it the effect 'll be canceled)
- Fixed inconsistent poisoning (sometimes taking 1 extra HP)
- Fixed missing HP restore sound in infection zone
- Fixed Ghost kept invisible on hit and some actions

Maps:
- Updated `infc_canyon` (tech update simplifying the quads geometry)
- Fixed `infc_half_provence` borders
- Added infection zones highlight to `infc_sewers`
- Added infc_headquarter_winter by Pointer

Maintenance:
- Added 'pingex' capability support from DDNet
- Added `kill_pl` command from DDNet
- Added `set_hook_protection`, `set_invincible` commands
- Added `sv_slash_me` config variable
- Added `inf_bio_mine_lasers` config variable
- Added `start_round ?s[round_type]` command
- `remove_vote` now also lookup the votes by the command

## InfclassR v1.4.2 - 2022-04-21

- Fixed network clipping for looper and engineer walls
- Fixed a re-connected player could still use humans
  spawn after infection started (regression)
- Fixed prediction for hook of locked Sniper
- Fixed slow-motion effect kept after revive
- A build for Windows is available as a tech preview (not tested)

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
