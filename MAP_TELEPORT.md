# Short map teleport landing

Local EN/CN update. Each map click replaces the old pending destination.
The current ped or vehicle is frozen briefly, its movement/rotation speeds
are cleared, and it moves to the destination at a temporary safe height.
Scenery requests and REQUEST_COLLISION are submitted without synchronous
LOAD_SCENE or sleeps.

On each game update, the code queries ground height with an explicit success
flag. It lands immediately on success, or uses the sea fallback after four
queries or 150 milliseconds, whichever comes first (roughly 0.13 s at 30 fps).
Stock Vice City sea level is Z=6.0, verified in the local water.dat and
WATERPRO.DAT. The fallback is Z=6.5 plus the normal coordinate opcode's
model-base offset. Found ground below sea level also uses the water surface.

The previous freeze/infinite-mass bits are restored without overwriting
unrelated flags. Entity changes cancel the old request; vehicle pool-slot
generation is checked before restoring flags to avoid touching a reused slot.

The map selects the highest static surface below Z=1000, so rooftops or
upper bridge decks can be selected. The brief fallback intentionally does
not distinguish open sea from collision that is still unavailable.

Native addresses verified against local INTL and JP executables:

| Function | INTL | JP |
| --- | --- | --- |
| CWorld::FindGroundZFor3DCoord | 0x4D53A0 | 0x4D4D10 |
| CStreaming::AddModelsToRequestList | 0x40EA10 | 0x40EDB0 |

Automated EN/CN tests cover delayed and immediate landing, four-frame and
wall-clock fallback, a frozen vehicle, repeated clicks, exiting a vehicle,
recycled pool slots and invalid ground results. Test long-distance land,
ocean and vehicle destinations in-game before pushing.
