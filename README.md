# VC Custom Userfiles

Keeps Vice City's saves, settings and replays in `userfiles` beside
`gta-vc.exe`. Tested with INTL 1.0 and JP. Each game installation gets its own
folder. VC-IL-Utils is not required.

## Install

1. Close the game.
2. Copy **both** files from `bin` (`dinput8.dll` and `zz_VC-LocalUserFiles.asi`) beside `gta-vc.exe`.
3. To keep your settings and saves, copy the correct version's files from
   `Documents\GTA Vice City User Files` into `<game folder>\userfiles`.
   The settings filename is **`gta_vc.set`**. Use a known-good file from the
   same game version; an existing local file may already contain defaults.
4. Launch the game. The folder is created automatically if needed.

Keep your other ASI loaders and plugins. If a different `dinput8.dll` already
exists, arrange compatibility before replacing it. The supplied DLL loads
this ASI early enough for startup settings.

No files are migrated automatically. Two executables in the same game folder
share `userfiles`. The game folder must be writable.

## Why are both files needed?

`dinput8.dll` loads only the Userfiles ASI early in startup, then forwards
DirectInput calls to Windows' original DLL. It does not load other ASIs.
The usual later ASI loading was too late to redirect startup settings in our
tests. Keep both files for the tested setup. Another loader could replace
this DLL only if it reliably loads the ASI equally early.

`zz_VC-LocalUserFiles.asi` detects INTL or JP, creates the local directory,
redirects the game's user-directory handling and cached save paths, and opens
`gta_vc.set` through its full local path. It reapplies startup redirects around
SilentPatch's path hooks and writes diagnostic logs.

Changes are made in memory; the executable on disk is unchanged. The final
JP settings issue during testing was resolved by replacing a local file that
contained defaults with a known-good JP profile.

## SilentPatch

SpeedrunSilentPatch can still create a Documents folder for its own window
position INI. This plugin redirects the game's files, not SilentPatch's INI.
To remember a monitor position, use Alt+F to switch to Framed mode, move the
window, then press Alt+F to return to Borderless.

## Remove

Close the game and remove both supplied files. The game returns to its normal
Documents folder on the next launch. Files in `userfiles` remain untouched.

## Build / troubleshooting

Build with Visual Studio C++ x86 using `tools\local-userfiles\build.bat`.
Outputs are in `Release\local-userfiles`. The ASI writes
`VC-LocalUserFiles.log` beside the executable for troubleshooting.
See [DEVELOPMENT.md](tools/local-userfiles/DEVELOPMENT.md) for implementation and test details.
