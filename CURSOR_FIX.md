# Local v1.4.2 cursor fix

Based on the EN/CN sources in `单任务小工具？v1.4.2.zip`.

The old handlers hid the cursor on child-control mouse-leave and top-level
deactivation, including transfers to another tool window. Child hover also
raised the ShowCursor counter to 30, while info-panel refresh depended on
ForceCursor (off by default), and hiding some windows stopped their timers.

The fix uses the actual hovered or captured tool window to decide whether
the owning UI thread needs a visible cursor. It covers info, map, save,
mission selector and vehicle spawner windows. A positive cursor count is
not increased on refresh. Closing, hiding or leaving one tool window no
longer forces the pointer off in another. Child subclassing preserves
existing text/edit handlers and their normal cursors. Visibility refresh
is automatic; the legacy ForceCursor setting is no longer needed.

The follow-up for INTL tools on another monitor uses physical screen
coordinates for hit testing and restores the current UI thread's cursor
directly, rather than relying on a global GetCursorInfo handle. Cursor
events supply their window and normal cursor explicitly; timer refresh
can restore that cursor after GTA clears it. This keeps edit-field cursors
and does not change process-wide DPI settings or move the pointer.

The repeated-click follow-up also guards GTA's own User32 import slots for
SetCursorPos, ClipCursor and SetCapture while a tool is hovered, focused,
foreground or captured. This prevents the game from recentering the pointer
on its monitor, reapplying confinement or stealing a tool drag. Tool windows
keep the normal User32 APIs; original game behavior resumes outside tool
interaction. Import slots are restored on unload. The map clears drag state
on capture loss and ignores button releases without a matching press.
The map's teleport/ground-placement functions are unchanged.

The INTL-specific follow-up addresses DirectInput, which bypasses the User32
guards. The supported local INTL executable's `_InputGetMouseState` reader
at `0x5FF290` reads `RsGlobal.ps` through `0x9B48F4`, uses its mouse device
at offset `0x1C`, and calls GetDeviceState/Acquire directly. While a tool
owns the mouse, the replacement releases that device and supplies a zeroed
20-byte mouse state with a successful result, avoiding GTA's reacquire loop.
Tool activation and title-bar moving also release the device before normal
Windows handling. Otherwise the original reader runs through a trampoline.
Installation verifies the entire first instruction before patching it.
This additional interception is INTL-only; JP's input reader is untouched.

Regression checks exercise a synthetic reader and mouse device: repeated
tool interactions unacquire the device, return neutral input, title-bar
handling releases promptly, and normal input resumes through the original
reader after tools give up mouse ownership. These checks do not substitute
for the user's actual two-monitor INTL setup.

SSP comparison: its `ENABLE_FIX_MOUSE_MENU_LOCKUP` VC fix makes the
mouse-exclusivity predicate return false (INTL `0x601740`, JP `0x601530`).
The INTL function and its instruction bytes were independently verified
in the local supported executable. This build applies only that targeted
INTL policy alongside the existing tool-input guard and switches any
already-created INTL device to DISCL_NONEXCLUSIVE | DISCL_FOREGROUND.
The device is unacquired before changing its cooperative level. The
original policy bytes are restored on unload if they remain our patch.
No SSP DLL installation or changes to the JP input path are required.

Reference: `Grovespaz/SpeedrunSilentPatch`,
`SilentPatchVC/SilentPatchVC.cpp`, `ENABLE_FIX_MOUSE_MENU_LOCKUP`.
The repository README lists VC JP as supported and VC 1.0/1.1 as untested;
this does not establish the cause of the user's full-patch INTL crash.

The map bitmap was recovered from resource 101 in the supplied EN ASI
using data-only resource loading. It is included as `src/resources/vc-map.bmp`
and compiled into both language builds. The map reference supplied with
the update is https://github.com/denizariyan/GTA-Vice-City-Interactive-Map.

Build using `src/build.bat`, then use one language from
`Release/qiancheng/EN` or `Release/qiancheng/CN`.

Automated checks cover five tool-window classes, transitions between windows,
hidden windows, repeated cursor refresh without counter growth, preservation
of the text cursor, plus the existing rampage and interesting-vehicle checks.
Final confirmation is still needed in the running INTL/JP game: open several
tools together, move between backgrounds/buttons/text fields, drag the map,
close one tool while another is visible, and return to the game.

All changes are local. The tracked root EN/CN ASIs have not been replaced.
