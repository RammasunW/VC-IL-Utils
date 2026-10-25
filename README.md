# VC-IL-Utils

A utility ASI plugin for the PC version of **Grand Theft Auto: Vice City**, with support for both the **INTL 1.0** and **Japanese (JP)** executables.

VC-IL-Utils provides several gameplay and debugging utilities, including an interactive vehicle spawner, explosive barrel spawning, car-bomb controls, and vehicle lifetime/persistence controls.

The main implementation is a standalone Win32/x86 ASI and does **not** require plugin-sdk to build or run.

## Supported Versions

Currently tested:

- GTA Vice City INTL / English 1.0
- GTA Vice City Japanese version

The plugin identifies the executable at runtime and selects the appropriate engine addresses automatically.

Because the Japanese executable differs substantially from the INTL executable, JP support is implemented using independently verified per-version addresses rather than a fixed address offset.

## Features

Default controls:

| Key | Function |
| --- | --- |
| F4 | Spawn an explosive barrel |
| F5 | Arm the current vehicle with a car bomb |
| F6 | Remove the current vehicle's car bomb |
| F7 | Mark the current vehicle as a mission vehicle |
| F8 | Release the current vehicle as a random/traffic vehicle |
| F9 | Open the vehicle spawner |

All keys can be changed through `ILUtilsKeys.ini`.

## Vehicle Spawner

Press **F9 while on foot** to open the vehicle spawner.

The spawner uses a separate native Win32 input window rather than capturing arbitrary text directly through Vice City's keyboard processing.

This avoids instability encountered when trying to type arbitrary vehicle names through the game's own keyboard/cheat input path.

The native window only collects the requested vehicle name or ID. All actual Vice City engine operations are performed afterward on the game's main thread.

### Supported Input

Vehicles can be selected using:

- Numeric model IDs
- Internal IDE model names
- Friendly vehicle names
- Common aliases
- Unambiguous partial names
- Minor spelling mistakes

Examples:

```text
141
infernus
firetruck
pcj
police maverick
infrenus
```

The matching system first supports the game's original internal model names, but also includes human-readable vehicle names so users do not need to know internal IDE abbreviations such as `firetruk`.

The friendly vehicle-name mapping was inspired in part by **Vice City: Multiplayer (VC:MP)** and its `GetVehicleModelFromName()` / `GetVehicleNameFromModel()` implementation.

VC-IL-Utils extends that idea with its own:

- name normalization
- punctuation/spacing removal
- aliases
- prefix matching
- ambiguity detection
- small edit-distance matching
- adjacent-character transposition handling
- support for live IDE names, including custom installed models

For example:

```text
Firetruck
fire truck
firetruk
```

can resolve to the same vehicle where appropriate.

Ambiguous short names are rejected rather than selecting an arbitrary vehicle.

## Unsafe Vehicle Filtering

Not every model that Vice City classifies as a vehicle can safely be created through the normal `CREATE_CAR` and player-warp path.

The spawner therefore rejects special, scenery, or remote-controlled vehicles that are unsafe for this use.

Currently blocked examples include:

```text
CHOPPER
AIRTRAIN
DEADDODO
RCBANDIT
RCBARON
RCRAIDER
RCGOBLIN
```

Vehicle model metadata is also checked so unsupported vehicle hierarchy types are rejected before creation.

This prevents models such as `AIRTRAIN` from being passed through code paths intended for normal automobiles, bikes, and boats.

## Spawned Vehicle Behavior

A successfully spawned vehicle:

1. Is streamed into memory.
2. Is created in front of the player.
3. Matches the player's current heading.
4. Places the player into the driver seat.
5. Is released from script/mission retention.
6. Is handed back to Vice City's normal traffic cleanup system.

This means spawned vehicles can eventually despawn normally after the player abandons them.

F7 can explicitly retain the current vehicle as a mission vehicle.

F8 releases it back to normal random-vehicle handling.

The plugin also updates Vice City's internal vehicle-count bookkeeping when changing the creation type so the traffic counters remain balanced.

## Car Bomb Utilities

### F5 — Arm Car Bomb

While inside a vehicle:

- arms the current vehicle with a detonator-type car bomb
- gives the player a detonator
- plays a confirmation sound
- displays an immediate status message

### F6 — Remove Car Bomb

While inside a vehicle:

- clears the bomb rigger
- removes the active bomb type
- plays a confirmation sound
- displays an immediate status message

## Explosive Barrel

Press F4 to create an explosive barrel at the player's current location.

The barrel is created as a mission object and made targetable by the game.

## Immediate Status Messages

ILUtils status messages use Vice City's brief-message system.

New ILUtils messages replace the currently displayed ILUtils message immediately rather than waiting behind older messages in the brief-message queue.

For example, pressing several utility keys quickly does not cause each two-second message to wait for all previous messages to expire.

## Configuration

On first launch, the plugin creates:

```text
ILUtilsKeys.ini
```

beside the ASI file.

Default configuration:

```ini
[Keys]
SpawnBarrel=F4
RigCarBomb=F5
UnrigCarBomb=F6
MarkVehicle=F7
UnmarkVehicle=F8
CarSpawner=F9
```

Supported key formats include:

- function keys such as `F4`
- named keys such as `TAB`, `SPACE`, or `CTRL`
- letters
- numbers
- Windows virtual-key values

## Repository Layout

```text
VC-IL-Utils/
│
├── src/
│   └── Main.cpp
│
├── reference/
│   └── plugin-sdk/
│       └── Main_plugin_sdk.cpp
│
├── config/
│   └── ILUtilsKeys.ini
│
├── README.md
└── .gitignore
```

## Source Implementations

### `src/Main.cpp`

This is the production implementation.

It is a standalone Win32/x86 ASI supporting both the INTL 1.0 and JP executables.

It does not depend on plugin-sdk.

Vice City functions, globals, and hook locations are accessed through a runtime-selected address table.

The executable version is identified from its PE information before the corresponding address table is selected.

### `reference/plugin-sdk/Main_plugin_sdk.cpp`

This is the plugin-sdk implementation used as a readable development/reference version of ILUtils.

It preserves the much clearer high-level forms of operations such as:

```cpp
FindPlayerPed();
FindPlayerVehicle();

CStreaming::RequestModel(...);
CStreaming::LoadAllRequestedModels(...);

CModelInfo::GetModelInfo(...);
CCarCtrl::UpdateCarCount(...);

Command<Commands::CREATE_CAR>(...);
Command<Commands::WARP_PLAYER_INTO_CAR>(...);

CMessages::AddMessageJumpQ(...);
```

This source is useful when designing or prototyping new ILUtils functionality.

**Important:** the reference plugin-sdk source should not be assumed to support the Japanese executable.

The production JP-compatible implementation is `src/Main.cpp`.

## INTL / JP Compatibility

The Japanese executable is not simply the INTL executable shifted by one constant address.

Different portions of the executable move by different amounts.

For example, player helpers, streaming functions, script processing, globals, model information, and other engine subsystems can each have different INTL-to-JP relationships.

For that reason:

> Never derive a new JP function address by applying a single global INTL-to-JP delta.

Every new engine function or global should be identified independently.

Current standalone compatibility work includes mappings for functionality such as:

- player lookup
- player vehicle lookup
- player entity lookup
- player coordinates
- model streaming
- model unloading/deletability
- model information
- object allocation and construction
- world insertion
- vehicle pools
- vehicle creation
- vehicle traffic bookkeeping
- vehicle heading
- RenderWare frame updates
- weapon handling
- script commands
- brief/HUD messages
- script processing
- process hooks
- game timers
- INTL/JP-specific focus behavior

## JP Window-Focus Difference

The vehicle spawner revealed an important difference between the INTL and JP executables.

Vice City reacts to losing or regaining window focus by running game focus-restoration code that may activate the frontend/pause state.

The JP executable processes the popup focus transition differently from the INTL version.

VC-IL-Utils therefore contains version-specific focus suppression while the F9 spawner window is being opened or closed.

Only focus transitions caused by the ILUtils popup are suppressed.

Normal Alt-Tab behavior is left intact.

## Recommended Development Workflow

When adding a new feature:

1. Implement or prototype the behavior in `reference/plugin-sdk/Main_plugin_sdk.cpp`.
2. Test the plugin-sdk version against the supported INTL executable.
3. Identify every Vice City function, global, structure field, and script command required by the feature.
4. Determine the corresponding INTL addresses.
5. Determine the corresponding JP addresses independently.
6. Add the required mappings to the standalone address table.
7. Port the feature into `src/Main.cpp`.
8. Test INTL and JP independently.

The plugin-sdk implementation should be treated as a readable behavioral reference.

The standalone implementation is the authoritative cross-version build.

## Building

### Requirements

- Visual Studio 2022 or compatible MSVC toolchain
- Windows SDK
- Win32/x86 target

plugin-sdk is **not required** to build `src/Main.cpp`.

### Visual Studio Settings

Recommended settings:

```text
Configuration:       Release
Platform:            Win32
Configuration Type:  Dynamic Library (.dll)
Runtime Library:     /MT
Precompiled Headers: Disabled
Machine:             x86
```

Build the DLL and use `.asi` as the final extension.

For example:

```text
VC-IL-Utils.asi
```

### Command Line

An equivalent MSVC command is:

```bat
cl /nologo /EHsc /O2 /MT /LD src\Main.cpp ^
    /link /MACHINE:X86 /OUT:VC-IL-Utils.asi
```

The source links the Windows libraries required by the native vehicle-spawner interface.

## References and Credits

VC-IL-Utils was developed with the help of several existing Vice City modding and reverse-engineering projects.

### VC Line Practice Util

**0xMedinaZ — VCLinePracticeUtil**

https://github.com/0xMedinaZ/VCLinePracticeUtil

In particular:

https://github.com/0xMedinaZ/VCLinePracticeUtil/blob/main/VC_LinePracticeUtil.cpp

This project was an important architectural reference from the beginning of VC-IL-Utils development.

Its standalone approach demonstrated a practical pattern for:

- detecting the INTL and JP executables
- maintaining per-version address tables
- calling Vice City engine functions directly
- accessing game structures without requiring plugin-sdk
- installing lightweight hooks into the game

The INTL/JP standalone architecture of VC-IL-Utils was developed using this style as an important reference.

### plugin-sdk

**DK22Pac / plugin-sdk contributors**

https://github.com/DK22Pac/plugin-sdk

plugin-sdk was the primary high-level API and structure reference during development.

It was used to understand and prototype functionality involving classes and systems such as:

```text
CStreaming
CModelInfo
CCarCtrl
CMessages
CWorld
CVehicle
CPed
CTimer
ScriptCommands
```

The readable plugin-sdk implementation of ILUtils is retained in this repository under:

```text
reference/plugin-sdk/Main_plugin_sdk.cpp
```

### reVC

**reVC / GTA Vice City reverse-engineering project**

https://github.com/mrxenginner/reVC

reVC was used extensively to understand the internal behavior behind Vice City's engine functions rather than relying only on their addresses.

Examples include research into:

- script opcode implementations
- `CREATE_CAR`
- player warping
- vehicle creation types
- traffic vehicle cleanup
- model loading
- vehicle bookkeeping
- message queues
- Windows focus handling
- frontend/pause activation behavior
- keyboard processing
- entity and vehicle internals

The reVC source was particularly useful for determining why seemingly correct engine calls could produce different side effects.

### Vice City: Multiplayer

**Vice City: Multiplayer (VC:MP) — official Squirrel plugin**

https://github.com/vicecitymultiplayer/plugin-squirrel

Relevant implementation:

https://github.com/vicecitymultiplayer/plugin-squirrel/blob/master/FunctionHandler.cpp

VC:MP's vehicle utility functions were used as a reference while improving the ILUtils vehicle spawner's user-facing name handling.

In particular, VC:MP provides:

```cpp
GetVehicleModelFromName(...)
GetVehicleNameFromModel(...)
```

and maintains human-readable names for Vice City's vehicle model IDs, including names such as:

```text
Firetruck
FBI Washington
Mr. Whoopee
Sentinel XS
Sea Sparrow
PCJ-600
Police Maverick
Vice Squad Cheetah
```

This provided a useful reference for separating **human-readable vehicle names** from Vice City's sometimes abbreviated internal IDE names.

VC-IL-Utils uses its own matching implementation and extends the concept with normalization, aliases, ambiguity checking, prefix matching, typo tolerance, and live IDE-name support.

### OpenAI ChatGPT

**OpenAI ChatGPT**

https://chatgpt.com/

ChatGPT was used throughout development as a programming and reverse-engineering assistant.

Its contributions to the development process included:

- translating plugin-sdk implementations into standalone engine calls
- comparing INTL and JP executable behavior
- assisting with per-version address mapping
- analyzing plugin-sdk and reVC source
- debugging crashes and ABI/pointer issues
- investigating Vice City's Windows focus behavior
- developing the native vehicle-spawner input architecture
- improving vehicle-name matching
- identifying unsafe vehicle creation paths
- debugging vehicle lifetime/traffic bookkeeping
- preparing code revisions and documentation

All generated and suggested code was tested and iterated against the actual game executables during development.

## Notes on Reverse Engineering

Addresses documented in this project are specific to the supported executable builds.

They should not automatically be assumed to apply to:

- Vice City 1.1
- Steam
- modified/cracked executables
- other regional releases
- executable patches
- total conversions

Executable compatibility should always be verified before adding another version to the address table.

## Project Status

Current tested status:

```text
INTL 1.0: Working
JP:       Working
```

The repository is currently intended for development and testing.