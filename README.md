# Teeworlds InfClassR

Infection Mod with a class system for TeeWorlds, originally developed by
[necropotame](https://github.com/necropotame/teeworlds-infclass).

This version is based on [breton's fork](https://github.com/bretonium/my-infclass-server)
but also integrates latter [Yavl's fork](https://github.com/yavl/teeworlds-infclassR)
features such as Hero Turret. See the [ChangeLog](https://github.com/infclass/teeworlds-infclassR/blob/production/CHANGELOG.md#infclassr-v120---2021-03-10)
for details.

# Building

It is recommended to use [infclass-scripts](https://github.com/infclass/infclass-scripts) to build the server for GNU/Linux.

## Dependencies

### Mandatory
- CMake-3.15+
- Python3 (needed at build time to generate some code)
- ICU (needed to support server-side translations)
- libpng (needed for the classes menu)
- libcurl (needed to register the server in DDNet)
- sqlite (required by pieces of DDNet engine but not actually used)
- zlib (needed to read the map files)

You also need a build toolchain, such as GCC and Ninja, or MSVC. The compiler must support C++20.

### Optional
- [GeoLite2++](https://www.ccoderun.ca/GeoLite2++/api/) is used for IP geolocation
- OpenSSL (can be used instead of bundled crypto)
- Google Test (needed for internal tests)

## Configuration options

- `USE_CONAN` - only ever tried for Win64 builds
- `GEOLOCATION` - enables IP-based geolocation which is useful to suggest a language on a client connected

## Building

### Building on Linux

```
git clone https://github.com/infclass/teeworlds-infclassR sources
INSTALL_DIR=$(pwd)/install
mkdir build
cd build
cmake ../sources \
    -Wno-dev \
    -DCMAKE_INSTALL_PREFIX=$INSTALL_DIR \
    -GNinja

cmake --build . --target install
```

**The `install` target is necessary.**

# Server configuration

You don't need to patch the server to add your contacts. The `/about` chat
command reply is generated using the follow config variables:
- `about_source_url` - The server source code URL. Update if you're using a fork.
- `about_translation_url` - The translation site URL. Change if you also supply your own translation.
- `about_contacts_discord` - Discord server invite URL. Links to 'Official Infclass' Discord server by default.
- `about_contacts_telegram` - Telegram URL or ID. Empty by default.
- `about_contacts_matrix` - Matrix room URL. Links to some Infclass matrix server by default.

## Notes

This product includes GeoLite2 data created by MaxMind, available from
[https://maxmind.com](https://www.maxmind.com).
