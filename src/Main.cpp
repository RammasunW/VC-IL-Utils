#include <Windows.h>
#include <CommCtrl.h>
#include <process.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <cctype>
#include <cmath>

#pragma comment(lib, "User32.lib")
#pragma comment(lib, "Comctl32.lib")

static_assert(sizeof(void*) == 4, "Build this ASI as Win32/x86.");

struct CVector {
    float x, y, z;
};

// -------------------------------------------------------
// GTA VC version detection
// -------------------------------------------------------

enum GameVersion {
    VER_INTL_10,
    VER_JP,
    VER_UNKNOWN
};

static GameVersion g_version = VER_UNKNOWN;

// PE timestamps:
// INTL 1.0 = 0x504D6947
// JP       = 0x40758BC6
static GameVersion DetectVersion() {
    BYTE* base = reinterpret_cast<BYTE*>(0x400000);
    DWORD pe = *reinterpret_cast<DWORD*>(base + 0x3C);
    DWORD timestamp = *reinterpret_cast<DWORD*>(base + pe + 8);

    if (timestamp == 0x504D6947)
        return VER_INTL_10;
    if (timestamp == 0x40758BC6)
        return VER_JP;

    return VER_UNKNOWN;
}

// -------------------------------------------------------
// Version-specific addresses
// -------------------------------------------------------

struct AddrTable {
    DWORD FindPlayerPed;
    DWORD FindPlayerVehicle;
    DWORD FindPlayerCoors;

    DWORD RequestModel;
    DWORD LoadAllRequestedModels;
    DWORD SetModelIsDeletable;

    DWORD ObjectNew;
    DWORD ObjectCtor;
    DWORD WorldAdd;
    DWORD GiveWeapon;

    DWORD AddMessageJumpQ;
    DWORD UpdateCarCount;
    DWORD RemoveInterestingVehicle;
    DWORD SetHeading;
    DWORD UpdateRwFrame;

    DWORD ProcessOneCommand;
    DWORD ProcessScripts;
    DWORD HookSiteProcessScripts;

    DWORD ScriptSpace;
    DWORD ObjectPool;
    DWORD GameTimer;
    DWORD ModelInfoTable;
    DWORD StreamingInfoTable;
};

static const AddrTable kAddr_INTL = {
    0x4BC120, // FindPlayerPed
    0x4BC1E0, // FindPlayerVehicle
    0x4BC240, // FindPlayerCoors

    0x40E310, // CStreaming::RequestModel
    0x40B5F0, // CStreaming::LoadAllRequestedModels
    0x40AEA0, // CStreaming::SetModelIsDeletable

    0x4E4070, // CObject::operator new
    0x4E41B0, // CObject::CObject(int, bool)
    0x4DB3F0, // CWorld::Add
    0x4FFA30, // CPed::GiveWeapon

    0x584300, // CMessages::AddMessageJumpQ(wchar_t const*, uint, ushort)
    0x41C350, // CCarCtrl::UpdateCarCount
    0x41D320, // CCarCtrl::RemoveFromInterestingVehicleList
    0x4BBA80, // CPlaceable::SetHeading
    0x489360, // CEntity::UpdateRwFrame

    0x44FBE0, // CRunningScript::ProcessOneCommand
    0x5A92E0, // CTheScripts::Process
    0x4A45AA, // call site for CTheScripts::Process

    0x821280, // CTheScripts::ScriptSpace
    0x94DBE0, // CPools::ms_pObjectPool
    0x974B2C, // CTimer::m_snTimeInMilliseconds
    0x92D4C8, // CModelInfo::ms_modelInfoPtrs[]
    0x94DDD0  // CStreaming::ms_aInfoForModel[]
};

static const AddrTable kAddr_JP = {
    0x4BBA40, // FindPlayerPed
    0x4BBB00, // FindPlayerVehicle
    0x4BBB60, // FindPlayerCoors

    0x40E6B0, // CStreaming::RequestModel
    0x40B990, // CStreaming::LoadAllRequestedModels
    0x40B240, // CStreaming::SetModelIsDeletable

    0x4E39E0, // CObject::operator new
    0x4E3B20, // CObject::CObject(int, bool)
    0x4DAD60, // CWorld::Add
    0x4FF3A0, // CPed::GiveWeapon

    0x584220, // CMessages::AddMessageJumpQ(wchar_t const*, uint, ushort)
    0x41C6F0, // CCarCtrl::UpdateCarCount
    0x41D6C0, // CCarCtrl::RemoveFromInterestingVehicleList
    0x4BB3A0, // CPlaceable::SetHeading
    0x489740, // CEntity::UpdateRwFrame

    0x44FF50, // CRunningScript::ProcessOneCommand
    0x5A90A0, // CTheScripts::Process
    0x4A3ECA, // call site for CTheScripts::Process

    0x81E288, // CTheScripts::ScriptSpace
    0x94ABE8, // CPools::ms_pObjectPool
    0x971B34, // CTimer::m_snTimeInMilliseconds
    0x92A4D0, // CModelInfo::ms_modelInfoPtrs[]
    0x94ADD8  // CStreaming::ms_aInfoForModel[]
};

static const AddrTable* g_addr = nullptr;

// -------------------------------------------------------
// Engine function pointers
// -------------------------------------------------------

typedef void* (__cdecl* FindPlayerPed_t)();
typedef void* (__cdecl* FindPlayerVehicle_t)();
typedef void  (__cdecl* FindPlayerCoors_t)(CVector* out);

typedef void  (__cdecl* RequestModel_t)(int modelIndex, int streamingFlags);
typedef void  (__cdecl* LoadAllRequestedModels_t)(bool onlyPriorityRequests);
typedef void  (__cdecl* SetModelIsDeletable_t)(int modelIndex);

typedef void* (__cdecl* ObjectNew_t)(unsigned int size);
typedef void  (__thiscall* ObjectCtor_t)(void* object, int modelId, bool createRwObject);
typedef void  (__cdecl* WorldAdd_t)(void* entity);
typedef void  (__thiscall* GiveWeapon_t)(void* ped, int weaponType, unsigned int ammo, bool likeUnused);

typedef void  (__cdecl* AddMessageJumpQ_t)(const wchar_t* text, unsigned int time, unsigned short flag);
typedef void  (__cdecl* UpdateCarCount_t)(void* vehicle, unsigned char remove);
typedef void  (__cdecl* RemoveInterestingVehicle_t)(void* vehicle);
typedef void  (__thiscall* SetHeading_t)(void* entity, float heading);
typedef void  (__thiscall* UpdateRwFrame_t)(void* entity);

typedef char  (__thiscall* ProcessOneCommand_t)(void* script);
typedef void  (__cdecl* ProcessScripts_t)();

static FindPlayerPed_t FindPlayerPedFn = nullptr;
static FindPlayerVehicle_t FindPlayerVehicleFn = nullptr;
static FindPlayerCoors_t FindPlayerCoorsFn = nullptr;

static RequestModel_t RequestModelFn = nullptr;
static LoadAllRequestedModels_t LoadAllRequestedModelsFn = nullptr;
static SetModelIsDeletable_t SetModelIsDeletableFn = nullptr;

static ObjectNew_t ObjectNewFn = nullptr;
static ObjectCtor_t ObjectCtorFn = nullptr;
static WorldAdd_t WorldAddFn = nullptr;
static GiveWeapon_t GiveWeaponFn = nullptr;

static AddMessageJumpQ_t AddMessageJumpQFn = nullptr;
static UpdateCarCount_t UpdateCarCountFn = nullptr;
static RemoveInterestingVehicle_t RemoveInterestingVehicleFn = nullptr;
static SetHeading_t SetHeadingFn = nullptr;
static UpdateRwFrame_t UpdateRwFrameFn = nullptr;

static ProcessOneCommand_t ProcessOneCommandFn = nullptr;
static ProcessScripts_t ProcessScriptsOriginal = nullptr;

static int* GameTimer = nullptr;


// -------------------------------------------------------
// configurable keys
// -------------------------------------------------------

static int g_keySpawnBarrel = VK_F4;
static int g_keyRigBomb = VK_F5;
static int g_keyUnrigBomb = VK_F6;
static int g_keyMarkVeh = VK_F7;
static int g_keyUnmarkVeh = VK_F8;
static int g_keyCarSpawner = VK_F9;

static char g_iniPath[MAX_PATH];

static const char* iniFileName = "ILUtilsKeys.ini";

static int ParseKey(const char* str) {
    if (!str || !str[0])
        return -1;

    // Common named keys
    struct KeyName {
        const char* name;
        int vk;
    };

    static const KeyName keys[] = {
        { "TAB",        VK_TAB },
        { "VK_TAB",     VK_TAB },

        { "SPACE",      VK_SPACE },
        { "VK_SPACE",   VK_SPACE },

        { "ENTER",      VK_RETURN },
        { "RETURN",     VK_RETURN },
        { "VK_RETURN",  VK_RETURN },

        { "SHIFT",      VK_SHIFT },
        { "VK_SHIFT",   VK_SHIFT },
        { "LSHIFT",     VK_LSHIFT },
        { "RSHIFT",     VK_RSHIFT },

        { "CTRL",       VK_CONTROL },
        { "CONTROL",    VK_CONTROL },
        { "VK_CONTROL", VK_CONTROL },
        { "LCTRL",      VK_LCONTROL },
        { "RCTRL",      VK_RCONTROL },

        { "ALT",        VK_MENU },
        { "VK_MENU",    VK_MENU },
        { "LALT",       VK_LMENU },
        { "RALT",       VK_RMENU },

        { "INSERT",     VK_INSERT },
        { "VK_INSERT",  VK_INSERT },

        { "DELETE",     VK_DELETE },
        { "DEL",        VK_DELETE },
        { "VK_DELETE",  VK_DELETE },

        { "HOME",       VK_HOME },
        { "END",        VK_END },

        { "PAGEUP",     VK_PRIOR },
        { "PGUP",       VK_PRIOR },
        { "PAGEDOWN",   VK_NEXT },
        { "PGDN",       VK_NEXT },

        { "UP",         VK_UP },
        { "DOWN",       VK_DOWN },
        { "LEFT",       VK_LEFT },
        { "RIGHT",      VK_RIGHT },

        { "BACKSPACE",  VK_BACK },
        { "ESCAPE",     VK_ESCAPE },
        { "ESC",        VK_ESCAPE },

        { "CAPSLOCK",   VK_CAPITAL },

        { "NUMPAD0",    VK_NUMPAD0 },
        { "NUMPAD1",    VK_NUMPAD1 },
        { "NUMPAD2",    VK_NUMPAD2 },
        { "NUMPAD3",    VK_NUMPAD3 },
        { "NUMPAD4",    VK_NUMPAD4 },
        { "NUMPAD5",    VK_NUMPAD5 },
        { "NUMPAD6",    VK_NUMPAD6 },
        { "NUMPAD7",    VK_NUMPAD7 },
        { "NUMPAD8",    VK_NUMPAD8 },
        { "NUMPAD9",    VK_NUMPAD9 },
    };

    for (const auto& key : keys) {
        if (_stricmp(str, key.name) == 0)
            return key.vk;
    }

    // F1 through F24, accepting both "F4" and "VK_F4"
    const char* fstr = str;

    if (_strnicmp(fstr, "VK_", 3) == 0)
        fstr += 3;

    if ((fstr[0] == 'F' || fstr[0] == 'f') && fstr[1]) {
        char* end = nullptr;
        long n = strtol(fstr + 1, &end, 10);

        if (*end == '\0' && n >= 1 && n <= 24)
            return VK_F1 + (int)n - 1;
    }

    // Single letters/numbers:
    // A-Z, 0-9
    if (str[0] && str[1] == '\0') {
        char c = str[0];

        if (c >= 'a' && c <= 'z')
            c -= ('a' - 'A');

        if ((c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9'))
            return c;
    }

    // Also allow raw numeric VK codes:
    // e.g. 115 or 0x73 = F4
    char* end = nullptr;
    long value = strtol(str, &end, 0);

    if (*end == '\0' && value >= 0 && value <= 255)
        return (int)value;

    return -1;
}

static void GetIniPath() {
    HMODULE module = nullptr;

    // Get the module containing this function, i.e. this ASI/DLL,
    // rather than gta-vc.exe.
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&GetIniPath),
        &module
    );

    GetModuleFileNameA(module, g_iniPath, MAX_PATH);

    char* slash = strrchr(g_iniPath, '\\');

    if (slash)
        strcpy_s(
            slash + 1,
            MAX_PATH - (slash - g_iniPath) - 1,
            iniFileName
        );
    else
        strcpy_s(g_iniPath, MAX_PATH, iniFileName);
}

static int ReadKey(
    const char* section,
    const char* name,
    const char* defaultValue,
    int defaultKey
) {
    char buf[128];

    GetPrivateProfileStringA(
        section,
        name,
        defaultValue,
        buf,
        sizeof(buf),
        g_iniPath
    );

    int key = ParseKey(buf);

    return key != -1 ? key : defaultKey;
}

static void LoadConfig() {
    GetIniPath();

    // Create a default INI the first time if it doesn't exist.
    if (GetFileAttributesA(g_iniPath) == INVALID_FILE_ATTRIBUTES) {
        WritePrivateProfileStringA(
            "Keys",
            "SpawnBarrel",
            "F4",
            g_iniPath
        );

        WritePrivateProfileStringA(
            "Keys",
            "RigCarBomb",
            "F5",
            g_iniPath
        );

        WritePrivateProfileStringA(
            "Keys",
            "UnrigCarBomb",
            "F6",
            g_iniPath
        );

        WritePrivateProfileStringA(
            "Keys",
            "MarkVehicle",
            "F7",
            g_iniPath
        );

        WritePrivateProfileStringA(
            "Keys",
            "UnmarkVehicle",
            "F8",
            g_iniPath
        );

        WritePrivateProfileStringA(
            "Keys",
            "CarSpawner",
            "F9",
            g_iniPath
        );
    }

    g_keySpawnBarrel = ReadKey(
        "Keys",
        "SpawnBarrel",
        "F4",
        VK_F4
    );

    g_keyRigBomb = ReadKey(
        "Keys",
        "RigCarBomb",
        "F5",
        VK_F5
    );

    g_keyUnrigBomb = ReadKey(
        "Keys",
        "UnrigCarBomb",
        "F6",
        VK_F6
    );

    g_keyMarkVeh = ReadKey(
        "Keys",
        "MarkVehicle",
        "F7",
        VK_F7
    );

    g_keyUnmarkVeh = ReadKey(
        "Keys",
        "UnmarkVehicle",
        "F8",
        VK_F8
    );

    g_keyCarSpawner = ReadKey(
        "Keys",
        "CarSpawner",
        "F9",
        VK_F9
    );
}


// -------------------------------------------------------
// Per-frame key edge detection
// -------------------------------------------------------

struct ActionKeys {
    bool spawnBarrel;
    bool rigBomb;
    bool unrigBomb;
    bool markVehicle;
    bool unmarkVehicle;
};

static bool s_keyWasDown[256] = {};

static ActionKeys ReadActionKeys() {
    const int keys[5] = {
        g_keySpawnBarrel,
        g_keyRigBomb,
        g_keyUnrigBomb,
        g_keyMarkVeh,
        g_keyUnmarkVeh
    };

    bool down[5] = {};
    bool pressed[5] = {};

    for (int i = 0; i < 5; ++i) {
        const int key = keys[i];
        if (key >= 0 && key < 256) {
            down[i] = (GetAsyncKeyState(key) & 0x8000) != 0;
            pressed[i] = down[i] && !s_keyWasDown[key];
        }
    }

    for (int i = 0; i < 5; ++i) {
        const int key = keys[i];
        if (key >= 0 && key < 256)
            s_keyWasDown[key] = down[i];
    }

    ActionKeys result = {};
    result.spawnBarrel = pressed[0];
    result.rigBomb = pressed[1];
    result.unrigBomb = pressed[2];
    result.markVehicle = pressed[3];
    result.unmarkVehicle = pressed[4];
    return result;
}



// -------------------------------------------------------
// Small raw-layout helpers
// -------------------------------------------------------

static CVector GetPlayerCoors() {
    CVector result = {};
    FindPlayerCoorsFn(&result);
    return result;
}

static void* FindPlayerEntityRaw() {
    void* vehicle = FindPlayerVehicleFn();
    return vehicle ? vehicle : FindPlayerPedFn();
}

// CEntity contains the CMatrix base starting at +0x04 because of its vtable.
// Matrix vectors: right +0x04, forward +0x14, up +0x24, position +0x34.
static const CVector& EntityRight(void* entity) {
    return *reinterpret_cast<const CVector*>(
        reinterpret_cast<const BYTE*>(entity) + 0x04
    );
}

static const CVector& EntityForward(void* entity) {
    return *reinterpret_cast<const CVector*>(
        reinterpret_cast<const BYTE*>(entity) + 0x14
    );
}

static const CVector& EntityUp(void* entity) {
    return *reinterpret_cast<const CVector*>(
        reinterpret_cast<const BYTE*>(entity) + 0x24
    );
}

static const CVector& EntityPosition(void* entity) {
    return *reinterpret_cast<const CVector*>(
        reinterpret_cast<const BYTE*>(entity) + 0x34
    );
}

static float EntityHeading(void* entity) {
    const CVector& forward = EntityForward(entity);
    return atan2f(-forward.x, forward.y);
}

// CEntity has a vtable at +0x00, while its non-polymorphic CPlaceable/CMatrix
// base subobject begins at +0x04.  CPlaceable::SetHeading expects ECX to point
// at that base subobject, not at the CEntity/CVehicle vtable.
static void SetEntityHeading(void* entity, float heading) {
    if (!entity || !SetHeadingFn)
        return;

    SetHeadingFn(
        reinterpret_cast<BYTE*>(entity) + 0x04,
        heading
    );
}

static CVector TransformFromObjectSpace(
    void* entity,
    const CVector& offset
) {
    const CVector& right = EntityRight(entity);
    const CVector& forward = EntityForward(entity);
    const CVector& up = EntityUp(entity);
    const CVector& pos = EntityPosition(entity);

    CVector out = {};
    out.x = pos.x + right.x * offset.x + forward.x * offset.y + up.x * offset.z;
    out.y = pos.y + right.y * offset.x + forward.y * offset.y + up.y * offset.z;
    out.z = pos.z + right.z * offset.x + forward.z * offset.y + up.z * offset.z;
    return out;
}

// -------------------------------------------------------
// Message display
// -------------------------------------------------------

// Call the game's real AddMessageJumpQ directly so each new ILUtils message
// immediately replaces the currently displayed brief message instead of being
// appended to the normal brief-message queue. The engine stores the text
// pointer, so keep converted strings in persistent rotating buffers.
static wchar_t g_messageBuffers[8][256] = {};
static unsigned int g_messageBufferIndex = 0;

static void ShowMessage(const char* text) {
    if (!text || !AddMessageJumpQFn)
        return;

    wchar_t* wide = g_messageBuffers[g_messageBufferIndex];
    g_messageBufferIndex = (g_messageBufferIndex + 1) % 8;
    wide[0] = L'\0';

    int converted = MultiByteToWideChar(
        CP_ACP,
        0,
        text,
        -1,
        wide,
        256
    );

    if (converted <= 0) {
        size_t i = 0;
        for (; text[i] && i + 1 < 256; ++i)
            wide[i] = static_cast<unsigned char>(text[i]);
        wide[i] = L'\0';
    }

    AddMessageJumpQFn(wide, 2000, 0);
}

// -------------------------------------------------------
// Model / streaming raw access
// -------------------------------------------------------

static const int VEHICLE_SPAWNER_MODEL_LIMIT = 6500;
static const int MODEL_INFO_VEHICLE_RAW = 6;

static void* GetModelInfoRaw(int modelId) {
    if (modelId < 0 || modelId >= VEHICLE_SPAWNER_MODEL_LIMIT)
        return nullptr;

    void** table = reinterpret_cast<void**>(g_addr->ModelInfoTable);
    return table[modelId];
}

static const char* GetModelNameRaw(void* modelInfo) {
    return modelInfo
        ? reinterpret_cast<const char*>(
            reinterpret_cast<const BYTE*>(modelInfo) + 0x04
        )
        : "";
}

static signed char GetModelTypeRaw(void* modelInfo) {
    if (!modelInfo)
        return -1;

    return *reinterpret_cast<const signed char*>(
        reinterpret_cast<const BYTE*>(modelInfo) + 0x19
    );
}

static int GetVehicleTypeRaw(void* modelInfo) {
    if (!modelInfo)
        return -1;

    return *reinterpret_cast<const int*>(
        reinterpret_cast<const BYTE*>(modelInfo) + 0x3C
    );
}

static bool IsVehicleModelInRange(int modelId) {
    void* info = GetModelInfoRaw(modelId);
    return info && GetModelTypeRaw(info) == MODEL_INFO_VEHICLE_RAW;
}

static bool ModelNameEqualsRaw(void* modelInfo, const char* name) {
    if (!modelInfo || !name)
        return false;

    char modelName[22] = {};
    memcpy(modelName, GetModelNameRaw(modelInfo), 21);
    modelName[21] = '\0';
    return _stricmp(modelName, name) == 0;
}

static void* FindModelInfoByNameRaw(const char* name, int* modelId) {
    if (modelId)
        *modelId = -1;

    if (!name || !name[0])
        return nullptr;

    for (int id = 0; id < VEHICLE_SPAWNER_MODEL_LIMIT; ++id) {
        void* info = GetModelInfoRaw(id);
        if (info && ModelNameEqualsRaw(info, name)) {
            if (modelId)
                *modelId = id;
            return info;
        }
    }

    return nullptr;
}

// CStreamingInfo is 0x14 bytes; m_nLoadState is at +0x08.
// LOADSTATE_LOADED = 1.
static bool HasModelLoadedRaw(int modelId) {
    if (modelId < 0 || modelId >= VEHICLE_SPAWNER_MODEL_LIMIT)
        return false;

    const BYTE* info = reinterpret_cast<const BYTE*>(
        g_addr->StreamingInfoTable + modelId * 0x14
    );

    return info[0x08] == 1;
}

// -------------------------------------------------------
// Pool helpers
// -------------------------------------------------------

struct CPoolRaw {
    BYTE* entries;
    BYTE* flags;
    int size;
    int allocPtr;
};

static int GetObjectHandle(void* object) {
    CPoolRaw* pool = *reinterpret_cast<CPoolRaw**>(g_addr->ObjectPool);

    if (!pool || !pool->entries || !pool->flags || !object)
        return -1;

    const uintptr_t objectAddr = reinterpret_cast<uintptr_t>(object);
    const uintptr_t entriesAddr = reinterpret_cast<uintptr_t>(pool->entries);

    if (objectAddr < entriesAddr)
        return -1;

    const uintptr_t byteOffset = objectAddr - entriesAddr;
    static const uintptr_t OBJECT_POOL_STRIDE = 0x1A0;

    if ((byteOffset % OBJECT_POOL_STRIDE) != 0)
        return -1;

    const int index = static_cast<int>(byteOffset / OBJECT_POOL_STRIDE);

    if (index < 0 || index >= pool->size)
        return -1;

    return static_cast<int>(pool->flags[index]) + (index << 8);
}

// -------------------------------------------------------
// Tiny script-command bridge
// -------------------------------------------------------

#pragma pack(push, 4)
struct CRunningScriptRaw {
    void* next;                  // 0x00
    void* prev;                  // 0x04
    char name[8];                // 0x08
    int ip;                      // 0x10
    int stack[6];                // 0x14
    unsigned short sp;           // 0x2C
    unsigned char pad2E[2];      // 0x2E
    uint32_t localVars[16];      // 0x30
    int timers[2];               // 0x70
    bool isActive;               // 0x78
    bool condResult;             // 0x79
    bool useMissionCleanup;      // 0x7A
    bool skipWakeTime;           // 0x7B
    int wakeTime;                // 0x7C
    unsigned short logicalOp;    // 0x80
    bool notFlag;                // 0x82
    bool wastedBustedCheck;      // 0x83
    bool wastedOrBusted;         // 0x84
    bool isMission;              // 0x85
    unsigned char pad86[2];      // 0x86
};
#pragma pack(pop)

static_assert(sizeof(CRunningScriptRaw) == 0x88, "CRunningScriptRaw layout mismatch.");

enum ScriptParamType : BYTE {
    SCRIPTPARAM_STATIC_INT_32BITS = 1,
    SCRIPTPARAM_LOCAL_VARIABLE = 3,
    SCRIPTPARAM_STATIC_FLOAT = 6
};

struct ScriptCode {
    BYTE data[96];
    size_t size;

    explicit ScriptCode(unsigned short command) : data{}, size(0) {
        AddBytes(&command, sizeof(command));
    }

    void AddBytes(const void* src, size_t count) {
        if (size + count > sizeof(data))
            return;

        memcpy(data + size, src, count);
        size += count;
    }

    void AddInt(int value) {
        BYTE type = SCRIPTPARAM_STATIC_INT_32BITS;
        AddBytes(&type, sizeof(type));
        AddBytes(&value, sizeof(value));
    }

    void AddFloat(float value) {
        BYTE type = SCRIPTPARAM_STATIC_FLOAT;
        AddBytes(&type, sizeof(type));
        AddBytes(&value, sizeof(value));
    }

    void AddLocalVar(unsigned short index) {
        BYTE type = SCRIPTPARAM_LOCAL_VARIABLE;
        AddBytes(&type, sizeof(type));
        AddBytes(&index, sizeof(index));
    }
};

static CRunningScriptRaw RunScriptCode(ScriptCode& code) {
    CRunningScriptRaw script = {};

    memcpy(script.name, "ilutils", 7);
    script.wastedBustedCheck = true;
    script.isMission = false;
    script.useMissionCleanup = false;
    script.notFlag = false;

    script.ip = static_cast<int>(
        reinterpret_cast<uintptr_t>(code.data) -
        static_cast<uintptr_t>(g_addr->ScriptSpace)
    );

    ProcessOneCommandFn(&script);
    return script;
}

static void MakeObjectTargettable(void* object) {
    const int handle = GetObjectHandle(object);
    if (handle == -1)
        return;

    // COMMAND_MAKE_OBJECT_TARGETTABLE = 0x035D
    ScriptCode code(0x035D);
    code.AddInt(handle);
    RunScriptCode(code);
}

static void AddOneOffSound(const CVector& position, int soundId) {
    // COMMAND_ADD_ONE_OFF_SOUND = 0x018C
    ScriptCode code(0x018C);
    code.AddFloat(position.x);
    code.AddFloat(position.y);
    code.AddFloat(position.z);
    code.AddInt(soundId);
    RunScriptCode(code);
}

static int CreateCarScript(int modelId, const CVector& position) {
    // COMMAND_CREATE_CAR = 0x00A5
    ScriptCode code(0x00A5);
    code.AddInt(modelId);
    code.AddFloat(position.x);
    code.AddFloat(position.y);
    code.AddFloat(position.z);
    code.AddLocalVar(0);

    CRunningScriptRaw script = RunScriptCode(code);
    return static_cast<int>(script.localVars[0]);
}

static void WarpPlayerIntoCarScript(int vehicleHandle) {
    // COMMAND_WARP_PLAYER_INTO_CAR = 0x0369
    ScriptCode code(0x0369);
    code.AddInt(0); // player index
    code.AddInt(vehicleHandle);
    RunScriptCode(code);
}

// -------------------------------------------------------
// Carbomb / vehicle-retention helpers
// -------------------------------------------------------

static void Rig(void* vehicle) {
    *reinterpret_cast<void**>(
        reinterpret_cast<BYTE*>(vehicle) + 0x210
    ) = FindPlayerPedFn();

    // CVehicle bomb flags byte +0x1FE; low 3 bits are carbomb type.
    // 11 & 7 == 3.
    *reinterpret_cast<BYTE*>(
        reinterpret_cast<BYTE*>(vehicle) + 0x1FE
    ) = 11;
}

static void Unrig(void* vehicle) {
    *reinterpret_cast<void**>(
        reinterpret_cast<BYTE*>(vehicle) + 0x210
    ) = nullptr;

    // 8 & 7 == 0.
    *reinterpret_cast<BYTE*>(
        reinterpret_cast<BYTE*>(vehicle) + 0x1FE
    ) = 8;
}

static BYTE& VehicleCreatedBy(void* vehicle) {
    return *reinterpret_cast<BYTE*>(
        reinterpret_cast<BYTE*>(vehicle) + 0x1F8
    );
}

static void SetVehicleScriptLocked(void* vehicle, bool locked) {
    BYTE& flags = *reinterpret_cast<BYTE*>(
        reinterpret_cast<BYTE*>(vehicle) + 0x1F9
    );

    if (locked)
        flags |= 0x08;
    else
        flags &= static_cast<BYTE>(~0x08);
}

static void ChangeVehicleCreationType(void* vehicle, BYTE type) {
    BYTE& createdBy = VehicleCreatedBy(vehicle);

    if (createdBy == type)
        return;

    // Keep the game's random/mission/parked/permanent totals balanced.
    UpdateCarCountFn(vehicle, 1);
    createdBy = type;
    UpdateCarCountFn(vehicle, 0);
}

static void ReleaseVehicleToTraffic(void* vehicle) {
    // RANDOM_VEHICLE = 1.
    ChangeVehicleCreationType(vehicle, 1);

    // CREATE_CAR sets this script-retention flag independently of CreatedBy.
    SetVehicleScriptLocked(vehicle, false);
    RemoveInterestingVehicleFn(vehicle);
}

static void KeepVehicleAsMission(void* vehicle) {
    // MISSION_VEHICLE = 2.
    ChangeVehicleCreationType(vehicle, 2);
    SetVehicleScriptLocked(vehicle, true);
}


// -------------------------------------------------------
// Native Win32 vehicle-spawner window
// -------------------------------------------------------

#define ID_SPAWNER_EDIT   1001
#define ID_SPAWNER_SPAWN  1002
#define ID_SPAWNER_CANCEL 1003

static volatile LONG g_spawnerWindowOpen = 0;
static volatile LONG g_spawnRequestPending = 0;

static char g_spawnRequest[64] = {};

static HWND g_gameWindow = nullptr;
static HWND g_spawnerWindow = nullptr;
static HWND g_spawnerEdit = nullptr;
static HWND g_spawnerSpawnButton = nullptr;

static WNDPROC g_originalEditProc = nullptr;

static UINT g_spawnerClosedMessage = 0;
static volatile LONG g_spawnerFocusReturnPending = 0;
static volatile LONG g_suppressNextGameFocus = 0;
// JP calls CGame::InitAfterFocusLoss for every WM_ACTIVATE, both when the
// game is deactivated and when it is activated again.  Keep this armed for
// the entire popup activation handoff instead of relying on WM_ACTIVATE.lParam
// (which is unreliable here because the popup runs on another UI thread).
static volatile LONG g_suppressJPActivationForSpawner = 0;
static volatile LONG g_spawnerShuttingDown = 0;
static volatile LONG g_spawnerThreadFinished = 0;
static volatile LONG g_spawnerWindowError = 0;

// These handles and subclass state belong to the game thread. Keep the
// worker handle until it has actually exited; do not detach a live UI thread.
static HANDLE g_spawnerThread = nullptr;static HANDLE g_spawnerStopEvent = nullptr;
static bool g_gameSubclassInstalled = false;
static bool g_spawnerFocusHandled = false;
static UINT_PTR g_spawnerGeneration = 0;
static const UINT_PTR SPAWNER_SUBCLASS_ID = 1;

struct SpawnerThreadContext {
    HWND owner;
    HINSTANCE instance;
    HANDLE stopEvent;
    UINT closedMessage;
    UINT_PTR generation;
};
static SpawnerThreadContext g_spawnerContext = {};

static const char* SPAWNER_WINDOW_CLASS =
"ILUtilsVehicleSpawnerWindow";

static void ShutdownVehicleSpawner();
static LRESULT CALLBACK GameWindowProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);

static void RemoveGameWindowHook() {
    if (g_gameSubclassInstalled) {
        if (IsWindow(g_gameWindow))
            RemoveWindowSubclass(g_gameWindow, GameWindowProc, SPAWNER_SUBCLASS_ID);
        g_gameSubclassInstalled = false;
    }
}

static void FinishSpawnerFocus() {
    if (g_spawnerFocusHandled ||
        !InterlockedCompareExchange(&g_spawnerThreadFinished, 0, 0))
        return;

    g_spawnerFocusHandled = true;
    if (!InterlockedCompareExchange(&g_spawnerShuttingDown, 0, 0) &&
        InterlockedCompareExchange(&g_spawnerFocusReturnPending, 0, 0) &&
        IsWindow(g_gameWindow)) {
        HWND foreground = GetForegroundWindow();
        if (!foreground || foreground == g_gameWindow) {
            if (IsIconic(g_gameWindow))
                ShowWindow(g_gameWindow, SW_RESTORE);
            SetForegroundWindow(g_gameWindow);
            SetFocus(g_gameWindow);
        }
    }

    // SetForegroundWindow/SetFocus above can synchronously deliver JP's
    // WM_ACTIVATE and WM_SETFOCUS.  Do not disarm JP activation suppression
    // until that return-to-game handoff has completed.
    InterlockedExchange(&g_suppressJPActivationForSpawner, 0);
    InterlockedExchange(&g_suppressNextGameFocus, 0);
    InterlockedExchange(&g_spawnerFocusReturnPending, 0);
    // No callback into the ASI remains on an idle game window.
    RemoveGameWindowHook();
}

// Comctl32 maintains the callback chain, including removal during dispatch.
// Install/remove this subclass only on the game window's owning thread.
static LRESULT CALLBACK GameWindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR,
    DWORD_PTR
) {
    if (g_spawnerClosedMessage && msg == g_spawnerClosedMessage) {
        // Ignore a queued notification from an earlier popup session.
        if (wParam == g_spawnerGeneration)
            FinishSpawnerFocus();
        return 0;
    }

    if (msg == WM_CLOSE)
        ShutdownVehicleSpawner();

    // JP differs from INTL here: its WndProc calls CGame::InitAfterFocusLoss()
    // for BOTH sides of WM_ACTIVATE (WA_INACTIVE and WA_ACTIVE/CLICKACTIVE).
    // The popup lives on another UI thread, so WM_ACTIVATE.lParam is not a
    // reliable way to identify the transfer.  Instead OpenVehicleSpawnerWindow
    // arms this flag before the popup thread starts, and FinishSpawnerFocus
    // clears it only after focus has been handed back to GTA.
    if (g_version == VER_JP && msg == WM_ACTIVATE &&
        InterlockedCompareExchange(&g_suppressJPActivationForSpawner, 0, 0)) {
        return 0;
    }

    // During teardown the renderer/audio may already have been shut down.
    if (msg == WM_SETFOCUS &&
        InterlockedCompareExchange(&g_spawnerShuttingDown, 0, 0))
        return 0;

    if (msg == WM_SETFOCUS &&
        InterlockedExchange(&g_suppressNextGameFocus, 0)) {
        DWORD previousProcess = 0;
        if (wParam)
            GetWindowThreadProcessId(reinterpret_cast<HWND>(wParam), &previousProcess);

        // A known external source is a real focus return, not our popup.
        if (!previousProcess || previousProcess == GetCurrentProcessId())
            return 0;
    }

    if (msg == WM_NCDESTROY) {
        InterlockedExchange(&g_spawnerShuttingDown, 1);
        if (g_spawnerStopEvent)
            SetEvent(g_spawnerStopEvent);
        InterlockedExchange(&g_suppressJPActivationForSpawner, 0);
        InterlockedExchange(&g_suppressNextGameFocus, 0);
        InterlockedExchange(&g_spawnerFocusReturnPending, 0);
        RemoveGameWindowHook();
        g_gameWindow = nullptr;
    }

    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static bool InstallGameWindowHook(HWND hwnd) {
    if (g_gameSubclassInstalled)
        return g_gameWindow == hwnd;

    // Install from the game thread, never from the popup thread.
    if (GetWindowThreadProcessId(hwnd, nullptr) != GetCurrentThreadId())
        return false;

    if (!g_spawnerClosedMessage) {
        g_spawnerClosedMessage = RegisterWindowMessageA(
            "ILUtils.VehicleSpawner.Closed"
        );
        if (!g_spawnerClosedMessage)
            return false;
    }

    if (!SetWindowSubclass(hwnd, GameWindowProc, SPAWNER_SUBCLASS_ID, 0))
        return false;

    g_gameSubclassInstalled = true;
    g_gameWindow = hwnd;
    return true;
}

static void CloseSpawnerWindow(HWND hwnd) {
    HWND foreground = GetForegroundWindow();
    const bool returnFocus = !InterlockedCompareExchange(&g_spawnerShuttingDown, 0, 0) &&
        IsWindow(g_spawnerContext.owner) &&
        (foreground == hwnd || (foreground && IsChild(hwnd, foreground)));

    if (returnFocus) {
        // Arm BEFORE destruction: Windows can reactivate the owner inside
        // DestroyWindow, before or during WM_DESTROY.
        InterlockedExchange(&g_spawnerFocusReturnPending, 1);
        InterlockedExchange(&g_suppressNextGameFocus, 1);
    }

    DestroyWindow(hwnd);
    // The worker posts one completion after destroying its windows and
    // unregistering its class, including cancel and startup-failure paths.
}

static LRESULT CALLBACK SpawnerEditProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_RETURN) {
            HWND parent = GetParent(hwnd);

            SendMessageA(
                parent,
                WM_COMMAND,
                MAKEWPARAM(ID_SPAWNER_SPAWN, BN_CLICKED),
                reinterpret_cast<LPARAM>(g_spawnerSpawnButton)
            );

            return 0;
        }

        if (wParam == VK_ESCAPE) {
            PostMessageA(
                GetParent(hwnd),
                WM_CLOSE,
                0,
                0
            );

            return 0;
        }
    }

    return CallWindowProcA(
        g_originalEditProc,
        hwnd,
        msg,
        wParam,
        lParam
    );
}

static LRESULT CALLBACK SpawnerWindowProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
) {
    switch (msg) {
    case WM_CREATE:
    {
        HFONT font = static_cast<HFONT>(
            GetStockObject(DEFAULT_GUI_FONT)
            );

        HWND label = CreateWindowExA(
            0,
            "STATIC",
            "Enter vehicle model name or ID:",
            WS_CHILD | WS_VISIBLE,
            12,
            12,
            260,
            20,
            hwnd,
            nullptr,
            nullptr,
            nullptr
        );

        SendMessageA(
            label,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font),
            TRUE
        );

        g_spawnerEdit = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "EDIT",
            "",
            WS_CHILD |
            WS_VISIBLE |
            WS_TABSTOP |
            ES_AUTOHSCROLL,
            12,
            35,
            260,
            24,
            hwnd,
            reinterpret_cast<HMENU>(ID_SPAWNER_EDIT),
            nullptr,
            nullptr
        );

        if (!g_spawnerEdit)
            return -1;

        // Friendly names may be longer than an internal 20-character IDE key.
        SendMessageA(
            g_spawnerEdit,
            EM_SETLIMITTEXT,
            sizeof(g_spawnRequest) - 1,
            0
        );

        SendMessageA(
            g_spawnerEdit,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font),
            TRUE
        );

        g_spawnerSpawnButton = CreateWindowExA(
            0,
            "BUTTON",
            "Spawn",
            WS_CHILD |
            WS_VISIBLE |
            WS_TABSTOP |
            BS_DEFPUSHBUTTON,
            76,
            72,
            90,
            28,
            hwnd,
            reinterpret_cast<HMENU>(ID_SPAWNER_SPAWN),
            nullptr,
            nullptr
        );

        SendMessageA(
            g_spawnerSpawnButton,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font),
            TRUE
        );

        HWND cancelButton = CreateWindowExA(
            0,
            "BUTTON",
            "Cancel",
            WS_CHILD |
            WS_VISIBLE |
            WS_TABSTOP,
            178,
            72,
            90,
            28,
            hwnd,
            reinterpret_cast<HMENU>(ID_SPAWNER_CANCEL),
            nullptr,
            nullptr
        );

        SendMessageA(
            cancelButton,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(font),
            TRUE
        );

        // Enter = Spawn, Escape = Cancel while focus is in the edit box.
        g_originalEditProc = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrA(
                g_spawnerEdit,
                GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(&SpawnerEditProc)
            )
            );

        SetFocus(g_spawnerEdit);
        return 0;
    }

    case WM_COMMAND:
    {
        const int id = LOWORD(wParam);

        if (InterlockedCompareExchange(&g_spawnerShuttingDown, 0, 0)) {
            CloseSpawnerWindow(hwnd);
            return 0;
        }

        if (id == ID_SPAWNER_SPAWN) {
            char text[64] = {};

            GetWindowTextA(
                g_spawnerEdit,
                text,
                sizeof(text)
            );

            if (!text[0]) {
                MessageBeep(MB_ICONWARNING);
                SetFocus(g_spawnerEdit);
                return 0;
            }

            strcpy_s(
                g_spawnRequest,
                sizeof(g_spawnRequest),
                text
            );

            // Publish the completed string to the GTA game thread.
            InterlockedExchange(
                &g_spawnRequestPending,
                1
            );

            CloseSpawnerWindow(hwnd);
            return 0;
        }

        if (id == ID_SPAWNER_CANCEL) {
            CloseSpawnerWindow(hwnd);
            return 0;
        }

        break;
    }

    case WM_CLOSE:
        CloseSpawnerWindow(hwnd);
        return 0;

    case WM_DESTROY:
        // Child windows can still receive destruction messages here. Keep
        // their subclass procedure valid until the window thread exits.
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(
        hwnd,
        msg,
        wParam,
        lParam
    );
}

static void RunSpawnerWindow(const SpawnerThreadContext& context) {
    if (WaitForSingleObject(context.stopEvent, 0) == WAIT_OBJECT_0)
        return;

    HINSTANCE instance = context.instance;

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SpawnerWindowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = SPAWNER_WINDOW_CLASS;

    if (!RegisterClassExA(&wc)) {
        InterlockedExchange(&g_spawnerWindowError, 1);
        return;
    }

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
        SPAWNER_WINDOW_CLASS,
        "ILUtils Vehicle Spawner",
        WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        300,
        145,
        context.owner,
        nullptr,
        instance,
        nullptr
    );

    if (!hwnd) {
        UnregisterClassA(SPAWNER_WINDOW_CLASS, instance);
        InterlockedExchange(&g_spawnerWindowError, 1);
        return;
    }

    // Publish the popup handle for normal lifetime/focus bookkeeping.  JP's
    // activation suppression itself is armed on the game thread before this
    // worker is started, so it does not depend on this cross-thread handle.
    g_spawnerWindow = hwnd;

    if (WaitForSingleObject(context.stopEvent, 0) != WAIT_OBJECT_0) {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(g_spawnerEdit);
    }

    bool done = false;
    while (!done) {
        const DWORD result = MsgWaitForMultipleObjectsEx(
            1, &context.stopEvent, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE
        );
        if (result != WAIT_OBJECT_0 + 1)
            break;

        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                done = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
            if (WaitForSingleObject(context.stopEvent, 0) == WAIT_OBJECT_0) {
                done = true;
                break;
            }
        }
    }

    if (IsWindow(hwnd))
        CloseSpawnerWindow(hwnd);

    if (g_spawnerWindow == hwnd)
        g_spawnerWindow = nullptr;

    // A class registered by a DLL is not automatically unregistered when
    // that DLL unloads. Never leave its window procedure registered at exit.
    UnregisterClassA(SPAWNER_WINDOW_CLASS, instance);
}

static unsigned __stdcall SpawnerWindowThread(void* parameter) {
    const SpawnerThreadContext context = *static_cast<SpawnerThreadContext*>(parameter);
    RunSpawnerWindow(context);
    g_spawnerWindow = nullptr;
    g_spawnerEdit = nullptr;
    g_spawnerSpawnButton = nullptr;
    g_originalEditProc = nullptr;
    InterlockedExchange(&g_spawnerThreadFinished, 1);
    PostMessageA(context.owner, context.closedMessage, context.generation, 0);
    return 0;
}

static void ReapSpawnerThread() {
    if (!g_spawnerThread || WaitForSingleObject(g_spawnerThread, 0) != WAIT_OBJECT_0)
        return;

    // Also handles a failed PostMessage without leaving suppression armed.
    FinishSpawnerFocus();
    CloseHandle(g_spawnerThread);
    CloseHandle(g_spawnerStopEvent);
    g_spawnerThread = nullptr;
    g_spawnerStopEvent = nullptr;
    InterlockedExchange(&g_spawnerWindowOpen, 0);
}

static void ShutdownVehicleSpawner() {
    // Called by the game thread before RenderWare teardown, never in DllMain.
    static bool inShutdown = false;
    if (inShutdown)
        return;
    inShutdown = true;
    InterlockedExchange(&g_spawnerShuttingDown, 1);
    InterlockedExchange(&g_spawnRequestPending, 0);
    if (g_spawnerStopEvent)
        SetEvent(g_spawnerStopEvent);

    bool hadQuitMessage = false;
    int quitCode = 0;
    while (g_spawnerThread && WaitForSingleObject(g_spawnerThread, 0) == WAIT_TIMEOUT) {
        // Owned windows can send messages to the game thread during their
        // destruction. Pump them while joining to avoid a cross-thread deadlock.
        const DWORD result = MsgWaitForMultipleObjectsEx(
            1, &g_spawnerThread, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE
        );
        if (result == WAIT_FAILED)
            break;
        if (result == WAIT_OBJECT_0 + 1) {
            MSG msg;
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    hadQuitMessage = true;
                    quitCode = static_cast<int>(msg.wParam);
                }
                else {
                    TranslateMessage(&msg);
                    DispatchMessageA(&msg);
                }
            }
        }
    }

    ReapSpawnerThread();
    RemoveGameWindowHook();
    InterlockedExchange(&g_spawnRequestPending, 0);
    InterlockedExchange(&g_suppressJPActivationForSpawner, 0);
    InterlockedExchange(&g_suppressNextGameFocus, 0);
    InterlockedExchange(&g_spawnerFocusReturnPending, 0);
    if (hadQuitMessage)
        PostQuitMessage(quitCode);
    inShutdown = false;
}

static void OpenVehicleSpawnerWindow(HWND owner) {
    ReapSpawnerThread();
    if (InterlockedCompareExchange(&g_spawnerShuttingDown, 0, 0) || g_spawnerThread)
        return;

    if (InterlockedCompareExchange(
        &g_spawnerWindowOpen,
        1,
        0
    ) != 0) {
        return;
    }

    // Do not open another prompt until the previous request
    // has been consumed by the game thread.
    if (InterlockedCompareExchange(
        &g_spawnRequestPending,
        0,
        0
    ) != 0 || InterlockedCompareExchange(&g_spawnerFocusReturnPending, 0, 0) != 0) {
        InterlockedExchange(
            &g_spawnerWindowOpen,
            0
        );
        return;
    }

    if (!owner || !IsWindow(owner)) {
        InterlockedExchange(
            &g_spawnerWindowOpen,
            0
        );

        ShowMessage(
            "Unable to open vehicle spawner"
        );
        return;
    }

    if (!InstallGameWindowHook(owner)) {
        InterlockedExchange(&g_spawnerWindowOpen, 0);
        ShowMessage("Unable to install spawner focus hook");
        return;
    }

    // Arm this on the GTA/game thread BEFORE the worker creates or shows its
    // window.  JP's first WM_ACTIVATE can arrive as soon as the popup becomes
    // active, and its lParam cannot be trusted across these two UI threads.
    if (g_version == VER_JP)
        InterlockedExchange(&g_suppressJPActivationForSpawner, 1);

    HMODULE module = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCSTR>(&SpawnerWindowThread), &module);
    g_spawnerStopEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (module && g_spawnerStopEvent) {
        ++g_spawnerGeneration;
        g_spawnerFocusHandled = false;
        InterlockedExchange(&g_spawnerThreadFinished, 0);
        InterlockedExchange(&g_spawnerWindowError, 0);
        g_spawnerContext = { g_gameWindow, module, g_spawnerStopEvent,
            g_spawnerClosedMessage, g_spawnerGeneration };
        g_spawnerThread = reinterpret_cast<HANDLE>(_beginthreadex(
            nullptr, 0, SpawnerWindowThread, &g_spawnerContext, 0, nullptr
        ));
    }

    if (!g_spawnerThread) {
        if (g_spawnerStopEvent)
            CloseHandle(g_spawnerStopEvent);
        g_spawnerStopEvent = nullptr;
        InterlockedExchange(&g_suppressJPActivationForSpawner, 0);
        RemoveGameWindowHook();
        InterlockedExchange(
            &g_spawnerWindowOpen,
            0
        );

        ShowMessage(
            "Unable to open vehicle spawner"
        );
        return;
    }

}

// Friendly names are independent of IDE filenames (firetruck vs firetruk).
// IDs 130 through 236, in model order. Numbered racers stay distinguishable.
static const char* const VEHICLE_DISPLAY_NAMES[] = {
    "Landstalker", "Idaho", "Stinger", "Linerunner", "Perennial", "Sentinel",
    "Rio", "Firetruck", "Trashmaster", "Stretch", "Manana", "Infernus",
    "Voodoo", "Pony", "Mule", "Cheetah", "Ambulance", "FBI Washington",
    "Moonbeam", "Esperanto", "Taxi", "Washington", "Bobcat", "Mr. Whoopee",
    "BF Injection", "Hunter", "Police", "Enforcer", "Securicar", "Banshee",
    "Predator", "Bus", "Rhino", "Barracks OL", "Cuban Hermes", "Chopper",
    "Angel", "Coach", "Cabbie", "Stallion", "Rumpo", "RC Bandit",
    "Romero's Hearse", "Packer", "Sentinel XS", "Admiral", "Squalo", "Sea Sparrow",
    "Pizza Boy", "Gang Burrito", "Airtrain", "Dead Dodo", "Speeder", "Reefer",
    "Tropic", "Flatbed", "Yankee", "Caddy", "Zebra Cab", "Top Fun",
    "Skimmer", "PCJ-600", "Faggio", "Freeway", "RC Baron", "RC Raider",
    "Glendale", "Oceanic", "Sanchez", "Sparrow", "Patriot", "Love Fist",
    "Coast Guard", "Dinghy", "Hermes", "Sabre", "Sabre Turbo", "Phoenix",
    "Walton", "Regina", "Comet", "Deluxo", "Burrito", "Spand Express",
    "Marquis", "Baggage Handler", "Kaufman Cab", "Maverick", "VCN Maverick", "Rancher",
    "FBI Rancher", "Virgo", "Greenwood", "Cuban Jetmax", "Hotring Racer 1", "Sandking",
    "Blista Compact", "Police Maverick", "Boxville", "Benson", "Mesa Grande", "RC Goblin",
    "Hotring Racer 2", "Hotring Racer 3", "Bloodring Banger 1", "Bloodring Banger 2",
    "Vice Squad Cheetah"
};
static_assert(sizeof(VEHICLE_DISPLAY_NAMES) / sizeof(VEHICLE_DISPLAY_NAMES[0]) == 107,
    "Vehicle name table must cover models 130 through 236");

struct VehicleAlias { int modelId; const char* name; };
static const VehicleAlias VEHICLE_ALIASES[] = {
    {137, "fire engine"}, {153, "mr whoopee"}, {163, "barracks"},
    {172, "romero"}, {172, "romero hearse"}, {188, "zebra taxi"},
    {191, "pcj"}, {201, "love fist limo"}, {215, "baggage"},
    {216, "kaufman taxi"}, {223, "jetmax"},
    {224, "hotring"}, {224, "hotring1"}, {232, "hotring2"}, {233, "hotring3"},
    {234, "bloodring1"}, {235, "bloodring2"},
    {236, "vice cheetah"}, {236, "undercover cheetah"}, {236, "police cheetah"}
};

enum class VehicleMatch { Found, Unknown, Ambiguous };

static void NormalizeVehicleName(const char* source, size_t limit, char(&result)[64]) {
    size_t out = 0;

    for (size_t i = 0; i < limit && source[i] && out < sizeof(result) - 1; ++i) {
        unsigned char ch = static_cast<unsigned char>(source[i]);

        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';

        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))
            result[out++] = static_cast<char>(ch);
    }

    result[out] = '\0';
}

static int VehicleNameDistance(const char* a, const char* b) {
    // Edit distance with adjacent transpositions: "infrenus" -> "infernus".
    // Inputs have already been bounded and normalized to at most 63 bytes.
    const size_t an = strlen(a);
    const size_t bn = strlen(b);

    int rows[3][64] = {};

    for (size_t j = 0; j <= bn; ++j)
        rows[0][j] = static_cast<int>(j);

    for (size_t i = 1; i <= an; ++i) {
        int* row = rows[i % 3];
        const int* previous = rows[(i - 1) % 3];

        row[0] = static_cast<int>(i);

        for (size_t j = 1; j <= bn; ++j) {
            int cost = previous[j - 1] + (a[i - 1] != b[j - 1]);

            if (previous[j] + 1 < cost)
                cost = previous[j] + 1;

            if (row[j - 1] + 1 < cost)
                cost = row[j - 1] + 1;

            if (i > 1 &&
                j > 1 &&
                a[i - 1] == b[j - 2] &&
                a[i - 2] == b[j - 1] &&
                rows[(i - 2) % 3][j - 2] + 1 < cost) {

                cost = rows[(i - 2) % 3][j - 2] + 1;
            }

            row[j] = cost;
        }
    }

    return rows[an % 3][bn];
}

struct VehicleNameMatches {
    int modelId = -1;
    int score = INT_MAX;
    bool ambiguous = false;

    void Consider(
        const char* query,
        const char* name,
        size_t nameLimit,
        int id
    ) {
        char key[64];
        NormalizeVehicleName(name, nameLimit, key);

        if (!key[0])
            return;

        const size_t length = strlen(query);
        const size_t candidateLength = strlen(key);

        int candidateScore = INT_MAX;

        if (strcmp(query, key) == 0) {
            candidateScore = 0;
        }
        else if (length >= 2 && strncmp(key, query, length) == 0) {
            candidateScore = 1;
        }
        else if (length >= 4 && candidateLength >= 4) {
            const int maxEdits = length >= 8 ? 2 : 1;

            if (abs(
                static_cast<int>(length) -
                static_cast<int>(candidateLength)
            ) <= maxEdits) {

                const int distance =
                    VehicleNameDistance(query, key);

                if (distance <= maxEdits)
                    candidateScore = 10 + distance;
            }
        }

        if (candidateScore == INT_MAX)
            return;

        if (candidateScore < score) {
            modelId = id;
            score = candidateScore;
            ambiguous = false;
        }
        else if (candidateScore == score && modelId != id) {
            ambiguous = true;
        }
    }
};

static VehicleMatch ResolveVehicleModel(
    const char* input,
    int& modelId
) {
    modelId = -1;

    if (!input)
        return VehicleMatch::Unknown;

    // Trim outside whitespace before recognizing numeric IDs.
    while (*input && std::isspace(static_cast<unsigned char>(*input)))
        ++input;

    char text[64];
    size_t length = 0;

    while (input[length] && length < sizeof(text) - 1) {
        text[length] = input[length];
        ++length;
    }

    if (input[length])
        return VehicleMatch::Unknown; // Never silently truncate.

    while (length &&
           std::isspace(static_cast<unsigned char>(text[length - 1]))) {
        --length;
    }

    text[length] = '\0';

    if (!length)
        return VehicleMatch::Unknown;

    // Numeric model ID first.
    char* end = nullptr;
    errno = 0;

    long value = strtol(
        text,
        &end,
        0
    );

    if (end != text && *end == '\0') {
        if (errno == ERANGE ||
            value < 0 ||
            value > INT_MAX ||
            value >= VEHICLE_SPAWNER_MODEL_LIMIT) {
            return VehicleMatch::Unknown;
        }

        if (!IsVehicleModelInRange(static_cast<int>(value)))
            return VehicleMatch::Unknown;

        modelId = static_cast<int>(value);
        return VehicleMatch::Found;
    }

    // Malformed numeric text must not be fuzzy-matched to a name.
    if ((text[0] >= '0' && text[0] <= '9') ||
        text[0] == '+' ||
        text[0] == '-') {
        return VehicleMatch::Unknown;
    }

    // Exact internal IDE name lookup.
    if (length <= 20) {
        int exactId = -1;
        void* info = FindModelInfoByNameRaw(text, &exactId);

        if (info && IsVehicleModelInRange(exactId)) {
            modelId = exactId;
            return VehicleMatch::Found;
        }
    }

    char query[64];
    NormalizeVehicleName(text, sizeof(text), query);

    if (!query[0])
        return VehicleMatch::Unknown;

    VehicleNameMatches matches;

    // Include live IDE names so installed custom models keep working too.
    for (int id = 0; id < VEHICLE_SPAWNER_MODEL_LIMIT; ++id) {
        void* info = GetModelInfoRaw(id);

        if (info && GetModelTypeRaw(info) == MODEL_INFO_VEHICLE_RAW) {
            matches.Consider(
                query,
                GetModelNameRaw(info),
                21,
                id
            );
        }
    }

    for (size_t i = 0;
         i < sizeof(VEHICLE_DISPLAY_NAMES) / sizeof(VEHICLE_DISPLAY_NAMES[0]);
         ++i) {

        const int id = 130 + static_cast<int>(i);

        if (IsVehicleModelInRange(id)) {
            matches.Consider(
                query,
                VEHICLE_DISPLAY_NAMES[i],
                64,
                id
            );
        }
    }

    for (const auto& alias : VEHICLE_ALIASES) {
        if (IsVehicleModelInRange(alias.modelId)) {
            matches.Consider(
                query,
                alias.name,
                64,
                alias.modelId
            );
        }
    }

    if (matches.modelId < 0)
        return VehicleMatch::Unknown;

    if (matches.ambiguous)
        return VehicleMatch::Ambiguous;

    modelId = matches.modelId;
    return VehicleMatch::Found;
}

static bool IsForbiddenSpawnModel(int modelId) {
    // This spawner always warps Tommy into the driver seat. These models
    // are scenery/special aircraft or remote-control vehicles, not suitable
    // for that path. Resolve first so names and numeric IDs share this check.
    switch (modelId) {
    case 165:  // chopper: special police helicopter (not polmav)
    case 171:  // rcbandit
    case 180:  // airtrain
    case 181:  // deaddodo
    case 194:  // rcbaron
    case 195:  // rcraider
    case 231:  // rcgoblin
        return true;

    default:
        break;
    }

    // CREATE_CAR constructs a boat, bike or automobile. Other IDE types
    // use incompatible model hierarchies. VC's drivable helicopters and
    // Skimmer are represented through the automobile path.
    void* info = GetModelInfoRaw(modelId);
    const int type = GetVehicleTypeRaw(info);

    // eVehicleType: AUTOMOBILE=0, BOAT=1, BIKE=5.
    return type != 0 && type != 1 && type != 5;
}



// -------------------------------------------------------
// Vehicle spawning
// -------------------------------------------------------

static void SpawnVehicle(int modelId) {
    // Validate at the creation boundary before streaming or moving Tommy.
    if (!IsVehicleModelInRange(modelId)) {
        ShowMessage("Invalid vehicle model");
        return;
    }

    if (IsForbiddenSpawnModel(modelId)) {
        ShowMessage("Forbidden vehicle: cannot spawn and enter this model");
        return;
    }

    void* player = FindPlayerPedFn();

    if (!player)
        return;

    // The popup is only opened on foot. Keep this guard in case a mission
    // changes the player's state while the native window is open.
    if (FindPlayerVehicleFn()) {
        ShowMessage("You must be on foot to spawn a vehicle");
        return;
    }

    void* playerEntity = FindPlayerEntityRaw();

    if (!playerEntity)
        return;
    const float playerHeading = EntityHeading(playerEntity);

    const CVector spawnPos = TransformFromObjectSpace(
        playerEntity,
        CVector{ 0.0f, 4.0f, 0.5f }
    );

    RequestModelFn(modelId, 0);
    LoadAllRequestedModelsFn(false);

    if (!HasModelLoadedRaw(modelId)) {
        SetModelIsDeletableFn(modelId);
        ShowMessage("Failed to load vehicle model");
        return;
    }

    const int vehicleHandle =
        CreateCarScript(modelId, spawnPos);

    if (vehicleHandle < 0) {
        SetModelIsDeletableFn(modelId);
        ShowMessage("Failed to create vehicle");
        return;
    }

    // Do not reconstruct a CVehicle* from the raw pool.  CREATE_CAR gives us
    // a script handle, and WARP_PLAYER_INTO_CAR resolves that handle through
    // the game's own vehicle pool.  After the warp, FindPlayerVehicle returns
    // the exact live object pointer for this executable/version.
    WarpPlayerIntoCarScript(vehicleHandle);

    void* vehicle = FindPlayerVehicleFn();

    if (!vehicle) {
        SetModelIsDeletableFn(modelId);
        ShowMessage("Failed to enter spawned vehicle");
        return;
    }

    SetEntityHeading(vehicle, playerHeading);
    UpdateRwFrameFn(vehicle);

    // Let normal distance/visibility cleanup handle the spawned vehicle
    // after Tommy leaves it. F7 can explicitly keep it as a mission vehicle.
    ReleaseVehicleToTraffic(vehicle);

    SetModelIsDeletableFn(modelId);

    char msg[128];
    sprintf_s(
        msg,
        sizeof(msg),
        "Spawned vehicle model %d",
        modelId
    );

    ShowMessage(msg);
}

static void ProcessVehicleSpawnRequest() {
    // Wait for popup destruction and focus handoff to finish first.
    if (InterlockedCompareExchange(&g_spawnerWindowOpen, 0, 0) != 0 ||
        InterlockedCompareExchange(&g_spawnerFocusReturnPending, 0, 0) != 0 ||
        InterlockedCompareExchange(&g_spawnRequestPending, 0, 0) == 0) {
        return;
    }

    char input[64] = {};

    strcpy_s(
        input,
        sizeof(input),
        g_spawnRequest
    );

    InterlockedExchange(
        &g_spawnRequestPending,
        0
    );

    int modelId = -1;

    const VehicleMatch match =
        ResolveVehicleModel(input, modelId);

    if (match == VehicleMatch::Ambiguous) {
        ShowMessage(
            "Ambiguous vehicle name: use a longer name or model ID"
        );
        return;
    }

    if (match != VehicleMatch::Found) {
        char msg[128];

        sprintf_s(
            msg,
            sizeof(msg),
            "Unknown vehicle: %s",
            input
        );

        ShowMessage(msg);
        return;
    }

    SpawnVehicle(modelId);
}

static bool IsCurrentProcessWindow(HWND hwnd) {
    if (!hwnd)
        return false;

    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);

    return processId == GetCurrentProcessId();
}

static void ProcessVehicleSpawner() {
    ReapSpawnerThread();

    if (InterlockedExchange(
        &g_spawnerWindowError,
        0
    )) {
        ShowMessage("Unable to open vehicle spawner");
    }

    ProcessVehicleSpawnRequest();

    static bool spawnerKeyWasDown = false;

    const bool down =
        (GetAsyncKeyState(g_keyCarSpawner) & 0x8000) != 0;

    HWND foreground = GetForegroundWindow();

    if (down &&
        !spawnerKeyWasDown &&
        FindPlayerPedFn() &&
        !FindPlayerVehicleFn() &&
        IsCurrentProcessWindow(foreground)) {

        OpenVehicleSpawnerWindow(foreground);
    }

    spawnerKeyWasDown = down;
}



// -------------------------------------------------------
// Barrel spawning
// -------------------------------------------------------

static void SpawnBarrel() {
    RequestModelFn(350, 0);
    LoadAllRequestedModelsFn(false);

    void* barrel = ObjectNewFn(0x194);

    if (!barrel) {
        ShowMessage("Failed to spawn barrel");
        return;
    }

    ObjectCtorFn(barrel, 350, false);

    const CVector playerPos = GetPlayerCoors();

    *reinterpret_cast<CVector*>(
        reinterpret_cast<BYTE*>(barrel) + 0x34
    ) = playerPos;

    WorldAddFn(barrel);

    // CObject::m_nObjectType +0x16C, OBJECT_MISSION = 2.
    *reinterpret_cast<BYTE*>(
        reinterpret_cast<BYTE*>(barrel) + 0x16C
    ) = 2;

    MakeObjectTargettable(barrel);
    AddOneOffSound(playerPos, 16);

    ShowMessage("Explosive barrel spawned");
}

// -------------------------------------------------------
// Main per-frame logic
// -------------------------------------------------------

static int s_keyPressTime = 0;

static void OnProcessScripts() {
    if (InterlockedCompareExchange(
        &g_spawnerShuttingDown,
        0,
        0
    )) {
        return;
    }

    ProcessVehicleSpawner();

    void* ped = FindPlayerPedFn();

    if (!ped)
        return;

    const ActionKeys keys = ReadActionKeys();
    const int timer = *GameTimer;

    // Barrel spawner.
    if (keys.spawnBarrel &&
        timer - s_keyPressTime > 500) {

        s_keyPressTime = timer;
        SpawnBarrel();
    }

    // Carbomb rigger.
    if (keys.rigBomb &&
        timer - s_keyPressTime > 500) {

        s_keyPressTime = timer;

        void* vehicle = FindPlayerVehicleFn();

        if (vehicle) {
            Rig(vehicle);

            RequestModelFn(291, 0);
            LoadAllRequestedModelsFn(false);

            // WEAPONTYPE_DETONATOR = 34.
            GiveWeaponFn(
                ped,
                34,
                1,
                false
            );

            AddOneOffSound(
                GetPlayerCoors(),
                16
            );

            ShowMessage("Car bomb armed");
        }
        else {
            ShowMessage(
                "You must be inside a vehicle to rig the car bomb"
            );
        }
    }

    // Unrig carbomb.
    if (keys.unrigBomb &&
        timer - s_keyPressTime > 500) {

        s_keyPressTime = timer;

        void* vehicle = FindPlayerVehicleFn();

        if (vehicle) {
            Unrig(vehicle);

            AddOneOffSound(
                GetPlayerCoors(),
                10
            );

            ShowMessage("Car bomb removed");
        }
        else {
            ShowMessage(
                "You must be inside a vehicle to remove the car bomb"
            );
        }
    }

    // Mark current vehicle as mission-created.
    if (keys.markVehicle) {
        void* vehicle = FindPlayerVehicleFn();

        if (vehicle) {
            KeepVehicleAsMission(vehicle);

            AddOneOffSound(
                GetPlayerCoors(),
                57
            );

            ShowMessage(
                "Vehicle marked as mission vehicle"
            );
        }
        else {
            ShowMessage(
                "You must be inside a vehicle to mark it"
            );
        }
    }

    // Release current vehicle back to random traffic cleanup.
    if (keys.unmarkVehicle) {
        void* vehicle = FindPlayerVehicleFn();

        if (vehicle) {
            ReleaseVehicleToTraffic(vehicle);

            AddOneOffSound(
                GetPlayerCoors(),
                61
            );

            ShowMessage(
                "Vehicle marked as random vehicle"
            );
        }
        else {
            ShowMessage(
                "You must be inside a vehicle to unmark it"
            );
        }
    }
}

// -------------------------------------------------------
// ProcessScripts hook
// -------------------------------------------------------

static BYTE s_processScriptsOrigBytes[5];

static void __cdecl HookedProcessScripts() {
    ProcessScriptsOriginal();
    OnProcessScripts();
}

static bool PatchCall(
    DWORD callSite,
    void* newFunction,
    BYTE* savedBytes
) {
    if (*reinterpret_cast<BYTE*>(callSite) != 0xE8)
        return false;

    DWORD oldProtect = 0;

    if (!VirtualProtect(
        reinterpret_cast<void*>(callSite),
        5,
        PAGE_EXECUTE_READWRITE,
        &oldProtect
    )) {
        return false;
    }

    memcpy(
        savedBytes,
        reinterpret_cast<void*>(callSite),
        5
    );

    const DWORD newRel =
        static_cast<DWORD>(
            reinterpret_cast<uintptr_t>(newFunction)
        ) - (callSite + 5);

    BYTE patch[5] = {
        0xE8, 0, 0, 0, 0
    };

    memcpy(
        patch + 1,
        &newRel,
        sizeof(newRel)
    );

    memcpy(
        reinterpret_cast<void*>(callSite),
        patch,
        5
    );

    FlushInstructionCache(
        GetCurrentProcess(),
        reinterpret_cast<void*>(callSite),
        5
    );

    DWORD dummy = 0;

    VirtualProtect(
        reinterpret_cast<void*>(callSite),
        5,
        oldProtect,
        &dummy
    );

    return true;
}

static void InitFunctionPointers() {
    FindPlayerPedFn =
        reinterpret_cast<FindPlayerPed_t>(
            g_addr->FindPlayerPed
        );

    FindPlayerVehicleFn =
        reinterpret_cast<FindPlayerVehicle_t>(
            g_addr->FindPlayerVehicle
        );

    FindPlayerCoorsFn =
        reinterpret_cast<FindPlayerCoors_t>(
            g_addr->FindPlayerCoors
        );

    RequestModelFn =
        reinterpret_cast<RequestModel_t>(
            g_addr->RequestModel
        );

    LoadAllRequestedModelsFn =
        reinterpret_cast<LoadAllRequestedModels_t>(
            g_addr->LoadAllRequestedModels
        );

    SetModelIsDeletableFn =
        reinterpret_cast<SetModelIsDeletable_t>(
            g_addr->SetModelIsDeletable
        );

    ObjectNewFn =
        reinterpret_cast<ObjectNew_t>(
            g_addr->ObjectNew
        );

    ObjectCtorFn =
        reinterpret_cast<ObjectCtor_t>(
            g_addr->ObjectCtor
        );

    WorldAddFn =
        reinterpret_cast<WorldAdd_t>(
            g_addr->WorldAdd
        );

    GiveWeaponFn =
        reinterpret_cast<GiveWeapon_t>(
            g_addr->GiveWeapon
        );

    AddMessageJumpQFn =
        reinterpret_cast<AddMessageJumpQ_t>(
            g_addr->AddMessageJumpQ
        );

    UpdateCarCountFn =
        reinterpret_cast<UpdateCarCount_t>(
            g_addr->UpdateCarCount
        );

    RemoveInterestingVehicleFn =
        reinterpret_cast<RemoveInterestingVehicle_t>(
            g_addr->RemoveInterestingVehicle
        );

    SetHeadingFn =
        reinterpret_cast<SetHeading_t>(
            g_addr->SetHeading
        );

    UpdateRwFrameFn =
        reinterpret_cast<UpdateRwFrame_t>(
            g_addr->UpdateRwFrame
        );

    ProcessOneCommandFn =
        reinterpret_cast<ProcessOneCommand_t>(
            g_addr->ProcessOneCommand
        );

    ProcessScriptsOriginal =
        reinterpret_cast<ProcessScripts_t>(
            g_addr->ProcessScripts
        );

    GameTimer =
        reinterpret_cast<int*>(
            g_addr->GameTimer
        );
}

// -------------------------------------------------------
// Entry point
// -------------------------------------------------------

BOOL APIENTRY DllMain(
    HMODULE hModule,
    DWORD reason,
    LPVOID
) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        g_version = DetectVersion();

        switch (g_version) {
        case VER_INTL_10:
            g_addr = &kAddr_INTL;
            break;

        case VER_JP:
            g_addr = &kAddr_JP;
            break;

        default:
            MessageBoxA(
                nullptr,
                "ILUtils: unsupported GTA Vice City executable.\n"
                "Only INTL 1.0 and Japanese are supported.",
                "ILUtils",
                MB_OK | MB_ICONERROR
            );
            return FALSE;
        }

        InitFunctionPointers();
        LoadConfig();

        if (!PatchCall(
            g_addr->HookSiteProcessScripts,
            reinterpret_cast<void*>(
                &HookedProcessScripts
            ),
            s_processScriptsOrigBytes
        )) {
            MessageBoxA(
                nullptr,
                "ILUtils: failed to install ProcessScripts hook.",
                "ILUtils",
                MB_OK | MB_ICONERROR
            );
            return FALSE;
        }
    }

    return TRUE;
}