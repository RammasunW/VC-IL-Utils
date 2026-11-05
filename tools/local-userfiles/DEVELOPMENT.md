# VC Local User Files

Standalone Win32/x86 ASI for the supported Vice City INTL 1.0 and Japanese
executables. It redirects the game's user files to an absolute `userfiles`
directory beside `gta-vc.exe`, independent of the working directory or where
the ASI itself is installed. It does not require VC-IL-Utils or plugin-sdk.

## Installation

Place **both** `dinput8.dll` and `zz_VC-LocalUserFiles.asi` beside `gta-vc.exe`.
Version 6 includes a small early-loading DirectInput proxy. Miles-based ASI
loaders can load plugins after the game already read its startup settings;
an ASI alone cannot retroactively redirect that read. This proxy loads only
the User Files ASI during DLL initialization, before the game's main startup,
and forwards normal DirectInput exports to the Windows system DLL. It does
not enumerate or load other ASIs. The normal ASI loader may load the same
ASI later without initializing it twice.

Keep existing `ddraw.dll`, `Mss32.dll`, `d3d8.dll` and other plugins.
If a different `dinput8.dll` already exists, do not overwrite it without
arranging compatibility/chaining first. The supplied proxy uses the Windows
system DirectInput DLL; it does not chain an existing custom dinput8 wrapper.

The ASI creates `userfiles` automatically; no INI is needed.
Use the same ASI in both installations. Each game installation gets its own
directory. Two executables in the same directory will share `userfiles`.

```text
Vice City INTL/
  gta-vc.exe
  dinput8.dll
  zz_VC-LocalUserFiles.asi
  userfiles/

Vice City JP/
  gta-vc.exe
  dinput8.dll
  zz_VC-LocalUserFiles.asi
  userfiles/
```

The game will initially see an empty profile. To keep existing saves/settings,
copy only the files appropriate for that version from Documents into its new
`userfiles` directory while the game is closed. The tool does not migrate,
convert, overwrite or delete existing save files. Exit the game before
installing/removing it. Removing the ASI restores the original path on the
next launch; files remain in `userfiles`. Remove the supplied `dinput8.dll`
together with the ASI, since it requires the companion ASI at startup.

## Compatibility

The shared user-directory getter, startup save-directory setter and direct
file-manager user-directory switch are redirected. The cached save-slot prefix
is updated too. The startup setter reapplies the getter after ASI loading so
SpeedrunSilentPatch's normal Documents-path hook does not split the game's
saves and settings. Tests simulate SSP installed before and after this ASI,
plus replacing its getter again after startup. The file-manager hook switches
the actual directory directly so settings reads do not rely on a cached path.
Version 4 additionally intercepts the CFileMgr::Initialise call immediately
before the inline startup folder block. After that initialization it reapplies
the inline CALL/get-local-path and JMP/skip-Documents sequence, including when
SSP overwrote it later in DLL initialization. The existing native continuation
then receives the local path. No executable file on disk is modified.
The `zz_` prefix also favors loading after other ASIs with alphabetical loaders.
SSP's own auxiliary settings are outside the scope of this game-file redirect.
Other portable-profile/path mods should not be combined with this ASI.

The executable timestamp and startup instruction bytes are checked before
patching. Unsupported versions or unwritable/unsupported paths produce an
error and disable the ASI. The game uses legacy ANSI filenames and short
save-filename buffers: this tool uses a short-path alias if needed and available,
and otherwise refuses paths exceeding its safe 180-byte directory limit or
containing characters unrepresentable by the game. It does not fall back to
Documents silently.

## Build and checks

Run `tools\local-userfiles\build.bat` with Visual Studio C++ x86 tools.
Optionally pass a toolset version, e.g. `build.bat 14.44`, and set
`QIANCHENG_VS_PATH` to choose a Visual Studio installation.
Outputs are in `Release\local-userfiles`.

Tests cover executable-relative paths, spaces, drive roots, UNC paths, length
limits, directory creation/access, startup redirection, the shared path buffer,
hook restoration, both SSP load orders, save-prefix correction and actual
relative file reads/writes. In-game testing is still required.

Version 4 writes `VC-LocalUserFiles.log` beside `gta-vc.exe`, recording the
selected path and first startup/file-manager calls. If the old folder is
still used, this log helps distinguish loading problems from later overrides.
Version 6 opens `gta_vc.set` by its absolute local path for both reads and
writes. This removes dependence on the process-wide working directory during
the open itself. Other filenames retain their existing handling.
It traces settings reads and closes using the game's own CRT functions,
including initial control records and the preference reads after the controls.
It records call sites, destination addresses, the first eight bytes of selected
reads, short reads and totals. It also compares sampled values in the game's
image at subsequent settings closes, reporting changes since the initial load.
Stack buffers are excluded from these comparisons. This can reveal resets
between preference loading and the later input-settings load; it does not
observe arbitrary changes after the last settings close.
This is a diagnostic build: successful opens alone do not establish that the
game accepted or retained the settings. It keeps the original text/binary mode
and delegates all reads, seeks and closes to the existing game routines.
Expect `Early startup gate invoked before inline Documents-folder block`,
`Startup save-directory setter invoked` and `SETTINGS OPEN` lines showing the
absolute path, mode and success/failure of game settings opens. These trace
CFileMgr::OpenFile without redirecting arbitrary file operations. SSP may
still create a Documents directory for its own window-placement settings;
the actual game's settings-open path is the decisive check.
In the inspected SpeedrunSilentPatch source, window positions are saved only
in Framed mode, in `SpeedrunSilentPatchVC.WindowedMode.ini` under its own
Documents path. Borderless startup can read that position, but moving in
Borderless does not save it. Switch with Alt+F to Framed, move the game to the
desired monitor, then use Alt+F to return to Borderless to save a position.
This ASI does not modify SilentPatch's own window-placement code or file path.
A separate bootstrap test verifies ASI initialization before main
and forwarding to Windows DirectInput without capturing an input device.

Verified function addresses:

| Function | INTL | JP |
| --- | --- | --- |
| User-files directory getter | 0x602240 | 0x601FE0 |
| C_PcSave::SetSaveDirectory | 0x61D930 | 0x61D640 |
| CFileMgr::SetDirMyDocuments | 0x48E020 | 0x48E410 |
| Pre-folder startup call | 0x601A3B | 0x60182B |
| Inline folder block | 0x601A40 | 0x601830 |
| Continuation | 0x601B2F | 0x60191F |
| CFileMgr::OpenFile | 0x48DF90 | 0x48E380 |
| CFileMgr::Read | 0x48DF50 | 0x48E340 |
| CFileMgr::CloseFile | 0x48DEA0 | 0x48E290 |

All source and tests are contained in this directory. No gameplay, mouse,
input-method or RC-vehicle hooks are part of this ASI.
