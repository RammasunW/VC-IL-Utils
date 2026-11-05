# Local IME and RC vehicle changes

## Game input method

The game HWND is disassociated from its IME context on the game window's
owning thread. Active composition is cancelled and IME composition messages
are suppressed. Context is detached again after language/focus changes.
Ordinary gameplay key messages, including Ctrl, are still delivered.
Tool edit windows retain their IME contexts. This does not remove Chinese
input methods or change Windows-wide language shortcuts/settings.
The original game context is restored when removing the policy.

## Drivable RC vehicles

RC Bandit (171), RC Baron (194), RC Raider (195) and RC Goblin (231) can
be spawned and entered. CHOPPER, AIRTRAIN and DEADDODO remain blocked.
All four RC models are automobiles in the stock IDE. Their missing door
frames are incompatible with normal door-animation code: Automobile::OpenDoor
accesses the selected frame without checking it for null.

Player RC entry uses WARP_PLAYER_INTO_CAR. Player RC exit uses
WARP_PLAYER_FROM_CAR_TO_COORD at a point beside the vehicle, with a ground
height adjustment when available. These paths bypass door animations and
use the normal enter/exit controls. Ordinary vehicles and NPC behavior retain
the original entry/exit functions. RigAllCarBombs continues to exclude RCs.

Hook addresses were verified in the local INTL/JP executables:

| Function | INTL | JP |
| --- | --- | --- |
| CPed::SetEnterCar | 0x518080 | 0x517A20 |
| CPed::SetExitCar | 0x516C60 | 0x5165D0 |

Both complete 7-byte prologues are verified before installing hooks.
The original functions run through trampolines for unaffected vehicles.

Automated EN/CN checks cover RC model eligibility, entry/exit routing,
ground-adjusted exit, ordinary-vehicle fallthrough, hook restoration,
unchanged mass-bomb RC exclusion, game-only IME suppression, Ctrl messages,
tool IME preservation and restoration of the game's original context.

Before pushing, test all four RC models in-game: spawn, drive, exit with
the normal vehicle-exit control, then approach and re-enter. Also test Ctrl
with the Chinese IME selected and verify normal typing in the tool windows.

## NPC RC door guard

The shared automobile vtable now guards OpenDoor and ProcessOpenDoor for
all four RC models, regardless of who is interacting with them. Both door-
missing query variants return true for RCs. Normal automobile models call
the saved original functions. This extends protection beyond the earlier
player-only entry/exit bypass to police and other pedestrian interactions.

Verified automobile vtables: INTL `0x69AD90`, JP `0x6980C8`.
The patched slots are 22, 23, 27 and 29; every original target is verified
before installation and restored when removing the hooks.
Tests cover all four RC models, normal-car fallthrough and restoration.
Retest a police chase followed by RC entry to confirm the reported crash.
