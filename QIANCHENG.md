# qiancheng branch

The original EN/CN sources, configurations and compiled ASIs from
`VC-IL-Utils-合并版 (3).zip` were imported unchanged in commit `32db0f0`.
`src/Main_EN.cpp` and `src/Main_CN.cpp` are the contributed implementations.
`src/Main.cpp` and `reference/plugin-sdk/` retain the earlier implementation;
the plugin-sdk reference does not include qiancheng's additions.

## Local rampage timer addition

| Default key | Action |
| --- | --- |
| PgUp | Add 5 seconds to the active rampage |
| PgDn | Remove 5 seconds from the active rampage |

The exact messages in both language versions are `Rampage is not active`,
`Rampage timer +5s`, and `Rampage timer -5s`.
The keys act once per press; pressing both together cancels the adjustment.
The duration is bounded to zero and the signed 32-bit maximum.

Add these to an existing `[Keys]` section to customize the defaults:

```ini
RampageTimerIncrease=PGUP
RampageTimerDecrease=PGDN
```

Missing entries use those defaults, so existing configurations still work.

Only `CDarkel::TimeLimit` is changed (milliseconds): INTL `0x974BE0`,
JP `0x971BE8`. `TimeOfFrenzyStart` remains unchanged.
Active status is checked separately: INTL short `0xA10A72`, JP short
`0xA0DA82`, with value `1` meaning active. JP `FrenzyOnGoing` at
`0x42A360` and the timer references were verified in the local JP executable.

## Local information-panel additions

The panel lists both interesting vehicle slots with English vehicle names and
their entry timestamps in whole seconds, matching `readCars.ahk` (not elapsed
time since entry). Empty or invalid slots display None / 无 and zero seconds.
Current-vehicle CreatedBy is shown only while in a vehicle, including its
numeric value: Random / 街车, Mission / 任务车, Parked / 停车场用车,
Permanent / 永久车. Unknown values retain their numeric value.

INTL pointer slots start at `0x7E9D80` and timestamps at `0x94B928`;
JP pointer slots start at `0x7E6D88` and timestamps at `0x948930`.
Pointers are checked against occupied vehicle-pool slots before dereferencing.
The panel is enlarged to keep the new lines above the coordinate controls.

## Building both language versions

Run `src\build.bat` with a Visual Studio C++ x86 toolchain installed.
An optional argument selects its version, such as `src\build.bat 14.44`.
Set `QIANCHENG_VS_PATH` to select a specific Visual Studio installation.
EN uses UTF-8; CN preserves GBK source and execution encoding.

Outputs and matching configuration files are placed in
`Release\qiancheng\EN` and `Release\qiancheng\CN`.
Install one language's ASI beside `gta-vc.exe`; each supports INTL and JP.
The root `EN` and `CN` ASIs contain the tested rampage and info-panel additions.
The original imported binaries remain available in commit `32db0f0`.
