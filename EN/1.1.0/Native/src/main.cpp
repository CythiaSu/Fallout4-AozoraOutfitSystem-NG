#include <cstdio>
#include <string>
#include <string_view>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <cctype>
#include <utility>
#include <algorithm>
#include <random>
#include <array>
#include <cstdlib>
#include <thread>
#include <optional>
#include <cmath>
#include <tuple>
#include <functional>
#include <unordered_set>
#include <limits>
#include <atomic>
#include <chrono>

#include "RE/Fallout.h"
#include "F4SE/F4SE.h"
#include "F4SE/API.h"

#include <windows.h>
#include "PrismaUI_F4_API.h"

#define OUTFITMANAGER_EXPORT extern "C" [[maybe_unused]] __declspec(dllexport)

namespace fs = std::filesystem;

OUTFITMANAGER_EXPORT F4SE::PluginVersionData F4SEPlugin_Version = []() noexcept {
    F4SE::PluginVersionData v{};
    v.PluginVersion({ 1, 1, 2, 0 });
    v.PluginName("OutfitManager");
    v.AuthorName("OutfitManager Author");
    v.UsesAddressLibrary(true);
    v.UsesAddressLibraryNG(true);
    v.UsesSigScanning(false);
    v.IsLayoutDependent(true);
    v.IsLayoutDependentNG(true);
    v.HasNoStructUse(false);
    v.CompatibleVersions({
        F4SE::RUNTIME_1_10_162, F4SE::RUNTIME_1_10_163,
        F4SE::RUNTIME_1_10_980, F4SE::RUNTIME_1_10_984,
        F4SE::RUNTIME_1_11_137, F4SE::RUNTIME_1_11_159,
        F4SE::RUNTIME_1_11_169, F4SE::RUNTIME_1_11_191,
        F4SE::RUNTIME_1_11_221, F4SE::RUNTIME_1_11_240
    });
    return v;
}();

constexpr int MAX_SLOTS = 500;
constexpr const char* DATA_DIR = "F4SE/Plugins/OutfitManager";
constexpr const char* MENU_HTML = "OutfitManager/menu.html";

static fs::path PrimaryDataRoot();
static std::string BuildUiTuningJson();
static std::string ReadUiLayoutJson();
static void ReloadPreviewTuning();
static std::string EscapeJavaScriptString(std::string_view text);
static void SendUiResult(std::string_view kind, bool ok, int slot, std::string_view message, std::string_view extra);

static void LogLine(const std::string& line) {
    try {
        static std::mutex logMutex;
        static std::ofstream logFile;
        std::lock_guard<std::mutex> lock(logMutex);
        if (!logFile.is_open()) {
            std::error_code ec;
            fs::create_directories(PrimaryDataRoot(), ec);
            logFile.open(PrimaryDataRoot() / "OutfitManager.log", std::ios::app);
        }
        if (logFile.is_open()) {
            logFile << line << '\n';
            logFile.flush();
        }
    } catch (...) {
    }
}

namespace json {
    inline std::string esc(std::string_view s) {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size());
        for (const unsigned char c : s) {
            switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += hex[(c >> 4) & 0x0F];
                    out += hex[c & 0x0F];
                } else {
                    out += static_cast<char>(c);
                }
                break;
            }
        }
        return out;
    }
    inline std::string getStr(std::string_view j, std::string_view k) {
        auto s = "\"" + std::string(k) + "\"";
        auto p = j.find(s);
        if (p == std::string_view::npos) return {};
        p = j.find(':', p + s.size());
        if (p == std::string_view::npos) return {};
        ++p;
        while (p < j.size() && std::isspace(static_cast<unsigned char>(j[p]))) ++p;
        if (p >= j.size() || j[p] != '"') return {};
        ++p;
        std::string out;
        for (; p < j.size(); ++p) {
            const char c = j[p];
            if (c == '"') return out;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (++p >= j.size()) return {};
            switch (j[p]) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                if (p + 4 >= j.size()) return {};
                unsigned value = 0;
                for (int digit = 0; digit < 4; ++digit) {
                    const char hex = j[p + 1 + digit];
                    value <<= 4;
                    if (hex >= '0' && hex <= '9') value += static_cast<unsigned>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') value += static_cast<unsigned>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') value += static_cast<unsigned>(hex - 'A' + 10);
                    else return {};
                }
                if (value > 0x7F) return {};
                out += static_cast<char>(value);
                p += 4;
                break;
            }
            default: return {};
            }
        }
        return {};
    }
    inline int getInt(std::string_view j, std::string_view k, int d = 0) {
        auto s = "\"" + std::string(k) + "\"";
        auto p = j.find(s);
        if (p == std::string_view::npos) return d;
        p = j.find(':', p + s.size());
        if (p == std::string_view::npos) return d;
        ++p;
        try { return std::stoi(std::string(j.substr(p))); } catch (...) { return d; }
    }
    inline uint32_t getHexID(std::string_view j, std::string_view k, uint32_t d = 0) {
        auto value = getStr(j, k);
        if (value.empty()) return d;
        try { return static_cast<uint32_t>(std::stoul(value, nullptr, 16)); } catch (...) { return d; }
    }
}

static bool WriteTextFileAtomic(const fs::path& path, std::string_view content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    const auto tempPath = fs::path(path.wstring() + L".tmp");
    const auto backupPath = fs::path(path.wstring() + L".bak");
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;
        file.write(content.data(), static_cast<std::streamsize>(content.size()));
        file.flush();
        if (!file.good()) {
            file.close();
            fs::remove(tempPath, ec);
            return false;
        }
    }

    const auto target = path.wstring();
    const auto temp = tempPath.wstring();
    const auto backup = backupPath.wstring();
    bool replaced = false;
    if (fs::exists(path, ec) && !ec) {
        replaced = ReplaceFileW(
            target.c_str(),
            temp.c_str(),
            backup.c_str(),
            REPLACEFILE_IGNORE_MERGE_ERRORS,
            nullptr,
            nullptr) != FALSE;
    }
    if (!replaced) {
        replaced = MoveFileExW(
            temp.c_str(),
            target.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!replaced) {
        LogLine("2.0 atomic write failed path=" + path.string() +
            " error=" + std::to_string(GetLastError()));
        fs::remove(tempPath, ec);
    }
    return replaced;
}

static std::mutex g_mx;
static int g_curSlot = 1, g_lastRandSlot = 0, g_lastEquippedSlot = 0;
static bool g_eqBusy = false, g_randBusy = false;
static ULONGLONG g_eqBusySinceMs = 0, g_randBusySinceMs = 0;
static float g_eqLastT = 0, g_randLastT = 0;
static float g_menuOpenT = 0, g_menuActionT = 0;
static int g_menuAct = 0, g_menuActSlot = 0;
static PRISMA_UI_API::IVPrismaUI10* g_prisma = nullptr;
static PrismaView g_view = 0;
static PrismaView g_focusAttemptedView = 0;
static bool g_menuOpen = false;
static bool g_hiddenBehindGameMenu = false;
static bool g_quickSaveMode = false;
static bool g_quickOutfitMode = false;
static int g_quickOutfitPage = 0;
static bool g_directStudioMode = false;
static int g_quickSaveSlot = 0;
static bool g_viewReady = false;
static bool g_menuPresentationPending = false;
static bool g_firstFullMenuOpen = true;
static std::mutex g_viewScriptMx;
static std::string g_pendingMenuStateScript;
static bool g_cacheLoaded = false;
static std::array<std::string, MAX_SLOTS + 1> g_slotNames{};
static RE::ObjectRefHandle g_menuTarget;
static RE::ObjectRefHandle g_menuActionTarget;
static RE::ObjectRefHandle g_lockedNpcTarget;
static std::atomic_uint32_t g_lockedNpcSerial{ 0 };
static bool g_lockedNpcWasRestrained = false;
static RE::NiPoint3 g_lockedNpcPosition{};
static float g_lockedNpcHeading = 0.0F;
static float g_lockedNpcOriginalHeading = 0.0F;
static float g_lockedNpcCameraHeading = 0.0F;
static bool g_lockedNpcTransformValid = false;
static std::uint8_t g_lockedNpcStopBursts = 0;
struct InputIsolationSession {
    bool active = false;
    bool playerControlsWasBlocked = false;
};
static InputIsolationSession g_inputIsolation;
static bool g_timePaused = false;
static float g_previousTimeMult = 1.0f;
static bool g_previousMainFreezeTime = false;
static bool g_mainFreezeCaptured = false;
static bool g_hudVisibilityCaptured = false;
static bool g_hudWasVisible = true;
static std::string g_uiLayoutProfile = "standard";
using ClipCursorFunction = BOOL(WINAPI*)(const RECT*);
static std::atomic<ClipCursorFunction> g_originalClipCursor{ nullptr };

static BOOL WINAPI AEClipCursorHook(const RECT* requestedRect) noexcept {
    const auto original = g_originalClipCursor.load(std::memory_order_acquire);
    if (g_menuOpen && !g_hiddenBehindGameMenu && REX::FModule::IsRuntimeAE()) {
        const auto foreground = GetForegroundWindow();
        DWORD processID = 0;
        if (foreground &&
            GetWindowThreadProcessId(foreground, &processID) != 0 &&
            processID == GetCurrentProcessId()) {
            RECT client{};
            if (GetClientRect(foreground, &client)) {
                POINT corners[2]{
                    { client.left, client.top },
                    { client.right, client.bottom }
                };
                MapWindowPoints(foreground, nullptr, corners, 2);
                RECT fullClient{
                    corners[0].x, corners[0].y,
                    corners[1].x, corners[1].y
                };
                // Bypass the previous IAT layer only while our menu is open.
                // The game otherwise keeps its original ClipCursor behavior.
                return ::ClipCursor(&fullClient);
            }
        }
    }
    return original ? original(requestedRect) : ::ClipCursor(requestedRect);
}

static void InstallAEClipCursorGuard() noexcept {
    if (!REX::FModule::IsRuntimeAE()) return;
    auto game = REX::FModule::GetExecutingModule();
    auto* importSlot = game.GetImportFunctionPointer("ClipCursor", "USER32.dll");
    if (!importSlot) {
        LogLine("1.1 AE ClipCursor guard unavailable: game IAT entry not found");
        return;
    }
    const auto previous = *reinterpret_cast<ClipCursorFunction*>(importSlot);
    if (previous == &AEClipCursorHook) return;
    if (!game.SetImportFunctionPointer(
            "ClipCursor", "USER32.dll", reinterpret_cast<void*>(&AEClipCursorHook))) {
        LogLine("1.1 AE ClipCursor guard install failed");
        return;
    }
    g_originalClipCursor.store(
        reinterpret_cast<ClipCursorFunction>(previous), std::memory_order_release);
    LogLine("1.1 AE ClipCursor guard installed");
}

struct QuickTargetShaderState {
    RE::TESEffectShader* shader = nullptr;
    RE::EffectShaderData original{};
    bool captured = false;
};
static QuickTargetShaderState g_quickTargetShader;

static bool IsLightweightMenuMode() noexcept
{
    return g_quickSaveMode || g_quickOutfitMode;
}

static void OnPrismaConsoleMessage(
    PrismaView view,
    PRISMA_UI_API::ConsoleMessageLevel level,
    const char* message)
{
    try {
        const char* severity = "log";
        switch (level) {
        case PRISMA_UI_API::ConsoleMessageLevel::Warning: severity = "warning"; break;
        case PRISMA_UI_API::ConsoleMessageLevel::Error: severity = "error"; break;
        case PRISMA_UI_API::ConsoleMessageLevel::Debug: severity = "debug"; break;
        case PRISMA_UI_API::ConsoleMessageLevel::Info: severity = "info"; break;
        default: break;
        }
        LogLine("1.1 Prisma JS [" + std::string(severity) + "] view=" +
            std::to_string(view) + " " + (message ? message : ""));
    } catch (...) {
    }
}

struct PreviewLightingPreset {
    // The vanilla light keeps its original power.  Position is therefore the
    // main control: moderate forward distance, symmetric lateral offsets, and
    // a body-height source avoid a bright chest with a dark lower body.
    int keyLightForward = 108;
    int keyLightRight = -42;
    int keyLightHeight = 110;
    int fillLightForward = 108;
    int fillLightRight = 42;
    int fillLightHeight = 96;
    // Outdoor scenes get one additional low, centered fill for the legs.
    int lowerLightForward = 86;
    int lowerLightRight = 0;
    int lowerLightHeight = 44;
};
struct PreviewTuning {
    int targetScanRadius = 512;
    int visualScalePercent = 110;
    int cameraDistance = 277;
    int cameraRight = -30;
    int cameraHeight = 98;
    int cameraFov = 70;
    int cameraYawOffsetDegrees = -20;
    int cameraPitchDegrees = 6;
    PreviewLightingPreset exteriorDay{};
    PreviewLightingPreset interiorDay{
        46, -26, 96,
        46, 26, 84,
        52, 0, 42
    };
};
static PreviewTuning g_previewTuning;
struct SavedMod {
    uint32_t fid = 0;
    std::uint8_t index = 0;
    std::uint8_t rank = 0;
    bool disabled = false;
};
struct OE {
    uint32_t fid = 0;
    std::string name;
    std::vector<SavedMod> mods;
    std::optional<float> color;
    bool weapon = false;
    bool modSnapshot = false;
};
struct SlotInfo {
    int slot = 0;
    int gender = -1;
    int count = 0;
    int usageCount = 0;
};
struct SlotDetail {
    std::string name;
    std::string summary;
    std::vector<std::string> itemNames;
};
struct TargetInfo {
    uint32_t formID = 0;
    std::string name;
    int sex = -1;
    float distance = 0.0f;
    bool player = false;
    RE::ObjectRefHandle handle;
};
struct MItems { std::vector<uint32_t> m, d; bool dr = false; };
struct ManagedInstanceRef {
    std::uint32_t fid = 0;
    const RE::ExtraDataList* extra = nullptr;
    std::string name;
    std::vector<SavedMod> mods;
};
struct SlotReadStatus {
    int missingItems = 0;
    int missingMods = 0;

    [[nodiscard]] int RemovedCount() const noexcept {
        return missingItems + missingMods;
    }
};
enum class OutfitTargetState : int {
    kAllowed = 1,
    kCombat = -6,
    kSceneControlled = -7,
    kSharedTemplate = -8
};
struct EquippedStackSnapshot {
    std::uint32_t fid = 0;
    const RE::ExtraDataList* extra = nullptr;
};
struct PreviewTempInstance {
    ManagedInstanceRef ref;
    std::uint32_t slots = 0;
};
struct PreviewSession {
    bool active = false;
    RE::ObjectRefHandle actor;
    std::vector<EquippedStackSnapshot> original;
    std::vector<OE> originalOutfit;
    std::vector<OE> draftOutfit;
    std::vector<PreviewTempInstance> temporary;
    std::vector<ManagedInstanceRef> persistentPreview;
    int previewSlot = 0;
    bool hasSavedDraft = false;
    int lastSavedSlot = 0;
    bool includesWeapons = false;
};
struct MaterialRestoreSession {
    bool active = false;
    RE::ObjectRefHandle actor;
    std::vector<OE> equipped;
    std::vector<OE> committed;
    std::string lastGeneratedName;
};
static MaterialRestoreSession g_materialRestore;
struct CameraSession {
    bool active = false;
    bool originalStateCaptured = false;
    bool wasFree = false;
    bool wasFirstPerson = false;
    bool playerModelStateCaptured = false;
    bool playerModelWasShown = false;
    bool playerFirstPersonGeometryWasHidden = false;
    bool playerPreviewFullRefreshPending = false;
    bool freeCameraRunInput = true;
    std::uint32_t targetFormID = 0;
    float worldFOV = 0.0F;
    RE::NiPoint3 translation{};
    RE::BSTPoint2<float> rotation{};
};
static constexpr std::size_t kPreviewLightCapacity = 5;
static_assert(kPreviewLightCapacity == 5);

struct PreviewLightSession {
    std::uint32_t targetFormID = 0;
    std::uint32_t cellFormID = 0;
    bool enabled = false;
    ULONGLONG deferUntilMs = 0;
    std::array<RE::ObjectRefHandle, kPreviewLightCapacity> references{};
};
struct StudioItemRef {
    int token = 0;
    OE item;
    std::string displayName;
    std::string category;
    std::uint32_t slots = 0;
    ManagedInstanceRef ref;
    int stackID = -1;
    int count = 1;
    bool equipped = false;
};
struct MaterialChoice {
    std::uint32_t fid = 0;
    std::string name;
    std::uint16_t attachPoint = 0;
    bool exact = false;
};
static std::unordered_map<uint32_t, MItems> g_mi;
static std::unordered_map<uint32_t, int> g_lastAppliedSlots;
static std::unordered_map<uint32_t, std::vector<ManagedInstanceRef>> g_managedInstances;
static std::unordered_set<std::uint32_t> g_playerGrantedAmmo;
static std::unordered_set<std::uint64_t> g_sessionNpcGrantedAmmo;
static std::vector<SlotInfo> g_index;
static std::unordered_map<int, SlotInfo> g_indexBySlot;
static std::unordered_map<int, SlotDetail> g_slotDetails;
static std::unordered_map<int, std::vector<int>> g_randomBags;
static std::unordered_map<uint32_t, RE::ObjectRefHandle> g_targetHandles;
struct SaveDraft { int gender = -1; std::vector<OE> items; };
static std::unordered_map<int, SaveDraft> g_saveDrafts;
static PreviewSession g_preview;
static CameraSession g_camera;
static PreviewLightSession g_previewLights;
static std::vector<StudioItemRef> g_studioItems;
static std::unordered_map<int, std::vector<MaterialChoice>> g_materialCache;
static int g_nextStudioToken = 1;
static bool g_inputSinkRegistered = false;
static bool g_coverMonitorStarted = false;
static std::atomic_bool g_coverMonitorTaskQueued = false;
static bool g_dialogueGuardRegistered = false;
static std::atomic_bool g_windowWasInactive = false;
static std::atomic_uint32_t g_focusRestoreSerial{ 0 };
static ULONGLONG g_lastUiWakeMs = 0;
static std::atomic_uint32_t g_previewRequestSerial{ 0 };
static std::atomic_uint32_t g_restoreSerial{ 0 };
static bool g_actorCellEventRegistered = false;
static std::uint32_t g_nextActorCellReapplySerial = 0;
static std::unordered_map<std::uint32_t, std::uint32_t> g_actorCellReapplySerials;
// Closing the menu queues appearance work.  A serial prevents an old delayed
// close from re-enabling gameplay after the user has opened the menu again.
static std::atomic_uint32_t g_closeReleaseSerial{ 0 };
// Keep a closing View owned by the close finalizer. Without this guard a
// rapid Pip-Boy/menu transition can reuse the old View before Destroy runs.
static std::atomic_bool g_closeAnimationPending{ false };
static std::atomic_bool g_closeQuickActionPending{ false };
static int g_closeQuickActionSlot = 0;
static RE::ObjectRefHandle g_closeQuickActionTarget;
// A close key/button can otherwise be delivered to the next game menu in the
// same input burst. Keep this guard short and targeted: it consumes only the
// hand-off input, while an already-open native menu remains fully usable.
static std::atomic<ULONGLONG> g_closeInputGuardUntilMs{ 0 };
static std::atomic<ULONGLONG> g_lastMenuOpenMs{ 0 };
static constexpr ULONGLONG kMenuOpenDebounceMs = 500;
static constexpr ULONGLONG kCloseInputGuardMs = 160;
static constexpr ULONGLONG kQuickCloseInputGuardMs = 80;
static constexpr int kOutfitManagerViewOrder = 1000;

static bool IsSelectableTarget(RE::Actor* a);
static bool IsActorInPowerArmor(RE::Actor* a);
static bool IsPlayerTeammateNative(RE::Actor* actor);
static bool IsIgnoredOutfitArmor(RE::TESForm* f);
static bool IsManagedOutfitArmor(RE::TESForm* f);
static OutfitTargetState CheckOutfitTargetState(RE::Actor* actor, bool allowSharedTemplate);
static std::string OutfitTargetStateMessage(OutfitTargetState state);
static void SetMenuTimePaused(bool enabled);
static std::string FormIDHex(uint32_t formID);
static std::string GetName(RE::Actor* a);
static int GetSex(RE::Actor* a);
static void RollbackPreview(bool scheduleRetry = false);
static void RefreshActorAppearance(RE::Actor* actor, bool settled = false);
static void AcceptPreviewAsCurrent(RE::Actor* actor, int slot);
static void RestorePreviewCamera(bool preserveOriginalState = false);
static void ClearPreviewCameraInputIfNeeded(bool force = false);
static bool EnsureThirdPersonForPreview();
static bool IsThirdPersonPreviewReady();
static void PositionPreviewCamera(RE::Actor* actor);
static bool ApplyPreviewCameraIfReady(const RE::ObjectRefHandle& previewHandle, bool positionCamera);
static void RotatePreviewTarget(int direction);
static void ClearPreviewLights();
static void DestroyPreviewLights();
static void EnsurePreviewLights(RE::Actor* actor);
static bool CommitSavedStudioOutfit();
static void CloseMenuInternal(bool rollback);
static void FinishMenuCloseAfterCamera(std::uint32_t closeSerial);
static void HideClosingViewForSceneSwitch();
static void FinalizeCloseForNativeMenu();
static void ReleaseClosedMenu(std::uint32_t closeSerial);
static std::vector<EquippedStackSnapshot> CaptureEquippedStacks(RE::Actor* actor, bool includeWeapons = false);
static bool IsWearingSavedOutfitNative(int slot, RE::Actor* actor);
static bool IsCloseActionSettled();
static bool IsCloseRestoreSettled(const RE::ObjectRefHandle& actorHandle,
    const std::vector<EquippedStackSnapshot>& originals);

static bool IsCloseInputGuardActive() {
    const auto until = g_closeInputGuardUntilMs.load();
    if (until == 0) return false;
    const auto now = GetTickCount64();
    if (now >= until) {
        g_closeInputGuardUntilMs.store(0);
        return false;
    }
    return true;
}

static void ArmCloseInputGuard(bool quickOutfit) {
    const auto duration = quickOutfit ? kQuickCloseInputGuardMs : kCloseInputGuardMs;
    g_closeInputGuardUntilMs.store(GetTickCount64() + duration);
    LogLine("2.0 close input guard armed ms=" + std::to_string(duration));
}
static void ScheduleCloseFinalization(
    std::uint32_t closeSerial,
    const RE::ObjectRefHandle& actorHandle,
    std::shared_ptr<const std::vector<EquippedStackSnapshot>> originals,
    int attempt = 0);
static void QueueGameTask(std::function<void()> task);
static void EnsureCacheLoaded();
static void ScheduleAttachedActorOutfitReapply(RE::Actor* actor);
static void ClearExpiredBusyState();
static RE::Actor* ResolveMenuActorByFormID(std::uint32_t formID);
static int CommitPreviewForActor(int slot, RE::Actor* actor);
static int PreviewStudioItem(int token, RE::Actor* actor);
static int UnequipStudioItem(int token, RE::Actor* actor);
static int BeginStudioDraft(RE::Actor* actor);
static int RebuildStudioDraft(RE::Actor* actor, const std::vector<OE>& draft);
static int PreviewSavedItems(RE::Actor* actor, const std::vector<OE>& items, int slot);
static std::string DraftDebug(const std::vector<OE>& draft);
static void ScheduleStudioInventoryRefresh(std::uint32_t previewSerial);
static void RefreshStudioInventoryNow();
static StudioItemRef* FindStudioItem(int token);
static std::vector<OE> CollectEquippedArmor(RE::Actor* actor);
static void AppendEquippedWeapons(RE::Actor* actor, std::vector<OE>& items);
static bool SameSavedMods(const std::vector<SavedMod>& lhs, const std::vector<SavedMod>& rhs);
static int PreviewMaterialItem(
    int token,
    std::uint32_t modID,
    RE::Actor* actor,
    std::string* outChangedName = nullptr);
static int CommitMaterialItem(int token, std::uint32_t modID, RE::Actor* actor, std::string* outGeneratedName = nullptr);
static int ValidateOutfitForActor(
    int slot,
    RE::Actor* actor,
    std::vector<OE>& items,
    SlotReadStatus* status = nullptr,
    std::vector<OE>* resolvedSlotItems = nullptr,
    int* slotGenderOut = nullptr);
static int EquipOutfitForActor(
    int slot,
    RE::Actor* actor,
    bool allowSharedTemplate = false,
    bool acknowledgeMissing = false);
int OM_ResetMenuActionTargetOutfit(std::monostate);
static std::string BuildStudioInventoryJson();
static std::string BuildMaterialChoicesJson(int token);
static int ChooseRandomSlotNative(int sex, int current, int last);
static bool SaveWeaponsEnabled();
static bool GetBoolSetting(std::string_view key, bool fallback);
static int GetIntSetting(std::string_view key, int fallback);
static float GetFloatSetting(std::string_view key, float fallback);
static bool OutfitWorkbenchEnabled();
static std::string StudioCategory(std::uint32_t slots, std::string_view displayName);

static void InvokeMenuScript(const char* script) {
    std::lock_guard<std::mutex> lk(g_mx);
    if (!g_menuOpen || !g_prisma || !g_prisma->IsValid(g_view) || !g_viewReady || !script) {
        LogLine("2.0 UI invoke skipped menu=" + std::to_string(g_menuOpen) +
            " ready=" + std::to_string(g_viewReady));
        return;
    }
    g_prisma->Invoke(g_view, script);
}

// PrismaUI 2.1 runs JavaScript through a bounded single owner thread. Use the
// named JSON interop path for data payloads so large state/inventory messages do
// not require another round of JavaScript parsing and string escaping.
static void InteropMenuCall(const char* functionName, std::string_view argument) {
    PRISMA_UI_API::IVPrismaUI10* prisma = nullptr;
    PrismaView view = 0;
    bool skip = false;
    bool menuOpen = false;
    bool viewReady = false;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        menuOpen = g_menuOpen;
        viewReady = g_viewReady;
        prisma = g_prisma;
        view = g_view;
        skip = !menuOpen || !prisma || !prisma->IsValid(view) || !viewReady ||
            !functionName || !*functionName;
    }
    if (skip) {
        LogLine("1.1 UI interop skipped menu=" + std::to_string(menuOpen) +
            " ready=" + std::to_string(viewReady) +
            " function=" + (functionName ? functionName : "<null>"));
        return;
    }
    const std::string payload = argument.empty() ? "{}" : std::string(argument);
    // Do not hold g_mx across Prisma's owner-thread queue. A JS callback can
    // synchronously cause native state changes, so the external call must be
    // outside our state mutex.
    prisma->InteropCall(view, functionName, payload.c_str());
}

static void InvokeGamepadMenuScript(const char* script) {
    if (!script) return;
    std::string combined = "if(window.omUseGamepad)window.omUseGamepad();";
    combined += script;
    InvokeMenuScript(combined.c_str());
}

static void QueueGameTask(std::function<void()> task) {
    if (!task) return;
    if (auto* tasks = F4SE::GetTaskInterface()) {
        tasks->AddTask(std::move(task));
    }
}

static void SetGameplayInputBlocked(bool block) {
    if (block) {
        if (!g_inputIsolation.active) {
            if (auto* playerControls = RE::PlayerControls::GetSingleton()) {
                g_inputIsolation.playerControlsWasBlocked = playerControls->blockPlayerInput;
            }
            g_inputIsolation.active = true;
        }

        // PrismaUI FocusMenu owns keyboard/mouse capture. Only stop the
        // gameplay movement and look vectors here; disabling the global input
        // layer or MenuControls prevents Prisma from routing pointer events.
        if (auto* playerControls = RE::PlayerControls::GetSingleton()) {
            playerControls->blockPlayerInput = true;
            playerControls->data.moveInputVec = {};
            playerControls->data.lookInputVec = {};
            playerControls->data.lookInputVecNormalized = {};
            playerControls->data.prevMoveVec = {};
            playerControls->data.prevLookVec = {};
            playerControls->data.autoMove = false;
        }
        return;
    }

    if (g_inputIsolation.active) {
        if (auto* playerControls = RE::PlayerControls::GetSingleton()) {
            playerControls->blockPlayerInput = g_inputIsolation.playerControlsWasBlocked;
            playerControls->data.moveInputVec = {};
            playerControls->data.lookInputVec = {};
            playerControls->data.lookInputVecNormalized = {};
            playerControls->data.prevMoveVec = {};
            playerControls->data.prevLookVec = {};
            playerControls->data.autoMove = false;
        }
        g_inputIsolation = {};
    }
}

static void SetMenuTimePaused(bool enabled) {
    auto* timer = RE::BSTimer::GetSingleton();
    auto* main = RE::Main::GetSingleton();

    if (enabled) {
        if (!g_timePaused) {
            g_previousTimeMult = RE::BSTimer::QGlobalTimeMultiplier();
            if (main) {
                g_previousMainFreezeTime = main->freezeTime;
                g_mainFreezeCaptured = true;
            }
            g_timePaused = true;
        }
        if (main) main->freezeTime = true;
        if (timer) timer->SetGlobalTimeMultiplier(0.0f, true);
        return;
    }

    if (g_timePaused) {
        if (timer) timer->SetGlobalTimeMultiplier(g_previousTimeMult <= 0.0f ? 1.0f : g_previousTimeMult, true);
        if (main && g_mainFreezeCaptured) main->freezeTime = g_previousMainFreezeTime;
        g_timePaused = false;
        g_mainFreezeCaptured = false;
    }
}

static bool IsGameWindowForeground() noexcept {
    const auto foreground = GetForegroundWindow();
    if (!foreground) return false;
    DWORD processID = 0;
    GetWindowThreadProcessId(foreground, &processID);
    return processID == GetCurrentProcessId();
}

static void SetSceneHudHidden(bool hidden) {
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return;
    auto hud = ui->GetMenu(RE::BSFixedString("HUDMenu"));
    if (!hud) return;

    if (hidden) {
        if (!g_hudVisibilityCaptured) {
            g_hudWasVisible = hud->menuCanBeVisible;
            g_hudVisibilityCaptured = true;
        }
        if (hud->menuCanBeVisible) {
            hud->menuCanBeVisible = false;
            hud->OnMenuDisplayStateChanged();
        }
        return;
    }

    if (g_hudVisibilityCaptured) {
        hud->menuCanBeVisible = g_hudWasVisible;
        hud->OnMenuDisplayStateChanged();
        g_hudVisibilityCaptured = false;
    }
}

static bool AnyNativeMenuOpen(std::initializer_list<const char*> names) {
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return true;
    for (const auto* name : names) {
        if (ui->GetMenuOpen(RE::BSFixedString(name))) return true;
    }
    return false;
}

static bool BlockingGameMenuOpen() {
    return AnyNativeMenuOpen({
        "Console",
        "PauseMenu",
        "PipboyMenu",
        "MessageBoxMenu",
        "DialogueMenu",
        "ContainerMenu",
        "BarterMenu",
        "TerminalMenu",
        "LoadingMenu",
        "MainMenu"
    });
}

static constexpr std::array<DWORD, 4> kActorCellReapplyDelaysMs = {
    240u, 720u, 1600u, 3000u
};

static void FinishAttachedActorOutfitReapply(
    std::uint32_t actorID,
    std::uint32_t serial)
{
    std::lock_guard<std::mutex> lock(g_mx);
    const auto pending = g_actorCellReapplySerials.find(actorID);
    if (pending != g_actorCellReapplySerials.end() && pending->second == serial) {
        g_actorCellReapplySerials.erase(pending);
    }
}

static void ScheduleAttachedActorOutfitReapply(RE::Actor* actor) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!actor || actor == player || IsPlayerTeammateNative(actor) || !IsSelectableTarget(actor)) return;
    ClearExpiredBusyState();
    if (g_eqBusy || g_randBusy) return;

    const auto actorID = actor->GetFormID();
    int slot = 0;
    std::uint32_t serial = 0;
    {
        std::lock_guard<std::mutex> lock(g_mx);
        const auto active = g_lastAppliedSlots.find(actorID);
        if (active == g_lastAppliedSlots.end() || active->second < 1 ||
            active->second > MAX_SLOTS) {
            return;
        }
        slot = active->second;
        serial = ++g_nextActorCellReapplySerial;
        if (serial == 0) serial = ++g_nextActorCellReapplySerial;
        g_actorCellReapplySerials[actorID] = serial;
    }

    const auto actorHandle = actor->GetHandle();
    LogLine("2.0 actor cell attach queued target=" + FormIDHex(actorID) +
        " slot=" + std::to_string(slot));
    std::thread([actorHandle, actorID, slot, serial] {
        for (std::size_t attempt = 0; attempt < kActorCellReapplyDelaysMs.size(); ++attempt) {
            Sleep(kActorCellReapplyDelaysMs[attempt]);
            QueueGameTask([actorHandle, actorID, slot, serial, attempt] {
                bool stillPending = false;
                {
                    std::lock_guard<std::mutex> lock(g_mx);
                    const auto pending = g_actorCellReapplySerials.find(actorID);
                    const auto active = g_lastAppliedSlots.find(actorID);
                    stillPending = pending != g_actorCellReapplySerials.end() &&
                        pending->second == serial &&
                        active != g_lastAppliedSlots.end() && active->second == slot;
                }
                if (!stillPending) return;

                auto ref = actorHandle.get();
                auto* attachedActor = ref ? ref->As<RE::Actor>() : nullptr;
                if (!attachedActor || attachedActor->IsDead(false)) {
                    if (attempt + 1 == kActorCellReapplyDelaysMs.size()) {
                        FinishAttachedActorOutfitReapply(actorID, serial);
                    }
                    return;
                }

                EnsureCacheLoaded();
                if (IsWearingSavedOutfitNative(slot, attachedActor)) {
                    LogLine("2.0 actor cell attach outfit already present target=" +
                        FormIDHex(actorID) + " slot=" + std::to_string(slot));
                    FinishAttachedActorOutfitReapply(actorID, serial);
                    return;
                }

                ClearExpiredBusyState();
                if (g_eqBusy || g_randBusy) {
                    LogLine("2.0 actor cell attach reapply deferred: outfit operation busy target=" +
                        FormIDHex(actorID));
                } else {
                    g_eqBusy = true;
                    g_eqBusySinceMs = GetTickCount64();
                    const int result = EquipOutfitForActor(slot, attachedActor, true, true);
                    g_eqBusy = false;
                    g_eqBusySinceMs = 0;
                    if (result > 0) {
                        LogLine("2.0 actor cell attach outfit reapplied target=" +
                            FormIDHex(actorID) + " slot=" + std::to_string(slot) +
                            " equipped=" + std::to_string(result));
                        FinishAttachedActorOutfitReapply(actorID, serial);
                        return;
                    }
                    LogLine("2.0 actor cell attach outfit reapply failed target=" +
                        FormIDHex(actorID) + " slot=" + std::to_string(slot) +
                        " result=" + std::to_string(result));
                }

                if (attempt + 1 == kActorCellReapplyDelaysMs.size()) {
                    FinishAttachedActorOutfitReapply(actorID, serial);
                    LogLine("2.0 actor cell attach reapply exhausted target=" +
                        FormIDHex(actorID) + " slot=" + std::to_string(slot));
                }
            });
        }
    }).detach();
}

class OutfitManagerActorCellEventHandler final :
    public RE::BSTEventSink<RE::TESCellAttachDetachEvent>,
    public RE::BSTEventSink<RE::TESCellFullyLoadedEvent>,
    public RE::BSTEventSink<RE::ActorEquipManagerEvent::Event> {
public:
    static OutfitManagerActorCellEventHandler* GetSingleton() {
        static OutfitManagerActorCellEventHandler instance;
        return &instance;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCellAttachDetachEvent& event,
        RE::BSTEventSource<RE::TESCellAttachDetachEvent>*) override
    {
        if (!event.isAttaching || !event.refr) return RE::BSEventNotifyControl::kContinue;
        if (auto* actor = event.refr->As<RE::Actor>()) {
            ScheduleAttachedActorOutfitReapply(actor);
        }
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::TESCellFullyLoadedEvent& event,
        RE::BSTEventSource<RE::TESCellFullyLoadedEvent>*) override
    {
        if (!event.cell) return RE::BSEventNotifyControl::kContinue;
        event.cell->ForEachReference([](RE::TESObjectREFR* ref) {
            if (auto* actor = ref ? ref->As<RE::Actor>() : nullptr) {
                ScheduleAttachedActorOutfitReapply(actor);
            }
            return RE::BSContainer::ForEachResult::kContinue;
        });
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::ActorEquipManagerEvent::Event& event,
        RE::BSTEventSource<RE::ActorEquipManagerEvent::Event>*) override
    {
        auto* actor = event.actorAffected;
        auto* item = event.itemAffected ? event.itemAffected->object : nullptr;
        if (!actor || actor == RE::PlayerCharacter::GetSingleton() ||
            !IsManagedOutfitArmor(item)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        // AI schedule changes and default-outfit evaluation can replace a
        // managed NPC item without a cell transition.  Feed the same delayed
        // recovery path so the real locked inventory outfit wins again.
        ScheduleAttachedActorOutfitReapply(actor);
        return RE::BSEventNotifyControl::kContinue;
    }
};

static void RegisterActorCellEventHandler() {
    if (g_actorCellEventRegistered) return;
    auto* handler = OutfitManagerActorCellEventHandler::GetSingleton();
    auto* attachSource = RE::TESCellAttachDetachEvent::GetEventSource();
    auto* loadedSource = RE::TESCellFullyLoadedEvent::GetEventSource();
    auto* equipSource = RE::ActorEquipManager::GetSingleton();
    if (attachSource) attachSource->RegisterSink(handler);
    if (loadedSource) loadedSource->RegisterSink(handler);
    if (equipSource) {
        equipSource->RegisterSink(
            static_cast<RE::BSTEventSink<RE::ActorEquipManagerEvent::Event>*>(handler));
    }
    g_actorCellEventRegistered = attachSource || loadedSource || equipSource;
    if (g_actorCellEventRegistered) {
        LogLine("2.0 actor cell outfit persistence events registered");
    }
}

class OutfitManagerMenuOpenCloseHandler final : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
public:
    static OutfitManagerMenuOpenCloseHandler* GetSingleton() {
        static OutfitManagerMenuOpenCloseHandler instance;
        return &instance;
    }

    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent& event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
    {
        if (event.opening && !g_menuOpen) {
            // A real native menu now owns input. Do not carry the close guard
            // into its first frame.
            g_closeInputGuardUntilMs.store(0);
        }
        if (event.opening && !g_menuOpen && g_closeAnimationPending.load()) {
            // The UI has already released its own menu flag, so the normal
            // game-menu cover monitor no longer sees this transition. Hide the
            // reusable view immediately; the 2.1 compositor owns held-frame
            // invalidation and no destroy/recreate cycle is needed.
            HideClosingViewForSceneSwitch();
        }
        if (g_menuOpen && event.opening && event.menuName == RE::BSFixedString("DialogueMenu")) {
            // DialogueMenu owns the conversation camera and would temporarily
            // hide Prisma. Reject only this opening while our panel is active.
            RE::DialogueMenuUtils::CloseMenu();
            LogLine("2.0 blocked DialogueMenu while OutfitManager is open");
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};

static void RegisterDialogueMenuGuard() {
    if (g_dialogueGuardRegistered) return;
    auto* ui = RE::UI::GetSingleton();
    if (!ui) return;
    ui->RegisterSink(OutfitManagerMenuOpenCloseHandler::GetSingleton());
    g_dialogueGuardRegistered = true;
}

static bool PresentMenuView(const char* reason, bool ensureVisible = true) {
    if (!g_menuOpen || g_hiddenBehindGameMenu || !g_viewReady ||
        !g_prisma || g_view == 0 || !g_prisma->IsValid(g_view)) {
        return false;
    }

    if (ensureVisible) g_prisma->Show(g_view);
    InstallAEClipCursorGuard();
    InteropMenuCall("omWake", "{}");
    // This is a standalone menu opened from gameplay. Keep Prisma's FocusMenu
    // active so it owns the game cursor and routes mouse input to this view.
    const auto previousFocusedView = g_prisma->GetFocusedView();
    const bool alreadyFocused = previousFocusedView == g_view && g_prisma->HasFocus(g_view);
    const bool requestFocus = !alreadyFocused && g_focusAttemptedView != g_view;
    bool queued = alreadyFocused;
    if (requestFocus) {
        queued = g_prisma->Focus(g_view, false, false);
        g_focusAttemptedView = g_view;
    }
    const bool focused = alreadyFocused || g_prisma->HasFocus(g_view);
    const auto focusedView = g_prisma->GetFocusedView();
    LogLine("2.0 UI focus reason=" + std::string(reason ? reason : "unknown") +
        " view=" + std::to_string(g_view) +
        " queued=" + std::to_string(queued) +
        " focused=" + std::to_string(focused) +
        " focusedView=" + std::to_string(focusedView));
    return queued || focused || focusedView == g_view;
}

static void RestorePrismaFocusIfNeeded(const char* reason) {
    if (!g_menuOpen || g_hiddenBehindGameMenu || !g_prisma ||
        g_view == 0 || !g_viewReady || !g_prisma->IsValid(g_view)) {
        return;
    }
    if (g_prisma->IsHidden(g_view)) g_prisma->Show(g_view);
    const auto focusedView = g_prisma->GetFocusedView();
    if (focusedView == g_view && g_prisma->HasFocus(g_view)) return;
    const bool queued = g_prisma->Focus(g_view, false, false);
    LogLine("2.0 UI focus restored reason=" + std::string(reason ? reason : "unknown") +
        " queued=" + std::to_string(queued) +
        " focusedView=" + std::to_string(g_prisma->GetFocusedView()));
}

static void SchedulePrismaFocusRestore(const char* reason) {
    const std::string reasonText = reason ? reason : "unknown";
    const auto serial = ++g_focusRestoreSerial;
    std::thread([reasonText, serial] {
        for (const auto delay : { 35u, 160u }) {
            Sleep(delay);
            if (serial != g_focusRestoreSerial.load()) return;
            QueueGameTask([reasonText, serial] {
                if (serial != g_focusRestoreSerial.load()) return;
                RestorePrismaFocusIfNeeded(reasonText.c_str());
            });
        }
    }).detach();
}

static constexpr ULONGLONG kSlowUiOperationMs = 180;

class SlowUiFocusRecovery final {
public:
    explicit SlowUiFocusRecovery(const char* reason) :
        started_(GetTickCount64()),
        reason_(reason ? reason : "slow-operation")
    {}

    ~SlowUiFocusRecovery() noexcept {
        const auto elapsed = GetTickCount64() - started_;
        const bool slow = elapsed >= kSlowUiOperationMs;
        if (slow) {
            LogLine("2.0 UI slow operation reason=" + reason_ +
                " ms=" + std::to_string(elapsed));
        }
        // This guard is destroyed after the native result has been sent. The
        // normal path performs only a cheap state check; Focus() runs only if
        // Prisma actually lost the view focus.
        RestorePrismaFocusIfNeeded(reason_.c_str());
        if (slow) SchedulePrismaFocusRestore(reason_.c_str());
    }

private:
    ULONGLONG started_;
    std::string reason_;
};

static bool MenuViewHasFocus() {
    return g_prisma && g_view != 0 && g_prisma->IsValid(g_view) &&
        (g_prisma->GetFocusedView() == g_view || g_prisma->HasFocus(g_view));
}

static void ScheduleMenuFocusRetry(PrismaView view, std::uint32_t openSerial) {
    std::thread([view, openSerial] {
        const std::array<DWORD, 4> delays{ 60u, 220u, 600u, 1200u };
        for (std::size_t attempt = 0; attempt < delays.size(); ++attempt) {
            const auto delay = delays[attempt];
            Sleep(delay);
            QueueGameTask([view, openSerial, attempt] {
                if (openSerial != g_closeReleaseSerial.load() || !g_menuOpen ||
                    g_hiddenBehindGameMenu || view == 0 || view != g_view ||
                    !g_viewReady || !g_prisma || !g_prisma->IsValid(view)) {
                    return;
                }
                // Ultralight can report focus before its first native input
                // episode has settled. Re-arm focus only during the first two
                // retries; later retries remain conditional so normal mouse
                // movement cannot reset the cursor.
                const bool warmup = attempt < 2 && g_focusAttemptedView == view;
                if (warmup) {
                    const bool queued = g_prisma->Focus(view, false, false);
                    InteropMenuCall("omWake", "{}");
                    LogLine("1.1 UI focus warmup view=" + std::to_string(view) +
                        " attempt=" + std::to_string(attempt) +
                        " queued=" + std::to_string(queued));
                } else if (g_prisma->IsHidden(view) || !MenuViewHasFocus()) {
                    RestorePrismaFocusIfNeeded("post-load");
                }
            });
        }
    }).detach();
}

static bool PresentDelayedMenuIfReady(const char* reason) {
    if (!g_menuPresentationPending) return true;
    if (!g_menuOpen || g_hiddenBehindGameMenu || !g_viewReady ||
        !g_prisma || g_view == 0 || !g_prisma->IsValid(g_view)) {
        return false;
    }
    if (!PresentMenuView(reason)) return false;
    g_menuPresentationPending = false;
    ScheduleMenuFocusRetry(g_view, g_closeReleaseSerial.load());
    InteropMenuCall("omWake", "{}");
    LogLine("2.0 UI presented after third-person camera ready");
    return true;
}

static bool OtherPrismaViewOpen() {
    if (!g_prisma) {
        g_prisma = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI10>();
    }
    if (!g_prisma) return false;
    return g_prisma->IsAnyPanelVisible(g_view);
}

static bool NativeMenuBlocksOpen() {
    auto* ui = RE::UI::GetSingleton();
    if (!ui || BlockingGameMenuOpen()) return true;
    // Prisma's FocusMenu can raise menuMode for another panel. That panel is
    // allowed underneath OutfitManager; only a real native game menu should
    // prevent our UI from opening.
    return !g_menuOpen && ui->menuMode != 0 && !OtherPrismaViewOpen();
}

static void HideMenuForGameMenu() {
    if (!g_prisma || !g_prisma->IsValid(g_view)) return;
    g_hiddenBehindGameMenu = true;
    g_prisma->Unfocus(g_view);
    g_focusAttemptedView = 0;
    g_prisma->Hide(g_view);
    ClearPreviewLights();
    // A native menu temporarily owns the camera, but the OutfitManager
    // session still owns the original first/third-person restore contract.
    // Keep that contract alive so returning from a Pip-Boy or alt-tab does
    // not lose the player model state.
    RestorePreviewCamera(true);
    SetGameplayInputBlocked(false);
    SetMenuTimePaused(false);
    SetSceneHudHidden(false);
}

static void RestoreMenuAfterGameMenu() {
    if (!g_menuOpen || !g_hiddenBehindGameMenu || !g_prisma || !g_prisma->IsValid(g_view)) return;
    g_hiddenBehindGameMenu = false;
    SetGameplayInputBlocked(true);
    // Keep global time running while the wardrobe UI is open.
    // Outfit/OMOD biped rebuilds can remain visually stale at time scale 0 and
    // only resolve after an alt-tab or another external 3D tick.  Keep gameplay
    // input blocked, camera locked, and HUD hidden, but let the actor/update
    // pipeline keep ticking so workbench equip/unequip refreshes naturally.
    SetMenuTimePaused(g_quickOutfitMode);
    SetSceneHudHidden(true);
    if (!IsLightweightMenuMode()) {
        auto previewRef = g_preview.active ? g_preview.actor.get() : g_menuTarget.get();
        if (auto* actor = previewRef ? previewRef->As<RE::Actor>() : nullptr) {
            const bool thirdPersonTransition = EnsureThirdPersonForPreview();
            if (!thirdPersonTransition && IsThirdPersonPreviewReady()) {
                PositionPreviewCamera(actor);
            } else {
                const auto handle = actor->GetHandle();
                std::thread([handle] {
                    Sleep(180);
                    if (auto* tasks = F4SE::GetTaskInterface()) {
                        tasks->AddTask([handle] {
                            ApplyPreviewCameraIfReady(handle, true);
                        });
                    }
                }).detach();
            }
        }
    }
    PresentMenuView("game-menu-close");
}

static void KeepMenuViewAwake() {
    if (!g_menuOpen || g_hiddenBehindGameMenu || !g_prisma || !g_prisma->IsValid(g_view)) return;
    SetSceneHudHidden(true);
    const bool foreground = IsGameWindowForeground();
    const bool returnedFromInactive = g_windowWasInactive.exchange(!foreground) && foreground;
    if (returnedFromInactive) {
        if (!IsLightweightMenuMode()) {
            auto previewRef = g_preview.active ? g_preview.actor.get() : g_menuTarget.get();
            if (auto* actor = previewRef ? previewRef->As<RE::Actor>() : nullptr) {
                EnsurePreviewLights(actor);
            }
        }
        PresentMenuView("window-activate");
        SchedulePrismaFocusRestore("window-activate");
    }
    if (!IsLightweightMenuMode()) {
        auto previewRef = g_preview.active ? g_preview.actor.get() : g_menuTarget.get();
        if (auto* actor = previewRef ? previewRef->As<RE::Actor>() : nullptr) {
            EnsurePreviewLights(actor);
        }
    }
    const auto now = GetTickCount64();
    if (now - g_lastUiWakeMs < 1000) return;
    g_lastUiWakeMs = now;
    const bool hidden = g_prisma->IsHidden(g_view);
    if (hidden) PresentMenuView("wake");
}

static void UpdateGameMenuCoverState() {
    if (!g_menuOpen) return;
    if (auto* ui = RE::UI::GetSingleton(); ui &&
        ui->GetMenuOpen(RE::BSFixedString("DialogueMenu"))) {
        RE::DialogueMenuUtils::CloseMenu();
        LogLine("2.0 dialogue guard closed DialogueMenu during OutfitManager session");
        return;
    }
    if (NativeMenuBlocksOpen()) {
        if (!g_hiddenBehindGameMenu) HideMenuForGameMenu();
        return;
    }
    RestoreMenuAfterGameMenu();
    if (!g_hiddenBehindGameMenu) {
        KeepMenuViewAwake();
    }
}

static bool PerformNpcStopAction(RE::Actor* actor, RE::DEFAULT_OBJECT actionID) {
    // BGSDefaultObjectManager::GetSingleton() is inlined on NG/AE and its
    // legacy relocation is intentionally 0 in the multiruntime CommonLib.
    // The motion-stop helper is cosmetic, so skip it on those runtimes rather
    // than resolving an invalid Address Library ID during outfit operations.
    if (!REX::FModule::IsRuntimeOG()) return false;
    auto* defaults = RE::BGSDefaultObjectManager::GetSingleton();
    auto* action = defaults ? defaults->GetDefaultObject<RE::BGSAction>(actionID) : nullptr;
    return actor && action && actor->PerformAction(action, nullptr);
}

static void StopSelectedNpcMotion(RE::Actor* actor, bool logResult) {
    if (!actor || actor == RE::PlayerCharacter::GetSingleton()) return;
    const bool moveStopped = PerformNpcStopAction(actor, RE::DEFAULT_OBJECT::kActionMoveStop);
    const bool turnStopped = PerformNpcStopAction(actor, RE::DEFAULT_OBJECT::kActionTurnStop);
    const bool idleStopped = PerformNpcStopAction(actor, RE::DEFAULT_OBJECT::kActionIdleStopInstant);
    if (logResult) {
        LogLine("2.0 NPC preview motion stop actor=" + FormIDHex(actor->formID) +
            " move=" + (moveStopped ? "1" : "0") +
            " turn=" + (turnStopped ? "1" : "0") +
            " idle=" + (idleStopped ? "1" : "0"));
    }
}

static void StopSelectedNpcTranslationAndTurn(RE::Actor* actor) {
    if (!actor || actor == RE::PlayerCharacter::GetSingleton()) return;
    PerformNpcStopAction(actor, RE::DEFAULT_OBJECT::kActionMoveStop);
    PerformNpcStopAction(actor, RE::DEFAULT_OBJECT::kActionTurnStop);
}

static void MaintainSelectedNpcPosition() {
    if (!g_menuOpen || g_hiddenBehindGameMenu || !g_lockedNpcTransformValid) return;
    auto ref = g_lockedNpcTarget ? g_lockedNpcTarget.get() : RE::NiPointer<RE::TESObjectREFR>{};
    auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
    if (!actor) return;
    const bool isNpc = actor != RE::PlayerCharacter::GetSingleton();

    const auto current = actor->GetPosition();
    const float dx = current.x - g_lockedNpcPosition.x;
    const float dy = current.y - g_lockedNpcPosition.y;
    const float dz = current.z - g_lockedNpcPosition.z;
    if (isNpc && g_lockedNpcStopBursts > 0) {
        if (g_lockedNpcStopBursts == 2) {
            StopSelectedNpcTranslationAndTurn(actor);
            const auto command = FormIDHex(actor->formID) + ".evp";
            RE::Console::ExecuteCommand(command.c_str());
        } else {
            PerformNpcStopAction(actor, RE::DEFAULT_OBJECT::kActionIdleStop);
        }
        // Repeated IdleStopInstant calls can pin a walk cycle on a raised-leg
        // frame. Use it only at selection time, then let a normal idle settle.
        --g_lockedNpcStopBursts;
    }

    constexpr float kPositionCorrectionDistance = 1.5F;
    constexpr float kHeadingCorrectionRadians = 0.02F;
    if (dx * dx + dy * dy + dz * dz >
        kPositionCorrectionDistance * kPositionCorrectionDistance) {
        if (isNpc) StopSelectedNpcTranslationAndTurn(actor);
        actor->SetPosition(g_lockedNpcPosition, true);
    }
    if (std::abs(std::remainder(actor->GetHeading() - g_lockedNpcHeading, 6.28318530717958647692F)) >
        kHeadingCorrectionRadians) {
        if (isNpc) StopSelectedNpcTranslationAndTurn(actor);
        actor->SetHeading(g_lockedNpcHeading);
    }
}

static void ReleaseLockedNpc() {
    ++g_lockedNpcSerial;
    auto ref = g_lockedNpcTarget ? g_lockedNpcTarget.get() : RE::NiPointer<RE::TESObjectREFR>{};
    auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
    const bool restoreHeading = g_lockedNpcTransformValid;
    const float originalHeading = g_lockedNpcOriginalHeading;
    g_lockedNpcTarget = {};
    g_lockedNpcTransformValid = false;
    g_lockedNpcStopBursts = 0;
    if (actor && restoreHeading) {
        actor->SetHeading(originalHeading);
    }
    if (actor && actor != RE::PlayerCharacter::GetSingleton()) {
        if (!g_lockedNpcWasRestrained) {
            const auto command = FormIDHex(actor->formID) + ".SetRestrained 0";
            RE::Console::ExecuteCommand(command.c_str());
            LogLine("2.0 NPC preview restraint cleared actor=" + FormIDHex(actor->formID));
        } else {
            LogLine("2.0 NPC preview preserved original restraint actor=" + FormIDHex(actor->formID));
        }
    }
    g_lockedNpcWasRestrained = false;
}

static void LockNpcForPreview(RE::Actor* actor) {
    if (!actor) return;
    if (g_lockedNpcTarget) {
        auto currentRef = g_lockedNpcTarget.get();
        auto* current = currentRef ? currentRef->As<RE::Actor>() : nullptr;
        if (current == actor) return;
    }
    ReleaseLockedNpc();
    const bool isNpc = actor != RE::PlayerCharacter::GetSingleton();
    g_lockedNpcWasRestrained = isNpc &&
        actor->lifeState == static_cast<std::uint32_t>(RE::ACTOR_LIFE_STATE::kRestrained);
    g_lockedNpcTarget = actor->GetHandle();
    g_lockedNpcPosition = actor->GetPosition();
    g_lockedNpcHeading = actor->GetHeading();
    g_lockedNpcOriginalHeading = g_lockedNpcHeading;
    g_lockedNpcCameraHeading = g_lockedNpcHeading;
    g_lockedNpcTransformValid = true;
    g_lockedNpcStopBursts = isNpc ? 2 : 0;
    ++g_lockedNpcSerial;
    if (isNpc) {
        const auto command = FormIDHex(actor->formID) + ".SetRestrained 1";
        RE::Console::ExecuteCommand(command.c_str());
        StopSelectedNpcMotion(actor, true);
        LogLine("2.0 NPC preview restrained actor=" + FormIDHex(actor->formID));
    }
}

static void StartGameMenuCoverMonitor() {
    if (g_coverMonitorStarted) return;
    g_coverMonitorStarted = true;
    std::thread([] {
        while (true) {
            Sleep(250);
            if (!g_menuOpen && !g_closeAnimationPending.load()) continue;
            if (g_coverMonitorTaskQueued.exchange(true)) continue;
            auto* tasks = F4SE::GetTaskInterface();
            if (!tasks) {
                g_coverMonitorTaskQueued.store(false);
                continue;
            }
            tasks->AddTask([] {
                    g_coverMonitorTaskQueued.store(false);
                    if (g_menuOpen) {
                        MaintainSelectedNpcPosition();
                        UpdateGameMenuCoverState();
                    } else if (g_closeAnimationPending.load() &&
                        BlockingGameMenuOpen()) {
                        HideClosingViewForSceneSwitch();
                    }
                });
        }
    }).detach();
}

class OutfitManagerInputHandler : public RE::BSInputEventUser {
public:
    static OutfitManagerInputHandler* GetSingleton() {
        static OutfitManagerInputHandler instance;
        return &instance;
    }

    bool ShouldHandleEvent(const RE::InputEvent* event) override {
        if (!event) return false;
        if (!g_menuOpen) {
            if (!IsCloseInputGuardActive() || BlockingGameMenuOpen()) return false;
            return (event->device == RE::INPUT_DEVICE::kKeyboard &&
                    event->eventType == RE::INPUT_EVENT_TYPE::kButton) ||
                (event->device == RE::INPUT_DEVICE::kGamepad &&
                    (event->eventType == RE::INPUT_EVENT_TYPE::kButton ||
                     event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick));
        }
        if (g_hiddenBehindGameMenu) return false;
        if (event->device == RE::INPUT_DEVICE::kKeyboard) {
            // Prisma focus routes keyboard input to the browser, but returning
            // false here also lets other game-side mods see the same key. We
            // keep the browser path isolated by forwarding the normalized key
            // from OnButtonEvent while consuming the original input event.
            return event->eventType == RE::INPUT_EVENT_TYPE::kButton;
        }
        return event->device == RE::INPUT_DEVICE::kGamepad &&
            (event->eventType == RE::INPUT_EVENT_TYPE::kButton ||
             event->eventType == RE::INPUT_EVENT_TYPE::kThumbstick);
    }

    void OnThumbstickEvent(const RE::ThumbstickEvent* event) override {
        if (!event) return;
        if (!g_menuOpen || g_hiddenBehindGameMenu) {
            leftThumbstickDirection_ = 0;
            leftThumbstickStartMs_ = 0;
            lastLeftThumbActionMs_ = 0;
            if (!g_menuOpen && IsCloseInputGuardActive() && !BlockingGameMenuOpen()) {
                auto* mutableEvent = const_cast<RE::ThumbstickEvent*>(event);
                mutableEvent->xValue = 0.0F;
                mutableEvent->yValue = 0.0F;
                mutableEvent->handled = RE::InputEvent::HANDLED_RESULT::kStop;
            }
            return;
        }

        constexpr float kDeadZone = 0.55F;
        constexpr float kReleaseZone = 0.35F;
        const auto now = GetTickCount64();

        const float x = event->xValue;
        const float y = event->yValue;
        auto* mutableEvent = const_cast<RE::ThumbstickEvent*>(event);
        mutableEvent->xValue = 0.0F;
        mutableEvent->yValue = 0.0F;
        mutableEvent->handled = RE::InputEvent::HANDLED_RESULT::kStop;

        if (event->QIDCode() == RE::ThumbstickEvent::kLeft &&
            std::abs(x) < kReleaseZone && std::abs(y) < kReleaseZone) {
            leftThumbstickDirection_ = 0;
            leftThumbstickStartMs_ = 0;
            lastLeftThumbActionMs_ = 0;
        }
        if (std::abs(x) < kDeadZone && std::abs(y) < kDeadZone) {
            return;
        }

        if (event->QIDCode() == RE::ThumbstickEvent::kRight) {
            if (now - lastThumbScrollMs_ < 120) {
                return;
            }
            if (std::abs(y) >= kDeadZone) {
                InvokeGamepadMenuScript(y > 0.0F ?
                    "if(window.omScrollItems)window.omScrollItems(-48,true);" :
                    "if(window.omScrollItems)window.omScrollItems(48,true);");
                lastThumbScrollMs_ = now;
            }
            return;
        }

        if (event->QIDCode() != RE::ThumbstickEvent::kLeft) {
            return;
        }
        const int direction = std::abs(y) >= std::abs(x) ?
            (y > 0.0F ? 1 : 2) : (x < 0.0F ? 3 : 4);
        const auto dispatch = [&]() {
            switch (direction) {
                case 1:
                    InvokeGamepadMenuScript("if(window.omMoveFocus)window.omMoveFocus(-1);");
                    break;
                case 2:
                    InvokeGamepadMenuScript("if(window.omMoveFocus)window.omMoveFocus(1);");
                    break;
                case 3:
                    InvokeGamepadMenuScript("if(window.omSlotPage)window.omSlotPage(-1);");
                    break;
                case 4:
                    InvokeGamepadMenuScript("if(window.omSlotPage)window.omSlotPage(1);");
                    break;
                default:
                    break;
            }
        };
        if (direction != leftThumbstickDirection_) {
            leftThumbstickDirection_ = direction;
            leftThumbstickStartMs_ = now;
            lastLeftThumbActionMs_ = now;
            dispatch();
            return;
        }
        const auto heldMs = now - leftThumbstickStartMs_;
        const auto repeatMs = heldMs < kThumbstickRepeatDelayMs ?
            kThumbstickRepeatDelayMs : kThumbstickRepeatMs;
        if (now - lastLeftThumbActionMs_ < repeatMs) return;
        lastLeftThumbActionMs_ = now;
        dispatch();
    }

    void OnButtonEvent(const RE::ButtonEvent* event) override {
        if (!event) return;
        if (!g_menuOpen || g_hiddenBehindGameMenu) {
            if (!g_menuOpen && IsCloseInputGuardActive() && !BlockingGameMenuOpen()) {
                const_cast<RE::ButtonEvent*>(event)->handled = RE::InputEvent::HANDLED_RESULT::kStop;
            }
            return;
        }
        auto* mutableEvent = const_cast<RE::ButtonEvent*>(event);
        if (event->device == RE::INPUT_DEVICE::kKeyboard) {
            mutableEvent->handled = RE::InputEvent::HANDLED_RESULT::kStop;
            const auto code = static_cast<std::int32_t>(event->GetBSButtonCode());
            const bool justPressed = event->QJustPressed();
            const bool held = event->QHeldDown();
            if (justPressed && code == kConsoleVirtualKeyCode) {
                // Let the vanilla console receive its toggle key, but close
                // our preview first so the two layers never coexist.
                CloseMenuInternal(false);
                mutableEvent->handled = RE::InputEvent::HANDLED_RESULT::kUnhandled;
                return;
            }
            // When Prisma owns the view, the framework already receives the physical
            // key event. Forwarding another synthetic key here makes one press move two
            // focus steps. Keep consuming the game event so Fallout and other menus
            // cannot see it, but leave the browser path as the single UI input source.
            if (MenuViewHasFocus()) return;
            const bool isRotateKey =
                code == static_cast<std::int32_t>(RE::BS_BUTTON_CODE::kJ) ||
                code == static_cast<std::int32_t>(RE::BS_BUTTON_CODE::kL);
            if (isRotateKey) {
                if (!justPressed && !held) return;
                const auto now = GetTickCount64();
                if (justPressed || now - lastRotateInputMs_ >= kRotateRepeatMs) {
                    InvokeMenuScript(code == static_cast<std::int32_t>(RE::BS_BUTTON_CODE::kJ) ?
                        "if(window.omRotateTarget)window.omRotateTarget(-1);" :
                        "if(window.omRotateTarget)window.omRotateTarget(1);");
                    lastRotateInputMs_ = now;
                }
                return;
            }
            std::string key;
            if (code >= static_cast<std::int32_t>(RE::BS_BUTTON_CODE::kA) &&
                code <= static_cast<std::int32_t>(RE::BS_BUTTON_CODE::kZ)) {
                key.assign(1, static_cast<char>('a' + code - static_cast<std::int32_t>(RE::BS_BUTTON_CODE::kA)));
            } else if (code >= static_cast<std::int32_t>(RE::BS_BUTTON_CODE::k0) &&
                       code <= static_cast<std::int32_t>(RE::BS_BUTTON_CODE::k9)) {
                key.assign(1, static_cast<char>('0' + code - static_cast<std::int32_t>(RE::BS_BUTTON_CODE::k0)));
            } else {
                switch (static_cast<RE::BS_BUTTON_CODE>(code)) {
                    case RE::BS_BUTTON_CODE::kBackspace: key = "backspace"; break;
                    case RE::BS_BUTTON_CODE::kTab: key = "tab"; break;
                    case RE::BS_BUTTON_CODE::kEnter: key = "enter"; break;
                    case RE::BS_BUTTON_CODE::kEscape: key = "escape"; break;
                    case RE::BS_BUTTON_CODE::kSpace: key = " "; break;
                    case RE::BS_BUTTON_CODE::kPageUp: key = "pageup"; break;
                    case RE::BS_BUTTON_CODE::kPageDown: key = "pagedown"; break;
                    case RE::BS_BUTTON_CODE::kLeft: key = "arrowleft"; break;
                    case RE::BS_BUTTON_CODE::kUp: key = "arrowup"; break;
                    case RE::BS_BUTTON_CODE::kRight: key = "arrowright"; break;
                    case RE::BS_BUTTON_CODE::kDown: key = "arrowdown"; break;
                    case RE::BS_BUTTON_CODE::kMinus: key = "-"; break;
                    default: break;
                }
            }
            if (!justPressed) {
                if (!held && !key.empty()) {
                    const std::string script = "if(window.omNativeKeyUp)window.omNativeKeyUp(\"" + key + "\");";
                    InvokeMenuScript(script.c_str());
                }
                return;
            }
            if (!key.empty()) {
                const std::string script = "if(window.omNativeKey)window.omNativeKey(\"" + key + "\");";
                InvokeMenuScript(script.c_str());
            }
            return;
        }
        mutableEvent->handled = RE::InputEvent::HANDLED_RESULT::kStop;
        const auto code = static_cast<uint32_t>(event->GetBSButtonCode());
        const bool justPressed = event->QJustPressed();
        const bool held = event->QHeldDown();
        const auto button = static_cast<RE::BS_BUTTON_CODE>(code);
        // Keep the shoulder buttons for target rotation.  LT/RT retain the
        // former LB/RB tab/category/page action below.
        const bool isRotateButton =
            button == RE::BS_BUTTON_CODE::kLShoulder ||
            button == RE::BS_BUTTON_CODE::kRShoulder;
        if (isRotateButton) {
            if (!justPressed && !held) return;
            const auto now = GetTickCount64();
            if (justPressed || now - lastRotateInputMs_ >= kRotateRepeatMs) {
                InvokeGamepadMenuScript(button == RE::BS_BUTTON_CODE::kLShoulder ?
                    "if(window.omRotateTarget)window.omRotateTarget(-1);" :
                    "if(window.omRotateTarget)window.omRotateTarget(1);");
                lastRotateInputMs_ = now;
            }
            return;
        }
        if (!justPressed) return;

        switch (static_cast<RE::BS_BUTTON_CODE>(code)) {
            case RE::BS_BUTTON_CODE::kAButton:
                InvokeGamepadMenuScript("if(window.omActivateFocused)window.omActivateFocused();");
                break;
            case RE::BS_BUTTON_CODE::kBButton:
                InvokeGamepadMenuScript("if(window.omBackOrClose)window.omBackOrClose(true);");
                break;
            case RE::BS_BUTTON_CODE::kDPAD_Up:
                InvokeGamepadMenuScript("if(window.omMoveFocus)window.omMoveFocus(-1);");
                break;
            case RE::BS_BUTTON_CODE::kDPAD_Down:
                InvokeGamepadMenuScript("if(window.omMoveFocus)window.omMoveFocus(1);");
                break;
            case RE::BS_BUTTON_CODE::kDPAD_Left:
                InvokeGamepadMenuScript("if(window.omSlotPage)window.omSlotPage(-1);");
                break;
            case RE::BS_BUTTON_CODE::kDPAD_Right:
                InvokeGamepadMenuScript("if(window.omSlotPage)window.omSlotPage(1);");
                break;
            case RE::BS_BUTTON_CODE::kLTrigger:
                InvokeGamepadMenuScript("if(window.omGamepadShoulder)window.omGamepadShoulder(-1);");
                break;
            case RE::BS_BUTTON_CODE::kRTrigger:
                InvokeGamepadMenuScript("if(window.omGamepadShoulder)window.omGamepadShoulder(1);");
                break;
            case RE::BS_BUTTON_CODE::kXButton:
                InvokeGamepadMenuScript("if(window.omContextSlot)window.omContextSlot();");
                break;
            case RE::BS_BUTTON_CODE::kYButton:
                InvokeGamepadMenuScript("if(window.omContextFunction)window.omContextFunction();");
                break;
            case RE::BS_BUTTON_CODE::kBack:
                InvokeGamepadMenuScript("if(window.omBackOrClose)window.omBackOrClose(true);");
                break;
            case RE::BS_BUTTON_CODE::kStart:
                InvokeGamepadMenuScript("if(window.omContextSave)window.omContextSave();");
                break;
            default:
                break;
        }
    }

private:
    // VK_OEM_3 (`~` / backtick), Fallout 4's default console toggle key.
    static constexpr std::int32_t kConsoleVirtualKeyCode = 0xC0;
    static constexpr ULONGLONG kRotateRepeatMs = 100;
    static constexpr ULONGLONG kThumbstickRepeatDelayMs = 280;
    static constexpr ULONGLONG kThumbstickRepeatMs = 90;
    int leftThumbstickDirection_ = 0;
    ULONGLONG leftThumbstickStartMs_ = 0;
    ULONGLONG lastLeftThumbActionMs_ = 0;
    ULONGLONG lastThumbScrollMs_ = 0;
    ULONGLONG lastRotateInputMs_ = 0;
};

static void RegisterInputSink() {
    if (g_inputSinkRegistered) return;
    if (auto* menuControls = RE::MenuControls::GetSingleton()) {
        menuControls->handlers.insert(menuControls->handlers.begin(), OutfitManagerInputHandler::GetSingleton());
        g_inputSinkRegistered = true;
    }
}

static fs::path PrimaryDataRoot() {
    return fs::path("Data") / DATA_DIR;
}

static std::string BuildUiTuningJson() {
    constexpr int kDefaultScale = 100;
    std::string raw;
    for (const auto& path : { PrimaryDataRoot() / "ui-tuning.json", fs::path(DATA_DIR) / "ui-tuning.json" }) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) continue;
        raw.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (!raw.empty()) {
            LogLine("2.0 UI tuning loaded: " + path.string());
            break;
        }
    }
    const auto clampScale = [&](std::string_view key) {
        return (std::clamp)(json::getInt(raw, key, kDefaultScale), 70, 160);
    };
    return "{\"globalScale\":" + std::to_string(clampScale("globalScale")) +
        ",\"titleScale\":" + std::to_string(clampScale("titleScale")) +
        ",\"bodyScale\":" + std::to_string(clampScale("bodyScale")) +
        ",\"menuScale\":" + std::to_string(clampScale("menuScale")) +
        ",\"auxiliaryScale\":" + std::to_string(clampScale("auxiliaryScale")) +
        ",\"footerScale\":" + std::to_string(clampScale("footerScale")) + "}";
}
static std::string ReadUiLayoutJson() {
    const auto readProfile = [](const char* filename) -> std::string {
        for (const auto& path : { PrimaryDataRoot() / filename, fs::path(DATA_DIR) / filename }) {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open()) continue;
            std::string raw{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
            if (!raw.empty()) {
                LogLine("2.0 UI layout loaded: " + path.string());
                return raw;
            }
        }
        return {};
    };
    const auto profile169 = readProfile("ui-layout-16x9.json");
    const auto profile1610 = readProfile("ui-layout-16x10.json");
    if (!profile169.empty() && !profile1610.empty()) {
        return "{\"profiles\":{\"compact16x9\":" + profile169 +
            ",\"compact16x10\":" + profile1610 +
            ",\"standard\":" + profile169 + "}}";
    }
    return "{}";
}
static void ReloadPreviewTuning() {
    std::string raw;
    for (const auto& path : { PrimaryDataRoot() / "preview-tuning.json", fs::path(DATA_DIR) / "preview-tuning.json" }) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) continue;
        raw.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
        if (!raw.empty()) {
            LogLine("2.0 preview tuning loaded: " + path.string());
            break;
        }
    }
    const auto value = [&](std::string_view key, int fallback, int minimum, int maximum) {
        return (std::clamp)(json::getInt(raw, key, fallback), minimum, maximum);
    };

    PreviewTuning tuning{};
    tuning.targetScanRadius = value("targetScanRadius", tuning.targetScanRadius, 128, 2048);
    tuning.visualScalePercent = value("visualScalePercent", tuning.visualScalePercent, 55, 180);
    tuning.cameraDistance = value("cameraDistance", tuning.cameraDistance, 120, 600);
    tuning.cameraRight = value("cameraRight", tuning.cameraRight, -280, 280);
    tuning.cameraHeight = value("cameraHeight", tuning.cameraHeight, -120, 280);
    tuning.cameraFov = value("cameraFov", tuning.cameraFov, 35, 100);
    tuning.cameraYawOffsetDegrees = value("cameraYawOffsetDegrees", tuning.cameraYawOffsetDegrees, -45, 45);
    tuning.cameraPitchDegrees = value("cameraPitchDegrees", tuning.cameraPitchDegrees, -30, 30);
    g_previewTuning = tuning;
}
static std::string EscapeJavaScriptString(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 16);
    for (const char ch : text) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped += ch; break;
        }
    }
    return escaped;
}
static fs::path SlotPath(int s) {
    auto b = PrimaryDataRoot(); std::error_code ec; fs::create_directories(b, ec);
    char fn[32]; snprintf(fn, 32, "slot%03d.json", s); return b / fn;
}

static fs::path SlotNamesPath() {
    auto base = PrimaryDataRoot();
    std::error_code ec;
    fs::create_directories(base, ec);
    return base / "slot-names.txt";
}

static fs::path LegacySlotNamesPath() {
    return PrimaryDataRoot() / "slot-names.json";
}

static std::string CleanSlotName(std::string name) {
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char ch) {
        return ch < 0x20 || ch == 0x7F;
    }), name.end());
    const auto first = name.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const auto last = name.find_last_not_of(" \t");
    name = name.substr(first, last - first + 1);
    if (name.size() > 96) {
        std::size_t offset = 0;
        std::size_t validBytes = 0;
        while (offset < name.size() && offset < 96) {
            const auto lead = static_cast<unsigned char>(name[offset]);
            const std::size_t width = (lead & 0x80) == 0 ? 1 :
                (lead & 0xE0) == 0xC0 ? 2 : (lead & 0xF0) == 0xE0 ? 3 : (lead & 0xF8) == 0xF0 ? 4 : 1;
            if (offset + width > 96 || offset + width > name.size()) break;
            validBytes = offset + width;
            offset += width;
        }
        name.resize(validBytes);
    }
    return name;
}

static bool LoadSlotNames() {
    g_slotNames.fill({});
    std::ifstream file(SlotNamesPath(), std::ios::binary);
    bool loadedLegacyJson = false;
    if (!file.is_open()) {
        file.clear();
        file.open(LegacySlotNamesPath(), std::ios::binary);
        loadedLegacyJson = file.is_open();
    }
    if (!file.is_open()) return false;
    const std::string raw{ std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
    if (loadedLegacyJson) {
        for (int slot = 1; slot <= MAX_SLOTS; ++slot) {
            char key[8];
            std::snprintf(key, sizeof(key), "%03d", slot);
            g_slotNames[slot] = CleanSlotName(json::getStr(raw, key));
        }
        return false; // Ask the caller to write the friendlier text format.
    }
    std::istringstream lines(raw);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#') continue;
        const auto equals = line.find('=', first);
        if (equals == std::string::npos) continue;
        const auto key = CleanSlotName(line.substr(first, equals - first));
        int slot = 0;
        try { slot = std::stoi(key); } catch (...) { continue; }
        if (slot >= 1 && slot <= MAX_SLOTS) g_slotNames[slot] = CleanSlotName(line.substr(equals + 1));
    }
    return true;
}

static bool WriteSlotNames() {
    std::ostringstream file;
    file << "# OutfitManager slot names (UTF-8)\n";
    file << "# Format: 001 = Name. Leave the value empty to use the default slot name.\n";
    for (int slot = 1; slot <= MAX_SLOTS; ++slot) {
        char key[8];
        std::snprintf(key, sizeof(key), "%03d", slot);
        file << key << " = " << g_slotNames[slot] << "\n";
    }
    return WriteTextFileAtomic(SlotNamesPath(), file.str());
}

static std::string ReadSlotName(int s) {
    return s >= 1 && s <= MAX_SLOTS ? g_slotNames[s] : std::string{};
}

static bool WriteSlotName(int s, std::string name) {
    if (s < 1 || s > MAX_SLOTS) return false;
    g_slotNames[s] = CleanSlotName(std::move(name));
    return WriteSlotNames();
}

static uint32_t HexToUInt(std::string_view value) {
    if (value.empty()) return 0;
    try {
        return static_cast<uint32_t>(std::stoul(std::string(value), nullptr, 16));
    } catch (...) {
        return 0;
    }
}

static RE::TESForm* ResolveSavedForm(std::string_view plugin, std::string_view localHex, std::string_view fullHex) {
    uint32_t localID = HexToUInt(localHex);
    if (!plugin.empty() && plugin != "Unknown.esm" && localID != 0) {
        if (auto* data = RE::TESDataHandler::GetSingleton()) {
            auto* file = data->LookupModByName(plugin);
            if (!file || !file->IsActive()) return nullptr;
            const auto normalizedLocalID = file->IsLight() ? localID & 0xFFF : localID & 0xFFFFFF;
            return normalizedLocalID != 0 ? data->LookupForm(normalizedLocalID, plugin) : nullptr;
        }
        // A known plugin identity is authoritative. Falling back to the old
        // load-order FormID can resolve an unrelated form after a mod is
        // removed or reordered.
        return nullptr;
    }

    uint32_t formID = HexToUInt(fullHex);
    return formID != 0 ? RE::TESForm::GetFormByID(formID) : nullptr;
}

static size_t FindMatchingToken(std::string_view text, size_t openPos, char openToken, char closeToken) {
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t i = openPos; i < text.size(); ++i) {
        const char ch = text[i];
        if (inString) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') inString = false;
            continue;
        }
        if (ch == '"') {
            inString = true;
        } else if (ch == openToken) {
            ++depth;
        } else if (ch == closeToken && --depth == 0) {
            return i;
        }
    }
    return std::string_view::npos;
}

static void GetSavedFormIdentity(RE::TESForm* form, std::string& plugin, std::string& localHex, std::string& fullHex) {
    plugin = "Unknown.esm";
    localHex = "000000";
    fullHex = "00000000";
    if (!form) return;
    const uint32_t id = form->GetFormID();
    char buf[16]{};
    auto* file = form->GetFile(0);
    const auto localID = file && file->IsLight() ? id & 0xFFF : id & 0xFFFFFF;
    std::snprintf(buf, sizeof(buf), "%06X", localID);
    localHex = buf;
    std::snprintf(buf, sizeof(buf), "%08X", id);
    fullHex = buf;
    if (file) plugin = file->GetFilename();
}

static std::string GetFormEditorIdentity(RE::TESForm* form) {
    const char* editorID = form ? form->GetFormEditorID() : nullptr;
    return editorID ? editorID : "";
}

static std::string GetBaseFormName(RE::TESForm* form) {
    const char* name = nullptr;
    if (auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr) {
        name = armor->GetFullName();
    } else if (auto* weapon = form ? form->As<RE::TESObjectWEAP>() : nullptr) {
        name = weapon->GetFullName();
    }
    return name ? name : "";
}

static void CollapseRepeatedDisplayTags(std::string& display) {
    for (;;) {
        bool collapsed = false;
        std::size_t open = display.find(" [");
        while (open != std::string::npos) {
            const auto close = display.find(']', open + 2);
            if (close == std::string::npos) break;
            const auto tagLength = close - open + 1;
            const auto next = close + 1;
            if (next + tagLength <= display.size() &&
                display.compare(open, tagLength, display, next, tagLength) == 0) {
                display.erase(next, tagLength);
                collapsed = true;
                break;
            }
            open = display.find(" [", close + 1);
        }
        if (!collapsed) break;
    }
}

static void AppendDisplayLabels(std::string& display, const std::vector<std::string>& labels) {
    CollapseRepeatedDisplayTags(display);
    for (const auto& label : labels) {
        if (label.empty()) continue;
        const std::string tag = " [" + label + "]";
        if (display.find(tag) == std::string::npos) display += tag;
    }
    CollapseRepeatedDisplayTags(display);
}

static bool MatchesSavedFormMetadata(
    RE::TESForm* form,
    std::string_view entry,
    RE::ENUM_FORM_ID expectedType)
{
    if (!form || form->GetFormType() != expectedType) return false;

    const int savedType = json::getInt(entry, "formType", -1);
    if (savedType >= 0 &&
        savedType != static_cast<int>(form->GetFormType())) {
        return false;
    }

    const auto savedEditorID = json::getStr(entry, "editorID");
    if (!savedEditorID.empty()) {
        return savedEditorID == GetFormEditorIdentity(form);
    }

    const auto savedBaseName = json::getStr(entry, "baseName");
    if (!savedBaseName.empty() &&
        savedBaseName != GetBaseFormName(form)) {
        return false;
    }
    return true;
}

static std::string SavedItemDisplayName(const OE& item) {
    std::string display = item.name.empty() ? "Unknown" : item.name;
    if (item.weapon) {
        if (!item.mods.empty()) AppendDisplayLabels(display, { "Modified" });
        return display;
    }
    std::vector<std::string> labels;
    for (const auto& savedMod : item.mods) {
        if (savedMod.disabled) continue;
        auto* modForm = RE::TESForm::GetFormByID(savedMod.fid);
        auto* objectMod = modForm ? modForm->As<RE::BGSMod::Attachment::Mod>() : nullptr;
        const char* fullName = objectMod ? objectMod->GetFullName() : nullptr;
        std::string label = fullName && fullName[0] != '\0' ? fullName : "OMOD " + FormIDHex(savedMod.fid);
        if (std::find(labels.begin(), labels.end(), label) == labels.end()) labels.push_back(std::move(label));
    }
    if (item.color) labels.push_back("Color " + std::to_string(*item.color));
    AppendDisplayLabels(display, labels);
    return display;
}

static std::string AozoraGeneratedItemName(std::string name) {
    constexpr std::string_view suffix = " ◆";
    if (name.empty()) name = "Unknown";
    if (name.size() < suffix.size() ||
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) {
        name += suffix;
    }
    return name;
}

static int ReadSlotUsageCount(int slot) {
    if (slot < 1 || slot > MAX_SLOTS) return 0;
    std::ifstream file(SlotPath(slot), std::ios::binary);
    if (!file.is_open()) return 0;
    const std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    return (std::max)(0, json::getInt(content, "usageCount", 0));
}

static bool WriteSlotUsageCount(int slot, int usageCount) {
    if (slot < 1 || slot > MAX_SLOTS) return false;
    const auto path = SlotPath(slot);
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;
    std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();
    usageCount = (std::max)(0, usageCount);

    const auto key = content.find("\"usageCount\"");
    if (key != std::string::npos) {
        const auto colon = content.find(':', key);
        if (colon == std::string::npos) return false;
        auto valueBegin = content.find_first_not_of(" \t\r\n", colon + 1);
        if (valueBegin == std::string::npos) return false;
        auto valueEnd = valueBegin;
        if (content[valueEnd] == '+' || content[valueEnd] == '-') ++valueEnd;
        while (valueEnd < content.size() && std::isdigit(static_cast<unsigned char>(content[valueEnd]))) ++valueEnd;
        content.replace(valueBegin, valueEnd - valueBegin, std::to_string(usageCount));
    } else {
        const auto itemsKey = content.find("\"items\"");
        if (itemsKey == std::string::npos) return false;
        const auto lineBegin = content.rfind('\n', itemsKey);
        const auto insertAt = lineBegin == std::string::npos ? itemsKey : lineBegin + 1;
        content.insert(insertAt, "  \"usageCount\": " + std::to_string(usageCount) + ",\n");
    }
    return WriteTextFileAtomic(path, content);
}

static bool IncrementSlotUsageCount(int slot) {
    const int current = ReadSlotUsageCount(slot);
    const int next = current < (std::numeric_limits<int>::max)() ? current + 1 : current;
    const bool written = WriteSlotUsageCount(slot, next);
    if (written) {
        if (auto found = g_indexBySlot.find(slot); found != g_indexBySlot.end()) found->second.usageCount = next;
        if (auto found = std::find_if(g_index.begin(), g_index.end(), [slot](const SlotInfo& info) {
                return info.slot == slot;
            }); found != g_index.end()) {
            found->usageCount = next;
        }
        LogLine("2.0 outfit usage increment slot=" + std::to_string(slot) +
            " count=" + std::to_string(next));
    }
    return written;
}

static bool WriteSlot(int s, int g, const std::vector<OE>& items) {
    for (const auto& item : items) {
        auto* form = RE::TESForm::GetFormByID(item.fid);
        if (!form ||
            form->GetFormType() !=
                (item.weapon ? RE::ENUM_FORM_ID::kWEAP : RE::ENUM_FORM_ID::kARMO)) {
            return false;
        }
        for (const auto& mod : item.mods) {
            auto* modForm = RE::TESForm::GetFormByID(mod.fid);
            if (!modForm || modForm->GetFormType() != RE::ENUM_FORM_ID::kOMOD) return false;
        }
    }

    const int usageCount = ReadSlotUsageCount(s);
    std::string body;
    {
        std::ostringstream out;
        out << "{\n  \"version\": 6,\n  \"slot\": " << s << ",\n  \"gender\": " << g
            << ",\n  \"usageCount\": " << usageCount << ",\n  \"items\": [\n";
    for (size_t i = 0; i < items.size(); ++i) {
        auto* form = RE::TESForm::GetFormByID(items[i].fid);
        const std::string savedName = GetBaseFormName(form).empty() ? items[i].name : GetBaseFormName(form);
        std::string plugin, localHex, fullHex;
        GetSavedFormIdentity(form, plugin, localHex, fullHex);
        out << "    { \"plugin\": \"" << json::esc(plugin)
           << "\", \"localID\": \"" << localHex
           << "\", \"formID\": \"" << fullHex
           << "\", \"name\": \"" << json::esc(savedName)
           << "\", \"baseName\": \"" << json::esc(GetBaseFormName(form))
           << "\", \"editorID\": \"" << json::esc(GetFormEditorIdentity(form))
           << "\", \"formType\": " << static_cast<int>(form->GetFormType());
        if (items[i].weapon) out << ", \"type\": \"weapon\"";
        if (items[i].color) out << ", \"color\": " << *items[i].color;
        if (items[i].modSnapshot) out << ", \"modSnapshot\": 1";
        if (items[i].modSnapshot || !items[i].mods.empty()) {
            out << ", \"mods\": [";
            for (size_t modIndex = 0; modIndex < items[i].mods.size(); ++modIndex) {
                const auto& mod = items[i].mods[modIndex];
                std::string modPlugin, modLocalHex, modFullHex;
                GetSavedFormIdentity(RE::TESForm::GetFormByID(mod.fid), modPlugin, modLocalHex, modFullHex);
                auto* modForm = RE::TESForm::GetFormByID(mod.fid);
                if (modIndex) out << ",";
                out << "{ \"plugin\": \"" << json::esc(modPlugin)
                    << "\", \"localID\": \"" << modLocalHex
                    << "\", \"formID\": \"" << modFullHex
                    << "\", \"editorID\": \"" << json::esc(GetFormEditorIdentity(modForm))
                    << "\", \"formType\": " << static_cast<int>(modForm->GetFormType())
                    << ", \"index\": " << static_cast<int>(mod.index)
                    << ", \"rank\": " << static_cast<int>(mod.rank)
                    << ", \"disabled\": " << (mod.disabled ? 1 : 0)
                    << " }";
            }
            out << "]";
        }
        out << " }";
        if (i < items.size() - 1) out << ",";
        out << "\n";
    }
        out << "  ]\n}\n";
        body = out.str();
    }
    return WriteTextFileAtomic(SlotPath(s), body);
}

static bool ReadSlotMetadata(
    int s,
    int& gender,
    std::vector<std::string>& itemNames,
    std::string& summary,
    int* usageCountOut = nullptr)
{
    std::ifstream file(SlotPath(s), std::ios::binary);
    if (!file.is_open()) return false;
    const std::string content(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    gender = json::getInt(content, "gender", -1);
    if (usageCountOut) *usageCountOut = (std::max)(0, json::getInt(content, "usageCount", 0));
    itemNames.clear();
    summary.clear();

    auto itemsKey = content.find("\"items\"");
    if (itemsKey == std::string::npos) return false;
    const auto itemsOpen = content.find('[', itemsKey);
    if (itemsOpen == std::string::npos) return false;
    const auto itemsEnd = FindMatchingToken(content, itemsOpen, '[', ']');
    if (itemsEnd == std::string::npos) return false;

    std::size_t position = itemsOpen + 1;
    while (position < itemsEnd) {
        const auto entryBegin = content.find('{', position);
        if (entryBegin == std::string::npos || entryBegin >= itemsEnd) break;
        const auto entryEnd = FindMatchingToken(content, entryBegin, '{', '}');
        if (entryEnd == std::string::npos || entryEnd > itemsEnd) return false;

        const std::string entry = content.substr(entryBegin, entryEnd - entryBegin + 1);
        std::string displayName = json::getStr(entry, "name");
        if (displayName.empty()) displayName = "Unknown";
        std::vector<std::string> displayLabels;
        const auto addDisplayMod = [&displayLabels](RE::TESForm* modForm) {
            auto* objectMod = modForm ? modForm->As<RE::BGSMod::Attachment::Mod>() : nullptr;
            const char* fullName = objectMod ? objectMod->GetFullName() : nullptr;
            if (!fullName || fullName[0] == '\0') return;
            if (std::find(displayLabels.begin(), displayLabels.end(), fullName) == displayLabels.end()) {
                displayLabels.emplace_back(fullName);
            }
        };

        const auto modsKey = entry.find("\"mods\"");
        if (modsKey != std::string::npos) {
            const auto modsOpen = entry.find('[', modsKey);
            const auto modsEnd = modsOpen == std::string::npos ?
                std::string::npos :
                FindMatchingToken(entry, modsOpen, '[', ']');
            if (modsEnd != std::string::npos) {
                const auto firstMod = entry.find_first_not_of(" \t\r\n", modsOpen + 1);
                if (firstMod != std::string::npos && firstMod < modsEnd) {
                    if (entry[firstMod] == '"') {
                        std::size_t modPos = firstMod;
                        while (modPos < modsEnd) {
                            const auto quoteBegin = entry.find('"', modPos);
                            if (quoteBegin == std::string::npos || quoteBegin >= modsEnd) break;
                            const auto quoteEnd = entry.find('"', quoteBegin + 1);
                            if (quoteEnd == std::string::npos || quoteEnd > modsEnd) break;
                            std::vector<std::string> fields;
                            std::stringstream tokenStream(entry.substr(quoteBegin + 1, quoteEnd - quoteBegin - 1));
                            std::string field;
                            while (std::getline(tokenStream, field, '|')) fields.push_back(field);
                            if (fields.size() >= 6 && fields[5] == "0") {
                                addDisplayMod(ResolveSavedForm(fields[0], fields[1], fields[2]));
                            }
                            modPos = quoteEnd + 1;
                        }
                    } else {
                        std::size_t modPos = firstMod;
                        while (modPos < modsEnd) {
                            const auto modBegin = entry.find('{', modPos);
                            if (modBegin == std::string::npos || modBegin >= modsEnd) break;
                            const auto modEnd = FindMatchingToken(entry, modBegin, '{', '}');
                            if (modEnd == std::string::npos || modEnd > modsEnd) break;
                            const auto modEntry = entry.substr(modBegin, modEnd - modBegin + 1);
                            if (json::getInt(modEntry, "disabled", 0) == 0) {
                                addDisplayMod(ResolveSavedForm(
                                    json::getStr(modEntry, "plugin"),
                                    json::getStr(modEntry, "localID"),
                                    json::getStr(modEntry, "formID")));
                            }
                            modPos = modEnd + 1;
                        }
                    }
                }
            }
        }
        AppendDisplayLabels(displayName, displayLabels);
        itemNames.push_back(std::move(displayName));
        position = entryEnd + 1;
    }

    for (std::size_t i = 0; i < itemNames.size() && i < 5; ++i) {
        if (i) summary += ", ";
        summary += itemNames[i];
    }
    if (itemNames.size() > 5) summary += "...";
    return !itemNames.empty();
}

static bool ReadSlot(
    int s,
    int& g,
    std::vector<OE>& items,
    std::string& sum,
    SlotReadStatus* status = nullptr)
{
    auto p = SlotPath(s); std::ifstream f(p);
    if (!f.is_open()) return false;
    std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    g = json::getInt(c, "gender", -1);
    auto ip = c.find("\"items\"");
    if (ip == std::string::npos) return false;
    ip = c.find('[', ip);
    if (ip == std::string::npos) return false;
    items.clear();
    const auto itemsEnd = FindMatchingToken(c, ip, '[', ']');
    if (itemsEnd == std::string::npos) return false;
    SlotReadStatus localStatus;
    size_t pos = ip + 1;
    while (pos < itemsEnd) {
        auto b = c.find('{', pos); if (b == std::string::npos || b >= itemsEnd) break;
        auto e = FindMatchingToken(c, b, '{', '}'); if (e == std::string::npos || e > itemsEnd) break;
        std::string entry = c.substr(b, e - b + 1);
        std::string plugin = json::getStr(entry, "plugin");
        std::string localHex = json::getStr(entry, "localID");
        std::string fullHex = json::getStr(entry, "formID");
        std::string nm = json::getStr(entry, "name");
        const bool savedWeapon = json::getStr(entry, "type") == "weapon";
        auto* form = ResolveSavedForm(plugin, localHex, fullHex);
        if (!MatchesSavedFormMetadata(
                form,
                entry,
                savedWeapon ? RE::ENUM_FORM_ID::kWEAP : RE::ENUM_FORM_ID::kARMO)) {
            form = nullptr;
        }
        const bool ignoredArmor = !savedWeapon && IsIgnoredOutfitArmor(form);
        if (ignoredArmor) form = nullptr;
        uint32_t fid = form ? form->GetFormID() : 0;
        if (fid != 0) {
            OE outfitItem;
            outfitItem.fid = fid;
            outfitItem.name = nm;
            outfitItem.weapon = savedWeapon;
            outfitItem.modSnapshot = json::getInt(entry, "modSnapshot", 0) != 0;
            const auto colorKey = entry.find("\"color\"");
            if (colorKey != std::string::npos) {
                const auto colon = entry.find(':', colorKey);
                if (colon != std::string::npos) {
                    try { outfitItem.color = std::stof(entry.substr(colon + 1)); } catch (...) {}
                }
            }
            const auto modsKey = entry.find("\"mods\"");
            if (modsKey != std::string::npos) {
                outfitItem.modSnapshot = true;
                const auto modsOpen = entry.find('[', modsKey);
                const auto modsEnd = modsOpen == std::string::npos ? std::string::npos : FindMatchingToken(entry, modsOpen, '[', ']');
                size_t modPos = modsOpen == std::string::npos ? entry.size() : modsOpen + 1;
                size_t firstValue = modPos;
                while (modsEnd != std::string::npos && firstValue < modsEnd && std::isspace(static_cast<unsigned char>(entry[firstValue]))) ++firstValue;
                if (modsEnd != std::string::npos && firstValue < modsEnd && entry[firstValue] == '"') {
                    modPos = firstValue;
                    while (modPos < modsEnd) {
                        const auto quoteBegin = entry.find('"', modPos);
                        if (quoteBegin == std::string::npos || quoteBegin >= modsEnd) break;
                        const auto quoteEnd = entry.find('"', quoteBegin + 1);
                        if (quoteEnd == std::string::npos || quoteEnd > modsEnd) break;
                        std::vector<std::string> fields;
                        std::stringstream tokenStream(entry.substr(quoteBegin + 1, quoteEnd - quoteBegin - 1));
                        std::string field;
                        while (std::getline(tokenStream, field, '|')) fields.push_back(field);
                        if (fields.size() >= 6) {
                            auto* modForm = ResolveSavedForm(fields[0], fields[1], fields[2]);
                            if (!modForm || modForm->GetFormType() != RE::ENUM_FORM_ID::kOMOD) {
                                modForm = nullptr;
                            }
                            const uint32_t modFormID = modForm ? modForm->GetFormID() : 0;
                            if (modFormID != 0) {
                                try {
                                    outfitItem.mods.push_back({
                                        modFormID,
                                        static_cast<std::uint8_t>(std::clamp(std::stoi(fields[3]), 0, 255)),
                                        static_cast<std::uint8_t>(std::clamp(std::stoi(fields[4]), 0, 255)),
                                        std::stoi(fields[5]) != 0
                                    });
                                } catch (...) {}
                            } else {
                                ++localStatus.missingMods;
                            }
                        }
                        modPos = quoteEnd + 1;
                    }
                } else while (modsEnd != std::string::npos && modPos < modsEnd) {
                    const auto modBegin = entry.find('{', modPos);
                    if (modBegin == std::string::npos || modBegin >= modsEnd) break;
                    const auto modEnd = FindMatchingToken(entry, modBegin, '{', '}');
                    if (modEnd == std::string::npos || modEnd > modsEnd) break;
                    const std::string modEntry = entry.substr(modBegin, modEnd - modBegin + 1);
                    auto* modForm = ResolveSavedForm(json::getStr(modEntry, "plugin"), json::getStr(modEntry, "localID"), json::getStr(modEntry, "formID"));
                    if (!MatchesSavedFormMetadata(
                            modForm,
                            modEntry,
                            RE::ENUM_FORM_ID::kOMOD)) {
                        modForm = nullptr;
                    }
                    const uint32_t modFormID = modForm ? modForm->GetFormID() : 0;
                    if (modFormID != 0) {
                        outfitItem.mods.push_back({
                            modFormID,
                            static_cast<std::uint8_t>(std::clamp(json::getInt(modEntry, "index", 0), 0, 255)),
                            static_cast<std::uint8_t>(std::clamp(json::getInt(modEntry, "rank", 0), 0, 255)),
                            json::getInt(modEntry, "disabled", 0) != 0
                        });
                    } else {
                        ++localStatus.missingMods;
                    }
                    modPos = modEnd + 1;
                }
            }
            items.push_back(std::move(outfitItem));
        } else if (!ignoredArmor) {
            ++localStatus.missingItems;
        }
        pos = e + 1;
    }
    if (localStatus.RemovedCount() > 0) {
        LogLine("2.0 unresolved slot records detected slot=" + std::to_string(s) +
            " items=" + std::to_string(localStatus.missingItems) +
            " mods=" + std::to_string(localStatus.missingMods) +
            " preserved=1");
    }
    if (status) *status = localStatus;
    // Build a human-readable summary that distinguishes material/OMOD variants.
    sum.clear();
    for (size_t i = 0; i < items.size() && i < 5; ++i) {
        if (i) sum += ", ";
        sum += SavedItemDisplayName(items[i]);
    }
    if (items.size() > 5) sum += "...";
    return !items.empty();
}

static bool DelSlot(int s) {
    std::error_code ec;
    const bool removed = fs::remove(SlotPath(s), ec);
    if (s >= 1 && s <= MAX_SLOTS) {
        g_slotNames[s].clear();
        WriteSlotNames();
    }
    return removed && !ec;
}

// ======== Index & State Persistence ========
static void RebuildIndexCache() {
    g_index.clear();
    g_indexBySlot.clear();
    g_slotDetails.clear();
    g_randomBags.clear();
    for (int i = 1; i <= MAX_SLOTS; ++i) {
        int g = -1;
        int usageCount = 0;
        std::vector<std::string> itemNames;
        std::string sum;
        if (ReadSlotMetadata(i, g, itemNames, sum, &usageCount) && g >= 0) {
            SlotInfo info;
            info.slot = i;
            info.gender = g;
            info.count = static_cast<int>(itemNames.size());
            info.usageCount = usageCount;
            SlotDetail detail;
            detail.name = ReadSlotName(i);
            detail.summary = sum;
            detail.itemNames = std::move(itemNames);
            g_index.push_back(info);
            g_indexBySlot[i] = info;
            g_slotDetails[i] = std::move(detail);
        }
    }
}

static bool LoadIndexCache() {
    auto p = PrimaryDataRoot() / "index.json";
    std::ifstream f(p);
    if (!f.is_open()) return false;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();

    const auto slotsKey = content.find("\"slots\"");
    const auto arrayBegin = slotsKey == std::string::npos ? std::string::npos : content.find('[', slotsKey);
    const auto arrayEnd = arrayBegin == std::string::npos ? std::string::npos : content.find(']', arrayBegin);
    if (arrayBegin == std::string::npos || arrayEnd == std::string::npos) return false;

    g_index.clear();
    g_indexBySlot.clear();
    g_slotDetails.clear();
    g_randomBags.clear();

    std::size_t pos = arrayBegin + 1;
    while (pos < arrayEnd) {
        const auto objectBegin = content.find('{', pos);
        if (objectBegin == std::string::npos || objectBegin >= arrayEnd) break;
        const auto objectEnd = content.find('}', objectBegin);
        if (objectEnd == std::string::npos || objectEnd > arrayEnd) return false;

        const std::string entry = content.substr(objectBegin, objectEnd - objectBegin + 1);
        const int slot = json::getInt(entry, "slot", 0);
        int gender = -1;
        int usageCount = 0;
        std::vector<std::string> itemNames;
        std::string summary;
        if (slot >= 1 && slot <= MAX_SLOTS &&
            ReadSlotMetadata(slot, gender, itemNames, summary, &usageCount) &&
            gender >= 0) {
            SlotInfo info{ slot, gender, static_cast<int>(itemNames.size()), usageCount };
            SlotDetail detail;
            detail.name = ReadSlotName(slot);
            detail.summary = summary;
            detail.itemNames = std::move(itemNames);
            g_index.push_back(info);
            g_indexBySlot[slot] = info;
            g_slotDetails[slot] = std::move(detail);
        }
        pos = objectEnd + 1;
    }

    std::sort(g_index.begin(), g_index.end(), [](const SlotInfo& lhs, const SlotInfo& rhs) {
        return lhs.slot < rhs.slot;
    });

    // An interrupted index write must not hide a newly created slot forever.
    // Probe only unlisted files as raw JSON metadata; no game Form is resolved.
    for (int slot = 1; slot <= MAX_SLOTS; ++slot) {
        if (g_indexBySlot.contains(slot)) continue;
        int gender = -1;
        std::vector<std::string> itemNames;
        std::string summary;
        if (ReadSlotMetadata(slot, gender, itemNames, summary) && gender >= 0) {
            return false;
        }
    }
    return true;
}

static void WriteIndex() {
    RebuildIndexCache();
    std::ostringstream f;
    f << "{\n  \"version\": 1,\n  \"maxSlot\": " << MAX_SLOTS << ",\n  \"slots\": [";
    bool first = true;
    for (const auto& info : g_index) {
        if (!first) f << ",";
        f << "\n    { \"slot\": " << info.slot << ", \"gender\": " << info.gender
          << ", \"count\": " << info.count << ", \"usageCount\": " << info.usageCount << " }";
        first = false;
    }
    f << (first ? "" : "\n  ") << "]\n}\n";
    WriteTextFileAtomic(PrimaryDataRoot() / "index.json", f.str());
}

static bool BackupSlotBeforeCleanup(int slot, fs::path& backupPath) {
    const auto source = SlotPath(slot);
    std::error_code ec;
    if (!fs::exists(source, ec) || ec) return false;

    const auto recoveryRoot = PrimaryDataRoot() / "recovery";
    fs::create_directories(recoveryRoot, ec);
    if (ec) return false;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    char filename[128]{};
    std::snprintf(
        filename,
        sizeof(filename),
        "slot%03d-%04u%02u%02u-%02u%02u%02u-%llu.json",
        slot,
        static_cast<unsigned>(now.wYear),
        static_cast<unsigned>(now.wMonth),
        static_cast<unsigned>(now.wDay),
        static_cast<unsigned>(now.wHour),
        static_cast<unsigned>(now.wMinute),
        static_cast<unsigned>(now.wSecond),
        static_cast<unsigned long long>(GetTickCount64()));
    backupPath = recoveryRoot / filename;
    return fs::copy_file(source, backupPath, fs::copy_options::none, ec) && !ec;
}

static bool PersistResolvedSlotRecords(
    int slot,
    int gender,
    const std::vector<OE>& resolvedItems,
    const SlotReadStatus& status)
{
    if (status.RemovedCount() <= 0) return true;

    fs::path backupPath;
    if (!BackupSlotBeforeCleanup(slot, backupPath)) {
        LogLine("2.0 explicit slot cleanup backup failed slot=" + std::to_string(slot));
        return false;
    }
    if (!WriteSlot(slot, gender, resolvedItems)) {
        LogLine("2.0 explicit slot cleanup write failed slot=" + std::to_string(slot) +
            " backup=" + backupPath.string());
        return false;
    }

    WriteIndex();
    LogLine("2.0 explicit slot cleanup complete slot=" + std::to_string(slot) +
        " items=" + std::to_string(status.missingItems) +
        " mods=" + std::to_string(status.missingMods) +
        " backup=" + backupPath.string());
    return true;
}

static void WriteState() {
    std::ostringstream f;
    f << "{\n  \"version\": 3,\n"
      << "  \"currentSlot\": " << g_curSlot << "\n}\n";
    WriteTextFileAtomic(PrimaryDataRoot() / "state.json", f.str());
}

static void LoadState() {
    auto p = PrimaryDataRoot() / "state.json";
    std::ifstream f(p); if (!f.is_open()) return;
    std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    int cs = json::getInt(c, "currentSlot", 1);
    if (cs >= 1 && cs <= MAX_SLOTS) g_curSlot = cs;
}

static int GetActiveSlotForActor(RE::Actor* actor) {
    if (!actor) return 0;
    int slot = 0;
    {
        std::lock_guard<std::mutex> lock(g_mx);
        const auto active = g_lastAppliedSlots.find(actor->GetFormID());
        if (active == g_lastAppliedSlots.end()) return 0;
        slot = active->second;
    }
    const auto it = g_indexBySlot.find(slot);
    const int sex = GetSex(actor);
    return it != g_indexBySlot.end() &&
        it->second.gender == sex &&
        it->second.count > 0 ? slot : 0;
}

static void SetActiveSlotForActor(RE::Actor* actor, int slot) {
    if (!actor || slot < 1 || slot > MAX_SLOTS) return;
    {
        std::lock_guard<std::mutex> lock(g_mx);
        g_mi.try_emplace(actor->GetFormID());
        g_lastAppliedSlots[actor->GetFormID()] = slot;
    }
    g_lastEquippedSlot = slot;
}

static void ClearActiveSlotReferences(int slot) {
    {
        std::lock_guard<std::mutex> lock(g_mx);
        for (auto it = g_lastAppliedSlots.begin(); it != g_lastAppliedSlots.end();) {
            if (it->second == slot) {
                it = g_lastAppliedSlots.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (g_lastEquippedSlot == slot) g_lastEquippedSlot = 0;
}

static int FindFirstEmptySlot() {
    EnsureCacheLoaded();
    for (int slot = 1; slot <= MAX_SLOTS; ++slot) {
        if (!g_indexBySlot.contains(slot)) return slot;
    }
    return 0;
}

static void WriteActors() {
    // Actor ownership is save-specific and is persisted through F4SE
    // serialization. Never write it to the shared outfit data directory.
}

static void EnsureCacheLoaded() {
    if (g_cacheLoaded) return;
    LoadState();
    // Under MO2 this is created in overwrite, beside slotXXX.json rather
    // than in the mod package.
    if (!LoadSlotNames()) WriteSlotNames();
    if (!LoadIndexCache()) RebuildIndexCache();
    g_cacheLoaded = true;
}

static constexpr std::uint32_t MakeRecordType(char a, char b, char c, char d) {
    return static_cast<std::uint32_t>(a) |
        (static_cast<std::uint32_t>(b) << 8) |
        (static_cast<std::uint32_t>(c) << 16) |
        (static_cast<std::uint32_t>(d) << 24);
}

static constexpr std::uint32_t kActorStateRecord = MakeRecordType('A', 'O', 'M', 'S');
static constexpr std::uint32_t kActorStateVersion = 1;

template <class T>
static bool ReadSerializedValue(const F4SE::SerializationInterface* intfc, T& value) {
    return intfc && intfc->ReadRecordData(value) == sizeof(T);
}

static bool WriteSerializedString(
    const F4SE::SerializationInterface* intfc,
    std::string_view value)
{
    const auto length = static_cast<std::uint32_t>((std::min)(value.size(), std::size_t{ 1024 }));
    return intfc->WriteRecordData(length) &&
        (length == 0 || intfc->WriteRecordData(value.data(), length));
}

static bool ReadSerializedString(
    const F4SE::SerializationInterface* intfc,
    std::string& value)
{
    std::uint32_t length = 0;
    if (!ReadSerializedValue(intfc, length) || length > 1024) return false;
    value.assign(length, '\0');
    return length == 0 || intfc->ReadRecordData(value.data(), length) == length;
}

static void F4SEAPI SaveActorState(const F4SE::SerializationInterface* intfc) {
    if (!intfc || !intfc->OpenRecord(kActorStateRecord, kActorStateVersion)) return;
    std::lock_guard<std::mutex> lock(g_mx);
    const auto actorCount = static_cast<std::uint32_t>(g_mi.size());
    intfc->WriteRecordData(actorCount);
    for (const auto& [actorID, items] : g_mi) {
        intfc->WriteRecordData(actorID);
        const std::uint8_t hasDefault = items.dr ? 1 : 0;
        intfc->WriteRecordData(hasDefault);
        const auto writeIDs = [intfc](const std::vector<std::uint32_t>& ids) {
            const auto count = static_cast<std::uint32_t>((std::min)(ids.size(), std::size_t{ 256 }));
            intfc->WriteRecordData(count);
            for (std::uint32_t index = 0; index < count; ++index) {
                intfc->WriteRecordData(ids[index]);
            }
        };
        writeIDs(items.d);
        writeIDs(items.m);
        const int lastSlot = g_lastAppliedSlots.contains(actorID) ? g_lastAppliedSlots.at(actorID) : 0;
        intfc->WriteRecordData(lastSlot);

        const auto refs = g_managedInstances.find(actorID);
        const auto refCount = refs == g_managedInstances.end() ? 0u :
            static_cast<std::uint32_t>((std::min)(refs->second.size(), std::size_t{ 256 }));
        intfc->WriteRecordData(refCount);
        if (refs != g_managedInstances.end()) {
            for (std::uint32_t refIndex = 0; refIndex < refCount; ++refIndex) {
                const auto& ref = refs->second[refIndex];
                intfc->WriteRecordData(ref.fid);
                WriteSerializedString(intfc, ref.name);
                const auto modCount = static_cast<std::uint32_t>((std::min)(ref.mods.size(), std::size_t{ 256 }));
                intfc->WriteRecordData(modCount);
                for (std::uint32_t modIndex = 0; modIndex < modCount; ++modIndex) {
                    const auto& mod = ref.mods[modIndex];
                    intfc->WriteRecordData(mod.fid);
                    intfc->WriteRecordData(mod.index);
                    intfc->WriteRecordData(mod.rank);
                    const std::uint8_t disabled = mod.disabled ? 1 : 0;
                    intfc->WriteRecordData(disabled);
                }
            }
        }
    }

    const auto playerAmmoCount = static_cast<std::uint32_t>(
        (std::min)(g_playerGrantedAmmo.size(), std::size_t{ 1024 }));
    intfc->WriteRecordData(playerAmmoCount);
    std::uint32_t written = 0;
    for (const auto ammoID : g_playerGrantedAmmo) {
        if (written++ >= playerAmmoCount) break;
        intfc->WriteRecordData(ammoID);
    }

    const auto npcAmmoCount = static_cast<std::uint32_t>(
        (std::min)(g_sessionNpcGrantedAmmo.size(), std::size_t{ 4096 }));
    intfc->WriteRecordData(npcAmmoCount);
    written = 0;
    for (const auto grant : g_sessionNpcGrantedAmmo) {
        if (written++ >= npcAmmoCount) break;
        intfc->WriteRecordData(grant);
    }
}

static void ClearPerSaveActorState() {
    std::lock_guard<std::mutex> lock(g_mx);
    g_mi.clear();
    g_lastAppliedSlots.clear();
    g_managedInstances.clear();
    g_playerGrantedAmmo.clear();
    g_sessionNpcGrantedAmmo.clear();
}

static void F4SEAPI RevertActorState(const F4SE::SerializationInterface*) {
    ClearPerSaveActorState();
}

static void F4SEAPI LoadActorState(const F4SE::SerializationInterface* intfc) {
    ClearPerSaveActorState();
    if (!intfc) return;
    std::uint32_t type = 0;
    std::uint32_t version = 0;
    std::uint32_t length = 0;
    while (intfc->GetNextRecordInfo(type, version, length)) {
        if (type != kActorStateRecord || version != kActorStateVersion) continue;
        std::uint32_t actorCount = 0;
        if (!ReadSerializedValue(intfc, actorCount) || actorCount > 4096) return;
        std::lock_guard<std::mutex> lock(g_mx);
        for (std::uint32_t actorIndex = 0; actorIndex < actorCount; ++actorIndex) {
            std::uint32_t savedActorID = 0;
            std::uint8_t hasDefault = 0;
            if (!ReadSerializedValue(intfc, savedActorID) ||
                !ReadSerializedValue(intfc, hasDefault)) return;
            const auto resolvedActor = intfc->ResolveFormID(savedActorID);
            MItems loadedItems;
            loadedItems.dr = hasDefault != 0;
            const auto readIDs = [intfc](std::vector<std::uint32_t>& ids) {
                std::uint32_t count = 0;
                if (!ReadSerializedValue(intfc, count) || count > 256) return false;
                for (std::uint32_t index = 0; index < count; ++index) {
                    std::uint32_t savedID = 0;
                    if (!ReadSerializedValue(intfc, savedID)) return false;
                    if (auto resolved = intfc->ResolveFormID(savedID); resolved) ids.push_back(*resolved);
                }
                return true;
            };
            if (!readIDs(loadedItems.d) || !readIDs(loadedItems.m)) return;
            int lastSlot = 0;
            if (!ReadSerializedValue(intfc, lastSlot)) return;

            std::uint32_t refCount = 0;
            if (!ReadSerializedValue(intfc, refCount) || refCount > 256) return;
            std::vector<ManagedInstanceRef> refs;
            refs.reserve(refCount);
            for (std::uint32_t refIndex = 0; refIndex < refCount; ++refIndex) {
                std::uint32_t savedItemID = 0;
                std::string name;
                std::uint32_t modCount = 0;
                if (!ReadSerializedValue(intfc, savedItemID) ||
                    !ReadSerializedString(intfc, name) ||
                    !ReadSerializedValue(intfc, modCount) ||
                    modCount > 256) return;
                std::vector<SavedMod> mods;
                mods.reserve(modCount);
                for (std::uint32_t modIndex = 0; modIndex < modCount; ++modIndex) {
                    std::uint32_t savedModID = 0;
                    std::uint8_t modIndexValue = 0;
                    std::uint8_t rank = 0;
                    std::uint8_t disabled = 0;
                    if (!ReadSerializedValue(intfc, savedModID) ||
                        !ReadSerializedValue(intfc, modIndexValue) ||
                        !ReadSerializedValue(intfc, rank) ||
                        !ReadSerializedValue(intfc, disabled)) return;
                    if (auto resolvedMod = intfc->ResolveFormID(savedModID); resolvedMod) {
                        mods.push_back({ *resolvedMod, modIndexValue, rank, disabled != 0 });
                    }
                }
                if (auto resolvedItem = intfc->ResolveFormID(savedItemID); resolvedItem) {
                    refs.push_back({ *resolvedItem, nullptr, std::move(name), std::move(mods) });
                }
            }
            if (resolvedActor) {
                g_mi[*resolvedActor] = std::move(loadedItems);
                if (lastSlot > 0) g_lastAppliedSlots[*resolvedActor] = lastSlot;
                if (!refs.empty()) g_managedInstances[*resolvedActor] = std::move(refs);
            }
        }

        std::uint32_t playerAmmoCount = 0;
        if (!ReadSerializedValue(intfc, playerAmmoCount) || playerAmmoCount > 1024) return;
        for (std::uint32_t index = 0; index < playerAmmoCount; ++index) {
            std::uint32_t savedAmmoID = 0;
            if (!ReadSerializedValue(intfc, savedAmmoID)) return;
            if (auto resolved = intfc->ResolveFormID(savedAmmoID); resolved) {
                g_playerGrantedAmmo.insert(*resolved);
            }
        }

        std::uint32_t npcAmmoCount = 0;
        if (!ReadSerializedValue(intfc, npcAmmoCount) || npcAmmoCount > 4096) return;
        for (std::uint32_t index = 0; index < npcAmmoCount; ++index) {
            std::uint64_t savedGrant = 0;
            if (!ReadSerializedValue(intfc, savedGrant)) return;
            const auto savedActorID = static_cast<std::uint32_t>(savedGrant >> 32);
            const auto savedAmmoID = static_cast<std::uint32_t>(savedGrant);
            const auto resolvedActor = intfc->ResolveFormID(savedActorID);
            const auto resolvedAmmo = intfc->ResolveFormID(savedAmmoID);
            if (resolvedActor && resolvedAmmo) {
                g_sessionNpcGrantedAmmo.insert(
                    (static_cast<std::uint64_t>(*resolvedActor) << 32) | *resolvedAmmo);
            }
        }
        LogLine("2.0 loaded per-save actor state actors=" + std::to_string(g_mi.size()));
    }
}

static void SetMenuAction(const char* js, int action) {
    std::string_view j(js ? js : "");
    const auto formID = json::getHexID(j, "targetId", 0);
    RE::Actor* actor = nullptr;
    if (formID) {
        if (auto it = g_targetHandles.find(formID); it != g_targetHandles.end() && it->second) {
            actor = it->second.get()->As<RE::Actor>();
        }
        if (!actor) {
            auto* form = RE::TESForm::GetFormByID(formID);
            actor = form ? form->As<RE::Actor>() : nullptr;
        }
    }

    std::lock_guard<std::mutex> lk(g_mx);
    if (actor) {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (actor == player || IsSelectableTarget(actor)) {
            g_menuTarget = actor->GetHandle();
            g_menuActionTarget = actor->GetHandle();
            LogLine("Menu action " + std::to_string(action) + " target " + FormIDHex(actor->formID) + " " + GetName(actor));
        }
    } else {
        LogLine("Menu action " + std::to_string(action) + " target parse failed from payload: " + std::string(j));
    }
    g_menuAct = action;
    g_menuActSlot = json::getInt(j, "slot", g_curSlot);
}

static void OnMenuSave(const char* js);
static void OnMenuEquipSlot(const char* js);
static void OnMenuRandomPreview(const char* js);
static void OnMenuSaveOriginal(const char* js);
static void OnMenuRestore(const char* js) { OnMenuEquipSlot(js); }
static void OnMenuRandom(const char* js) {
    const std::string payload = js ? js : "{}";
    const auto end = payload.rfind('}');
    if (end == std::string::npos) return;
    const std::string direct = payload.substr(0, end) + (end > 0 && payload[end - 1] == '{' ? "\"commit\":1}" : ",\"commit\":1}");
    OnMenuRandomPreview(direct.c_str());
}
static void OnMenuClearSlot(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        if (!g_menuOpen) return;
        const int slot = (std::clamp)(json::getInt(payload, "slot", g_curSlot), 1, MAX_SLOTS);
        const bool ok = DelSlot(slot);
        if (ok) {
            WriteIndex();
            ClearActiveSlotReferences(slot);
        }
        SendUiResult("clear", ok, slot, ok ? "Outfit Cleared" : "Failed to Clear Outfit", {});
    });
}
static void OnMenuResetNpcOutfit(const char* js) { SetMenuAction(js, 5); }
static void OnMenuPrevSlot(const char* js) { SetMenuAction(js, 6); }
static void OnMenuNextSlot(const char* js) { SetMenuAction(js, 7); }
static void OnMenuSelectSlot(const char* js) { SetMenuAction(js, 8); }

static void SendUiResult(
    std::string_view kind,
    bool ok,
    int slot,
    std::string_view message,
    std::string_view extra = {})
{
    std::string payload = "{\"kind\":\"" + json::esc(kind) +
        "\",\"ok\":" + (ok ? "true" : "false") +
        ",\"slot\":" + std::to_string(slot) +
        ",\"message\":\"" + json::esc(message) + "\"";
    if (!extra.empty()) payload += "," + std::string(extra);
    payload += "}";
    InteropMenuCall("omNativeResult", payload);
}

static std::string BuildCleanedSlotResultExtra(
    int gender,
    const std::vector<OE>& items,
    std::string_view request)
{
    std::string itemNames = "[";
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index) itemNames += ",";
        itemNames += "\"" + json::esc(SavedItemDisplayName(items[index])) + "\"";
    }
    itemNames += "]";
    return "\"gender\":" + std::to_string(gender) +
        ",\"count\":" + std::to_string(items.size()) +
        ",\"itemNames\":" + itemNames +
        ",\"request\":\"" + json::esc(request) + "\"";
}

static void SetMenuActor(RE::Actor* actor) {
    if (!actor || !g_menuOpen) return;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (actor != player && !IsSelectableTarget(actor)) return;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_menuTarget = actor->GetHandle();
        g_menuActionTarget = actor->GetHandle();
    }
    if (!IsLightweightMenuMode()) {
        LockNpcForPreview(actor);
    } else {
        ReleaseLockedNpc();
    }
}

static void RotatePreviewTarget(int direction) {
    if (!g_menuOpen || IsLightweightMenuMode() || direction == 0) {
        LogLine("2.0 preview rotate ignored open=" + std::to_string(g_menuOpen) +
            " lightweight=" + std::to_string(IsLightweightMenuMode()) +
            " direction=" + std::to_string(direction));
        return;
    }
    auto* actor = ResolveMenuActorByFormID(0);
    if (!actor) {
        LogLine("2.0 preview rotate ignored target=none");
        return;
    }
    LockNpcForPreview(actor);
    auto ref = g_lockedNpcTarget ? g_lockedNpcTarget.get() : RE::NiPointer<RE::TESObjectREFR>{};
    auto* lockedActor = ref ? ref->As<RE::Actor>() : nullptr;
    if (!lockedActor || lockedActor != actor || !g_lockedNpcTransformValid) {
        LogLine("2.0 preview rotate ignored target=" + FormIDHex(actor->formID) + " lock=invalid");
        return;
    }

    constexpr float kFullTurn = 6.28318530717958647692F;
    constexpr float kRotationStep = 0.26179938779914943654F;
    // Fallout's heading increases opposite to the visual left/right direction.
    float heading = std::remainder(g_lockedNpcHeading + (direction < 0 ? kRotationStep : -kRotationStep), kFullTurn);
    if (heading < 0.0F) heading += kFullTurn;
    g_lockedNpcHeading = heading;
    if (actor != RE::PlayerCharacter::GetSingleton()) StopSelectedNpcTranslationAndTurn(actor);
    actor->SetHeading(heading);
    if (actor != RE::PlayerCharacter::GetSingleton()) actor->Update3DPosition(true);
    LogLine("2.0 preview rotate actor=" + FormIDHex(actor->formID) +
        " npc=" + std::to_string(actor != RE::PlayerCharacter::GetSingleton()) +
        " direction=" + std::to_string(direction) +
        " heading=" + std::to_string(heading));
    ClearPreviewCameraInputIfNeeded(true);
}

static void HideClosingViewForSceneSwitch() {
    if (g_menuOpen || !g_closeAnimationPending.load()) return;
    if (!g_closeQuickActionPending.load()) {
        FinalizeCloseForNativeMenu();
        return;
    }
    const auto closeSerial = g_closeReleaseSerial.load();
    const auto closingView = g_view;
    if (closingView == 0 || !g_prisma) return;
    if (auto* tasks = F4SE::GetTaskInterface()) {
        tasks->AddTask([closeSerial, closingView] {
            if (g_menuOpen || !g_closeAnimationPending.load() ||
                closeSerial != g_closeReleaseSerial.load()) {
                return;
            }
            if (g_prisma && closingView == g_view &&
                g_prisma->IsValid(closingView)) {
                g_prisma->Invoke(closingView,
                    "if(window.omFinishClose)window.omFinishClose();");
                g_prisma->Unfocus(closingView);
                g_prisma->Hide(closingView);
                LogLine("2.0 UI close interrupted by native scene menu view=" +
                    std::to_string(closingView));
            }
        });
    }
}

static void FinalizeCloseForNativeMenu() {
    if (g_menuOpen || !g_closeAnimationPending.load() || g_closeQuickActionPending.load()) return;
    const auto closeSerial = g_closeReleaseSerial.load();
    const auto closingView = g_view;
    if (g_prisma && closingView != 0 && g_prisma->IsValid(closingView)) {
        g_prisma->Invoke(closingView, "if(window.omFinishClose)window.omFinishClose();");
        g_prisma->Unfocus(closingView);
        g_prisma->Hide(closingView);
        LogLine("1.1 UI native menu transition hid closing view=" + std::to_string(closingView));
    }
    g_closeAnimationPending.store(false);
    g_closeQuickActionPending.store(false);
    g_closeQuickActionSlot = 0;
    g_closeQuickActionTarget = {};
    ReleaseClosedMenu(closeSerial);
}

static void SaveCurrentMenuOutfit(const std::string& payload) {
    if (!g_menuOpen) return;
    const int slot = std::clamp(json::getInt(payload, "slot", g_curSlot), 1, MAX_SLOTS);
    auto* actor = ResolveMenuActorByFormID(json::getHexID(payload, "targetId", 0));
    if (!actor || IsActorInPowerArmor(actor)) {
        return SendUiResult("save", false, slot, "The current target cannot save an outfit");
    }

    SetMenuActor(actor);
    const int gender = GetSex(actor);
    std::vector<OE> items;
    bool savedPreviewDraft = false;
    if (g_preview.active) {
        auto previewRef = g_preview.actor.get();
        if (previewRef && previewRef.get() == actor) {
            items = g_preview.draftOutfit;
            savedPreviewDraft = !items.empty();
        }
    }
    if (items.empty()) {
        items = CollectEquippedArmor(actor);
        if (SaveWeaponsEnabled()) AppendEquippedWeapons(actor, items);
    }
    const auto clothingCount = std::count_if(items.begin(), items.end(), [](const OE& item) { return !item.weapon; });
    if (gender < 0 || clothingCount <= 0) {
        return SendUiResult("save", false, slot, "The current target has no clothing that can be saved");
    }
    if (!WriteSlot(slot, gender, items)) {
        return SendUiResult("save", false, slot, "Failed to write the outfit file");
    }

    WriteIndex();
    g_curSlot = slot;
    if (savedPreviewDraft) {
        // Saving in the studio does not equip/activate the slot yet.  Keep the
        // last successful save for the studio close path; unsaved edits made
        // afterwards must not become the outfit that survives closing.
        g_preview.hasSavedDraft = true;
        g_preview.lastSavedSlot = slot;
        LogLine("2.0 studio save recorded slot=" + std::to_string(slot));
    } else {
        SetActiveSlotForActor(actor, slot);
    }
    WriteState();

    std::string itemsJson = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i) itemsJson += ",";
        itemsJson += "\"" + json::esc(SavedItemDisplayName(items[i])) + "\"";
    }
    itemsJson += "]";
    SendUiResult(
        "save",
        true,
        slot,
        "Outfit saved to slot " + std::to_string(slot),
        "\"count\":" + std::to_string(items.size()) +
            ",\"gender\":" + std::to_string(gender) +
            ",\"items\":" + itemsJson);
}

static void OnMenuSave(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        SlowUiFocusRecovery focusRecovery("slow-save");
        SaveCurrentMenuOutfit(payload);
    });
}

static void OnMenuSaveOriginal(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        SlowUiFocusRecovery focusRecovery("slow-save-original");
        if (!g_menuOpen) return;
        RollbackPreview();
        SaveCurrentMenuOutfit(payload);
    });
}

static void OnMenuPreviewSlot(const char* js) {
    const std::string payload = js ? js : "";
    const auto requestSerial = ++g_previewRequestSerial;
    QueueGameTask([payload, requestSerial] {
        if (requestSerial != g_previewRequestSerial.load()) return;
        const bool commit = json::getInt(payload, "commit", 0) != 0;
        if (!g_menuOpen && !commit) return;
        const int slot = std::clamp(json::getInt(payload, "slot", g_curSlot), 1, MAX_SLOTS);
        const bool manage = json::getInt(payload, "manage", 1) != 0;
        const bool randomSource = json::getStr(payload, "source") == "random";
        SlowUiFocusRecovery focusRecovery(randomSource ? "slow-random-preview" : "slow-preview");
        if (manage) {
            g_curSlot = slot;
            WriteState();
        }
        if (!g_indexBySlot.contains(slot)) {
            return SendUiResult("previewEmpty", true, slot, "", "\"count\":0");
        }
        const bool cleanupOnly = json::getInt(payload, "cleanupOnly", 0) != 0;
        if (cleanupOnly) {
            int slotGender = -1;
            std::vector<OE> resolvedItems;
            std::string summary;
            SlotReadStatus status;
            const bool read = ReadSlot(slot, slotGender, resolvedItems, summary, &status);
            if (!read && status.RemovedCount() <= 0) {
                return SendUiResult(
                    "missingCleanupFailed",
                    false,
                    slot,
                    "Unable to read this outfit; the original record was not changed");
            }
            const bool cleaned = status.RemovedCount() <= 0 ||
                PersistResolvedSlotRecords(slot, slotGender, resolvedItems, status);
            return SendUiResult(
                cleaned ? "missingCleaned" : "missingCleanupFailed",
                cleaned,
                slot,
                cleaned ? "Missing records were removed from this outfit" : "Failed to back up and update this outfit",
                BuildCleanedSlotResultExtra(
                    slotGender,
                    resolvedItems,
                    randomSource ? "random" : "preview"));
        }
        const auto targetID = json::getHexID(payload, "targetId", 0);
        auto* actor = ResolveMenuActorByFormID(targetID);
        if (!actor) return SendUiResult("preview", false, slot, "Target unavailable");
        const bool allowSharedTemplate =
            json::getInt(payload, "allowSharedTemplate", 0) != 0 ||
            json::getInt(payload, "acknowledgeWarnings", 0) != 0;
        const bool acknowledgeMissing = json::getInt(payload, "acknowledgeMissing", 0) != 0;
        const auto targetState = CheckOutfitTargetState(actor, allowSharedTemplate);
        if (targetState != OutfitTargetState::kAllowed) {
            const auto kind = targetState == OutfitTargetState::kSharedTemplate ? "sharedTarget" : "targetBlocked";
            return SendUiResult(kind, false, slot, OutfitTargetStateMessage(targetState),
                std::string("\"request\":\"") + (randomSource ? "random" : "preview") + "\"");
        }
        std::vector<OE> probeItems;
        std::vector<OE> resolvedSlotItems;
        int slotGender = -1;
        SlotReadStatus status;
        const int validation = ValidateOutfitForActor(
            slot,
            actor,
            probeItems,
            &status,
            &resolvedSlotItems,
            &slotGender);
        if (status.RemovedCount() > 0 && !acknowledgeMissing) {
            return SendUiResult("missingOutfit", false, slot,
                "Missing records found; the original outfit has not been changed",
                "\"missingItems\":" + std::to_string(status.missingItems) +
                ",\"missingMods\":" + std::to_string(status.missingMods) +
                ",\"recordsPreserved\":true" +
                ",\"allowSharedTemplate\":" + std::string(allowSharedTemplate ? "true" : "false") +
                ",\"request\":\"" + std::string(randomSource ? "random" : "preview") + "\"");
        }
        if (status.RemovedCount() > 0) {
            if (!PersistResolvedSlotRecords(slot, slotGender, resolvedSlotItems, status)) {
                return SendUiResult(
                    "missingCleanupFailed",
                    false,
                    slot,
                    "Failed to back up and update this outfit");
            }
            SendUiResult(
                "missingCleaned",
                true,
                slot,
                "Missing records were removed from this outfit",
                BuildCleanedSlotResultExtra(
                    slotGender,
                    resolvedSlotItems,
                    randomSource ? "random" : "preview"));
        }
        if (validation <= 0) {
            return SendUiResult(
                randomSource ? "random" : "preview",
                false,
                slot,
                validation == -2 ? "The slot does not match the target" : "The outfit has no wearable items");
        }
        SetMenuActor(actor);
        const int result = PreviewSavedItems(actor, probeItems, slot);
        if (randomSource && result > 0) g_lastRandSlot = slot;
        SendUiResult(randomSource ? "random" : "preview", result > 0, slot,
            result > 0 ? "Previewing Saved Outfit" : (result == -2 ? "The slot does not match the target" : "Unable to Preview This Slot"),
            "\"count\":" + std::to_string((std::max)(result, 0)));
    });
}

static void OnMenuSetSlot(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        if (!g_menuOpen) return;
        const int slot = std::clamp(json::getInt(payload, "slot", g_curSlot), 1, MAX_SLOTS);
        g_curSlot = slot;
        WriteState();
        SendUiResult("slotSelected", true, slot, "Managed slot changed to " + std::to_string(slot));
    });
}

static void OnMenuEquipSlot(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        SlowUiFocusRecovery focusRecovery("slow-equip");
        const int slot = std::clamp(json::getInt(payload, "slot", g_curSlot), 1, MAX_SLOTS);
        auto* actor = ResolveMenuActorByFormID(json::getHexID(payload, "targetId", 0));
        if (!actor) return SendUiResult("equip", false, slot, "Target unavailable");
        SetMenuActor(actor);
        const int result = EquipOutfitForActor(slot, actor, true, true);
        if (result > 0) {
            g_curSlot = slot;
            WriteState();
        }
        const bool inventoryOnly = IsPlayerTeammateNative(actor);
        SendUiResult("equip", result > 0, slot,
            result > 0 ? (inventoryOnly ?
                "The outfit has been added to the target's inventory. Please equip it through the trade menu." : "Outfit Equipped") : "Outfit Change Failed",
            "\"count\":" + std::to_string((std::max)(result, 0)) +
                ",\"inventoryOnly\":" + std::string(inventoryOnly ? "true" : "false"));
    });
}

static void OnMenuRenameSlot(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        if (!g_menuOpen) return;
        const int slot = std::clamp(json::getInt(payload, "slot", g_curSlot), 1, MAX_SLOTS);
        if (!g_indexBySlot.contains(slot)) {
            return SendUiResult("rename", false, slot, "An empty slot cannot be named");
        }
        const std::string name = CleanSlotName(json::getStr(payload, "name"));
        if (!WriteSlotName(slot, name)) {
            return SendUiResult("rename", false, slot, "Failed to write the slot name");
        }
        g_slotDetails[slot].name = name;
        SendUiResult("rename", true, slot, name.empty() ? "Default slot name restored" : "Slot name saved",
            "\"name\":\"" + json::esc(name) + "\"");
    });
}

static void OnMenuConfirmPreview(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        SlowUiFocusRecovery focusRecovery("slow-confirm");
        if (!g_menuOpen) return;
        const int slot = std::clamp(json::getInt(payload, "slot", g_curSlot), 1, MAX_SLOTS);
        const auto targetID = json::getHexID(payload, "targetId", 0);
        auto* actor = ResolveMenuActorByFormID(targetID);
        if (!actor) return SendUiResult("confirm", false, slot, "Target unavailable");
        SetMenuActor(actor);
        const int result = CommitPreviewForActor(slot, actor);
        SendUiResult("confirm", result > 0, slot,
            result > 0 ? "Outfit Confirmed" : "Failed to Confirm Outfit",
            "\"count\":" + std::to_string((std::max)(result, 0)));
    });
}

static void OnMenuRandomPreview(const char* js) {
    const std::string payload = js ? js : "";
    const auto requestSerial = ++g_previewRequestSerial;
    QueueGameTask([payload, requestSerial] {
        if (requestSerial != g_previewRequestSerial.load()) return;
        const bool commit = json::getInt(payload, "commit", 0) != 0;
        if (!g_menuOpen && !commit) return;
        SlowUiFocusRecovery focusRecovery(commit ? "slow-random-equip" : "slow-random-preview");
        const auto targetID = json::getHexID(payload, "targetId", 0);
        auto* actor = ResolveMenuActorByFormID(targetID);
        if (!actor) return SendUiResult("random", false, 0, "Target unavailable");
        SetMenuActor(actor);
        const int slot = ChooseRandomSlotNative(GetSex(actor), 0, g_lastRandSlot);
        if (slot <= 0) return SendUiResult("random", false, 0, "No other saved outfit matches the target");
        if (commit) {
            const int result = EquipOutfitForActor(slot, actor, true, true);
            if (result > 0) {
                g_lastRandSlot = slot;
                g_curSlot = slot;
                WriteState();
            }
            return SendUiResult("equip", result > 0, slot,
                result > 0 ? "Outfit Equipped" : "Outfit Change Failed",
                "\"count\":" + std::to_string((std::max)(result, 0)));
        }
        const bool allowSharedTemplate =
            json::getInt(payload, "allowSharedTemplate", 0) != 0 ||
            json::getInt(payload, "acknowledgeWarnings", 0) != 0;
        const bool acknowledgeMissing = json::getInt(payload, "acknowledgeMissing", 0) != 0;
        const auto targetState = CheckOutfitTargetState(actor, allowSharedTemplate);
        if (targetState != OutfitTargetState::kAllowed) {
            const auto kind = targetState == OutfitTargetState::kSharedTemplate ? "sharedTarget" : "targetBlocked";
            return SendUiResult(kind, false, slot, OutfitTargetStateMessage(targetState),
                "\"request\":\"random\"");
        }
        std::vector<OE> probeItems;
        std::vector<OE> resolvedSlotItems;
        int slotGender = -1;
        SlotReadStatus status;
        const int validation = ValidateOutfitForActor(
            slot,
            actor,
            probeItems,
            &status,
            &resolvedSlotItems,
            &slotGender);
        if (status.RemovedCount() > 0 && !acknowledgeMissing) {
            return SendUiResult("missingOutfit", false, slot,
                "Missing records found; the original outfit has not been changed",
                "\"missingItems\":" + std::to_string(status.missingItems) +
                ",\"missingMods\":" + std::to_string(status.missingMods) +
                ",\"recordsPreserved\":true" +
                ",\"allowSharedTemplate\":" + std::string(allowSharedTemplate ? "true" : "false") +
                ",\"request\":\"random\"");
        }
        if (status.RemovedCount() > 0) {
            if (!PersistResolvedSlotRecords(slot, slotGender, resolvedSlotItems, status)) {
                return SendUiResult(
                    "missingCleanupFailed",
                    false,
                    slot,
                    "Failed to back up and update this outfit");
            }
            SendUiResult(
                "missingCleaned",
                true,
                slot,
                "Missing records were removed from this outfit",
                BuildCleanedSlotResultExtra(slotGender, resolvedSlotItems, "random"));
        }
        if (validation <= 0) {
            return SendUiResult("random", false, slot,
                validation == -2 ? "The slot does not match the target" : "The outfit has no wearable items");
        }
        const int result = PreviewSavedItems(actor, probeItems, slot);
        if (result > 0) g_lastRandSlot = slot;
        SendUiResult("random", result > 0, slot,
            result > 0 ? "Previewing Random Outfit" : "Random Preview Failed",
            "\"count\":" + std::to_string((std::max)(result, 0)));
    });
}

static bool CanOpenStudioForActor(RE::Actor* actor, bool allowSharedTemplate) {
    return actor && CheckOutfitTargetState(actor, allowSharedTemplate) == OutfitTargetState::kAllowed;
}

static void OnMenuCheckStudio(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        if (!g_menuOpen) return;
        if (!GetBoolSetting("bEnableOutfitWorkbench", true)) {
            SendUiResult("studioCheck", false, g_curSlot, "Enable Outfit Studio under MCM Advanced Options first");
            return;
        }
        auto* actor = ResolveMenuActorByFormID(0);
        const bool allowSharedTemplate =
            json::getInt(payload, "allowSharedTemplate", 0) != 0;
        const auto targetState = CheckOutfitTargetState(actor, allowSharedTemplate);
        if (targetState == OutfitTargetState::kSharedTemplate) {
            return SendUiResult(
                "sharedTarget",
                false,
                g_curSlot,
                OutfitTargetStateMessage(targetState),
                "\"request\":\"studio\"");
        }
        const bool allowed = targetState == OutfitTargetState::kAllowed &&
            CanOpenStudioForActor(actor, allowSharedTemplate);
        SendUiResult("studioCheck", allowed, g_curSlot,
            allowed ? "Outfit Studio is available" :
                (targetState == OutfitTargetState::kAllowed ?
                    "Please use Outfit Studio while the target is standing" :
                    OutfitTargetStateMessage(targetState)),
            "\"allowSharedTemplate\":" + std::string(allowSharedTemplate ? "true" : "false"));
    });
}

static void OnMenuOpenStudio(const char* js) {
    const std::string payload = js ? js : "";
    const auto requestSerial = ++g_previewRequestSerial;
    QueueGameTask([requestSerial, payload] {
        SlowUiFocusRecovery focusRecovery("slow-studio-open");
        if (!g_menuOpen) return;
        if (!GetBoolSetting("bEnableOutfitWorkbench", true)) {
            SendUiResult("studioOpen", false, g_curSlot, "Enable Outfit Studio under MCM Advanced Options first");
            return;
        }
        auto* actor = ResolveMenuActorByFormID(0);
        const bool allowSharedTemplate =
            json::getInt(payload, "allowSharedTemplate", 0) != 0;
        if (!CanOpenStudioForActor(actor, allowSharedTemplate)) {
            SendUiResult("studioOpen", false, g_curSlot, "Please use Outfit Studio while the target is standing");
            return;
        }
        if (BeginStudioDraft(actor) <= 0) {
            SendUiResult("studioOpen", false, g_curSlot, "Unable to create a studio draft");
            return;
        }
        ScheduleStudioInventoryRefresh(requestSerial);
        const auto data = BuildStudioInventoryJson();
        LogLine("1.1 studio inventory interop bytes=" + std::to_string(data.size()));
        InteropMenuCall("omReceiveStudioInventory", data);
    });
}

static void OnMenuPreviewStudioItem(const char* js) {
    const std::string payload = js ? js : "";
    const auto requestSerial = ++g_previewRequestSerial;
    QueueGameTask([payload, requestSerial] {
        SlowUiFocusRecovery focusRecovery("slow-studio-preview");
        if (requestSerial != g_previewRequestSerial.load()) return;
        if (!g_menuOpen) return;
        const int token = json::getInt(payload, "token", 0);
        auto* actor = ResolveMenuActorByFormID(json::getHexID(payload, "targetId", 0));
        const int result = PreviewStudioItem(token, actor);
        if (result > 0) {
            RefreshStudioInventoryNow();
            ScheduleStudioInventoryRefresh(requestSerial);
        }
        SendUiResult("studioPreview", result > 0, g_curSlot,
            result > 0 ? "Clothing Preview Updated" : "Unable to Preview This Item");
    });
}

static void OnMenuUnequipStudioItem(const char* js) {
    const std::string payload = js ? js : "";
    const auto requestSerial = ++g_previewRequestSerial;
    QueueGameTask([payload, requestSerial] {
        SlowUiFocusRecovery focusRecovery("slow-studio-unequip");
        if (requestSerial != g_previewRequestSerial.load()) return;
        if (!g_menuOpen) return;
        const int token = json::getInt(payload, "token", 0);
        auto* actor = ResolveMenuActorByFormID(json::getHexID(payload, "targetId", 0));
        const int result = UnequipStudioItem(token, actor);
        if (result > 0) {
            RefreshStudioInventoryNow();
            ScheduleStudioInventoryRefresh(requestSerial);
        }
        SendUiResult("studioUnequip", result > 0, g_curSlot,
            result > 0 ? "Item Unequipped" : "Unable to Unequip This Item");
    });
}

static void OnMenuOpenMaterials(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        SlowUiFocusRecovery focusRecovery("slow-material-open");
        if (!g_menuOpen) return;
        const int token = json::getInt(payload, "token", 0);
        auto* actor = ResolveMenuActorByFormID(json::getHexID(payload, "targetId", 0));
        g_materialRestore = {};
        if (actor) {
            g_materialRestore.active = true;
            g_materialRestore.actor = actor->GetHandle();
            if (g_preview.active) {
                g_materialRestore.equipped = g_preview.draftOutfit;
            } else {
                g_materialRestore.equipped = CollectEquippedArmor(actor);
                if (SaveWeaponsEnabled()) AppendEquippedWeapons(actor, g_materialRestore.equipped);
            }
        }
        const auto data = BuildMaterialChoicesJson(token);
        InteropMenuCall("omReceiveMaterialChoices", data);
    });
}

static void OnMenuPreviewMaterial(const char* js) {
    const std::string payload = js ? js : "";
    const auto requestSerial = ++g_previewRequestSerial;
    QueueGameTask([payload, requestSerial] {
        SlowUiFocusRecovery focusRecovery("slow-material-preview");
        if (requestSerial != g_previewRequestSerial.load()) return;
        if (!g_menuOpen) return;
        const int token = json::getInt(payload, "token", 0);
        const auto modID = json::getHexID(payload, "modId", 0);
        const auto* sourceEntry = FindStudioItem(token);
        const auto sourceFormID = sourceEntry ? sourceEntry->item.fid : 0;
        auto* actor = ResolveMenuActorByFormID(json::getHexID(payload, "targetId", 0));
        std::string changedName;
        const int result = PreviewMaterialItem(token, modID, actor, &changedName);
        int focusToken = 0;
        if (result > 0) {
            RefreshStudioInventoryNow();
            auto changed = std::find_if(g_studioItems.begin(), g_studioItems.end(), [&](const StudioItemRef& item) {
                return item.equipped && item.displayName == changedName;
            });
            if (changed == g_studioItems.end()) {
                changed = std::find_if(g_studioItems.begin(), g_studioItems.end(), [&](const StudioItemRef& item) {
                    return item.equipped && item.item.fid == sourceFormID &&
                        std::any_of(item.item.mods.begin(), item.item.mods.end(), [modID](const SavedMod& mod) {
                            return !mod.disabled && mod.fid == modID;
                        });
                });
            }
            if (changed != g_studioItems.end()) focusToken = changed->token;
            ScheduleStudioInventoryRefresh(requestSerial);
        }
        SendUiResult("materialPreview", result > 0, g_curSlot,
            result > 0 ? "Material Changed" : "Material Change Failed",
            result > 0 ? "\"token\":" + std::to_string(focusToken) +
                ",\"name\":\"" + json::esc(changedName) + "\"" : std::string{});
    });
}

static void OnMenuCommitMaterial(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        SlowUiFocusRecovery focusRecovery("slow-material-commit");
        if (!g_menuOpen) return;
        const int token = json::getInt(payload, "token", 0);
        const auto modID = json::getHexID(payload, "modId", 0);
        auto* actor = ResolveMenuActorByFormID(json::getHexID(payload, "targetId", 0));
        std::string generatedName;
        const int result = CommitMaterialItem(token, modID, actor, &generatedName);
        int focusToken = 0;
        if (result > 0) {
            // Keep the material page session alive.  The user may generate
            // several variants before returning to the workbench.  Each
            // successful generation replaces the return checkpoint so the
            // final generated variant wins.
            if (g_materialRestore.active && g_preview.active) {
                g_materialRestore.committed = g_preview.draftOutfit;
                g_materialRestore.lastGeneratedName = generatedName;
            }
            RefreshStudioInventoryNow();
            // BuildStudioInventoryJson above has rebuilt the native token table.
            // Select the permanent named instance that was just generated so
            // the workbench returns with one coherent visible/list/draft state.
            const auto generated = std::find_if(g_studioItems.begin(), g_studioItems.end(), [&](const StudioItemRef& item) {
                return item.equipped && item.displayName == generatedName;
            });
            if (generated != g_studioItems.end()) focusToken = generated->token;
            ScheduleStudioInventoryRefresh(g_previewRequestSerial.load());
        }
        SendUiResult("materialCommit", result > 0, g_curSlot,
            result > 0 ? "Material Variant Generated and Equipped" : "Failed to Generate Material Variant",
            result > 0 ? "\"token\":" + std::to_string(focusToken) +
                ",\"name\":\"" + json::esc(generatedName) + "\"" : std::string{});
    });
}

static void OnMenuCancelMaterial(const char* js) {
    (void)js;
    QueueGameTask([] {
        if (!g_menuOpen) return;
        // Material changes are direct in the NG release. Leaving this page only closes
        // its session; it must never rebuild the outfit captured on entry.
        ++g_previewRequestSerial;
        g_materialRestore = {};
        SendUiResult("materialCancel", true, g_curSlot, "Material Change Closed");
    });
}

static void OnMenuRollbackPreview(const char*) {
    QueueGameTask([] {
        if (!g_menuOpen) return;
        ++g_previewRequestSerial;
        RollbackPreview();
        auto* actor = ResolveMenuActorByFormID(0);
        if (actor) PositionPreviewCamera(actor);
        SendUiResult("rollback", true, g_curSlot, "Temporary Preview Reverted");
    });
}

static void OnMenuSelectTarget(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        if (!g_menuOpen) return;
        const auto formID = json::getHexID(payload, "targetId", 0);
        auto* actor = ResolveMenuActorByFormID(formID);
        if (!actor) return SendUiResult("target", false, 0, "Target unavailable");
        RollbackPreview();
        SetMenuActor(actor);
        PositionPreviewCamera(actor);
        SendUiResult("target", true, 0, "Target Selected",
            "\"targetId\":\"" + FormIDHex(actor->formID) + "\",\"targetName\":\"" + json::esc(GetName(actor)) + "\"");
    });
}
static void OnMenuRotateTarget(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        RotatePreviewTarget(json::getInt(payload, "direction", 0));
    });
}
static void OnMenuLayoutProfile(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        if (!g_menuOpen) return;
        const auto profile = json::getStr(payload, "profile");
        if (profile != "compact16x9" && profile != "compact16x10" && profile != "standard") return;
        if (g_uiLayoutProfile == profile) return;
        g_uiLayoutProfile = profile;
        auto previewRef = g_preview.active ? g_preview.actor.get() : g_menuTarget.get();
        if (auto* actor = previewRef ? previewRef->As<RE::Actor>() : nullptr) PositionPreviewCamera(actor);
    });
}
static void OnMenuPreviewDefaultOutfit(const char* js) {
    const std::string payload = js ? js : "";
    QueueGameTask([payload] {
        SlowUiFocusRecovery focusRecovery("slow-default-preview");
        if (!g_menuOpen) return;
        auto* actor = ResolveMenuActorByFormID(json::getHexID(payload, "targetId", 0));
        if (!actor) return SendUiResult("defaultPreview", false, g_curSlot, "Target unavailable");
        const bool allowSharedTemplate =
            json::getInt(payload, "allowSharedTemplate", 0) != 0 ||
            json::getInt(payload, "acknowledgeWarnings", 0) != 0;
        const auto targetState = CheckOutfitTargetState(actor, allowSharedTemplate);
        if (targetState != OutfitTargetState::kAllowed) {
            const auto kind = targetState == OutfitTargetState::kSharedTemplate ? "sharedTarget" : "targetBlocked";
            return SendUiResult(kind, false, g_curSlot, OutfitTargetStateMessage(targetState),
                "\"request\":\"default\"");
        }
        SetMenuActor(actor);
        ++g_previewRequestSerial;
        RollbackPreview();
        const int result = OM_ResetMenuActionTargetOutfit({});
        if (result > 0) {
            RefreshActorAppearance(actor, true);
            PositionPreviewCamera(actor);
        }
        SendUiResult("defaultPreview", result > 0, g_curSlot,
            result > 0 ? "Target Default State Restored and Previewed" : "No default outfit or managed items were recorded for this target");
    });
}

static void OnMenuClose(const char* js) {
    const int quickOutfitPage = js ? json::getInt(js, "quickOutfitPage", -1) : -1;
    QueueGameTask([quickOutfitPage] {
        if (quickOutfitPage >= 0) g_quickOutfitPage = (std::max)(0, quickOutfitPage);
        ++g_previewRequestSerial;
        CloseMenuInternal(false);
    });
}
static void OnMenuCommitSavedStudioOutfit(const char*) {
    QueueGameTask([] {
        SlowUiFocusRecovery focusRecovery("slow-studio-commit");
        if (!g_menuOpen) return;
        const bool kept = CommitSavedStudioOutfit();
        SendUiResult(
            "studioCommit",
            kept,
            g_curSlot,
            kept ? "The last saved outfit was kept" : "Unable to keep the last saved outfit");
    });
}

static void OnDomReady(PrismaView v) {
    if (!g_prisma || v == 0 || v != g_view || !g_prisma->IsValid(v)) {
        LogLine("2.0 UI ignored stale DOM-ready view=" + std::to_string(v) +
            " currentView=" + std::to_string(g_view));
        return;
    }
    // Keep the 2.1 JavaScriptCore console visible in the per-mod log. This is
    // especially useful for catching a single failed DOM/CSS script without
    // confusing it with a native view or D3D hook failure.
    g_prisma->RegisterConsoleCallback(v, OnPrismaConsoleMessage);
    g_prisma->BindUIEvent(v, "onOutfitManagerSave", OnMenuSave);
    g_prisma->BindUIEvent(v, "onOutfitManagerSaveOriginal", OnMenuSaveOriginal);
    g_prisma->BindUIEvent(v, "onOutfitManagerRestore", OnMenuRestore);
    g_prisma->BindUIEvent(v, "onOutfitManagerRandom", OnMenuRandom);
    g_prisma->BindUIEvent(v, "onOutfitManagerClearSlot", OnMenuClearSlot);
    g_prisma->BindUIEvent(v, "onOutfitManagerResetNpcOutfit", OnMenuResetNpcOutfit);
    g_prisma->BindUIEvent(v, "onOutfitManagerPreviewDefaultOutfit", OnMenuPreviewDefaultOutfit);
    g_prisma->BindUIEvent(v, "onOutfitManagerPrevSlot", OnMenuPrevSlot);
    g_prisma->BindUIEvent(v, "onOutfitManagerNextSlot", OnMenuNextSlot);
    g_prisma->BindUIEvent(v, "onOutfitManagerSelectSlot", OnMenuSelectSlot);
    g_prisma->BindUIEvent(v, "onOutfitManagerSetSlot", OnMenuSetSlot);
    g_prisma->BindUIEvent(v, "onOutfitManagerEquipSlot", OnMenuEquipSlot);
    g_prisma->BindUIEvent(v, "onOutfitManagerRenameSlot", OnMenuRenameSlot);
    g_prisma->BindUIEvent(v, "onOutfitManagerSelectTarget", OnMenuSelectTarget);
    g_prisma->BindUIEvent(v, "onOutfitManagerRotateTarget", OnMenuRotateTarget);
    g_prisma->BindUIEvent(v, "onOutfitManagerLayoutProfile", OnMenuLayoutProfile);
    g_prisma->BindUIEvent(v, "onOutfitManagerPreviewSlot", OnMenuPreviewSlot);
    g_prisma->BindUIEvent(v, "onOutfitManagerConfirmPreview", OnMenuConfirmPreview);
    g_prisma->BindUIEvent(v, "onOutfitManagerRandomPreview", OnMenuRandomPreview);
    g_prisma->BindUIEvent(v, "onOutfitManagerCheckStudio", OnMenuCheckStudio);
    g_prisma->BindUIEvent(v, "onOutfitManagerOpenStudio", OnMenuOpenStudio);
    g_prisma->BindUIEvent(v, "onOutfitManagerPreviewStudioItem", OnMenuPreviewStudioItem);
    g_prisma->BindUIEvent(v, "onOutfitManagerUnequipStudioItem", OnMenuUnequipStudioItem);
    g_prisma->BindUIEvent(v, "onOutfitManagerOpenMaterials", OnMenuOpenMaterials);
    g_prisma->BindUIEvent(v, "onOutfitManagerPreviewMaterial", OnMenuPreviewMaterial);
    g_prisma->BindUIEvent(v, "onOutfitManagerCommitMaterial", OnMenuCommitMaterial);
    g_prisma->BindUIEvent(v, "onOutfitManagerCancelMaterial", OnMenuCancelMaterial);
    g_prisma->BindUIEvent(v, "onOutfitManagerRollbackPreview", OnMenuRollbackPreview);
    g_prisma->BindUIEvent(v, "onOutfitManagerCommitSavedStudioOutfit", OnMenuCommitSavedStudioOutfit);
    g_prisma->BindUIEvent(v, "onOutfitManagerClose", OnMenuClose);
    g_viewReady = true;
    LogLine("1.1 UI DOM ready view=" + std::to_string(v));
    std::string pendingScript;
    {
        std::lock_guard<std::mutex> scriptLock(g_viewScriptMx);
        pendingScript = g_pendingMenuStateScript;
    }
    if (!pendingScript.empty()) {
        g_prisma->Invoke(v, pendingScript.c_str());
    }
    if (g_menuOpen && !pendingScript.empty() && !g_menuPresentationPending) {
        PresentMenuView("dom-ready");
        ScheduleMenuFocusRetry(v, g_closeReleaseSerial.load());
        InteropMenuCall("omWake", "{}");
    }
}
static bool EnsureView() {
    if (!g_prisma) { g_prisma = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI10>(); if (!g_prisma) return false; }
    if (g_view != 0 && g_prisma->IsValid(g_view)) {
        const auto health = g_prisma->GetViewHealth(g_view);
        const bool terminal =
            health == PRISMA_UI_API::ViewHealth::kLoadFailed ||
            health == PRISMA_UI_API::ViewHealth::kDomReadyTimeout ||
            health == PRISMA_UI_API::ViewHealth::kUnresponsive;
        if (terminal) {
            LogLine("1.1 UI recreating unhealthy view=" + std::to_string(g_view) +
                " health=" + std::to_string(static_cast<int>(health)));
            g_prisma->Unfocus(g_view);
            g_prisma->Hide(g_view);
            g_prisma->Destroy(g_view);
            g_view = 0;
            g_viewReady = false;
            std::lock_guard<std::mutex> scriptLock(g_viewScriptMx);
            g_pendingMenuStateScript.clear();
        }
    }
    if (g_view == 0 || !g_prisma->IsValid(g_view)) {
        g_viewReady = false;
        {
            std::lock_guard<std::mutex> scriptLock(g_viewScriptMx);
            g_pendingMenuStateScript.clear();
        }
        g_view = g_prisma->CreateView(MENU_HTML, OnDomReady);
        if (g_view == 0) return false;
        // Prisma views start visible. Keep the prewarmed page hidden until the
        // complete menu state is ready and the player explicitly opens it.
        g_prisma->Hide(g_view);
        g_prisma->SetOrder(g_view, kOutfitManagerViewOrder);
    }
    g_prisma->SetViewRole(g_view, PRISMA_UI_API::ViewRole::kPanel);
    // FocusMenu remains active for mouse capture, but let the page keep owning
    // Escape so its existing back/close state machine remains unchanged.
    g_prisma->SetViewOwnsEscape(g_view, true);
    return true;
}

static void PrewarmMenuView() {
    if (g_menuOpen || g_closeAnimationPending.load()) return;
    if (!EnsureView() || !g_prisma || !g_prisma->IsValid(g_view)) {
        LogLine("2.0 UI prewarm unavailable");
        return;
    }
    g_prisma->Hide(g_view);
    LogLine("2.0 UI prewarm view=" + std::to_string(g_view) +
        " ready=" + std::to_string(g_viewReady));
}

static void ResetTransientSessionForLoad(bool restoreOldWorld) {
    ++g_previewRequestSerial;
    ++g_restoreSerial;
    ++g_closeReleaseSerial;

    if (restoreOldWorld) {
        RollbackPreview(false);
        DestroyPreviewLights();
        RestorePreviewCamera();
        ReleaseLockedNpc();
    } else {
        DestroyPreviewLights();
        g_preview = {};
        g_previewLights = {};
        g_camera = {};
        g_lockedNpcTarget = {};
        g_lockedNpcWasRestrained = false;
        g_lockedNpcTransformValid = false;
        g_lockedNpcStopBursts = 0;
        g_lockedNpcHeading = 0.0F;
        g_lockedNpcOriginalHeading = 0.0F;
        g_lockedNpcCameraHeading = 0.0F;
    }

    if (g_prisma && g_prisma->IsValid(g_view)) {
        g_prisma->Unfocus(g_view);
        g_prisma->Hide(g_view);
    }
    g_menuOpen = false;
    g_focusAttemptedView = 0;
    g_hiddenBehindGameMenu = false;
    g_closeAnimationPending.store(false);
    g_closeQuickActionPending.store(false);
    g_closeQuickActionSlot = 0;
    g_closeQuickActionTarget = {};
    g_menuPresentationPending = false;
    g_quickSaveMode = false;
    g_quickOutfitMode = false;
    g_directStudioMode = false;
    g_quickSaveSlot = 0;
    g_quickOutfitPage = 0;
    g_menuAct = 0;
    g_menuActSlot = 0;
    g_menuTarget = {};
    g_menuActionTarget = {};
    g_materialRestore = {};
    g_studioItems.clear();
    g_materialCache.clear();
    g_saveDrafts.clear();
    g_targetHandles.clear();
    g_randomBags.clear();
    g_nextStudioToken = 1;
    g_lastRandSlot = 0;
    g_lastEquippedSlot = 0;
    g_eqBusy = false;
    g_randBusy = false;
    g_eqBusySinceMs = 0;
    g_randBusySinceMs = 0;
    g_eqLastT = 0.0F;
    g_randLastT = 0.0F;
    g_menuOpenT = 0.0F;
    g_menuActionT = 0.0F;
    g_lastMenuOpenMs.store(0);
    g_closeInputGuardUntilMs.store(0);
    g_lastUiWakeMs = 0;
    g_cacheLoaded = false;
    g_index.clear();
    g_indexBySlot.clear();
    g_slotDetails.clear();
    {
        std::lock_guard<std::mutex> lock(g_mx);
        g_actorCellReapplySerials.clear();
        g_nextActorCellReapplySerial = 0;
    }
    {
        std::lock_guard<std::mutex> scriptLock(g_viewScriptMx);
        g_pendingMenuStateScript.clear();
    }
    SetGameplayInputBlocked(false);
    SetMenuTimePaused(false);
    SetSceneHudHidden(false);
    LogLine(std::string("2.0 transient session reset phase=") +
        (restoreOldWorld ? "pre-load" : "post-load"));
}

static void F4SEMessageHandler(F4SE::MessagingInterface::Message* message) {
    if (!message) return;
    switch (message->type) {
    case F4SE::MessagingInterface::kGameDataReady:
        g_prisma = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI10>();
        InstallAEClipCursorGuard();
        RegisterActorCellEventHandler();
        LogLine(std::string("2.0 Prisma V10 ") + (g_prisma ? "ready" : "unavailable"));
        break;
    case F4SE::MessagingInterface::kPreLoadGame:
        ResetTransientSessionForLoad(true);
        break;
    case F4SE::MessagingInterface::kPostLoadGame:
    case F4SE::MessagingInterface::kNewGame:
        ResetTransientSessionForLoad(false);
        PrewarmMenuView();
        break;
    default:
        break;
    }
}

static float Rem(float n, float l, float d) { float e = n - l; return e >= d ? 0 : d - e; }
static bool Try(float n, float& l, float d) { if (n - l < d) return false; l = n; return true; }

static int GetSex(RE::Actor* a) {
    if (!a) return -1; auto* npc = a->GetNPC(); return npc ? (int)npc->GetSex() : -1;
}
static std::string GetName(RE::Actor* a) {
    if (!a) return "Unknown";
    auto* pl = RE::PlayerCharacter::GetSingleton();
    if (a == pl) return "Player";
    auto* npc = a->GetNPC(); if (!npc) return "Unknown";
    auto* fn = npc->GetFullName(); return fn ? fn : "Unknown";
}
static bool IsArmor(RE::TESForm* f) {
    if (!f || !f->Is(RE::ENUM_FORM_ID::kARMO)) return false;
    auto* armor = f->As<RE::TESObjectARMO>();
    auto* name = armor ? armor->GetFullName() : nullptr;
    return name && name[0] != '\0';
}
static std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}
static std::string CompactIdentity(std::string value) {
    value = Lower(std::move(value));
    value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char c) {
        return !std::isalnum(c);
    }), value.end());
    return value;
}
static bool IsIgnoredOutfitArmor(RE::TESForm* f) {
    auto* armor = f ? f->As<RE::TESObjectARMO>() : nullptr;
    if (!armor) return false;

    auto* sourceFile = armor->GetFile(0);
    const auto sourceName = CompactIdentity(sourceFile ? std::string(sourceFile->GetFilename()) : std::string{});
    if (sourceName.find("replaceablearmorplate") != std::string::npos ||
        sourceName == "armorplatesesp") {
        return true;
    }

    const auto editorID = CompactIdentity(armor->GetFormEditorID() ? armor->GetFormEditorID() : "");
    const auto displayName = Lower(armor->GetFullName() ? std::string(armor->GetFullName()) : std::string{});
    return displayName.find("[rap]") != std::string::npos ||
           (editorID.starts_with("rap") &&
            (editorID.find("plate") != std::string::npos ||
             editorID.find("armor") != std::string::npos));
}
static std::uint32_t GetIgnoredEquippedArmorSlots(RE::Actor* actor) {
    if (!actor) return 0;
    std::uint32_t slots = 0;
    if (const auto& biped = actor->GetCurrentBiped(); biped) {
        for (int i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
            auto* bip = biped->GetBipObject(static_cast<RE::BIPED_OBJECT>(i));
            auto* armor = bip && IsIgnoredOutfitArmor(bip->parent.object) ?
                bip->parent.object->As<RE::TESObjectARMO>() : nullptr;
            if (armor) slots |= armor->bipedModelData.bipedObjectSlots;
        }
    }
    if (actor->inventoryList) {
        for (auto& item : actor->inventoryList->data) {
            auto* armor = IsIgnoredOutfitArmor(item.object) ? item.object->As<RE::TESObjectARMO>() : nullptr;
            if (!armor) continue;
            for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                if (stack->IsEquipped()) {
                    slots |= armor->bipedModelData.bipedObjectSlots;
                    break;
                }
            }
        }
    }
    return slots;
}
static bool ConflictsWithIgnoredEquippedArmor(RE::Actor* actor, RE::TESForm* form) {
    auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr;
    if (!armor || IsIgnoredOutfitArmor(form)) return false;
    const auto ignoredSlots = GetIgnoredEquippedArmorSlots(actor);
    return ignoredSlots != 0 && (armor->bipedModelData.bipedObjectSlots & ignoredSlots) != 0;
}
static bool IsPowerArmor(RE::TESForm* f) {
    if (!f || !f->Is(RE::ENUM_FORM_ID::kARMO)) return false;
    auto* armor = f->As<RE::TESObjectARMO>();
    if (!armor) return false;
    if (armor->HasKeywordString("ArmorTypePower") ||
        armor->HasKeywordString("PowerArmorBodyPart") ||
        armor->HasKeywordString("PowerArmorFrame")) {
        return true;
    }
    if (auto* name = armor->GetFullName(); name && name[0] != '\0') {
        const auto lowerName = Lower(name);
        if (lowerName.find("power armor") != std::string::npos) {
            return true;
        }
    }
    return false;
}
static bool IsPipBoy(RE::TESForm* f) {
    if (!f || !f->Is(RE::ENUM_FORM_ID::kARMO)) return false;
    auto* armor = f->As<RE::TESObjectARMO>();
    if (!armor) return false;
    constexpr auto pipBoyBit = 1u << static_cast<std::uint32_t>(RE::BIPED_OBJECT::kPipboy);
    if ((armor->bipedModelData.bipedObjectSlots & pipBoyBit) != 0) return true;
    if (armor->HasKeywordString("Pipboy") ||
        armor->HasKeywordString("PipBoy") ||
        armor->HasKeywordString("ArmorTypePipboy") ||
        armor->HasKeywordString("ArmorTypePipBoy")) {
        return true;
    }
    if (auto* name = armor->GetFullName(); name && name[0] != '\0') {
        const auto lowerName = Lower(name);
        if (lowerName.find("pip-boy") != std::string::npos ||
            lowerName.find("pipboy") != std::string::npos ||
            lowerName.find("pip boy") != std::string::npos) {
            return true;
        }
    }
    return false;
}
static bool IsManagedOutfitArmor(RE::TESForm* f) {
    return IsArmor(f) && !IsPowerArmor(f) && !IsPipBoy(f) && !IsIgnoredOutfitArmor(f);
}
static bool IsSupportedSavedWeapon(RE::TESForm* f) {
    if (!f || !f->Is(RE::ENUM_FORM_ID::kWEAP)) return false;
    auto* weapon = f->As<RE::TESObjectWEAP>();
    const char* name = weapon ? weapon->GetFullName() : nullptr;
    return name && name[0] != '\0';
}
static bool IsStudioOutfitArmor(RE::TESForm* f) {
    if (!IsManagedOutfitArmor(f)) return false;
    auto* armor = f->As<RE::TESObjectARMO>();
    if (!armor) return false;
    constexpr auto reservedBits =
        (1u << static_cast<std::uint32_t>(RE::BIPED_OBJECT::kPipboy)) |
        (1u << static_cast<std::uint32_t>(RE::BIPED_OBJECT::kFX));
    return (armor->bipedModelData.bipedObjectSlots & ~reservedBits) != 0;
}
static bool IsActorInPowerArmor(RE::Actor* a) {
    if (!a) return false;
    try { return RE::PowerArmor::ActorInPowerArmor(*a); } catch (...) { return false; }
}
// Fallout 4's actor flags expose the player-teammate state at bit 26.
static constexpr std::uint32_t kActorTeammateFlag = 1u << 26;
static bool IsPlayerTeammateNative(RE::Actor* actor) {
    return actor && actor != RE::PlayerCharacter::GetSingleton() &&
        (actor->niFlags.flags & kActorTeammateFlag) != 0;
}
static bool IsHumanoidOutfitTarget(RE::TESNPC* npc) {
    if (!npc) return false;
    // Keep the target filter deliberately broad. Custom humanoid races,
    // ghouls, synths, children, and other NPC-style races may still support
    // clothing even when they do not use the vanilla human keywords.
    // Exclude only the race categories that are inherently non-wearable for
    // this system: animals, creatures, and robots such as Dogmeat.
    return !npc->HasApplicableKeywordString("ActorTypeAnimal") &&
           !npc->HasApplicableKeywordString("ActorTypeCreature") &&
           !npc->HasApplicableKeywordString("ActorTypeRobot");
}
static bool IsSelectableTarget(RE::Actor* a) {
    if (!a || a->IsDead(false) || IsActorInPowerArmor(a)) return false;
    auto* npc = a->GetNPC();
    if (!IsHumanoidOutfitTarget(npc)) return false;
    return true;
}

static OutfitTargetState CheckOutfitTargetState(RE::Actor* actor, [[maybe_unused]] bool allowSharedTemplate) {
    if (!actor || actor->IsDead(false)) return OutfitTargetState::kSceneControlled;
    if (actor != RE::PlayerCharacter::GetSingleton() && !allowSharedTemplate) {
        auto* npc = actor->GetNPC();
        if (npc && npc->UsesTemplate() && !npc->IsUnique()) return OutfitTargetState::kSharedTemplate;
    }
    return OutfitTargetState::kAllowed;
}

static std::string OutfitTargetStateMessage(OutfitTargetState state) {
    switch (state) {
    case OutfitTargetState::kCombat:
        return "Outfits cannot be changed during combat";
    case OutfitTargetState::kSceneControlled:
        return "The target is controlled by a quest, scene, or special animation and cannot change outfits now";
    case OutfitTargetState::kSharedTemplate:
        return "This NPC may use a shared template; changing outfits could affect similar NPCs";
    default:
        return {};
    }
}

static RE::Actor* ResolveMenuActorByFormID(std::uint32_t formID) {
    RE::Actor* actor = nullptr;
    if (formID) {
        if (auto it = g_targetHandles.find(formID); it != g_targetHandles.end() && it->second) {
            auto ref = it->second.get();
            actor = ref ? ref->As<RE::Actor>() : nullptr;
        }
        if (!actor) {
            auto* form = RE::TESForm::GetFormByID(formID);
            actor = form ? form->As<RE::Actor>() : nullptr;
        }
    }
    if (!actor && g_menuActionTarget) {
        auto ref = g_menuActionTarget.get();
        actor = ref ? ref->As<RE::Actor>() : nullptr;
    }
    if (!actor && g_menuTarget) {
        auto ref = g_menuTarget.get();
        actor = ref ? ref->As<RE::Actor>() : nullptr;
    }
    if (!actor) actor = RE::PlayerCharacter::GetSingleton();
    auto* player = RE::PlayerCharacter::GetSingleton();
    return actor == player || IsSelectableTarget(actor) ? actor : nullptr;
}

static bool CaptureOriginalPreviewCameraState() {
    if (g_camera.originalStateCaptured) return true;
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (!camera) return false;

    g_camera.wasFirstPerson = camera->QCameraEquals(RE::CameraStates::kFirstPerson);
    if (auto* player = RE::PlayerCharacter::GetSingleton(); player && (g_camera.wasFirstPerson || camera->pipboyMode)) {
        g_camera.playerModelWasShown = player->is3rdPersonModelShown;
        g_camera.playerFirstPersonGeometryWasHidden = player->hideFirstPersonGeometry;
        g_camera.playerPreviewFullRefreshPending = true;
        g_camera.playerModelStateCaptured = true;
    }
    g_camera.originalStateCaptured = true;
    return true;
}

static bool EnsureThirdPersonForPreview() {
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (!camera) return false;
    CaptureOriginalPreviewCameraState();
    const bool firstPerson = camera->QCameraEquals(RE::CameraStates::kFirstPerson);
    const bool needsCameraTransition = firstPerson || camera->pipboyMode;
    const bool bodyRefreshPending = g_camera.playerPreviewFullRefreshPending;
    if (!needsCameraTransition && !bodyRefreshPending) return false;

    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
        if (!g_camera.playerModelStateCaptured) {
            g_camera.playerModelWasShown = player->is3rdPersonModelShown;
            g_camera.playerFirstPersonGeometryWasHidden = player->hideFirstPersonGeometry;
            g_camera.playerModelStateCaptured = true;
        }
        // First-person camera state can leave this flag disabled even after
        // SetState(k3rdPerson).  The free preview camera needs the full player
        // model, not only the first-person arms/torso. Hide the separate
        // first-person geometry as well; SetState alone can leave it alive for
        // one or more frames when the UI is opened from first person.
        player->is3rdPersonModelShown = true;
        player->hideFirstPersonGeometry = true;
        player->Update3DPosition(true);
        if (bodyRefreshPending) {
            // GardenOfEden3 already requested the first/third-person body
            // render before OpenMenu. Avoid a second full Reset3D here; it
            // causes a visible hitch on slower disks and Steam Deck.
            g_camera.playerPreviewFullRefreshPending = false;
            LogLine("2.0 preview player body refresh delegated to GOE");
        }
        constexpr std::uint32_t kAppearanceRefresh = 0x13;
        if (auto* taskQueue = RE::TaskQueueInterface::GetSingleton()) taskQueue->QueueUpdate3D(player, kAppearanceRefresh);
    }
    if (camera->pipboyMode) camera->StopPipboyMode();
    if (needsCameraTransition) {
        // Game.ForceThirdPerson, called by the shared Papyrus entry path,
        // owns the native transition. Do not replace it with SetState here;
        // that hard cut is what leaves the player animation graph behind.
        LogLine("2.0 preview waiting for Game.ForceThirdPerson transition");
    }
    return false;
}

static bool IsThirdPersonPreviewReady() {
    auto* camera = RE::PlayerCamera::GetSingleton();
    return camera && !camera->pipboyMode && !camera->QCameraEquals(RE::CameraStates::kFirstPerson);
}

static void ClearPreviewLights() {
    std::size_t destroyed = 0;
    for (auto& handle : g_previewLights.references) {
        if (auto ref = handle.get()) {
            ref->Disable();
            ref->SetWantsDelete(true);
            ref->MarkAsDeleted();
            ++destroyed;
        }
        handle = {};
    }
    g_previewLights = {};
    LogLine("2.0 preview light group destroyed count=" + std::to_string(destroyed));
}

static void DestroyPreviewLights() {
    ClearPreviewLights();
}

// Each scene has its own 0.25 MCM grid. The records are templates; runtime
// still creates only three temporary references for either scene.
static constexpr std::array<std::uint32_t, 8> kPreviewLightExteriorIDs = {
    0x814, 0x815, 0x816, 0x817, 0x818, 0x819, 0x81A, 0x81B
};
static constexpr std::array<std::uint32_t, 8> kPreviewLightInteriorIDs = {
    0x81C, 0x81D, 0x81E, 0x823, 0x81F, 0x820, 0x821, 0x822
};

static int GetPreviewLightPresetIndex(bool interior) {
    const auto key = interior ?
        "fPreviewLightPowerScaleInterior" : "fPreviewLightPowerScaleExterior";
    const float requested = (std::clamp)(GetFloatSetting(key, 1.0F), 0.0F, 2.0F);
    if (requested <= 0.01F) return -1;
    // MCM writes values on the 0.25 grid. Round manually edited files to the
    // nearest supported tick so every non-zero value still selects one record.
    const int tick = (std::clamp)(static_cast<int>(std::lround(requested / 0.25F)),
        1, 8);
    if (tick <= 0) return -1;
    return tick - 1;
}

static RE::TESObjectLIGH* GetPreviewLightForm(
    RE::TESDataHandler* dataHandler,
    bool isInterior)
{
    if (!dataHandler) return nullptr;
    const int preset = GetPreviewLightPresetIndex(isInterior);
    if (preset < 0) {
        LogLine(std::string("2.0 preview light disabled scene=") +
            (isInterior ? "interior" : "exterior"));
        return nullptr;
    }

    const auto& ids = isInterior ? kPreviewLightInteriorIDs : kPreviewLightExteriorIDs;
    auto* light = dataHandler->LookupForm<RE::TESObjectLIGH>(
        ids[static_cast<std::size_t>(preset)],
        "OutfitManager.esp");
    if (!light) {
        light = dataHandler->LookupForm<RE::TESObjectLIGH>(
            0x000D8556, "Fallout4.esm");
        LogLine("2.0 preview light preset record unavailable; using stable vanilla fallback");
    }
    if (light) {
        LogLine(std::string("2.0 preview light preset=") +
            std::to_string((preset + 1) * 25) +
            " scene=" + (isInterior ? "interior" : "exterior"));
    }
    return light;
}

static std::array<RE::NiPoint3, kPreviewLightCapacity> BuildPreviewLightLocations(
    const RE::NiPoint3& anchor,
    float heading,
    const PreviewLightingPreset& preset)
{
    const float forwardX = std::sin(heading);
    const float forwardY = std::cos(heading);
    const float rightX = std::cos(heading);
    const float rightY = -std::sin(heading);
    const auto placeLight = [&](int forward, int right, int height) {
        return RE::NiPoint3{
            anchor.x + forwardX * static_cast<float>(forward) + rightX * static_cast<float>(right),
            anchor.y + forwardY * static_cast<float>(forward) + rightY * static_cast<float>(right),
            anchor.z + static_cast<float>(height)
        };
    };
    return {
        placeLight(preset.keyLightForward, preset.keyLightRight, preset.keyLightHeight),
        placeLight(preset.fillLightForward, preset.fillLightRight, preset.fillLightHeight),
        placeLight(preset.lowerLightForward, preset.lowerLightRight, preset.lowerLightHeight),
        placeLight(preset.lowerLightForward, preset.lowerLightRight, preset.lowerLightHeight)
    };
}

static void EnsurePreviewLights(RE::Actor* actor) {
    if (!actor || !g_menuOpen || g_hiddenBehindGameMenu || !g_camera.active ||
        !IsThirdPersonPreviewReady()) return;
    if (GetTickCount64() < g_previewLights.deferUntilMs) return;

    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    auto* cell = actor->GetParentCell();
    if (!dataHandler || !cell) return;

    const bool isInterior = cell->IsInterior();
    const auto& preset = isInterior ? g_previewTuning.interiorDay : g_previewTuning.exteriorDay;
    const auto cellID = cell->formID;
    const std::size_t lightCount = 3;

    bool groupReady = g_previewLights.targetFormID == actor->formID &&
        g_previewLights.cellFormID == cellID;
    for (std::size_t i = 0; groupReady && i < lightCount; ++i) {
        groupReady = g_previewLights.references[i].get() != nullptr;
    }
    if (groupReady) return;
    if (std::any_of(g_previewLights.references.begin(), g_previewLights.references.end(),
        [](const RE::ObjectRefHandle& handle) { return handle.get() != nullptr; })) {
        // Do not create a second world-light group in the same session. The
        // next update creates the replacement after this group is cleared.
        ClearPreviewLights();
        return;
    }

    auto* fillLight = GetPreviewLightForm(dataHandler, isInterior);
    if (!fillLight) {
        LogLine("2.0 vanilla preview light unavailable");
        return;
    }

    float heading = actor->GetHeading();
    if (g_lockedNpcTransformValid && g_lockedNpcTarget) {
        auto lockedRef = g_lockedNpcTarget.get();
        auto* lockedActor = lockedRef ? lockedRef->As<RE::Actor>() : nullptr;
        if (lockedActor == actor) heading = g_lockedNpcCameraHeading;
    }
    const auto actorPos = actor->GetPosition();
    const auto locations = BuildPreviewLightLocations(actorPos, heading, preset);
    for (std::size_t i = 0; i < lightCount; ++i) {
        RE::NEW_REFR_DATA data{};
        data.location = locations[i];
        data.direction = {};
        data.object = fillLight;
        data.interior = cell->IsInterior() ? cell : nullptr;
        data.world = cell->IsExterior() ? cell->worldSpace : nullptr;
        data.reference = nullptr;
        data.forcePersist = true;
        data.clearStillLoadingFlag = true;
        data.initializeScripts = false;
        data.initiallyDisabled = false;
        g_previewLights.references[i] = dataHandler->CreateReferenceAtLocation(data);
        if (auto ref = g_previewLights.references[i].get()) {
            LogLine("2.0 preview fill light active=" + FormIDHex(ref->formID));
        } else {
            LogLine("2.0 preview fill light creation failed index=" + std::to_string(i));
        }
    }
    g_previewLights.targetFormID = actor->formID;
    g_previewLights.cellFormID = cellID;
    g_previewLights.enabled = true;
    LogLine("2.0 vanilla preview fill lights created target=" + FormIDHex(actor->formID) +
        " count=" + std::to_string(lightCount) +
        " interior=" + (isInterior ? "1" : "0"));
}


static void PositionPreviewCamera(RE::Actor* actor) {
    if (!actor || !g_menuOpen) return;
    if (g_hiddenBehindGameMenu) {
        ClearPreviewLights();
        return;
    }
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (!camera) return;

    if (!g_camera.active) {
        g_camera.wasFree = camera->QCameraEquals(RE::CameraStates::kFree);
        g_camera.worldFOV = camera->worldFOV;
        if (auto freeState = camera->GetState<RE::FreeCameraState>(); freeState) {
            g_camera.translation = freeState->translation;
            g_camera.rotation = freeState->rotation;
            g_camera.freeCameraRunInput = freeState->runInput;
        }
        if (!g_camera.wasFree) camera->ToggleFreeCameraMode(false);
        g_camera.active = true;
        // Let the scene/camera settle before creating the camera light. A
        // visible delayed light is preferable to a render-thread race while
        // the preview camera is still being created.
        g_previewLights.deferUntilMs = GetTickCount64() + 500;
    }

    if (!camera->QCameraEquals(RE::CameraStates::kFree)) {
        camera->ToggleFreeCameraMode(false);
    }
    auto currentState = camera->GetCameraCurrentState();
    auto* freeState = camera->QCameraEquals(RE::CameraStates::kFree) ?
        static_cast<RE::FreeCameraState*>(currentState.get()) : nullptr;
    if (!freeState) {
        LogLine("2.0 preview camera failed to enter free camera");
        return;
    }
    const bool targetChanged = g_camera.targetFormID != actor->formID;
    g_camera.targetFormID = actor->formID;
    const auto& tuning = g_previewTuning;
    camera->worldFOV = static_cast<float>(tuning.cameraFov);
    const auto actorPos = actor->GetPosition();
    const float heading = actor->GetHeading();
    const int visualScale = g_uiLayoutProfile == "compact16x10" ?
        static_cast<int>(std::lround(static_cast<double>(tuning.visualScalePercent) * 1.08)) : tuning.visualScalePercent;
    const float distance = static_cast<float>(tuning.cameraDistance) * 100.0F / static_cast<float>(visualScale);
    const float lateral = static_cast<float>(tuning.cameraRight);
    const float forwardX = std::sin(heading);
    const float forwardY = std::cos(heading);
    const float rightX = std::cos(heading);
    const float rightY = -std::sin(heading);
    freeState->translation.x = actorPos.x + forwardX * distance + rightX * lateral;
    freeState->translation.y = actorPos.y + forwardY * distance + rightY * lateral;
    freeState->translation.z = actorPos.z + static_cast<float>(tuning.cameraHeight);
    const float lookX = actorPos.x - freeState->translation.x;
    const float lookY = actorPos.y - freeState->translation.y;
    constexpr float kDegreesToRadians = 0.01745329251994329577F;
    freeState->rotation.x = static_cast<float>(tuning.cameraPitchDegrees) * kDegreesToRadians;
    freeState->rotation.y = std::atan2(lookX, lookY) +
        static_cast<float>(tuning.cameraYawOffsetDegrees) * kDegreesToRadians;
    freeState->upDown = {};
    freeState->leftThumbstick = {};
    freeState->rightThumbstick = {};
    freeState->runInput = false;
    EnsurePreviewLights(actor);
    if (targetChanged) LogLine("2.0 preview camera locked target=" + FormIDHex(actor->formID));
}

static void RestorePreviewCamera(bool preserveOriginalState) {
    if (!g_camera.active && !g_camera.wasFirstPerson && !g_camera.playerModelStateCaptured) return;
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (camera) {
        if (g_camera.active && g_camera.worldFOV > 0.0F) camera->worldFOV = g_camera.worldFOV;
        if (g_camera.active && g_camera.wasFree) {
            if (auto freeState = camera->GetState<RE::FreeCameraState>(); freeState) {
                freeState->translation = g_camera.translation;
                freeState->rotation = g_camera.rotation;
                freeState->runInput = g_camera.freeCameraRunInput;
            }
        } else if (g_camera.active && camera->QCameraEquals(RE::CameraStates::kFree)) {
            camera->ToggleFreeCameraMode(false);
        }
        if (g_camera.playerModelStateCaptured) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                // Closing always leaves the player in third person, but the
                // first-person geometry must remain available for a later
                // Pip-Boy or tactical-tablet open.
                player->is3rdPersonModelShown = preserveOriginalState ?
                    g_camera.playerModelWasShown : true;
                player->hideFirstPersonGeometry = preserveOriginalState ?
                    g_camera.playerFirstPersonGeometryWasHidden : false;
                player->Update3DPosition(true);
                constexpr std::uint32_t kAppearanceRefresh = 0x13;
                if (auto* taskQueue = RE::TaskQueueInterface::GetSingleton()) taskQueue->QueueUpdate3D(player, kAppearanceRefresh);
            }
        }
    }
    if (preserveOriginalState) {
        // The preview camera is being yielded temporarily to a native menu.
        // Keep only the original restore contract; the next preview pass will
        // capture the current third-person/free-camera state again.
        const bool originalStateCaptured = g_camera.originalStateCaptured;
        const bool wasFirstPerson = g_camera.wasFirstPerson;
        const bool playerModelStateCaptured = g_camera.playerModelStateCaptured;
        const bool playerModelWasShown = g_camera.playerModelWasShown;
        const bool playerFirstPersonGeometryWasHidden = g_camera.playerFirstPersonGeometryWasHidden;
        const bool playerPreviewFullRefreshPending = g_camera.playerPreviewFullRefreshPending;
        g_camera = {};
        g_camera.originalStateCaptured = originalStateCaptured;
        g_camera.wasFirstPerson = wasFirstPerson;
        g_camera.playerModelStateCaptured = playerModelStateCaptured;
        g_camera.playerModelWasShown = playerModelWasShown;
        g_camera.playerFirstPersonGeometryWasHidden = playerFirstPersonGeometryWasHidden;
        g_camera.playerPreviewFullRefreshPending = playerPreviewFullRefreshPending;
    } else {
        g_camera = {};
    }
}

static void ClearPreviewCameraInputIfNeeded(bool force) {
    static ULONGLONG lastClearMs = 0;
    if (!g_menuOpen || g_hiddenBehindGameMenu) return;
    const auto now = GetTickCount64();
    if (!force && now - lastClearMs < 120) return;
    lastClearMs = now;
    auto* camera = RE::PlayerCamera::GetSingleton();
    if (!camera || !camera->QCameraEquals(RE::CameraStates::kFree)) return;
    auto freeState = camera->GetState<RE::FreeCameraState>();
    if (!freeState) return;
    freeState->upDown = {};
    freeState->leftThumbstick = {};
    freeState->rightThumbstick = {};
    freeState->runInput = false;
}

static bool ApplyPreviewCameraIfReady(const RE::ObjectRefHandle& previewHandle, bool positionCamera) {
    if (!g_menuOpen || g_hiddenBehindGameMenu) return false;

    // SetState can be deferred by the game's camera update.  Do not enter the
    // free camera in the same task that requested third person, otherwise the
    // player can remain in the first-person model-hidden state.
    if (EnsureThirdPersonForPreview() || !IsThirdPersonPreviewReady()) {
        LogLine("2.0 preview camera waiting for third-person state");
        return false;
    }

    if (positionCamera) {
        auto ref = previewHandle.get();
        auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
        if (!actor) return false;
        PositionPreviewCamera(actor);
        auto* camera = RE::PlayerCamera::GetSingleton();
        if (!camera || !camera->QCameraEquals(RE::CameraStates::kFree)) {
            LogLine("2.0 preview camera waiting for free-camera state");
            return false;
        }
    }
    if (g_menuPresentationPending) PresentDelayedMenuIfReady("third-person-ready");
    return !g_menuPresentationPending;
}

static void ReleaseClosedMenu(std::uint32_t closeSerial) {
    if (g_menuOpen || closeSerial != g_closeReleaseSerial.load()) return;
    if (auto* camera = RE::PlayerCamera::GetSingleton(); camera && camera->QCameraEquals(RE::CameraStates::kFree)) {
        camera->ToggleFreeCameraMode(false);
    }
    SetGameplayInputBlocked(false);
    SetMenuTimePaused(false);
    SetSceneHudHidden(false);
}

static bool IsCloseActionSettled() {
    if (!g_closeQuickActionPending.load()) return true;
    if (g_menuAct == 2 || g_menuAct == 3 || g_eqBusy || g_randBusy) return false;
    auto ref = g_closeQuickActionTarget.get();
    auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
    if (!actor) return true;
    const int slot = g_closeQuickActionSlot > 0 ? g_closeQuickActionSlot : g_lastRandSlot;
    if (slot <= 0) return true;
    return IsWearingSavedOutfitNative(slot, actor);
}

static bool IsCloseRestoreSettled(
    const RE::ObjectRefHandle& actorHandle,
    const std::vector<EquippedStackSnapshot>& originals)
{
    if (originals.empty()) return true;
    auto ref = actorHandle.get();
    auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
    if (!actor) return true;
    const bool includeWeapons = std::any_of(originals.begin(), originals.end(), [](const auto& original) {
        auto* form = RE::TESForm::GetFormByID(original.fid);
        return form && form->Is(RE::ENUM_FORM_ID::kWEAP);
    });
    const auto equipped = CaptureEquippedStacks(actor, includeWeapons);
    return std::all_of(originals.begin(), originals.end(), [&](const auto& original) {
        return std::any_of(equipped.begin(), equipped.end(), [&](const auto& current) {
            return current.fid == original.fid && current.extra == original.extra;
        });
    });
}

static void ScheduleCloseFinalization(
    std::uint32_t closeSerial,
    const RE::ObjectRefHandle& actorHandle,
    std::shared_ptr<const std::vector<EquippedStackSnapshot>> originals,
    int attempt)
{
    if (g_menuOpen || !g_closeAnimationPending.load() ||
        closeSerial != g_closeReleaseSerial.load()) {
        return;
    }
    QueueGameTask([closeSerial, actorHandle, originals, attempt] {
        if (g_menuOpen || !g_closeAnimationPending.load() ||
            closeSerial != g_closeReleaseSerial.load()) {
            return;
        }
        constexpr int kMinRestoreChecks = 4;
        constexpr int kMaxRestoreChecks = 32;
        const bool hasRestore = originals && !originals->empty();
        const bool hasAction = g_closeQuickActionPending.load();
        const bool restoreSettled = !hasRestore || IsCloseRestoreSettled(actorHandle, *originals);
        const bool actionSettled = !hasAction || IsCloseActionSettled();
        const bool settled = restoreSettled && actionSettled;
        if ((settled && (!(hasRestore || hasAction) || attempt >= kMinRestoreChecks)) ||
            attempt >= kMaxRestoreChecks) {
            if (hasAction && !actionSettled) {
                LogLine("2.0 close action settle timeout; continuing final cleanup");
            } else if (hasRestore && !restoreSettled) {
                LogLine("2.0 close restore settle timeout; continuing final cleanup");
            } else if (hasRestore || hasAction) {
                LogLine("2.0 close restore settled checks=" + std::to_string(attempt));
            }
            if (hasRestore) ++g_restoreSerial;
            FinishMenuCloseAfterCamera(closeSerial);
            return;
        }
        std::thread([closeSerial, actorHandle, originals, attempt] {
            Sleep(40);
            ScheduleCloseFinalization(closeSerial, actorHandle, originals, attempt + 1);
        }).detach();
    });
}

static void FinishMenuCloseAfterCamera(std::uint32_t closeSerial) {
    if (g_menuOpen || closeSerial != g_closeReleaseSerial.load()) return;
    const auto closingView = g_view;
    // PrismaUI 2.1 owns held-frame invalidation and asynchronous destruction.
    // Keep this view prewarmed and let the framework discard the hidden frame;
    // recreating it on every close only reintroduces the old backend race window.
    if (g_prisma && closingView != 0 && closingView == g_view && g_prisma->IsValid(closingView)) {
        g_prisma->Invoke(closingView, "if(window.omFinishClose)window.omFinishClose();");
        g_prisma->Unfocus(closingView);
        g_prisma->Hide(closingView);
        LogLine("1.1 UI close completed and hid reusable view=" + std::to_string(closingView));
    }
    g_closeAnimationPending.store(false);
    g_closeQuickActionPending.store(false);
    g_closeQuickActionSlot = 0;
    g_closeQuickActionTarget = {};
    ReleaseClosedMenu(closeSerial);
}

static void CloseMenuInternal(bool rollback) {
    std::uint32_t closeSerial = 0;
    PrismaView closingView = 0;
    RE::ObjectRefHandle closeRestoreActor;
    std::shared_ptr<const std::vector<EquippedStackSnapshot>> closeRestoreOriginals;
    bool quickOutfitClose = false;
    int preservedQuickAction = 0;
    int preservedQuickActionSlot = 0;
    if (!rollback && g_preview.active) {
        auto previewRef = g_preview.actor.get();
        auto* previewActor = previewRef ? previewRef->As<RE::Actor>() : nullptr;
        if (previewActor) {
            AcceptPreviewAsCurrent(previewActor,
                g_preview.lastSavedSlot > 0 ? g_preview.lastSavedSlot : g_preview.previewSlot);
        }
    }
    {
        std::lock_guard<std::mutex> lk(g_mx);
        if (!g_menuOpen) {
            LogLine("2.0 UI close ignored: menu already closed");
            return;
        }
        closeSerial = ++g_closeReleaseSerial;
        g_closeAnimationPending.store(true);
        g_closeQuickActionPending.store(false);
        g_closeQuickActionSlot = 0;
        g_closeQuickActionTarget = {};
        if (rollback && g_preview.active) {
            closeRestoreActor = g_preview.actor;
            closeRestoreOriginals = std::make_shared<const std::vector<EquippedStackSnapshot>>(g_preview.original);
        }
        g_menuOpen = false;
        g_hiddenBehindGameMenu = false;
        g_menuPresentationPending = false;
        g_quickSaveMode = false;
        quickOutfitClose = g_quickOutfitMode;
        if (quickOutfitClose && (g_menuAct == 2 || g_menuAct == 3)) {
            preservedQuickAction = g_menuAct;
            preservedQuickActionSlot = g_menuActSlot;
        }
        g_quickOutfitMode = false;
        g_directStudioMode = false;
        g_quickSaveSlot = 0;
        g_menuAct = 0;
        g_menuActSlot = 0;
        if (preservedQuickAction != 0) {
            // The UI releases Prisma focus immediately after dispatching the
            // quick action. Keep the action alive for Papyrus' polling loop;
            // ordinary CloseMenu calls still clear actions as before.
            g_menuAct = preservedQuickAction;
            g_menuActSlot = preservedQuickActionSlot;
            g_closeQuickActionPending.store(true);
            g_closeQuickActionSlot = preservedQuickActionSlot;
            g_closeQuickActionTarget = g_menuActionTarget;
            LogLine("2.0 preserved quick action=" + std::to_string(g_menuAct) +
                " slot=" + std::to_string(g_menuActSlot));
        }
        closingView = g_view;
        if (g_prisma && g_prisma->IsValid(closingView)) {
            g_prisma->Invoke(closingView, "if(window.omBeginClose)window.omBeginClose();");
            g_prisma->Unfocus(closingView);
            LogLine("1.1 UI close animation started reusable view=" + std::to_string(closingView));
        }
    }

    g_materialRestore = {};
    if (rollback) RollbackPreview(true);
    ClearPreviewLights();
    if (!quickOutfitClose) RestorePreviewCamera();
    ReleaseLockedNpc();
    SetMenuTimePaused(false);
    if (!rollback) {
        SetGameplayInputBlocked(false);
        SetSceneHudHidden(false);
    }
    // Rollback re-equips the original instances asynchronously. Finalize as
    // soon as the actual equipped stacks are present, instead of holding the
    // player behind a fixed 1.4s delay. A bounded retry remains as a safety
    // net for actors whose inventory/3D update is slow or unavailable.
    ScheduleCloseFinalization(closeSerial, closeRestoreActor, closeRestoreOriginals);
    ArmCloseInputGuard(quickOutfitClose);
    if (BlockingGameMenuOpen() && !g_closeQuickActionPending.load()) {
        FinalizeCloseForNativeMenu();
    }
}
static std::string FormIDHex(uint32_t formID) {
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "%08X", formID);
    return buf;
}
static std::vector<TargetInfo> BuildTargetList(RE::Actor* currentTarget) {
    std::vector<TargetInfo> targets;
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) return targets;

    targets.push_back({ player->formID, GetName(player), GetSex(player), 0.0f, true, player->GetHandle() });

    auto* cell = player->GetParentCell();
    if (!cell) return targets;

    const float scanRadius = static_cast<float>(g_previewTuning.targetScanRadius);
    constexpr std::size_t maxNpcTargets = 50;
    const auto origin = player->GetPosition();
    cell->ForEachReferenceInRange(origin, scanRadius, [&](RE::TESObjectREFR* ref) {
        auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
        if (!actor || actor == player || !IsSelectableTarget(actor) || !actor->GetCurrent3D()) {
            return RE::BSContainer::ForEachResult::kContinue;
        }
        if (CheckOutfitTargetState(actor, true) != OutfitTargetState::kAllowed) {
            return RE::BSContainer::ForEachResult::kContinue;
        }
        auto name = GetName(actor);
        if (name.empty() || name == "Unknown") {
            return RE::BSContainer::ForEachResult::kContinue;
        }
        targets.push_back({ actor->formID, name, GetSex(actor), ref->GetDistanceFromPoint(origin), false, actor->GetHandle() });
        return RE::BSContainer::ForEachResult::kContinue;
    });

    std::sort(targets.begin() + 1, targets.end(), [](const TargetInfo& a, const TargetInfo& b) {
        return a.distance < b.distance;
    });
    if (targets.size() > maxNpcTargets + 1) {
        targets.resize(maxNpcTargets + 1);
    }

    if (currentTarget && currentTarget != player &&
        IsSelectableTarget(currentTarget) &&
        CheckOutfitTargetState(currentTarget, true) == OutfitTargetState::kAllowed) {
        const auto currentID = currentTarget->formID;
        const auto existing = std::find_if(targets.begin(), targets.end(), [&](const TargetInfo& info) {
            return info.formID == currentID;
        });
        if (existing == targets.end()) {
            targets.push_back({ currentID, GetName(currentTarget), GetSex(currentTarget), 0.0f, false, currentTarget->GetHandle() });
        }
    }

    return targets;
}
static std::string BuildTargetsJson(RE::Actor* currentTarget) {
    auto targets = BuildTargetList(currentTarget);
    g_targetHandles.clear();
    for (const auto& target : targets) {
        if (target.formID != 0 && target.handle) {
            g_targetHandles[target.formID] = target.handle;
        }
    }
    std::string jsonText = "[";
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& target = targets[i];
        if (i) jsonText += ",";
        jsonText += "{\"id\":\"" + FormIDHex(target.formID) +
                    "\",\"name\":\"" + json::esc(target.name) +
                    "\",\"sex\":" + std::to_string(target.sex) +
                    ",\"distance\":" + std::to_string(static_cast<int>(target.distance)) +
                    ",\"player\":" + (target.player ? "true" : "false") + "}";
    }
    jsonText += "]";
    return jsonText;
}
static bool ReadBoolSettingFromFile(const fs::path& path, std::string_view key, bool fallback) {
    std::ifstream f(path);
    if (!f.is_open()) return fallback;
    bool value = fallback;
    std::string line;
    while (std::getline(f, line)) {
        auto semicolon = line.find(';');
        if (semicolon != std::string::npos) line.resize(semicolon);
        auto hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        auto k = line.substr(0, eq);
        k.erase(std::remove_if(k.begin(), k.end(), [](unsigned char c) { return std::isspace(c); }), k.end());
        if (k != key) continue;
        auto v = Lower(line.substr(eq + 1));
        v.erase(std::remove_if(v.begin(), v.end(), [](unsigned char c) { return std::isspace(c); }), v.end());
        // MCM may append a newer Profile1 section instead of rewriting the
        // earlier defaults. Continue scanning so the last persisted value is
        // authoritative.
        value = v == "1" || v == "true" || v == "yes" || v == "on";
    }
    return value;
}
static bool GetBoolSetting(std::string_view key, bool fallback) {
    const std::array<fs::path, 4> paths = {
        fs::path("Data/MCM/Config/OutfitManager/settings.ini"),
        fs::path("MCM/Config/OutfitManager/settings.ini"),
        fs::path("Data/MCM/Settings/OutfitManager.ini"),
        fs::path("MCM/Settings/OutfitManager.ini")
    };
    bool value = fallback;
    for (const auto& path : paths) {
        value = ReadBoolSettingFromFile(path, key, value);
    }
    return value;
}
static int ReadIntSettingFromFile(const fs::path& path, std::string_view key, int fallback) {
    std::ifstream file(path);
    if (!file.is_open()) return fallback;
    int value = fallback;
    std::string line;
    while (std::getline(file, line)) {
        if (const auto comment = line.find_first_of(";#"); comment != std::string::npos) line.resize(comment);
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        auto settingKey = line.substr(0, equals);
        settingKey.erase(std::remove_if(settingKey.begin(), settingKey.end(), [](unsigned char ch) {
            return std::isspace(ch);
        }), settingKey.end());
        if (settingKey != key) continue;
        auto settingValue = line.substr(equals + 1);
        settingValue.erase(std::remove_if(settingValue.begin(), settingValue.end(), [](unsigned char ch) {
            return std::isspace(ch);
        }), settingValue.end());
        try {
            value = std::stoi(settingValue);
        } catch (...) {
        }
    }
    return value;
}
static int GetIntSetting(std::string_view key, int fallback) {
    const std::array<fs::path, 4> paths = {
        fs::path("Data/MCM/Config/OutfitManager/settings.ini"),
        fs::path("MCM/Config/OutfitManager/settings.ini"),
        fs::path("Data/MCM/Settings/OutfitManager.ini"),
        fs::path("MCM/Settings/OutfitManager.ini")
    };
    int value = fallback;
    for (const auto& path : paths) value = ReadIntSettingFromFile(path, key, value);
    return value;
}
static float ReadFloatSettingFromFile(const fs::path& path, std::string_view key, float fallback) {
    std::ifstream file(path);
    if (!file.is_open()) return fallback;
    float value = fallback;
    std::string line;
    while (std::getline(file, line)) {
        if (const auto comment = line.find_first_of(";#"); comment != std::string::npos) line.resize(comment);
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        auto settingKey = line.substr(0, equals);
        settingKey.erase(std::remove_if(settingKey.begin(), settingKey.end(), [](unsigned char ch) {
            return std::isspace(ch);
        }), settingKey.end());
        if (settingKey != key) continue;
        auto settingValue = line.substr(equals + 1);
        settingValue.erase(std::remove_if(settingValue.begin(), settingValue.end(), [](unsigned char ch) {
            return std::isspace(ch);
        }), settingValue.end());
        try {
            value = std::stof(settingValue);
        } catch (...) {
        }
    }
    return value;
}
static float GetFloatSetting(std::string_view key, float fallback) {
    const std::array<fs::path, 4> paths = {
        fs::path("Data/MCM/Config/OutfitManager/settings.ini"),
        fs::path("MCM/Config/OutfitManager/settings.ini"),
        fs::path("Data/MCM/Settings/OutfitManager.ini"),
        fs::path("MCM/Settings/OutfitManager.ini")
    };
    float value = fallback;
    for (const auto& path : paths) value = ReadFloatSettingFromFile(path, key, value);
    return value;
}
static bool SaveWeaponsEnabled() {
    return GetBoolSetting("bSaveOutfitWeapons", false);
}
static bool OutfitWorkbenchEnabled() {
    return GetBoolSetting("bEnableOutfitWorkbench", true);
}
static std::string Summ(int s) {
    EnsureCacheLoaded();
    const auto detail = g_slotDetails.find(s);
    if (detail == g_slotDetails.end() || detail->second.summary.empty()) return "(Empty)";
    return detail->second.summary;
}

// ======== Native Functions ========
RE::BSFixedString OM_GetPluginVersion(std::monostate) { return "1.1.2"; }
RE::BSFixedString OM_GetSlotPath(std::monostate, int s) { return SlotPath(s).string().c_str(); }
bool OM_IsMenuAvailable(std::monostate) { return GetModuleHandleW(L"PrismaUI_F4.dll") != nullptr; }
bool OM_PreparePlayerPreview(std::monostate) {
    if (g_menuOpen || !CaptureOriginalPreviewCameraState()) return false;
    auto* camera = RE::PlayerCamera::GetSingleton();
    return camera && (g_camera.wasFirstPerson || camera->pipboyMode);
}
static bool OpenMenuInternal(
    RE::Actor* t,
    int quickSaveSlot,
    bool quickOutfit = false,
    bool directStudio = false)
{
    RegisterInputSink();
    RegisterDialogueMenuGuard();
    StartGameMenuCoverMonitor();
    if (!t) t = RE::PlayerCharacter::GetSingleton();
    std::unique_lock<std::mutex> lk(g_mx);
    const auto now = GetTickCount64();
    const auto lastOpen = g_lastMenuOpenMs.load();
    if (g_menuOpen || g_closeAnimationPending.load() ||
        (lastOpen != 0 && now - lastOpen < kMenuOpenDebounceMs)) {
        LogLine("2.0 OpenMenu debounce blocked openAge=" +
            std::to_string(lastOpen == 0 ? 0 : now - lastOpen) +
            " menuOpen=" + std::to_string(g_menuOpen) +
            " closePending=" + std::to_string(g_closeAnimationPending.load()));
        return false;
    }
    const bool nativeBlocked = NativeMenuBlocksOpen();
    if (nativeBlocked) {
        LogLine("2.0 OpenMenu blocked native=" + std::to_string(nativeBlocked) +
            " focusedView=" + std::to_string(g_prisma ? g_prisma->GetFocusedView() : 0) +
            " ownView=" + std::to_string(g_view));
        return false;
    }
    const bool quickSave = quickSaveSlot >= 1 && quickSaveSlot <= MAX_SLOTS;
    const bool lightweight = quickSave || quickOutfit;
    bool preparedFromFirstPerson = false;
    bool thirdPersonTransition = false;
    if (!quickOutfit) {
        if (!CaptureOriginalPreviewCameraState()) return false;
        preparedFromFirstPerson = g_camera.wasFirstPerson;
        thirdPersonTransition = EnsureThirdPersonForPreview();
    }
    g_menuPresentationPending = !quickOutfit && (thirdPersonTransition || preparedFromFirstPerson);
    if (!EnsureView() || !g_prisma->IsValid(g_view)) {
        g_menuPresentationPending = false;
        if (!quickOutfit) RestorePreviewCamera();
        return false;
    }
    g_prisma->SetOrder(g_view, kOutfitManagerViewOrder);
    g_menuTarget = t ? t->GetHandle() : RE::ObjectRefHandle{};
    g_menuActionTarget = {};
    g_lockedNpcTarget = {};
    LogLine(std::string(quickSave ? "OpenQuickSave initial target -> " :
        (quickOutfit ? "OpenQuickOutfit initial target -> " : "OpenMenu initial target -> ")) +
        (t ? (FormIDHex(t->formID) + " " + GetName(t)) : "None"));
    const auto openSerial = ++g_closeReleaseSerial;
    g_lastMenuOpenMs.store(now);
    g_menuOpen = true; g_hiddenBehindGameMenu = false; g_menuAct = 0; g_menuActSlot = 0;
    g_focusAttemptedView = 0;
    g_quickSaveMode = quickSave;
    g_quickOutfitMode = quickOutfit;
    g_directStudioMode = directStudio;
    g_quickSaveSlot = quickSave ? quickSaveSlot : 0;
    SetGameplayInputBlocked(true);
    // Keep animation and the equipment/biped update pipeline alive from the
    // first frame. RestoreMenuAfterGameMenu already follows this rule; the
    // initial open path must not freeze global time and wait for an alt-tab.
    SetMenuTimePaused(quickOutfit);
    SetSceneHudHidden(true);
    if (!lightweight && t) LockNpcForPreview(t);
    else ReleaseLockedNpc();

    // Cache/state loading can take the same mutex internally. On the first
    // full-menu open, present the already-prewarmed assistant before that work;
    // later opens still wait for their complete state to avoid stale content.
    lk.unlock();
    const bool firstFullMenuOpen = !lightweight && g_firstFullMenuOpen;
    const bool firstBootPresented = firstFullMenuOpen && g_viewReady && !g_menuPresentationPending;
    if (firstBootPresented) {
        ReloadPreviewTuning();
        std::string bootScript =
            "if(window.setOutfitManagerTuning)window.setOutfitManagerTuning(" + BuildUiTuningJson() + ");";
        bootScript +=
            "if(window.setOutfitManagerLayout)window.setOutfitManagerLayout(\"" +
            EscapeJavaScriptString(ReadUiLayoutJson()) + "\");";
        bootScript += "if(window.omBeginFirstBoot)window.omBeginFirstBoot(false);";
        g_prisma->Invoke(g_view, bootScript.c_str());
        PresentMenuView("first-boot");
        ScheduleMenuFocusRetry(g_view, openSerial);
        InteropMenuCall("omWake", "{}");
    }
    EnsureCacheLoaded();
    // Rebuild and persist names/metadata on every open so external edits or
    // deletions of slot files are reflected in both UI paths and index.json.
    LoadSlotNames();
    WriteIndex();
    if (!firstBootPresented) ReloadPreviewTuning();
    if (!lightweight && t) EnsurePreviewLights(t);
    lk.lock();
    if (!g_menuOpen) return true;

    int slot = g_curSlot, sex = t ? GetSex(t) : -1;
    std::string nm = GetName(t);
    const std::string targetID = t ? FormIDHex(t->formID) : "";
    const std::string targetsJson = BuildTargetsJson(t);
    bool hd = false;
    int sg = -1;
    std::string ss;
    std::vector<std::string> currentItems;
    if (auto it = g_indexBySlot.find(slot); it != g_indexBySlot.end()) {
        hd = true;
        sg = it->second.gender;
        if (auto detailIt = g_slotDetails.find(slot); detailIt != g_slotDetails.end()) {
            ss = detailIt->second.summary;
            currentItems = detailIt->second.itemNames;
        }
    }

    std::string summaries = "{";
    for (const auto& info : g_index) {
        if (summaries.size() > 1) {
            summaries += ",";
        }
        std::string slotItemsJson = "[";
        if (auto detailIt = g_slotDetails.find(info.slot); detailIt != g_slotDetails.end()) {
            const auto& names = detailIt->second.itemNames;
            for (std::size_t itemIndex = 0; itemIndex < names.size(); ++itemIndex) {
                if (itemIndex) {
                    slotItemsJson += ",";
                }
                slotItemsJson += "\"" + json::esc(names[itemIndex]) + "\"";
            }
        }
        slotItemsJson += "]";

        const auto detailIt = g_slotDetails.find(info.slot);
        const std::string slotName = detailIt != g_slotDetails.end() ? detailIt->second.name : "";
        summaries += "\"" + std::to_string(info.slot) + "\":{\"hasData\":true,\"name\":\"" + json::esc(slotName) + "\",\"gender\":" +
                     std::to_string(info.gender) + ",\"count\":" + std::to_string(info.count) +
                     ",\"usageCount\":" + std::to_string(info.usageCount) +
                     ",\"items\":" + slotItemsJson + "}";
    }
    summaries += "}";

    auto actualItems = CollectEquippedArmor(t);
    const auto actualClothingCount = actualItems.size();
    if (SaveWeaponsEnabled()) AppendEquippedWeapons(t, actualItems);
    std::string actualItemsJson = "[";
    for (std::size_t i = 0; i < actualItems.size(); ++i) {
        if (i) actualItemsJson += ",";
        actualItemsJson += "\"" + json::esc(SavedItemDisplayName(actualItems[i])) + "\"";
    }
    actualItemsJson += "]";

    const bool enableOutfitWorkbench = OutfitWorkbenchEnabled();
    const bool warnWorkbenchNudity = GetBoolSetting("bWarnWorkbenchNudity", true);
    const bool confirmResetDefault = GetBoolSetting("bConfirmResetDefault", true);
    const bool confirmClearSavedOutfit = GetBoolSetting("bConfirmClearSavedOutfit", true);
    const bool confirmOverwriteSavedOutfit = GetBoolSetting("bConfirmOverwriteSavedOutfit", true);
    const bool confirmCompanionInventory = GetBoolSetting("bConfirmCompanionInventory", true);
    const int quickOutfitSortMode = std::clamp(GetIntSetting("iQuickOutfitSortMode", 0), 0, 1);

    std::string itemsJson = "[";
    for (std::size_t i = 0; i < currentItems.size(); ++i) {
        if (i) {
            itemsJson += ",";
        }
        itemsJson += "\"" + json::esc(currentItems[i]) + "\"";
    }
    itemsJson += "]";

    std::string state = "{\"slot\":" + std::to_string(slot) +
                        ",\"maxSlot\":" + std::to_string(MAX_SLOTS) +
                        ",\"quickSaveSlot\":" + std::to_string(g_quickSaveSlot) +
                        ",\"quickOutfitMode\":" + (g_quickOutfitMode ? "true" : "false") +
                        ",\"quickOutfitPage\":" + std::to_string(g_quickOutfitPage) +
                        ",\"directStudioMode\":" + (g_directStudioMode ? "true" : "false") +
                        ",\"targetName\":\"" + json::esc(nm) +
                        "\",\"targetGender\":" + std::to_string(sex) +
                        ",\"targetSex\":" + std::to_string(sex) +
                        ",\"targetId\":\"" + targetID + "\"" +
                        ",\"hasData\":" + (hd ? "true" : "false") +
                        ",\"slotGender\":" + std::to_string(sg) +
                        ",\"summary\":\"" + json::esc(ss) +
                        "\",\"items\":" + itemsJson +
                        ",\"actualItems\":" + actualItemsJson +
                        ",\"actualClothingCount\":" + std::to_string(actualClothingCount) +
                        ",\"settings\":{" +
                        "\"enableOutfitWorkbench\":" + (enableOutfitWorkbench ? "true" : "false") +
                        ",\"quickOutfitSortMode\":" + std::to_string(quickOutfitSortMode) +
                        ",\"warnWorkbenchNudity\":" + (warnWorkbenchNudity ? "true" : "false") +
                        ",\"confirmResetDefault\":" + (confirmResetDefault ? "true" : "false") +
                        ",\"confirmClearSavedOutfit\":" + (confirmClearSavedOutfit ? "true" : "false") +
                        ",\"confirmOverwriteSavedOutfit\":" + (confirmOverwriteSavedOutfit ? "true" : "false") +
                        ",\"confirmCompanionInventory\":" + (confirmCompanionInventory ? "true" : "false") +
                        "}" +
                        ",\"targets\":" + targetsJson +
                        ",\"slotSummaries\":" + summaries + "}";

    std::string sc = "if(window.setOutfitManagerTuning)window.setOutfitManagerTuning(" + BuildUiTuningJson() + ");";
    sc += "if(window.setOutfitManagerLayout)window.setOutfitManagerLayout(\"" + EscapeJavaScriptString(ReadUiLayoutJson()) + "\");";
    sc += "if(window.omBeginFirstBoot)window.omBeginFirstBoot(" + std::string(lightweight ? "true" : "false") + ");";
    sc += "if(window.setOutfitManagerState)window.setOutfitManagerState(" + state + ");";
    {
        std::lock_guard<std::mutex> scriptLock(g_viewScriptMx);
        g_pendingMenuStateScript = sc;
    }
    if (firstFullMenuOpen) g_firstFullMenuOpen = false;
    // InteropMenuCall takes g_mx itself; release the open-state lock before
    // sending 2.1 JSON callbacks to avoid a same-thread mutex re-entry.
    lk.unlock();
    if (g_viewReady) {
        // 2.1's InteropCall is the stable path for JSON payloads. Keep the
        // small boolean boot expression on Invoke, but do not make the large
        // outfit state pass through JavaScript source construction.
        InteropMenuCall("setOutfitManagerTuning", BuildUiTuningJson());
        InteropMenuCall("setOutfitManagerLayout", ReadUiLayoutJson());
        const std::string bootScript =
            "if(window.omBeginFirstBoot)window.omBeginFirstBoot(" +
            std::string(lightweight ? "true" : "false") + ");";
        g_prisma->Invoke(g_view, bootScript.c_str());
        InteropMenuCall("setOutfitManagerState", state);
        if (!g_menuPresentationPending) {
            PresentMenuView("open");
            ScheduleMenuFocusRetry(g_view, openSerial);
            InteropMenuCall("omWake", "{}");
        }
    } else {
        LogLine("2.0 UI open waiting for DOM view=" + std::to_string(g_view));
    }
    if (!lightweight && !g_menuPresentationPending) PositionPreviewCamera(t);
    // Opening from a Pip-Boy item can restore the player's camera one frame later.
    // Reapply after that transition so the actor remains in the right preview space.
    const auto previewHandle = t ? t->GetHandle() : RE::ObjectRefHandle{};
    if (!lightweight || thirdPersonTransition || preparedFromFirstPerson) std::thread([
        previewHandle, lightweight, thirdPersonTransition, preparedFromFirstPerson] {
        if (thirdPersonTransition || preparedFromFirstPerson) {
            const auto completed = std::make_shared<std::atomic_bool>(false);
            for (int attempt = 0; attempt < 14 && !completed->load(); ++attempt) {
                Sleep(attempt == 0 ? 180u : 120u);
                if (auto* tasks = F4SE::GetTaskInterface()) {
                    tasks->AddTask([previewHandle, lightweight, completed] {
                        if (completed->load()) return;
                        if (ApplyPreviewCameraIfReady(previewHandle, !lightweight)) {
                            completed->store(true);
                        }
                    });
                }
            }
        } else if (!lightweight) {
            // Let a Pip-Boy item-use close animation return the camera before the final framing pass.
            Sleep(520);
            if (auto* tasks = F4SE::GetTaskInterface()) {
                tasks->AddTask([previewHandle] {
                    ApplyPreviewCameraIfReady(previewHandle, true);
                });
            }
        }
    }).detach();
    return true;
}
bool OM_OpenMenu(std::monostate, RE::Actor* t) {
    return OpenMenuInternal(t, 0, false, false);
}
bool OM_OpenQuickSaveMenu(std::monostate, RE::Actor* t, int slot) {
    return OpenMenuInternal(t, slot, false, false);
}
bool OM_OpenQuickOutfitMenu(std::monostate, RE::Actor* t) {
    return OpenMenuInternal(t, 0, true, false);
}
bool OM_OpenStudioMenu(std::monostate, RE::Actor* t) {
    return OpenMenuInternal(t, 0, false, true);
}
void OM_CloseMenu(std::monostate) {
    CloseMenuInternal(false);
}
bool OM_IsMenuOpen(std::monostate) { UpdateGameMenuCoverState(); return g_menuOpen; }
RE::Actor* OM_GetMenuTarget(std::monostate) {
    if (g_menuActionTarget) {
        auto ref = g_menuActionTarget.get();
        if (auto* actor = ref ? ref->As<RE::Actor>() : nullptr; actor) return actor;
    }
    auto ref = g_menuTarget.get();
    return ref ? ref->As<RE::Actor>() : nullptr;
}
RE::Actor* OM_GetMenuActionTarget(std::monostate) {
    auto ref = g_menuActionTarget.get();
    auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
    LogLine(std::string("GetMenuActionTarget -> ") + (actor ? (FormIDHex(actor->formID) + " " + GetName(actor)) : "None"));
    return actor;
}
RE::TESObjectREFR* OM_GetMenuActionTargetRef(std::monostate) {
    auto ref = g_menuActionTarget.get();
    auto* refr = ref.get();
    auto* actor = refr ? refr->As<RE::Actor>() : nullptr;
    LogLine(std::string("GetMenuActionTargetRef -> ") + (actor ? (FormIDHex(actor->formID) + " " + GetName(actor)) : "None"));
    return refr;
}
void OM_ClearMenuTarget(std::monostate) { g_menuTarget = {}; g_menuActionTarget = {}; }
float OM_GetMenuOpenCooldownRemaining(std::monostate, float n) { return Rem(n, g_menuOpenT, 1.0f); }
bool OM_BeginMenuOpen(std::monostate, float n, float d) { return Try(n, g_menuOpenT, d); }
float OM_GetMenuActionCooldownRemaining(std::monostate, float n) { return Rem(n, g_menuActionT, 0.6f); }
bool OM_BeginMenuAction(std::monostate, float n, float d) { return Try(n, g_menuActionT, d); }
int OM_GetMenuAction(std::monostate) { return g_menuAct; }
int OM_GetMenuActionSlot(std::monostate) { return g_menuActSlot; }
void OM_ClearMenuAction(std::monostate) { g_menuAct = 0; g_menuActSlot = 0; }

RE::Actor* OM_GetCameraTarget(std::monostate) { return nullptr; }
RE::BSFixedString OM_GetActorName(std::monostate, RE::Actor* a) { return GetName(a).c_str(); }
void OM_LogText(std::monostate, RE::BSFixedString text) { LogLine(text.c_str() ? text.c_str() : ""); }
bool OM_IsValidTarget(std::monostate, RE::Actor* a) { return a && !a->IsDead(false); }
bool OM_IsValidOutfitTarget(std::monostate, RE::Actor* a) {
    auto* player = RE::PlayerCharacter::GetSingleton();
    return a && a != player && IsSelectableTarget(a);
}
bool OM_StartQuickTargetEffect(std::monostate, RE::Actor* a) {
    if (!a || a == RE::PlayerCharacter::GetSingleton() || !IsSelectableTarget(a)) {
        LogLine("2.0 quick target effect rejected");
        return false;
    }

    auto* shader = RE::TESForm::GetFormByID<RE::TESEffectShader>(0x001E077C);
    if (!shader || !a->Get3D()) {
        LogLine(std::string("2.0 quick target effect unavailable shader=") +
            (shader ? "yes" : "no") + " 3d=" + (a->Get3D() ? "yes" : "no"));
        return false;
    }

    if (!g_quickTargetShader.captured || g_quickTargetShader.shader != shader) {
        if (g_quickTargetShader.captured && g_quickTargetShader.shader) {
            g_quickTargetShader.shader->data = g_quickTargetShader.original;
        }
        g_quickTargetShader.shader = shader;
        g_quickTargetShader.original = shader->data;
        g_quickTargetShader.captured = true;
    }

    // PowerArmorTargetingHUD normally fades in over game time. A true
    // freeze leaves that age at zero, so make this temporary session copy
    // render immediately and remain static until the menu closes.
    auto& data = shader->data;
    data.fillAlphaFadeInTime = 0.0f;
    data.fillAlphaFullTime = 0.0f;
    data.fillAlphaFadeOutTime = 0.0f;
    data.fillAlphaPersistentPercent = 1.0f;
    data.fillAlphaPulseAmplitude = 0.0f;
    data.fillAlphaPulseFrequency = 0.0f;
    data.fillAlphaFullPercent = 1.0f;
    data.edgeAlphaFadeInTime = 0.0f;
    data.edgeAlphaFullTime = 0.0f;
    data.edgeAlphaFadeOutTime = 0.0f;
    data.edgeAlphaPersistentPercent = 1.0f;
    data.edgeAlphaPulseAmplitude = 0.0f;
    data.edgeAlphaPulseFrequency = 0.0f;
    data.edgeAlphaFullPercent = 1.0f;

    // Apply the effect through the engine before the lightweight menu freezes
    // time. The Papyrus Play() fallback can be deferred until unpausing.
    auto* effect = a->ApplyEffectShader(shader, -1.0f);
    LogLine(std::string("2.0 quick target effect applied target=") +
        FormIDHex(a->GetFormID()) + " result=" + (effect ? "yes" : "no"));
    return effect != nullptr;
}
void OM_RestoreQuickTargetEffect(std::monostate) {
    if (!g_quickTargetShader.captured || !g_quickTargetShader.shader) {
        return;
    }
    g_quickTargetShader.shader->data = g_quickTargetShader.original;
    g_quickTargetShader = {};
    LogLine("2.0 quick target shader restored");
}
bool OM_IsOutfitItem(std::monostate, RE::TESForm* f) { return IsManagedOutfitArmor(f); }
bool OM_IsPowerArmorItem(std::monostate, RE::TESForm* f) { return IsPowerArmor(f); }
bool OM_AreOutfitNotificationsEnabled(std::monostate) { return GetBoolSetting("bEnableOutfitNotifications", false); }

int OM_GetCurrentSlot(std::monostate) { return g_curSlot; }
void OM_SetCurrentSlot(std::monostate, int s) { if (s >= 1 && s <= MAX_SLOTS) { g_curSlot = s; WriteState(); } }
int OM_GetActiveOutfitSlot(std::monostate, RE::Actor* actor) {
    EnsureCacheLoaded();
    return GetActiveSlotForActor(actor);
}
int OM_FindFirstEmptyOutfitSlot(std::monostate) { return FindFirstEmptySlot(); }
RE::BSFixedString OM_GetSavedOutfitName(std::monostate, int slot) {
    EnsureCacheLoaded();
    if (slot < 1 || slot > MAX_SLOTS || !g_indexBySlot.contains(slot)) return "";
    if (const auto detail = g_slotDetails.find(slot); detail != g_slotDetails.end() && !detail->second.name.empty()) {
        return detail->second.name.c_str();
    }
    char label[32]{};
    std::snprintf(label, sizeof(label), "Outfit %03d", slot);
    return label;
}
int OM_GetLastRandomSlot(std::monostate) { return g_lastRandSlot; }
int OM_GetLastEquippedSlot(std::monostate) { return g_lastEquippedSlot; }
void OM_SetLastEquippedSlot(std::monostate, int s) { if (s >= 1 && s <= MAX_SLOTS) g_lastEquippedSlot = s; }
static void ClearExpiredBusyState() {
    constexpr ULONGLONG kBusyTimeoutMs = 20000;
    const auto now = GetTickCount64();
    if (g_eqBusy && g_eqBusySinceMs != 0 && now - g_eqBusySinceMs >= kBusyTimeoutMs) {
        LogLine("2.0 equip busy watchdog expired");
        g_eqBusy = false;
        g_eqBusySinceMs = 0;
    }
    if (g_randBusy && g_randBusySinceMs != 0 && now - g_randBusySinceMs >= kBusyTimeoutMs) {
        LogLine("2.0 random busy watchdog expired");
        g_randBusy = false;
        g_randBusySinceMs = 0;
        g_eqBusy = false;
        g_eqBusySinceMs = 0;
    }
}

bool OM_IsEquipBusy(std::monostate) { ClearExpiredBusyState(); return g_eqBusy; }
float OM_GetEquipCooldownRemaining(std::monostate, float n) { ClearExpiredBusyState(); return Rem(n, g_eqLastT, 3.0f); }
bool OM_BeginEquipAction(std::monostate, float n, float d) {
    ClearExpiredBusyState();
    if (g_eqBusy) return false;
    if (!Try(n, g_eqLastT, d)) return false;
    g_eqBusy = true;
    g_eqBusySinceMs = GetTickCount64();
    return true;
}
void OM_FinishEquipAction(std::monostate) { g_eqBusy = false; g_eqBusySinceMs = 0; }
bool OM_IsRandomBusy(std::monostate) { ClearExpiredBusyState(); return g_randBusy; }
float OM_GetRandomCooldownRemaining(std::monostate, float n) { return Rem(n, g_randLastT, 6.0f); }
bool OM_BeginRandom(std::monostate, float n, float d) {
    ClearExpiredBusyState();
    if (g_randBusy || g_eqBusy) return false;
    if (!Try(n, g_randLastT, d)) return false;
    g_randBusy = true;
    g_eqBusy = true;
    g_randBusySinceMs = GetTickCount64();
    g_eqBusySinceMs = g_randBusySinceMs;
    return true;
}
void OM_FinishRandom(std::monostate, int s) {
    g_randBusy = false;
    g_eqBusy = false;
    g_randBusySinceMs = 0;
    g_eqBusySinceMs = 0;
    if (s > 0) g_lastRandSlot = s;
}

static int ChooseRandomSlotNative(int sex, int cur, int last) {
    EnsureCacheLoaded();
    std::vector<int> eligible;
    for (const auto& info : g_index) {
        if (info.slot == cur || info.slot == last) continue;
        if (info.gender == sex && info.count > 0) eligible.push_back(info.slot);
    }
    if (eligible.empty() && last > 0) {
        for (const auto& info : g_index) {
            if (info.slot == cur) continue;
            if (info.gender == sex && info.count > 0) eligible.push_back(info.slot);
        }
    }
    if (eligible.empty() && cur > 0) {
        for (const auto& info : g_index) {
            if (info.slot == cur && info.gender == sex && info.count > 0) {
                eligible.push_back(info.slot);
                break;
            }
        }
    }
    if (eligible.empty()) return 0;

    auto& bag = g_randomBags[sex];
    bag.erase(std::remove_if(bag.begin(), bag.end(), [&](int slot) {
        return std::find(eligible.begin(), eligible.end(), slot) == eligible.end();
    }), bag.end());

    if (bag.empty()) {
        bag = eligible;
        static std::mt19937 rng{ std::random_device{}() };
        std::shuffle(bag.begin(), bag.end(), rng);
    }

    LARGE_INTEGER perf{};
    QueryPerformanceCounter(&perf);
    const auto rotate = bag.empty() ? 0 : static_cast<std::size_t>(perf.QuadPart) % bag.size();
    std::rotate(bag.begin(), bag.begin() + rotate, bag.end());
    const int chosen = bag.back();
    bag.pop_back();
    return chosen;
}
int OM_ChooseRandomSlot(std::monostate, int sex, int cur, int last) {
    return ChooseRandomSlotNative(sex, cur, last);
}
int OM_FindMatchingSlot(std::monostate, int cur, int dir, int sex) {
    EnsureCacheLoaded();
    if (dir == 0) return 0;
    if (g_index.empty()) return 0;

    int bestSlot = 0;
    if (dir > 0) {
        int wrapSlot = 0;
        for (const auto& info : g_index) {
            if (info.gender != sex || info.count <= 0) continue;
            if (info.slot > cur && (bestSlot == 0 || info.slot < bestSlot)) bestSlot = info.slot;
            if (wrapSlot == 0 || info.slot < wrapSlot) wrapSlot = info.slot;
        }
        return bestSlot != 0 ? bestSlot : wrapSlot;
    }

    int wrapSlot = 0;
    for (const auto& info : g_index) {
        if (info.gender != sex || info.count <= 0) continue;
        if (info.slot < cur && info.slot > bestSlot) bestSlot = info.slot;
        if (info.slot > wrapSlot) wrapSlot = info.slot;
    }
    return bestSlot != 0 ? bestSlot : wrapSlot;
}
bool OM_HasSlotData(std::monostate, int s) { EnsureCacheLoaded(); return g_indexBySlot.find(s) != g_indexBySlot.end(); }
int OM_GetSlotGender(std::monostate, int s) {
    EnsureCacheLoaded();
    auto it = g_indexBySlot.find(s);
    return it == g_indexBySlot.end() ? -1 : it->second.gender;
}

static std::vector<SavedMod> CollectInstanceMods(RE::BGSObjectInstanceExtra* modExtra) {
    std::vector<SavedMod> mods;
    if (!modExtra || !modExtra->values) return mods;
    for (const auto& data : modExtra->GetIndexData()) {
        auto* form = RE::TESForm::GetFormByID(data.objectID);
        if (!form || !form->Is(RE::ENUM_FORM_ID::kOMOD)) continue;
        const auto duplicate = std::find_if(mods.begin(), mods.end(), [&data](const SavedMod& mod) {
            return mod.fid == data.objectID &&
                   mod.index == data.index &&
                   mod.rank == data.rank &&
                   mod.disabled == (data.disabled != 0);
        });
        if (duplicate == mods.end()) {
            mods.push_back({ data.objectID, data.index, data.rank, data.disabled != 0 });
        }
    }
    return mods;
}

static bool IsExcludedWeaponEffectMod(const SavedMod& savedMod) {
    auto* form = RE::TESForm::GetFormByID(savedMod.fid);
    auto* objectMod = form ? form->As<RE::BGSMod::Attachment::Mod>() : nullptr;
    if (!objectMod) return true;
    auto* sourceFile = objectMod->GetFile(0);
    if (!sourceFile || Lower(std::string(sourceFile->GetFilename())) != "fallout4.esm") {
        return false;
    }

    std::string identity;
    if (const char* editorID = objectMod->GetFormEditorID(); editorID) identity += editorID;
    if (const char* fullName = objectMod->GetFullName(); fullName) identity += std::string(" ") + fullName;
    if (auto* attachPoint = RE::detail::BGSKeywordGetTypedKeywordByIndex(
            RE::KeywordType::kAttachPoint, objectMod->attachPoint.keywordIndex)) {
        if (const char* editorID = attachPoint->GetFormEditorID(); editorID) identity += std::string(" ") + editorID;
    }
    identity = Lower(std::move(identity));
    return identity.find("unique") != std::string::npos ||
           identity.find("独特") != std::string::npos ||
           identity.find("唯一") != std::string::npos;
}

static std::vector<SavedMod> FilterWeaponMods(const std::vector<SavedMod>& mods) {
    std::vector<SavedMod> filtered;
    filtered.reserve(mods.size());
    for (const auto& mod : mods) {
        if (!IsExcludedWeaponEffectMod(mod)) filtered.push_back(mod);
    }
    return filtered;
}

static std::optional<float> GetInstanceColor(RE::TBO_InstanceData* instanceData) {
    if (!instanceData) return std::nullopt;
    const float color = instanceData->GetColorRemappingIndex();
    return std::isfinite(color) && color != (std::numeric_limits<float>::max)() ? std::optional<float>{ color } : std::nullopt;
}

static void MergeSavedMods(std::vector<SavedMod>& target, const std::vector<SavedMod>& source) {
    for (const auto& candidate : source) {
        const auto duplicate = std::find_if(target.begin(), target.end(), [&candidate](const SavedMod& existing) {
            return existing.fid == candidate.fid &&
                   existing.index == candidate.index &&
                   existing.rank == candidate.rank &&
                   existing.disabled == candidate.disabled;
        });
        if (duplicate == target.end()) target.push_back(candidate);
    }
}

static void AddOutfitEntry(
    std::vector<OE>& items,
    RE::TESForm* form,
    RE::BGSObjectInstanceExtra* modExtra = nullptr,
    RE::TBO_InstanceData* instanceData = nullptr)
{
    if (!IsManagedOutfitArmor(form)) return;
    uint32_t fid = form->GetFormID();
    const auto candidateMods = CollectInstanceMods(modExtra);
    const auto candidateColor = GetInstanceColor(instanceData);
    for (auto& e : items) {
        if (e.fid == fid &&
            e.color == candidateColor &&
            SameSavedMods(e.mods, candidateMods)) {
            MergeSavedMods(e.mods, CollectInstanceMods(modExtra));
            e.modSnapshot = e.modSnapshot || modExtra != nullptr;
            if (!e.color) e.color = GetInstanceColor(instanceData);
            return;
        }
    }

    OE e;
    e.fid = fid;
    auto* arm = form->As<RE::TESObjectARMO>();
    auto* fn = arm ? arm->GetFullName() : nullptr;
    e.name = fn ? fn : "Unknown";
    e.mods = candidateMods;
    e.color = GetInstanceColor(instanceData);
    e.modSnapshot = modExtra != nullptr;
    items.push_back(e);
}

static std::vector<OE> CollectEquippedArmor(RE::Actor* actor) {
    std::vector<OE> items;
    if (!actor) return items;

    if (const auto& biped = actor->GetCurrentBiped(); biped) {
        for (int i = 0; i < std::to_underlying(RE::BIPED_OBJECT::kTotal); ++i) {
            auto* bip = biped->GetBipObject(static_cast<RE::BIPED_OBJECT>(i));
            if (bip && bip->parent.object) {
                AddOutfitEntry(items, bip->parent.object, bip->modExtra, bip->parent.instanceData.get());
            }
        }
    }

    auto* inv = actor->inventoryList;
    if (!inv) return items;
    for (auto& item : inv->data) {
        auto* form = item.object;
        if (!IsManagedOutfitArmor(form)) continue;
        std::uint32_t stackID = 0;
        for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
            if (!stack->IsEquipped()) continue;
            auto* modExtra = stack->extra ? stack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
            AddOutfitEntry(items, form, modExtra, item.GetInstanceData(stackID));
        }
    }
    return items;
}

static void AppendEquippedWeapons(RE::Actor* actor, std::vector<OE>& items) {
    if (!actor || !actor->inventoryList) return;
    for (auto& inventoryItem : actor->inventoryList->data) {
        auto* form = inventoryItem.object;
        if (!IsSupportedSavedWeapon(form)) continue;
        std::uint32_t stackID = 0;
        for (auto* stack = inventoryItem.stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
            if (!stack->IsEquipped()) continue;
            if (std::any_of(items.begin(), items.end(), [form](const OE& entry) {
                return entry.weapon && entry.fid == form->GetFormID();
            })) continue;

            OE weapon;
            weapon.fid = form->GetFormID();
            const auto* baseWeapon = form->As<RE::TESObjectWEAP>();
            const char* name = baseWeapon ? baseWeapon->GetFullName() : nullptr;
            weapon.name = name && name[0] ? name : "Unknown Weapon";
            auto* objectExtra = stack->extra ? stack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
            weapon.mods = FilterWeaponMods(CollectInstanceMods(objectExtra));
            weapon.modSnapshot = objectExtra != nullptr;
            weapon.weapon = true;
            items.push_back(std::move(weapon));
        }
    }
}

static RE::Actor* ResolveMenuActionActor() {
    auto ref = g_menuActionTarget.get();
    return ref ? ref->As<RE::Actor>() : nullptr;
}

static bool SameSavedMods(const std::vector<SavedMod>& lhs, const std::vector<SavedMod>& rhs) {
    auto normalized = [](const std::vector<SavedMod>& source) {
        std::vector<std::tuple<uint32_t, std::uint8_t, std::uint8_t>> result;
        for (const auto& mod : source) {
            if (!mod.disabled) result.emplace_back(mod.fid, mod.index, mod.rank);
        }
        std::sort(result.begin(), result.end());
        return result;
    };
    return normalized(lhs) == normalized(rhs);
}

static bool ContainsSavedMods(const std::vector<SavedMod>& actual, const std::vector<SavedMod>& wanted) {
    for (const auto& expected : wanted) {
        if (expected.disabled) continue;
        const auto found = std::find_if(actual.begin(), actual.end(), [&expected](const SavedMod& candidate) {
            return !candidate.disabled && candidate.fid == expected.fid &&
                   candidate.index == expected.index &&
                   candidate.rank == expected.rank;
        });
        if (found == actual.end()) return false;
    }
    return true;
}

static bool IsWearingSavedOutfitNative(int slot, RE::Actor* actor) {
    std::vector<OE> desired;
    if (ValidateOutfitForActor(slot, actor, desired) <= 0) return false;
    int lastAppliedSlot = 0;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        const auto it = g_lastAppliedSlots.find(actor->GetFormID());
        if (it != g_lastAppliedSlots.end()) lastAppliedSlot = it->second;
    }
    if (lastAppliedSlot != slot) {
        LogLine("2.0 wearing check target=" + FormIDHex(actor->GetFormID()) +
            " requested=" + std::to_string(slot) + " last=" + std::to_string(lastAppliedSlot) + " result=0");
        return false;
    }

    auto equipped = CollectEquippedArmor(actor);
    if (SaveWeaponsEnabled()) AppendEquippedWeapons(actor, equipped);
    const bool allBaseItemsEquipped = std::all_of(desired.begin(), desired.end(), [&equipped](const OE& wanted) {
        return std::any_of(equipped.begin(), equipped.end(), [&wanted](const OE& current) {
            return current.fid == wanted.fid && current.weapon == wanted.weapon &&
                   (!wanted.weapon || SameSavedMods(current.mods, wanted.mods));
        });
    });
    LogLine("2.0 wearing check target=" + FormIDHex(actor->GetFormID()) +
        " requested=" + std::to_string(slot) + " last=" + std::to_string(lastAppliedSlot) +
        " result=" + (allBaseItemsEquipped ? "1" : "0"));
    return allBaseItemsEquipped;
}

static int ValidateOutfitForActor(
    int slot,
    RE::Actor* actor,
    std::vector<OE>& items,
    SlotReadStatus* status,
    std::vector<OE>* resolvedSlotItems,
    int* slotGenderOut)
{
    if (!actor) return -3;
    if (IsActorInPowerArmor(actor)) return -4;
    int slotGender = -1;
    std::string summary;
    SlotReadStatus localStatus;
    const bool hasResolvedItems = ReadSlot(slot, slotGender, items, summary, &localStatus);
    if (status) *status = localStatus;
    if (resolvedSlotItems) *resolvedSlotItems = items;
    if (slotGenderOut) *slotGenderOut = slotGender;
    if (!hasResolvedItems && localStatus.RemovedCount() <= 0) return -1;
    const int targetGender = GetSex(actor);
    if (targetGender >= 0 && slotGender >= 0 && targetGender != slotGender) return -2;
    const bool allowWeapons = SaveWeaponsEnabled();
    items.erase(std::remove_if(items.begin(), items.end(), [allowWeapons](const OE& item) {
        auto* form = RE::TESForm::GetFormByID(item.fid);
        return item.weapon ? !allowWeapons || !IsSupportedSavedWeapon(form) : !IsManagedOutfitArmor(form);
    }), items.end());
    items.erase(std::remove_if(items.begin(), items.end(), [actor](const OE& item) {
        if (item.weapon) return false;
        auto* form = RE::TESForm::GetFormByID(item.fid);
        const bool conflict = ConflictsWithIgnoredEquippedArmor(actor, form);
        if (conflict) {
            LogLine("2.0 skipped saved item conflicting with ignored armor target=" +
                FormIDHex(actor->GetFormID()) + " item=" + FormIDHex(item.fid));
        }
        return conflict;
    }), items.end());
    for (auto& item : items) {
        if (item.weapon) item.mods = FilterWeaponMods(item.mods);
    }
    if (items.size() > 44) items.resize(44);
    return items.empty() ? -1 : static_cast<int>(items.size());
}

bool OM_HasMenuActionTarget(std::monostate) { return ResolveMenuActionActor() != nullptr; }
int OM_GetMenuActionTargetSex(std::monostate) {
    auto* actor = ResolveMenuActionActor();
    return actor ? GetSex(actor) : -1;
}
RE::BSFixedString OM_GetMenuActionTargetName(std::monostate) {
    auto* actor = ResolveMenuActionActor();
    return actor ? GetName(actor).c_str() : "Unknown";
}
bool OM_IsMenuActionTargetPowerArmorBlocked(std::monostate) {
    auto* actor = ResolveMenuActionActor();
    return !actor || IsActorInPowerArmor(actor);
}
bool OM_IsMenuActionTargetPlayerTeammate(std::monostate) {
    return IsPlayerTeammateNative(ResolveMenuActionActor());
}
int OM_SaveMenuActionTargetOutfit(std::monostate, int slot) {
    EnsureCacheLoaded();
    auto* actor = ResolveMenuActionActor();
    if (!actor || slot < 1 || slot > MAX_SLOTS) return -3;
    if (IsActorInPowerArmor(actor)) return -4;
    const int sex = GetSex(actor);
    if (sex < 0) return -3;
    auto items = CollectEquippedArmor(actor);
    if (SaveWeaponsEnabled()) AppendEquippedWeapons(actor, items);
    if (items.empty()) return 0;
    for (const auto& item : items) {
        LogLine("2.0 save item=" + FormIDHex(item.fid) + " mods=" + std::to_string(item.mods.size()) + " color=" + (item.color ? std::to_string(*item.color) : "none"));
    }
    if (!WriteSlot(slot, sex, items)) return 0;
    WriteIndex();
    SetActiveSlotForActor(actor, slot);
    LogLine("Native menu save target=" + FormIDHex(actor->formID) + " slot=" + std::to_string(slot) + " count=" + std::to_string(items.size()));
    return static_cast<int>(items.size());
}
int OM_LoadMenuActionTargetOutfit(std::monostate, int slot) {
    EnsureCacheLoaded();
    std::vector<OE> items;
    return ValidateOutfitForActor(slot, ResolveMenuActionActor(), items);
}
bool OM_IsMenuActionTargetWearingOutfit(std::monostate, int slot) {
    EnsureCacheLoaded();
    return IsWearingSavedOutfitNative(slot, ResolveMenuActionActor());
}
bool OM_IsWearingOutfitNative(std::monostate, int slot, RE::Actor* actor) {
    EnsureCacheLoaded();
    return IsWearingSavedOutfitNative(slot, actor);
}
static RE::BGSInventoryItem* FindInventoryItem(RE::Actor* actor, RE::TESBoundObject* object) {
    if (!actor || !actor->inventoryList || !object) return nullptr;
    for (auto& item : actor->inventoryList->data) {
        if (item.object == object) return std::addressof(item);
    }
    return nullptr;
}

static int FindStackID(RE::BGSInventoryItem* item, const RE::ExtraDataList* preferredExtra, bool preferEquipped) {
    if (!item) return -1;
    int fallback = -1;
    int stackID = 0;
    for (auto* stack = item->stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
        if (preferredExtra && stack->extra.get() == preferredExtra) return stackID;
        if (preferEquipped && stack->IsEquipped()) return stackID;
        if (fallback < 0) fallback = stackID;
    }
    return preferredExtra ? -1 : fallback;
}

static bool SameSavedColor(RE::BGSInventoryItem* item, std::uint32_t stackID, const std::optional<float>& wanted) {
    const auto actual = item ? GetInstanceColor(item->GetInstanceData(stackID)) : std::nullopt;
    if (actual.has_value() != wanted.has_value()) return false;
    return !actual || std::abs(*actual - *wanted) <= 0.0001f;
}

static int FindStackIDBySavedAppearance(RE::BGSInventoryItem* item, const OE& wanted, bool requireName) {
    if (!item) return -1;
    std::uint32_t stackID = 0;
    for (auto* stack = item->stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
        if (requireName && !wanted.name.empty()) {
            const char* stackName = item->GetDisplayFullName(stackID);
            if (!stackName || wanted.name != stackName) continue;
        }
        auto* objectExtra = stack->extra ? stack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
        if (wanted.modSnapshot && !objectExtra) continue;
        if (!SameSavedMods(CollectInstanceMods(objectExtra), wanted.mods)) continue;
        if (!wanted.weapon && !SameSavedColor(item, stackID, wanted.color)) continue;
        return static_cast<int>(stackID);
    }
    return -1;
}

static int FindStackIDBySavedMods(RE::BGSInventoryItem* item, const std::vector<SavedMod>& wantedMods) {
    if (!item) return -1;
    int stackID = 0;
    for (auto* stack = item->stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
        auto* objectExtra = stack->extra ? stack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
        if (SameSavedMods(CollectInstanceMods(objectExtra), wantedMods)) return stackID;
    }
    return -1;
}

static int FindStackIDByNameAndSavedMods(
    RE::BGSInventoryItem* item,
    std::string_view wantedName,
    const std::vector<SavedMod>& wantedMods)
{
    if (!item || wantedName.empty()) return -1;
    std::uint32_t stackID = 0;
    for (auto* stack = item->stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
        const char* stackName = item->GetDisplayFullName(stackID);
        if (!stackName || wantedName != stackName) continue;
        auto* objectExtra = stack->extra ? stack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
        if (SameSavedMods(CollectInstanceMods(objectExtra), wantedMods)) {
            return static_cast<int>(stackID);
        }
    }
    return -1;
}

static int FindStackIDByNameContainingSavedMods(
    RE::BGSInventoryItem* item,
    std::string_view wantedName,
    const std::vector<SavedMod>& wantedMods)
{
    if (!item || wantedName.empty() || wantedMods.empty()) return -1;
    std::uint32_t stackID = 0;
    for (auto* stack = item->stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
        const char* stackName = item->GetDisplayFullName(stackID);
        if (!stackName || wantedName != stackName) continue;
        auto* objectExtra = stack->extra ? stack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
        if (ContainsSavedMods(CollectInstanceMods(objectExtra), wantedMods)) {
            return static_cast<int>(stackID);
        }
    }
    return -1;
}

static int FindStackIDByName(RE::BGSInventoryItem* item, std::string_view wantedName) {
    if (!item || wantedName.empty()) return -1;
    std::uint32_t stackID = 0;
    for (auto* stack = item->stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
        const char* stackName = item->GetDisplayFullName(stackID);
        if (stackName && wantedName == stackName) return static_cast<int>(stackID);
    }
    return -1;
}

static int FindUniqueStackIDByNameAndSavedMods(
    RE::BGSInventoryItem* item,
    std::string_view wantedName,
    const std::vector<SavedMod>& wantedMods)
{
    if (!item || wantedName.empty()) return -1;
    int match = -1;
    std::uint32_t stackID = 0;
    for (auto* stack = item->stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
        const char* stackName = item->GetDisplayFullName(stackID);
        if (!stackName || wantedName != stackName) continue;
        auto* objectExtra = stack->extra ?
            stack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
        if (!SameSavedMods(CollectInstanceMods(objectExtra), wantedMods)) continue;
        if (match >= 0) return -1;
        match = static_cast<int>(stackID);
    }
    return match;
}

static int FindUniqueStackIDByName(RE::BGSInventoryItem* item, std::string_view wantedName) {
    if (!item || wantedName.empty()) return -1;
    int match = -1;
    std::uint32_t stackID = 0;
    for (auto* stack = item->stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
        const char* stackName = item->GetDisplayFullName(stackID);
        if (!stackName || wantedName != stackName) continue;
        if (match >= 0) return -1;
        match = static_cast<int>(stackID);
    }
    return match;
}

static int FindStackIDContainingSavedMods(RE::BGSInventoryItem* item, const std::vector<SavedMod>& wantedMods) {
    if (!item || wantedMods.empty()) return -1;
    int stackID = 0;
    for (auto* stack = item->stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
        auto* objectExtra = stack->extra ? stack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
        if (ContainsSavedMods(CollectInstanceMods(objectExtra), wantedMods)) return stackID;
    }
    return -1;
}

static void ClearManagedEquipFlags(RE::Actor* actor, RE::BGSInventoryItem* item, int stackID) {
    if (!actor || !item || stackID < 0) return;
    auto* stack = item->GetStackByID(static_cast<std::uint32_t>(stackID));
    if (!stack) return;
    stack->flags.reset(RE::BGSInventoryItem::Stack::Flag::kEquipStateLocked);
    if (actor == RE::PlayerCharacter::GetSingleton()) {
        stack->flags.reset(RE::BGSInventoryItem::Stack::Flag::kInvShouldEquip);
    }
}

static void PrepareNpcInventoryTransfer(
    RE::Actor* actor,
    RE::TESBoundObject* object,
    RE::ActorEquipManager* equipManager)
{
    if (!actor || !object || actor == RE::PlayerCharacter::GetSingleton() || !equipManager) return;

    // Fallout 4's NPC inventory protection can restore the default outfit
    // when an armor record is added or removed by script.  ContainerMenu
    // first releases the target item, then equips the transferred stack with
    // the NPC equip-state lock.  Mirror that ordering for every real NPC
    // outfit item we create.
    RE::BGSObjectInstance existing(object, nullptr);
    equipManager->UnequipObject(
        actor, &existing, 1, nullptr, 0, false, true, false, true, nullptr);
}

static void AddOutfitItemToInventory(
    RE::Actor* actor,
    RE::TESBoundObject* object,
    const RE::BSTSmartPointer<RE::ExtraDataList>& extra,
    bool silentAdd)
{
    if (!actor || !object) return;

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (silentAdd && actor == player && actor->inventoryList) {
        // Only suppress the player's HUD message.  NPC inventory additions
        // must go through the real container path so the engine records the
        // same teammate-transfer state used by the trade menu.
        actor->inventoryList->AddItem2(object, 1, extra.get());
        return;
    }

    if (actor != player && player) {
        actor->AddObjectToContainer(
            object,
            extra,
            1,
            player,
            RE::ITEM_REMOVE_REASON::kStoreTeammate);
        return;
    }

    actor->AddObjectToContainer(
        object,
        extra,
        1,
        nullptr,
        RE::ITEM_REMOVE_REASON::kNone);
}

static bool ShouldForceEquip(RE::Actor* actor) {
    return actor && actor != RE::PlayerCharacter::GetSingleton();
}

static void RemovePreviousManagedItems(RE::Actor* actor, const std::vector<std::uint32_t>& managedIDs) {
    if (!actor || !actor->inventoryList) return;
    std::vector<ManagedInstanceRef> managedInstances;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        const auto it = g_managedInstances.find(actor->GetFormID());
        if (it != g_managedInstances.end()) {
            managedInstances = std::move(it->second);
            g_managedInstances.erase(it);
        }
    }
    auto* equipManager = RE::ActorEquipManager::GetSingleton();
    for (auto id : managedIDs) {
        auto* form = RE::TESForm::GetFormByID(id);
        if (IsIgnoredOutfitArmor(form)) {
            LogLine("2.0 preserved ignored managed armor target=" +
                FormIDHex(actor->GetFormID()) + " item=" + FormIDHex(id));
            continue;
        }
        auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
        PrepareNpcInventoryTransfer(actor, object, equipManager);
        auto* inventoryItem = FindInventoryItem(actor, object);
        if (!inventoryItem) continue;
        bool removedTrackedInstances = false;
        for (const auto& tracked : managedInstances) {
            if (tracked.fid != id || !tracked.extra) continue;
            inventoryItem = FindInventoryItem(actor, object);
            const int trackedStackID = FindStackID(inventoryItem, tracked.extra, false);
            if (trackedStackID < 0) continue;
            actor->inventoryList->RemoveItem1(object, static_cast<std::uint32_t>(trackedStackID), 1, false);
            removedTrackedInstances = true;
            LogLine("2.0 removed all tracked managed item=" + FormIDHex(id) +
                " stack=" + std::to_string(trackedStackID));
        }
        if (removedTrackedInstances) continue;
        const auto managedRef = std::find_if(managedInstances.begin(), managedInstances.end(), [id](const ManagedInstanceRef& entry) {
            return entry.fid == id;
        });
        const auto* preferredExtra = managedRef != managedInstances.end() ? managedRef->extra : nullptr;
        const int exactStackID = preferredExtra ? FindStackID(inventoryItem, preferredExtra, false) : -1;
        int signatureStackID = -1;
        if (exactStackID < 0 && managedRef != managedInstances.end() && !managedRef->name.empty()) {
            if (!managedRef->mods.empty()) {
                signatureStackID = FindUniqueStackIDByNameAndSavedMods(
                    inventoryItem, managedRef->name, managedRef->mods);
            } else {
                const char* baseName = nullptr;
                if (auto* armor = form->As<RE::TESObjectARMO>()) baseName = armor->GetFullName();
                else if (auto* weapon = form->As<RE::TESObjectWEAP>()) baseName = weapon->GetFullName();
                const bool customName = !baseName || managedRef->name != baseName;
                if (customName) {
                    signatureStackID = FindUniqueStackIDByName(inventoryItem, managedRef->name);
                }
            }
        }
        const int removalStackID = exactStackID >= 0 ? exactStackID : signatureStackID;
        if (removalStackID < 0) {
            LogLine("2.0 preserved ambiguous managed item target=" +
                FormIDHex(actor->GetFormID()) + " item=" + FormIDHex(id));
            continue;
        }
        actor->inventoryList->RemoveItem1(object, static_cast<std::uint32_t>(removalStackID), 1, false);
        LogLine("2.0 removed managed item=" + FormIDHex(id) +
            " stack=" + std::to_string(removalStackID) +
            " exact=" + (exactStackID >= 0 ? "1" : "0") +
            " signature=" + (signatureStackID >= 0 ? "1" : "0"));
    }
}

static std::string SavedModsDebug(const std::vector<SavedMod>& mods);

static void NotifyInventoryItemModified(RE::TESObjectREFR* owner, RE::TESBoundObject* object) {
    if (!owner || !object || !REX::FModule::IsRuntimeOG()) return;
    using func_t = void (*)(RE::TESObjectREFR*, RE::TESForm*, bool);
    static REL::Relocation<func_t> postModifyInventoryItem{ REL::ID(1153963) };
    postModifyInventoryItem(owner, object, false);
}

static void EnsureGeneratedWeaponAmmo(
    RE::Actor* actor,
    RE::BGSInventoryItem* inventoryItem,
    int stackID,
    std::uint32_t weaponFormID)
{
    if (!actor || !actor->inventoryList || !inventoryItem || stackID < 0) return;
    auto* instanceData = inventoryItem->GetInstanceData(static_cast<std::uint32_t>(stackID));
    auto* weaponData = instanceData ? static_cast<RE::TESObjectWEAP::InstanceData*>(instanceData) : nullptr;
    auto* ammo = weaponData ? weaponData->ammo : nullptr;
    if (!ammo) return;
    const auto ammoBefore = actor->inventoryList->GetItemCount(ammo);
    if (ammoBefore > 0) return;
    const auto ammoFormID = ammo->GetFormID();
    const auto npcGrantKey = (static_cast<std::uint64_t>(actor->GetFormID()) << 32) | ammoFormID;
    {
        std::lock_guard<std::mutex> lock(g_mx);
        if (actor == RE::PlayerCharacter::GetSingleton()) {
            if (g_playerGrantedAmmo.contains(ammoFormID)) return;
        } else {
            if (g_sessionNpcGrantedAmmo.contains(npcGrantKey)) return;
        }
    }

    const auto magazineCount = static_cast<std::uint32_t>(weaponData->ammoCapacity) * 3u;
    const auto ammoCount = (std::clamp)(magazineCount, 30u, 180u);
    actor->inventoryList->AddItem2(ammo, ammoCount);
    const auto ammoAfter = actor->inventoryList->GetItemCount(ammo);
    if (ammoAfter <= ammoBefore) {
        LogLine("2.0 failed to supply weapon ammo target=" + FormIDHex(actor->GetFormID()) +
            " weapon=" + FormIDHex(weaponFormID) +
            " ammo=" + FormIDHex(ammoFormID));
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mx);
        if (actor == RE::PlayerCharacter::GetSingleton()) {
            g_playerGrantedAmmo.insert(ammoFormID);
        } else {
            g_sessionNpcGrantedAmmo.insert(npcGrantKey);
        }
    }
    LogLine("2.0 supplied weapon ammo target=" + FormIDHex(actor->GetFormID()) +
        " weapon=" + FormIDHex(weaponFormID) +
        " ammo=" + FormIDHex(ammoFormID) +
        " count=" + std::to_string(ammoAfter - ammoBefore));
}

static int AddAndEquipSavedItem(
    RE::Actor* actor,
    const OE& savedItem,
    bool& managedOut,
    bool trackManaged = true,
    ManagedInstanceRef* createdRefOut = nullptr,
    bool equipNow = true,
    const std::string* instanceNameOverride = nullptr,
    bool silentAdd = false)
{
    managedOut = false;
    if (savedItem.weapon) {
        const auto filteredMods = FilterWeaponMods(savedItem.mods);
        if (filteredMods.size() != savedItem.mods.size()) {
            OE filteredItem = savedItem;
            filteredItem.mods = filteredMods;
            LogLine("2.0 weapon effects filtered item=" + FormIDHex(savedItem.fid) +
                " before=" + std::to_string(savedItem.mods.size()) +
                " after=" + std::to_string(filteredMods.size()));
            return AddAndEquipSavedItem(
                actor, filteredItem, managedOut, trackManaged, createdRefOut,
                equipNow, instanceNameOverride, silentAdd);
        }
    }
    auto* form = RE::TESForm::GetFormByID(savedItem.fid);
    auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
    auto* equipManager = RE::ActorEquipManager::GetSingleton();
    if (!actor || !object || !equipManager) return 0;
    if (!savedItem.weapon && ConflictsWithIgnoredEquippedArmor(actor, form)) {
        LogLine("2.0 skipped outfit item conflicting with ignored armor target=" +
            FormIDHex(actor->GetFormID()) + " item=" + FormIDHex(savedItem.fid));
        return 0;
    }

    RE::BSTSmartPointer<RE::ExtraDataList> createdExtra{ new RE::ExtraDataList() };
    RE::BGSMod::Template::Items::CreateInstanceDataForObjectAndExtra(*object, *createdExtra, nullptr, true);
    std::string instanceName = instanceNameOverride && !instanceNameOverride->empty() ?
        *instanceNameOverride : SavedItemDisplayName(savedItem);
    if (trackManaged && !instanceNameOverride) {
        // Keep a saved-outfit instance distinct from an untouched item with
        // the same base form. The visible suffix is sufficient for identity;
        // do not append an invisible Unicode marker that some fonts render as a box.
        instanceName = AozoraGeneratedItemName(std::move(instanceName));
    }
    createdExtra->SetOverrideName(instanceName.c_str());

    auto* objectExtra = createdExtra->GetByType<RE::BGSObjectInstanceExtra>();
    const bool explicitModSnapshot = savedItem.modSnapshot || !savedItem.mods.empty();
    if (explicitModSnapshot && objectExtra) {
        const auto templateMods = CollectInstanceMods(objectExtra);
        std::uint32_t removed = 0;
        for (const auto& templateMod : templateMods) {
            if (templateMod.disabled) continue;
            auto* modForm = RE::TESForm::GetFormByID(templateMod.fid);
            auto* objectMod = modForm ? modForm->As<RE::BGSMod::Attachment::Mod>() : nullptr;
            if (objectMod) removed += objectExtra->RemoveMod(objectMod, templateMod.index);
        }
        LogLine("2.0 instance template cleared item=" + FormIDHex(savedItem.fid) +
            " before=" + std::to_string(templateMods.size()) +
            " removed=" + std::to_string(removed) +
            " remaining=" + SavedModsDebug(CollectInstanceMods(objectExtra)));
    }

    bool modsPrepared = !explicitModSnapshot;
    if (explicitModSnapshot) {
        if (objectExtra) {
            for (const auto& savedMod : savedItem.mods) {
                if (savedMod.disabled) continue;
                auto* modForm = RE::TESForm::GetFormByID(savedMod.fid);
                auto* objectMod = modForm ? modForm->As<RE::BGSMod::Attachment::Mod>() : nullptr;
                if (!objectMod) {
                    LogLine("2.0 missing OMOD " + FormIDHex(savedMod.fid) + " for item " + FormIDHex(savedItem.fid));
                    continue;
                }
                objectExtra->AddMod(*objectMod, savedMod.index, savedMod.rank, false);
            }
            for (const auto& savedMod : savedItem.mods) {
                if (!savedMod.disabled) continue;
                auto* modForm = RE::TESForm::GetFormByID(savedMod.fid);
                auto* objectMod = modForm ? modForm->As<RE::BGSMod::Attachment::Mod>() : nullptr;
                if (objectMod) objectExtra->RemoveMod(objectMod, savedMod.index);
            }
            const auto preparedMods = CollectInstanceMods(objectExtra);
            modsPrepared = SameSavedMods(preparedMods, savedItem.mods);
            LogLine("2.0 AddMod inventory data item=" + FormIDHex(savedItem.fid) +
                " actual=" + SavedModsDebug(preparedMods));
        }
        // AddMod can rebuild instance data, so apply the unique name again.
        createdExtra->SetOverrideName(instanceName.c_str());
        LogLine("2.0 prepared material instance item=" + FormIDHex(savedItem.fid) +
            " mods=" + std::to_string(savedItem.mods.size()) +
            " result=" + (modsPrepared ? "1" : "0"));
    }

    const bool inventoryOnlyTeammate = !equipNow && IsPlayerTeammateNative(actor);
    const bool lockNpcEquipState = actor != RE::PlayerCharacter::GetSingleton() && trackManaged && equipNow;
    if (!inventoryOnlyTeammate) {
        PrepareNpcInventoryTransfer(actor, object, equipManager);
    }
    AddOutfitItemToInventory(actor, object, createdExtra, silentAdd);

    auto* inventoryItem = FindInventoryItem(actor, object);
    int stackID = FindStackID(inventoryItem, createdExtra.get(), false);
    if (stackID < 0) {
        stackID = FindStackIDByNameAndSavedMods(inventoryItem, instanceName, savedItem.mods);
    }
    if (stackID < 0) {
        stackID = FindStackIDByName(inventoryItem, instanceName);
    }
    const bool customInstance =
        trackManaged || savedItem.weapon || instanceNameOverride || explicitModSnapshot;
    if (stackID < 0 && !customInstance) {
        stackID = FindStackID(inventoryItem, nullptr, false);
    }
    const auto discardCreatedStack = [&]() {
        inventoryItem = FindInventoryItem(actor, object);
        int discardStackID = FindStackID(inventoryItem, createdExtra.get(), false);
        if (discardStackID < 0 && (trackManaged || instanceNameOverride)) {
            discardStackID = savedItem.mods.empty() ?
                FindUniqueStackIDByName(inventoryItem, instanceName) :
                FindUniqueStackIDByNameAndSavedMods(inventoryItem, instanceName, savedItem.mods);
        }
        if (actor->inventoryList && inventoryItem && discardStackID >= 0) {
            actor->inventoryList->RemoveItem1(object, static_cast<std::uint32_t>(discardStackID), 1, false);
            LogLine("2.0 discarded incomplete instance item=" + FormIDHex(savedItem.fid) +
                " name=" + instanceName + " stack=" + std::to_string(discardStackID));
            return true;
        }
        LogLine("2.0 preserved ambiguous incomplete instance item=" + FormIDHex(savedItem.fid) +
            " name=" + instanceName + " reason=no exact identity");
        return false;
    };
    if (stackID < 0) {
        LogLine("2.0 refused unsafe stack fallback item=" + FormIDHex(savedItem.fid) +
            " name=" + instanceName + " mods=" + std::to_string(savedItem.mods.size()));
        discardCreatedStack();
        return 0;
    }
    bool wroteModData = false;
    if (!modsPrepared && explicitModSnapshot && inventoryItem && stackID >= 0) {
        std::vector<SavedMod> appliedMods;
        for (const auto& savedMod : savedItem.mods) {
            if (savedMod.disabled) continue;
            auto* modForm = RE::TESForm::GetFormByID(savedMod.fid);
            auto* objectMod = modForm ? modForm->As<RE::BGSMod::Attachment::Mod>() : nullptr;
            if (!objectMod) {
                LogLine("2.0 missing OMOD " + FormIDHex(savedMod.fid) + " for item " + FormIDHex(savedItem.fid));
                continue;
            }
            bool writerResult = false;
            RE::BGSInventoryItem::CheckStackIDFunctor comparer(static_cast<std::uint32_t>(stackID));
            const auto writerIndex = static_cast<std::int8_t>(savedMod.index);
            RE::BGSInventoryItem::ModifyModDataFunctor writer(objectMod, writerIndex, true, &writerResult);
            actor->FindAndWriteStackDataForInventoryItem(object, comparer, writer);
            wroteModData = true;
            appliedMods.push_back(savedMod);
            inventoryItem = FindInventoryItem(actor, object);
            const int modifiedStackID = FindStackIDContainingSavedMods(inventoryItem, appliedMods);
            const bool applied = modifiedStackID >= 0;
            if (applied) stackID = modifiedStackID;
            LogLine("2.0 fallback apply OMOD=" + FormIDHex(savedMod.fid) + " item=" + FormIDHex(savedItem.fid) +
                " stack=" + std::to_string(stackID) + " writerIndex=" + std::to_string(writerIndex) +
                " writerResult=" + (writerResult ? "1" : "0") +
                " result=" + (applied ? "1" : "0"));
        }
        if (!savedItem.mods.empty()) {
            const int exactStackID = FindStackIDBySavedMods(inventoryItem, savedItem.mods);
            const int containingStackID = FindStackIDContainingSavedMods(inventoryItem, savedItem.mods);
            if (exactStackID >= 0) stackID = exactStackID;
            else if (containingStackID >= 0) stackID = containingStackID;
        }
    }
    if (wroteModData) {
        // The writer may split/rebuild the selected stack. Notify the game as
        // the workbench path does, then put the custom name on the actual OMOD
        // stack rather than on the pre-split ExtraDataList.
        NotifyInventoryItemModified(actor, object);
        inventoryItem = FindInventoryItem(actor, object);
        const int modifiedStackID = FindStackIDContainingSavedMods(inventoryItem, savedItem.mods);
        if (modifiedStackID >= 0) stackID = modifiedStackID;
        auto* modifiedStack = inventoryItem && stackID >= 0 ?
            inventoryItem->GetStackByID(static_cast<std::uint32_t>(stackID)) : nullptr;
        if (modifiedStack && modifiedStack->extra) {
            modifiedStack->extra->SetOverrideName(instanceName.c_str());
            NotifyInventoryItemModified(actor, object);
        }
    }
    if (explicitModSnapshot) {
        inventoryItem = FindInventoryItem(actor, object);
        const bool strictMaterialInstance = instanceNameOverride != nullptr || savedItem.weapon;
        const int verifiedStackID = savedItem.mods.empty() ?
            FindStackIDByName(inventoryItem, instanceName) :
            (strictMaterialInstance ?
                FindStackIDByNameAndSavedMods(inventoryItem, instanceName, savedItem.mods) :
                FindStackIDByNameContainingSavedMods(inventoryItem, instanceName, savedItem.mods));
        if (verifiedStackID < 0) {
            LogLine("2.0 material instance name/mod pairing failed item=" + FormIDHex(savedItem.fid) +
                " name=" + instanceName + " wanted=" + SavedModsDebug(savedItem.mods));
            discardCreatedStack();
            return 0;
        }
        stackID = verifiedStackID;
        auto* verifiedStack = inventoryItem && stackID >= 0 ?
            inventoryItem->GetStackByID(static_cast<std::uint32_t>(stackID)) : nullptr;
        auto* verifiedExtra = verifiedStack && verifiedStack->extra ?
            verifiedStack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
        const auto verifiedMods = CollectInstanceMods(verifiedExtra);
        const bool verified = SameSavedMods(verifiedMods, savedItem.mods);
        if (!verified) {
            LogLine("2.0 material instance verification failed item=" + FormIDHex(savedItem.fid) +
                " wanted=" + SavedModsDebug(savedItem.mods) +
                " actual=" + SavedModsDebug(verifiedMods));
            discardCreatedStack();
            return 0;
        }
        LogLine("2.0 material instance verified item=" + FormIDHex(savedItem.fid) +
            " name=" + instanceName + " stack=" + std::to_string(stackID));
    }
    if (inventoryItem && stackID >= 0) {
        if (auto* instanceData = inventoryItem->GetInstanceData(static_cast<std::uint32_t>(stackID)); instanceData && savedItem.color && form->Is(RE::ENUM_FORM_ID::kARMO)) {
            static_cast<RE::TESObjectARMO::InstanceData*>(instanceData)->colorRemappingIndex = *savedItem.color;
        }
    }
    RE::TBO_InstanceData* instanceData = stackID >= 0 && inventoryItem ? inventoryItem->GetInstanceData(static_cast<std::uint32_t>(stackID)) : nullptr;
    ClearManagedEquipFlags(actor, inventoryItem, stackID);
    RE::BGSObjectInstance instance(object, instanceData);
    bool equipped = true;
    if (equipNow) {
        equipped = equipManager->EquipObject(
            actor,
            instance,
            stackID >= 0 ? static_cast<std::uint32_t>(stackID) : 0,
            1,
            nullptr,
            false,
            ShouldForceEquip(actor),
            false,
            true,
            lockNpcEquipState);
    }
    if (!equipped) {
        LogLine("2.0 instance equip failed item=" + FormIDHex(savedItem.fid) +
            " name=" + instanceName);
        discardCreatedStack();
        return 0;
    }

    inventoryItem = FindInventoryItem(actor, object);
    const bool strictMaterialInstance = instanceNameOverride != nullptr || savedItem.weapon;
    int managedStackID = explicitModSnapshot ?
        (savedItem.mods.empty() ?
            FindStackIDByName(inventoryItem, instanceName) :
            (strictMaterialInstance ?
                FindStackIDByNameAndSavedMods(inventoryItem, instanceName, savedItem.mods) :
                FindStackIDByNameContainingSavedMods(inventoryItem, instanceName, savedItem.mods))) :
        FindStackID(inventoryItem, createdExtra.get(), false);
    if (managedStackID < 0 && explicitModSnapshot && !savedItem.mods.empty()) {
        const int modStackID = FindStackIDContainingSavedMods(inventoryItem, savedItem.mods);
        auto* modStack = inventoryItem && modStackID >= 0 ?
            inventoryItem->GetStackByID(static_cast<std::uint32_t>(modStackID)) : nullptr;
        if (modStack && modStack->extra) {
            modStack->extra->SetOverrideName(instanceName.c_str());
            NotifyInventoryItemModified(actor, object);
            inventoryItem = FindInventoryItem(actor, object);
            managedStackID = strictMaterialInstance ?
                FindStackIDByNameAndSavedMods(inventoryItem, instanceName, savedItem.mods) :
                FindStackIDByNameContainingSavedMods(inventoryItem, instanceName, savedItem.mods);
        }
    }
    if (managedStackID < 0 && !explicitModSnapshot) managedStackID = stackID;
    if (managedStackID < 0) {
        LogLine("2.0 generated instance lost after equip item=" + FormIDHex(savedItem.fid) +
            " name=" + instanceName + " wanted=" + SavedModsDebug(savedItem.mods));
        discardCreatedStack();
        return 0;
    }
    if (savedItem.weapon && trackManaged) {
        EnsureGeneratedWeaponAmmo(actor, inventoryItem, managedStackID, savedItem.fid);
    }
    if (!lockNpcEquipState) {
        ClearManagedEquipFlags(actor, inventoryItem, managedStackID);
    } else {
        // ContainerMenu passes abLockEquipState=true for NPCs.  Keep that
        // marker on the real managed stack so a cell reload cannot replace it
        // with the actor's default outfit.
        actor->SetEquipStateLocked(object, true);
    }
    const RE::ExtraDataList* managedExtra = nullptr;
    if (inventoryItem && managedStackID >= 0) {
        if (auto* stack = inventoryItem->GetStackByID(static_cast<std::uint32_t>(managedStackID)); stack) {
            managedExtra = stack->extra.get();
        }
    }
    if (createdRefOut) *createdRefOut = { savedItem.fid, managedExtra, instanceName, savedItem.mods };
    managedOut = true;
    if (trackManaged) {
        std::lock_guard<std::mutex> lk(g_mx);
        g_managedInstances[actor->GetFormID()].push_back(
            { savedItem.fid, managedExtra, instanceName, savedItem.mods });
    }
    LogLine("2.0 create item=" + FormIDHex(savedItem.fid) + " mods=" + std::to_string(savedItem.mods.size()) +
        " stack=" + std::to_string(managedStackID) + " removable=" +
        (actor == RE::PlayerCharacter::GetSingleton() ? "1" : "n/a") +
        " equip=" + (equipNow ? "1" : "0") + " result=" + (equipped ? "1" : "0"));
    return equipped && managedStackID >= 0 ? 1 : 0;
}

static std::uint32_t GetArmorSlots(std::uint32_t formID) {
    auto* form = RE::TESForm::GetFormByID(formID);
    auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr;
    return armor ? armor->bipedModelData.bipedObjectSlots : 0;
}

static std::string SlotsHex(std::uint32_t slots) {
    std::ostringstream ss;
    ss << "0x" << std::uppercase << std::hex << slots;
    return ss.str();
}

static std::string SavedModsDebug(const std::vector<SavedMod>& mods) {
    if (mods.empty()) return "mods=0";
    std::string result = "mods=" + std::to_string(mods.size()) + "[";
    for (std::size_t i = 0; i < mods.size() && i < 4; ++i) {
        if (i) result += ",";
        result += FormIDHex(mods[i].fid) + ":" + std::to_string(mods[i].index) +
            ":" + std::to_string(mods[i].rank);
    }
    if (mods.size() > 4) result += ",...";
    result += "]";
    return result;
}

static std::string OEDebug(const OE& item) {
    return FormIDHex(item.fid) +
        " slots=" + SlotsHex(GetArmorSlots(item.fid)) +
        " weapon=" + (item.weapon ? "1" : "0") +
        " " + SavedModsDebug(item.mods) +
        " name=" + item.name;
}

static std::string DraftDebug(const std::vector<OE>& draft) {
    std::string result = "draft=" + std::to_string(draft.size()) + "[";
    for (std::size_t i = 0; i < draft.size() && i < 8; ++i) {
        if (i) result += " | ";
        result += OEDebug(draft[i]);
    }
    if (draft.size() > 8) result += " | ...";
    result += "]";
    return result;
}

static std::string EquippedDebug(const std::vector<EquippedStackSnapshot>& equipped) {
    std::string result = "equippedStacks=" + std::to_string(equipped.size()) + "[";
    for (std::size_t i = 0; i < equipped.size() && i < 8; ++i) {
        if (i) result += " | ";
        result += FormIDHex(equipped[i].fid) + " slots=" + SlotsHex(GetArmorSlots(equipped[i].fid)) +
            " extra=" + (equipped[i].extra ? "1" : "0");
    }
    if (equipped.size() > 8) result += " | ...";
    result += "]";
    return result;
}

static bool SameDraftItem(const OE& lhs, const OE& rhs) {
    if (lhs.fid != rhs.fid ||
        lhs.weapon != rhs.weapon ||
        lhs.modSnapshot != rhs.modSnapshot ||
        lhs.color.has_value() != rhs.color.has_value()) {
        return false;
    }
    if (lhs.color && std::abs(*lhs.color - *rhs.color) > 0.0001f) return false;
    return SameSavedMods(lhs.mods, rhs.mods);
}

static bool DraftContainsItem(const OE& item) {
    if (!g_preview.active) return false;
    return std::any_of(g_preview.draftOutfit.begin(), g_preview.draftOutfit.end(), [&](const OE& current) {
        return SameDraftItem(current, item);
    });
}

static void RemoveDraftItem(const OE& item) {
    if (!g_preview.active) return;
    const auto before = g_preview.draftOutfit.size();
    g_preview.draftOutfit.erase(std::remove_if(g_preview.draftOutfit.begin(), g_preview.draftOutfit.end(), [&](const OE& current) {
        return SameDraftItem(current, item);
    }), g_preview.draftOutfit.end());
    if (before != g_preview.draftOutfit.size()) {
        LogLine("2.0 studio draft remove exact item=" + OEDebug(item) +
            " before=" + std::to_string(before) + " after=" + std::to_string(g_preview.draftOutfit.size()));
        return;
    }
    g_preview.draftOutfit.erase(std::remove_if(g_preview.draftOutfit.begin(), g_preview.draftOutfit.end(), [&](const OE& current) {
        return current.fid == item.fid && current.weapon == item.weapon;
    }), g_preview.draftOutfit.end());
    LogLine("2.0 studio draft remove by form item=" + OEDebug(item) +
        " before=" + std::to_string(before) + " after=" + std::to_string(g_preview.draftOutfit.size()));
}

static void AddDraftItem(const OE& item, std::uint32_t slots) {
    if (!g_preview.active) return;
    RemoveDraftItem(item);
    if (!item.weapon && slots != 0) {
        g_preview.draftOutfit.erase(std::remove_if(g_preview.draftOutfit.begin(), g_preview.draftOutfit.end(), [&](const OE& current) {
            if (current.weapon) return false;
            const auto currentSlots = GetArmorSlots(current.fid);
            return currentSlots != 0 && (currentSlots & slots) != 0;
        }), g_preview.draftOutfit.end());
    }
    g_preview.draftOutfit.push_back(item);
}

static std::vector<EquippedStackSnapshot> CaptureEquippedStacks(RE::Actor* actor, bool includeWeapons) {
    std::vector<EquippedStackSnapshot> result;
    if (!actor || !actor->inventoryList) return result;
    for (auto& item : actor->inventoryList->data) {
        if (!IsManagedOutfitArmor(item.object) &&
            !(includeWeapons && IsSupportedSavedWeapon(item.object))) continue;
        for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
            if (stack->IsEquipped()) result.push_back({ item.object->GetFormID(), stack->extra.get() });
        }
    }
    return result;
}

static bool EquipExistingStack(RE::Actor* actor, const EquippedStackSnapshot& snapshot) {
    auto* form = RE::TESForm::GetFormByID(snapshot.fid);
    auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
    auto* inventoryItem = FindInventoryItem(actor, object);
    auto* equipManager = RE::ActorEquipManager::GetSingleton();
    if (!actor || !object || !inventoryItem || !equipManager) return false;
    const int stackID = FindStackID(inventoryItem, snapshot.extra, false);
    if (stackID < 0) return false;
    ClearManagedEquipFlags(actor, inventoryItem, stackID);
    RE::BGSObjectInstance instance(object, inventoryItem->GetInstanceData(static_cast<std::uint32_t>(stackID)));
    const bool equipped = equipManager->EquipObject(
        actor, instance, static_cast<std::uint32_t>(stackID), 1, nullptr, false, ShouldForceEquip(actor), false, true, false);
    inventoryItem = FindInventoryItem(actor, object);
    ClearManagedEquipFlags(actor, inventoryItem, FindStackID(inventoryItem, snapshot.extra, false));
    return equipped;
}

static bool EquipExistingSavedItem(RE::Actor* actor, const OE& savedItem, ManagedInstanceRef* equippedRefOut = nullptr) {
    auto* form = RE::TESForm::GetFormByID(savedItem.fid);
    auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
    auto* inventoryItem = FindInventoryItem(actor, object);
    auto* equipManager = RE::ActorEquipManager::GetSingleton();
    if (!actor || !object || !inventoryItem || !equipManager) return false;
    if (!savedItem.weapon && ConflictsWithIgnoredEquippedArmor(actor, form)) return false;

    int stackID = FindStackIDBySavedAppearance(inventoryItem, savedItem, true);
    if (stackID < 0) stackID = FindStackIDBySavedAppearance(inventoryItem, savedItem, false);
    if (stackID < 0) return false;

    auto* stack = inventoryItem->GetStackByID(static_cast<std::uint32_t>(stackID));
    const auto* stackExtra = stack ? stack->extra.get() : nullptr;
    ClearManagedEquipFlags(actor, inventoryItem, stackID);
    RE::BGSObjectInstance instance(object, inventoryItem->GetInstanceData(static_cast<std::uint32_t>(stackID)));
    const bool equipped = equipManager->EquipObject(
        actor, instance, static_cast<std::uint32_t>(stackID), 1, nullptr, false, ShouldForceEquip(actor), false, true, false);
    if (!equipped) return false;

    inventoryItem = FindInventoryItem(actor, object);
    ClearManagedEquipFlags(actor, inventoryItem, FindStackID(inventoryItem, stackExtra, false));

    if (equippedRefOut) *equippedRefOut = {
        savedItem.fid, stackExtra, savedItem.name, savedItem.mods
    };
    LogLine("2.0 studio reused inventory item=" + FormIDHex(savedItem.fid) +
        " stack=" + std::to_string(stackID) + " " + SavedModsDebug(savedItem.mods));
    return true;
}

static bool UnequipExistingStack(RE::Actor* actor, const ManagedInstanceRef& ref) {
    auto* form = RE::TESForm::GetFormByID(ref.fid);
    auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
    auto* inventoryItem = FindInventoryItem(actor, object);
    auto* equipManager = RE::ActorEquipManager::GetSingleton();
    if (!actor || !object || !inventoryItem || !equipManager) return false;
    const int stackID = FindStackID(inventoryItem, ref.extra, true);
    if (stackID < 0) return false;
    RE::BGSObjectInstance instance(object, inventoryItem->GetInstanceData(static_cast<std::uint32_t>(stackID)));
    return equipManager->UnequipObject(actor, &instance, 1, nullptr, static_cast<std::uint32_t>(stackID), false, true, false, true, nullptr);
}

static bool RemoveExactInstance(RE::Actor* actor, const ManagedInstanceRef& ref) {
    auto* form = RE::TESForm::GetFormByID(ref.fid);
    auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
    auto* inventoryItem = FindInventoryItem(actor, object);
    if (!actor || !object || !inventoryItem) return false;
    const int stackID = FindStackID(inventoryItem, ref.extra, false);
    if (stackID < 0) return false;
    actor->inventoryList->RemoveItem1(object, static_cast<std::uint32_t>(stackID), 1, false);
    return true;
}

static void RemovePreviewTemps(RE::Actor* actor, std::uint32_t conflictSlots = 0, std::uint32_t sameForm = 0) {
    if (!actor || !g_preview.active) return;
    auto it = g_preview.temporary.begin();
    while (it != g_preview.temporary.end()) {
        const bool removeAll = conflictSlots == 0 && sameForm == 0;
        const bool overlaps = conflictSlots != 0 && (it->slots & conflictSlots) != 0;
        const bool same = sameForm != 0 && it->ref.fid == sameForm;
        if (removeAll || overlaps || same) {
            // Remove the equipped stack through ActorEquipManager first. A
            // direct inventory removal can leave the biped holding a stale
            // geometry reference until the next Pip-Boy/camera transition.
            UnequipExistingStack(actor, it->ref);
            RemoveExactInstance(actor, it->ref);
            it = g_preview.temporary.erase(it);
        } else {
            ++it;
        }
    }
}

static bool BeginPreview(RE::Actor* actor) {
    if (!g_menuOpen || !actor || IsActorInPowerArmor(actor)) return false;
    if (g_preview.active) {
        auto current = g_preview.actor.get();
        if (current && current.get() == actor) return true;
        RollbackPreview();
    }
    ++g_restoreSerial;
    g_preview = {};
    g_preview.active = true;
    g_preview.includesWeapons = SaveWeaponsEnabled();
    g_preview.actor = actor->GetHandle();
    g_preview.original = CaptureEquippedStacks(actor, g_preview.includesWeapons);
    g_preview.originalOutfit = CollectEquippedArmor(actor);
    if (g_preview.includesWeapons) AppendEquippedWeapons(actor, g_preview.originalOutfit);
    g_preview.draftOutfit = g_preview.originalOutfit;
    return true;
}

static void RefreshActorAppearance(RE::Actor* actor, bool settled) {
    if (!actor) return;
    constexpr std::uint32_t kAppearanceRefresh =
        static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kModel) |
        static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kSkin) |
        static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kScale);
    const bool previewActor = g_preview.active && [&] {
        const auto previewRef = g_preview.actor.get();
        return previewRef && previewRef.get() == actor;
    }();
    // EquipManager already rebuilds the player's biped and emits the equip
    // events used by high-heel systems. NG and AE need the same body-only
    // Reset3D path used by the workbench so a settled outfit becomes visible
    // while the menu is open. Keep OG on its original path.
    if (actor == RE::PlayerCharacter::GetSingleton() && !previewActor) {
        if (settled && (REX::FModule::IsRuntimeNG() || REX::FModule::IsRuntimeAE())) {
            constexpr std::uint32_t kBodyOnlyExcludeFlags =
                static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kHead) |
                static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kFace);
            actor->Reset3D(true, 0, true, kBodyOnlyExcludeFlags);
        }
        return;
    }
    // Match F4SE Actor.QueueUpdate(true, 0xC). F4SE documents 0xC as the
    // body-only update mask: equipment/BipedAnim is rebuilt while head and face
    // are excluded. This is the narrowest reliable rebuild for outfit changes
    // and avoids the full flags=0 teardown that caused black-face flashes.
    if (previewActor) {
        constexpr std::uint32_t kBodyOnlyExcludeFlags =
            static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kHead) |
            static_cast<std::uint32_t>(RE::RESET_3D_FLAGS::kFace);
        actor->Reset3D(true, 0, true, kBodyOnlyExcludeFlags);
        return;
    } else {
        if (settled) actor->UpdateReference3D();
        if (auto* taskQueue = RE::TaskQueueInterface::GetSingleton()) taskQueue->QueueUpdate3D(actor, kAppearanceRefresh);
    }
    if (!settled) return;
    const auto actorHandle = actor->GetHandle();
    std::thread([actorHandle, previewActor, kAppearanceRefresh] {
        // Equip/unequip requests and biped rebuilds can settle on different
        // frames. Refresh several times so closing the UI cannot expose an
        // underwear-only or half-deformed frame.
        for (const auto delay : { 0u, 40u, 160u, 520u, 900u }) {
            Sleep(delay);
            if (auto* tasks = F4SE::GetTaskInterface()) {
                tasks->AddTask([actorHandle, previewActor, kAppearanceRefresh] {
                    auto ref = actorHandle.get();
                    auto* refreshedActor = ref ? ref->As<RE::Actor>() : nullptr;
                    if (!refreshedActor) return;
                    if (!previewActor) {
                        refreshedActor->UpdateReference3D();
                        if (auto* taskQueue = RE::TaskQueueInterface::GetSingleton()) {
                            taskQueue->QueueUpdate3D(refreshedActor, kAppearanceRefresh);
                        }
                    }
                });
            }
        }
    }).detach();
}

static void SchedulePreviewRestoreRetry(
    const RE::ObjectRefHandle& actorHandle,
    const std::vector<EquippedStackSnapshot>& originals)
{
    if (originals.empty()) return;
    const auto serial = ++g_restoreSerial;
    std::thread([actorHandle, originals, serial] {
        for (const auto delay : { 240u, 680u, 1080u }) {
            Sleep(delay);
            if (serial != g_restoreSerial.load()) return;
            if (auto* tasks = F4SE::GetTaskInterface()) {
                tasks->AddTask([actorHandle, originals, serial] {
                    if (serial != g_restoreSerial.load()) return;
                    auto ref = actorHandle.get();
                    auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
                    if (!actor) return;
                    auto previewRef = g_preview.active ? g_preview.actor.get() : nullptr;
                    if (previewRef && previewRef.get() == actor) return;
                    int restored = 0;
                    for (const auto& original : originals) {
                        if (EquipExistingStack(actor, original)) ++restored;
                    }
                    if (restored > 0) {
                        constexpr std::uint32_t kAppearanceRefresh = 0x13;
                        if (auto* taskQueue = RE::TaskQueueInterface::GetSingleton()) {
                            taskQueue->QueueUpdate3D(actor, kAppearanceRefresh);
                        }
                    }
                });
            }
        }
    }).detach();
}

static void UpdatePreviewActor(RE::Actor* actor) {
    if (!actor) return;
    RefreshActorAppearance(actor);
}

static void RollbackPreview(bool scheduleRetry) {
    if (!g_preview.active) return;
    auto actorRef = g_preview.actor.get();
    auto* actor = actorRef ? actorRef->As<RE::Actor>() : nullptr;
    if (actor) {
        RemovePreviewTemps(actor);
        for (const auto& persistent : g_preview.persistentPreview) UnequipExistingStack(actor, persistent);
        // Studio preview can equip an existing inventory stack. It is not a
        // temporary instance, so explicitly remove every non-original stack
        // before restoring the exact equipment snapshot from menu entry.
        const auto currentlyEquipped = CaptureEquippedStacks(actor, g_preview.includesWeapons);
        for (const auto& current : currentlyEquipped) {
            const bool wasOriginal = std::any_of(g_preview.original.begin(), g_preview.original.end(), [&](const EquippedStackSnapshot& original) {
                return original.fid == current.fid && original.extra == current.extra;
            });
            if (!wasOriginal) UnequipExistingStack(actor, { current.fid, current.extra });
        }
        int exactRestored = 0;
        for (const auto& original : g_preview.original) {
            if (EquipExistingStack(actor, original)) ++exactRestored;
        }
        // Some outfits are represented by the live biped but do not expose a
        // stable inventory-stack pointer.  Recreate only missing original forms
        // as a recovery path so repeated previews cannot leave the actor naked.
        int recovered = 0;
        auto worn = CollectEquippedArmor(actor);
        if (g_preview.includesWeapons) AppendEquippedWeapons(actor, worn);
        for (const auto& original : g_preview.originalOutfit) {
            const bool alreadyWorn = std::any_of(worn.begin(), worn.end(), [&](const OE& current) {
                return SameDraftItem(current, original);
            });
            if (alreadyWorn) continue;
            bool added = false;
            if (AddAndEquipSavedItem(actor, original, added, false, nullptr, true, nullptr, true) > 0) {
                ++recovered;
                worn.push_back(original);
            }
        }
        RefreshActorAppearance(actor, true);
        LogLine("2.0 preview rollback target=" + FormIDHex(actor->formID) +
            " originals=" + std::to_string(g_preview.original.size()) +
            " exact=" + std::to_string(exactRestored) +
            " recovered=" + std::to_string(recovered));
        if (scheduleRetry) SchedulePreviewRestoreRetry(g_preview.actor, g_preview.original);
    }
    g_preview = {};
}

static bool CommitSavedStudioOutfit() {
    if (!g_preview.active || !g_preview.hasSavedDraft || g_preview.lastSavedSlot <= 0) return false;

    const int slot = g_preview.lastSavedSlot;
    auto actorRef = g_preview.actor.get();
    auto* actor = actorRef ? actorRef->As<RE::Actor>() : nullptr;
    if (!actor) {
        LogLine("2.0 studio close saved outfit skipped: target unloaded");
        return false;
    }

    // Restore the entry snapshot first, without scheduling the delayed
    // original-outfit retry.  The normal equip path then becomes the only
    // owner of the final equipment state and can rebuild managed instances
    // exactly like an ordinary outfit application.
    RollbackPreview(false);
    const int result = EquipOutfitForActor(slot, actor, true, true);
    if (result > 0) {
        g_lastEquippedSlot = slot;
        LogLine("2.0 studio close kept last saved outfit target=" +
            FormIDHex(actor->GetFormID()) + " slot=" + std::to_string(slot) +
            " equipped=" + std::to_string(result));
        return true;
    }

    LogLine("2.0 studio close failed to equip last saved outfit target=" +
        FormIDHex(actor->GetFormID()) + " slot=" + std::to_string(slot) +
        " result=" + std::to_string(result));
    return false;
}

static void AcceptPreviewAsCurrent(RE::Actor* actor, int slot) {
    if (!actor || !g_preview.active) return;
    auto previewActor = g_preview.actor.get();
    if (!previewActor || previewActor.get() != actor) return;

    std::vector<ManagedInstanceRef> accepted;
    accepted.reserve(g_preview.temporary.size());
    for (const auto& temporary : g_preview.temporary) {
        auto* form = RE::TESForm::GetFormByID(temporary.ref.fid);
        if (temporary.ref.extra &&
            (IsManagedOutfitArmor(form) ||
             (g_preview.includesWeapons && IsSupportedSavedWeapon(form)))) {
            accepted.push_back(temporary.ref);
        }
    }
    // Material-workbench output is a permanent player-owned inventory item.
    // It may remain equipped after saving, but must never enter managed cleanup.

    {
        std::lock_guard<std::mutex> lk(g_mx);
        auto& tracked = g_managedInstances[actor->GetFormID()];
        auto& managed = g_mi[actor->GetFormID()].m;
        for (const auto& ref : accepted) {
            const bool alreadyTracked = std::any_of(tracked.begin(), tracked.end(), [&](const ManagedInstanceRef& current) {
                return current.fid == ref.fid && current.extra == ref.extra;
            });
            if (!alreadyTracked) tracked.push_back(ref);
            if (std::find(managed.begin(), managed.end(), ref.fid) == managed.end()) managed.push_back(ref.fid);
        }
    }
    if (slot > 0) {
        SetActiveSlotForActor(actor, slot);
    } else {
        std::lock_guard<std::mutex> lk(g_mx);
        g_lastAppliedSlots.erase(actor->GetFormID());
    }
    WriteActors();
    LogLine("2.0 actual outfit session committed target=" + FormIDHex(actor->GetFormID()) +
        " slot=" + std::to_string(slot) + " managed=" + std::to_string(accepted.size()));
    g_preview = {};
    RefreshActorAppearance(actor, true);
}

static int PreviewSavedItems(RE::Actor* actor, const std::vector<OE>& items, int slot) {
    if (g_preview.active) RollbackPreview(false);
    if (!BeginPreview(actor)) return -3;
    RemovePreviewTemps(actor);
    for (const auto& original : g_preview.original) {
        UnequipExistingStack(actor, { original.fid, original.extra });
    }

    int equippedCount = 0;
    for (const auto& item : items) {
        bool added = false;
        ManagedInstanceRef ref;
        const std::string generatedName = AozoraGeneratedItemName(SavedItemDisplayName(item));
        const int result = AddAndEquipSavedItem(actor, item, added, false, &ref, true, &generatedName, true);
        if (added && ref.extra) g_preview.temporary.push_back({ ref, GetArmorSlots(item.fid) });
        equippedCount += result;
    }
    g_preview.draftOutfit = items;
    g_preview.previewSlot = equippedCount > 0 ? slot : 0;
    if (equippedCount <= 0) {
        RollbackPreview();
        return 0;
    }
    UpdatePreviewActor(actor);
    LogLine("2.0 slot preview target=" + FormIDHex(actor->formID) + " slot=" +
        std::to_string(slot) + " equipped=" + std::to_string(equippedCount));
    return equippedCount;
}

static int CommitPreviewForActor(int slot, RE::Actor* actor) {
    if (!g_menuOpen || !actor) return -3;
    if (g_preview.active) {
        auto previewRef = g_preview.actor.get();
        if (previewRef && previewRef.get() != actor) RollbackPreview();
        else RollbackPreview();
    }
    const int result = EquipOutfitForActor(slot, actor, true);
    if (result > 0) {
        g_lastEquippedSlot = slot;
    }
    PositionPreviewCamera(actor);
    return result;
}

static std::string StudioCategory(std::uint32_t slots, std::string_view displayName) {
    const auto lowerName = Lower(std::string(displayName));
    // A shoe/boot name wins over a sock keyword when both appear in the
    // localized display name.  This keeps footwear out of the legwear tab.
    if (lowerName.find("shoe") != std::string::npos ||
        lowerName.find("heel") != std::string::npos ||
        lowerName.find("boot") != std::string::npos ||
        lowerName.find("sneaker") != std::string::npos ||
        lowerName.find("sandal") != std::string::npos ||
        lowerName.find("footwear") != std::string::npos ||
        lowerName.find("鞋") != std::string::npos ||
        lowerName.find("靴") != std::string::npos) {
        return "feet";
    }
    if (lowerName.find("tights") != std::string::npos ||
        lowerName.find("pantyhose") != std::string::npos ||
        lowerName.find("stocking") != std::string::npos ||
        lowerName.find("hosiery") != std::string::npos ||
        lowerName.find("legwear") != std::string::npos ||
        lowerName.find("sock") != std::string::npos ||
        lowerName.find("连裤袜") != std::string::npos ||
        lowerName.find("丝袜") != std::string::npos ||
        lowerName.find("长袜") != std::string::npos ||
        lowerName.find("短袜") != std::string::npos ||
        lowerName.find("袜子") != std::string::npos ||
        lowerName.find("袜") != std::string::npos) {
        return "legs";
    }
    if (lowerName.find("dress") != std::string::npos ||
        lowerName.find("gown") != std::string::npos ||
        lowerName.find("one-piece") != std::string::npos ||
        lowerName.find("连衣裙") != std::string::npos ||
        lowerName.find("长裙") != std::string::npos ||
        lowerName.find("裙装") != std::string::npos ||
        lowerName.find("旗袍") != std::string::npos ||
        lowerName.find("和服") != std::string::npos ||
        lowerName.find("礼服") != std::string::npos) {
        return "body";
    }
    if (lowerName.find("skirt") != std::string::npos ||
        lowerName.find("pants") != std::string::npos ||
        lowerName.find("panties") != std::string::npos ||
        lowerName.find("shorts") != std::string::npos ||
        lowerName.find("bottom") != std::string::npos ||
        lowerName.find("lower") != std::string::npos ||
        lowerName.find("裙") != std::string::npos ||
        lowerName.find("裤") != std::string::npos ||
        lowerName.find("Lower Body") != std::string::npos) {
        return "lower";
    }
    auto has = [slots](RE::BIPED_OBJECT slot) {
        return (slots & (1u << static_cast<std::uint32_t>(slot))) != 0;
    };
    if (has(RE::BIPED_OBJECT::kHairTop) || has(RE::BIPED_OBJECT::kHairLong) ||
        has(RE::BIPED_OBJECT::kHeadband) || has(RE::BIPED_OBJECT::kScalp)) return "head";
    if (has(RE::BIPED_OBJECT::kFaceGenHead) || has(RE::BIPED_OBJECT::kEyes) ||
        has(RE::BIPED_OBJECT::kBeard) || has(RE::BIPED_OBJECT::kMouth)) return "other";
    if (has(RE::BIPED_OBJECT::kBody) || has(RE::BIPED_OBJECT::kUnderTorso) ||
        has(RE::BIPED_OBJECT::kAboveTorso)) return "body";
    if (has(RE::BIPED_OBJECT::kLeftHand) || has(RE::BIPED_OBJECT::kRightHand)) return "other";
    if (has(RE::BIPED_OBJECT::kUnderLeftArm) || has(RE::BIPED_OBJECT::kUnderRightArm) ||
        has(RE::BIPED_OBJECT::kAboveLeftArm) || has(RE::BIPED_OBJECT::kAboveRightArm)) return "other";
    if (has(RE::BIPED_OBJECT::kUnderLeftLeg) || has(RE::BIPED_OBJECT::kUnderRightLeg) ||
        has(RE::BIPED_OBJECT::kAboveLeftLeg) || has(RE::BIPED_OBJECT::kAboveRightLeg)) return "legs";
    if (has(RE::BIPED_OBJECT::kNeck) || has(RE::BIPED_OBJECT::kRing) || has(RE::BIPED_OBJECT::kShield)) return "accessories";
    return "other";
}

static bool IsPreviewExtra(const RE::ExtraDataList* extra) {
    if (!extra || !g_preview.active) return false;
    return std::any_of(g_preview.temporary.begin(), g_preview.temporary.end(), [extra](const PreviewTempInstance& entry) {
        return entry.ref.extra == extra;
    });
}

static std::string StudioItemGroupKey(const OE& item, [[maybe_unused]] std::string_view displayName) {
    std::vector<std::tuple<std::uint32_t, std::uint8_t, std::uint8_t, bool>> mods;
    for (const auto& mod : item.mods) {
        mods.emplace_back(mod.fid, mod.index, mod.rank, mod.disabled);
    }
    std::sort(mods.begin(), mods.end());
    std::ostringstream key;
    key << (item.weapon ? "W:" : "A:") << item.fid << "|S:" << (item.modSnapshot ? 1 : 0);
    for (const auto& [fid, index, rank, disabled] : mods) {
        key << '|' << fid << ':' << static_cast<int>(index) << ':' <<
            static_cast<int>(rank) << ':' << (disabled ? 1 : 0);
    }
    if (item.color) key << "|C:" << std::to_string(*item.color);
    return key.str();
}

static int StableStudioToken(std::string_view key) {
    // Keep the UI token stable when the inventory is rebuilt after a
    // preview/equip.  Sequential tokens changed whenever sort order changed,
    // which made the second click on some items (notably pantyhose) target a
    // different or missing native entry.
    std::uint32_t hash = 2166136261u;
    for (const auto ch : key) {
        hash ^= static_cast<std::uint8_t>(ch);
        hash *= 16777619u;
    }
    const auto token = static_cast<int>(hash & 0x7FFFFFFFu);
    return token > 0 ? token : 1;
}

static std::string BuildStudioInventoryJson() {
    g_studioItems.clear();
    g_materialCache.clear();
    auto* player = RE::PlayerCharacter::GetSingleton();
    const bool allowWeapons = SaveWeaponsEnabled();
    std::unordered_map<std::string, std::size_t> groupedItems;
    g_nextStudioToken = 1;
    if (player && player->inventoryList) {
        for (auto& inventoryItem : player->inventoryList->data) {
            auto* form = inventoryItem.object;
            const bool isWeapon = allowWeapons && IsSupportedSavedWeapon(form);
            if (!IsStudioOutfitArmor(form) && !isWeapon) continue;
            auto* armor = isWeapon ? nullptr : form->As<RE::TESObjectARMO>();
            auto* weapon = isWeapon ? form->As<RE::TESObjectWEAP>() : nullptr;
            if (!armor && !weapon) continue;
            std::uint32_t stackID = 0;
            for (auto* stack = inventoryItem.stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
                if (IsPreviewExtra(stack->extra.get())) continue;
                OE item;
                item.fid = form->GetFormID();
                item.weapon = isWeapon;
                const char* baseName = isWeapon ? weapon->GetFullName() : armor->GetFullName();
                item.name = baseName && baseName[0] ? baseName : "Unknown";
                auto* objectExtra = stack->extra ? stack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
                item.mods = isWeapon ?
                    FilterWeaponMods(CollectInstanceMods(objectExtra)) :
                    CollectInstanceMods(objectExtra);
                item.modSnapshot = objectExtra != nullptr;
                if (!isWeapon) item.color = GetInstanceColor(inventoryItem.GetInstanceData(stackID));
                const char* display = inventoryItem.GetDisplayFullName(stackID);
                const std::uint32_t slots = isWeapon ? 0 : armor->bipedModelData.bipedObjectSlots;
                const std::string displayName = display && display[0] ? display : (baseName ? baseName : "Unknown");
                const RE::ExtraDataList* stackExtra = stack->extra.get();
                const auto groupKey = StudioItemGroupKey(item, displayName);
                const int stackCount = static_cast<int>((std::max)(stack->GetCount(), 1u));
                const bool logicalEquipped = DraftContainsItem(item);
                const bool studioEquipped = g_preview.active ? logicalEquipped : stack->IsEquipped();
                if (const auto found = groupedItems.find(groupKey); found != groupedItems.end()) {
                    auto& grouped = g_studioItems[found->second];
                    grouped.count += stackCount;
                    if (studioEquipped && !grouped.equipped) {
                        grouped.ref = { form->GetFormID(), stackExtra };
                        grouped.stackID = static_cast<int>(stackID);
                        grouped.equipped = true;
                    }
                    continue;
                }
                groupedItems.emplace(groupKey, g_studioItems.size());
                g_studioItems.push_back({
                    StableStudioToken(groupKey),
                    std::move(item),
                    displayName,
                    isWeapon ? "weapons" : StudioCategory(slots, displayName),
                    slots,
                    { form->GetFormID(), stackExtra },
                    static_cast<int>(stackID),
                    stackCount,
                    studioEquipped
                });
            }
        }
    }
    if (g_preview.active) {
        for (const auto& draftItem : g_preview.draftOutfit) {
            auto* form = RE::TESForm::GetFormByID(draftItem.fid);
            if (!allowWeapons &&
                (draftItem.weapon || (form && form->Is(RE::ENUM_FORM_ID::kWEAP)))) {
                continue;
            }
            const bool isWeapon = draftItem.weapon || IsSupportedSavedWeapon(form);
            auto* armor = isWeapon ? nullptr : (form ? form->As<RE::TESObjectARMO>() : nullptr);
            auto* weapon = isWeapon ? (form ? form->As<RE::TESObjectWEAP>() : nullptr) : nullptr;
            if (!armor && !weapon) continue;
            const char* baseName = isWeapon ? weapon->GetFullName() : armor->GetFullName();
            const std::string displayName = !draftItem.name.empty() ? draftItem.name :
                (baseName && baseName[0] ? baseName : "Unknown");
            const auto groupKey = StudioItemGroupKey(draftItem, displayName);
            const std::uint32_t slots = isWeapon ? 0 : armor->bipedModelData.bipedObjectSlots;
            if (groupedItems.find(groupKey) != groupedItems.end()) continue;
            groupedItems.emplace(groupKey, g_studioItems.size());
            g_studioItems.push_back({
                StableStudioToken(groupKey),
                draftItem,
                displayName,
                isWeapon ? "weapons" : StudioCategory(slots, displayName),
                slots,
                { draftItem.fid, nullptr },
                -1,
                1,
                true
            });
        }
    }
    const auto categoryRank = [](std::string_view category) {
        constexpr std::array<std::string_view, 11> order{
            "body", "lower", "legs", "feet", "accessories", "head", "weapons", "face", "arms", "hands", "other"
        };
        const auto it = std::find(order.begin(), order.end(), category);
        return it == order.end() ? order.size() : static_cast<std::size_t>(std::distance(order.begin(), it));
    };
    std::stable_sort(g_studioItems.begin(), g_studioItems.end(), [&](const StudioItemRef& lhs, const StudioItemRef& rhs) {
        if (lhs.equipped != rhs.equipped) return lhs.equipped > rhs.equipped;
        if (lhs.category != rhs.category) return categoryRank(lhs.category) < categoryRank(rhs.category);
        return Lower(lhs.displayName) < Lower(rhs.displayName);
    });

    std::string result = "{\"items\":[";
    for (std::size_t i = 0; i < g_studioItems.size(); ++i) {
        const auto& entry = g_studioItems[i];
        if (i) result += ",";
        result += "{\"token\":" + std::to_string(entry.token) +
            ",\"formId\":\"" + FormIDHex(entry.item.fid) +
            "\",\"name\":\"" + json::esc(entry.displayName) +
            "\",\"baseName\":\"" + json::esc(entry.item.name) +
            "\",\"category\":\"" + entry.category +
            "\",\"weapon\":" + (entry.item.weapon ? "true" : "false") +
            ",\"equipped\":" + (entry.equipped ? "true" : "false") +
            ",\"modCount\":" + std::to_string(entry.item.mods.size()) +
            ",\"count\":" + std::to_string(entry.count) + "}";
    }
    result += "],\"total\":" + std::to_string(g_studioItems.size()) +
        ",\"weaponsEnabled\":" + (allowWeapons ? "true" : "false") + "}";
    LogLine("2.0 studio inventory groups=" + std::to_string(g_studioItems.size()));
    return result;
}

static void ScheduleStudioInventoryRefresh(std::uint32_t previewSerial) {
    std::thread([previewSerial] {
        // Inventory stack equipped flags may lag the visual biped rebuild. Two
        // focused refreshes at 220 ms and 620 ms preserve the stable behavior
        // without rebuilding and repainting the complete list four times.
        for (const auto delay : { 300u }) {
            Sleep(delay);
            if (previewSerial != g_previewRequestSerial.load()) return;
            if (auto* tasks = F4SE::GetTaskInterface()) {
                tasks->AddTask([previewSerial] {
                    if (!g_menuOpen || previewSerial != g_previewRequestSerial.load()) return;
                    const auto data = BuildStudioInventoryJson();
                    const std::string script = "if(window.omRefreshStudioInventory)window.omRefreshStudioInventory(" + data + ");";
                    InvokeMenuScript(script.c_str());
                });
            }
        }
    }).detach();
}

static void RefreshStudioInventoryNow() {
    const auto data = BuildStudioInventoryJson();
    const std::string script = "if(window.omRefreshStudioInventory)window.omRefreshStudioInventory(" + data + ");";
    InvokeMenuScript(script.c_str());
}

static StudioItemRef* FindStudioItem(int token) {
    auto it = std::find_if(g_studioItems.begin(), g_studioItems.end(), [token](const StudioItemRef& entry) {
        return entry.token == token;
    });
    return it == g_studioItems.end() ? nullptr : std::addressof(*it);
}

static int RebuildStudioDraft(RE::Actor* actor, const std::vector<OE>& draft) {
    if (!actor || !BeginPreview(actor)) return 0;

    RemovePreviewTemps(actor);
    const auto equippedBefore = CaptureEquippedStacks(actor, g_preview.includesWeapons);
    LogLine("2.0 studio rebuild begin actor=" + FormIDHex(actor->formID) + " " +
        DraftDebug(draft) + " " + EquippedDebug(equippedBefore));
    for (const auto& current : equippedBefore) {
        UnequipExistingStack(actor, { current.fid, current.extra });
    }
    const auto equippedAfterClear = CaptureEquippedStacks(actor, g_preview.includesWeapons);
    LogLine("2.0 studio rebuild after clear actor=" + FormIDHex(actor->formID) + " " +
        EquippedDebug(equippedAfterClear));

    g_preview.draftOutfit = draft;
    g_preview.previewSlot = 0;
    int equippedCount = 0;
    for (const auto& item : draft) {
        ManagedInstanceRef existingRef;
        if (EquipExistingSavedItem(actor, item, &existingRef)) {
            ++equippedCount;
            continue;
        }

        bool added = false;
        ManagedInstanceRef ref;
        const int result = AddAndEquipSavedItem(actor, item, added, false, &ref, true, nullptr, true);
        if (result > 0) {
            equippedCount += result;
            if (added && ref.extra) g_preview.temporary.push_back({ ref, GetArmorSlots(item.fid) });
        }
    }
    RefreshActorAppearance(actor, true);
    const auto equippedAfterRebuild = CaptureEquippedStacks(actor, g_preview.includesWeapons);
    LogLine("2.0 studio draft rebuilt items=" + std::to_string(draft.size()) +
        " equipped=" + std::to_string(equippedCount) + " " + EquippedDebug(equippedAfterRebuild));
    // An empty editor draft is valid: the workbench intentionally starts
    // with no outfit equipped.  Report success even when zero items were
    // rebuilt so the UI can finish opening and display the empty inventory.
    return draft.empty() ? 1 : equippedCount;
}

static int BeginStudioDraft(RE::Actor* actor) {
    if (!actor) return 0;
    g_preview = {};
    g_preview.active = true;
    g_preview.includesWeapons = SaveWeaponsEnabled();
    g_preview.actor = actor->GetHandle();
    g_preview.original = CaptureEquippedStacks(actor, g_preview.includesWeapons);
    g_preview.originalOutfit = CollectEquippedArmor(actor);
    if (g_preview.includesWeapons) AppendEquippedWeapons(actor, g_preview.originalOutfit);
    g_preview.draftOutfit = g_preview.originalOutfit;
    LogLine("2.0 studio opened from actual worn outfit target=" + FormIDHex(actor->formID) +
        " items=" + std::to_string(g_preview.draftOutfit.size()));
    return 1;
}

static int PreviewStudioOE(
    const OE& item,
    std::uint32_t slots,
    RE::Actor* actor,
    const std::string* instanceNameOverride = nullptr)
{
    if (!actor || !BeginPreview(actor)) return -3;
    const auto previousDraft = g_preview.draftOutfit;
    RemovePreviewTemps(actor, slots, item.fid);
    bool added = false;
    ManagedInstanceRef ref;
    const int result = AddAndEquipSavedItem(
        actor, item, added, false, &ref, true, instanceNameOverride, true);
    if (added && ref.extra) g_preview.temporary.push_back({ ref, slots });
    g_preview.previewSlot = 0;
    if (result > 0) {
        AddDraftItem(item, slots);
        RefreshActorAppearance(actor, true);
    } else {
        const int restored = RebuildStudioDraft(actor, previousDraft);
        LogLine("2.0 studio preview failed; previous draft restored result=" +
            std::to_string(restored) + " " + DraftDebug(previousDraft));
    }
    return result;
}

static int PreviewStudioItem(int token, RE::Actor* actor) {
    auto* entry = FindStudioItem(token);
    if (!entry || !actor) return 0;
    auto item = entry->item;
    if (!entry->displayName.empty()) item.name = entry->displayName;
    const auto slots = entry->slots;
    if (!BeginPreview(actor)) return 0;
    AddDraftItem(item, slots);
    const auto draft = g_preview.draftOutfit;
    const int result = RebuildStudioDraft(actor, draft);
    LogLine("2.0 studio equip rebuilt token=" + std::to_string(token) +
        " item=" + FormIDHex(item.fid) + " result=" + std::to_string(result));
    return result;
}

static int UnequipStudioItem(int token, RE::Actor* actor) {
    auto* entry = FindStudioItem(token);
    if (!entry || !actor) return 0;
    if (!BeginPreview(actor)) return 0;
    const auto item = entry->item;
    const bool logicalEquipped = DraftContainsItem(item);
    LogLine("2.0 studio unequip request token=" + std::to_string(token) +
        " display=" + entry->displayName +
        " entryEquipped=" + (entry->equipped ? "1" : "0") +
        " logicalEquipped=" + (logicalEquipped ? "1" : "0") +
        " item=" + OEDebug(item) + " " + DraftDebug(g_preview.draftOutfit) + " " +
        EquippedDebug(CaptureEquippedStacks(actor, g_preview.includesWeapons)));
    if (!logicalEquipped) {
        LogLine("2.0 studio unequip blocked not-in-draft token=" + std::to_string(token) +
            " item=" + OEDebug(item));
        return 0;
    }
    RemoveDraftItem(item);
    const auto draft = g_preview.draftOutfit;
    LogLine("2.0 studio unequip after draft remove token=" + std::to_string(token) +
        " " + DraftDebug(draft));
    const int result = RebuildStudioDraft(actor, draft);
    LogLine("2.0 studio unequip rebuilt token=" + std::to_string(token) +
        " item=" + FormIDHex(item.fid) + " remaining=" + std::to_string(draft.size()) +
        " result=" + std::to_string(result));
    return 1;
}

static bool IsArmorObjectModCandidate(const RE::BGSMod::Attachment::Mod* mod) {
    if (!mod || mod->targetFormType != RE::ENUM_FORM_ID::kARMO) return false;
    const char* name = mod->GetFullName();
    return name && name[0] != '\0';
}

static bool MatchesExplicitArmorFilter(
    const RE::BGSMod::Attachment::Mod* mod,
    const RE::TESObjectARMO* armor)
{
    if (!mod || !armor || !mod->filterKeywords.array || mod->filterKeywords.size == 0) return false;
    for (std::uint32_t i = 0; i < mod->filterKeywords.size; ++i) {
        auto* keyword = RE::detail::BGSKeywordGetTypedKeywordByIndex(
            RE::KeywordType::kInstantiationFilter,
            mod->filterKeywords.array[i].keywordIndex);
        if (keyword && armor->HasKeyword(keyword)) return true;
    }
    return false;
}

static bool HasMaterialSwapProperty(const RE::BGSMod::Attachment::Mod* mod) {
    if (!mod) return false;
    if (mod->swapForm) return true;
    // Mod::GetData() is inlined on NG/AE and has no callable relocation there.
    // The shared Container accessor remains valid across the supported
    // runtimes and exposes the property block we need for material detection.
    RE::BGSMod::Container::Data data{};
    const auto* container = static_cast<const RE::BGSMod::Container*>(mod);
    if (!container->GetData(&data)) return false;
    if (!data.propertyMods) return false;
    for (std::uint32_t i = 0; i < data.propertyModCount; ++i) {
        const auto& property = data.propertyMods[i];
        if (property.type == RE::BGSMod::Property::TYPE::kForm &&
            property.data.form &&
            property.data.form->GetFormType() == RE::ENUM_FORM_ID::kMSWP) {
            return true;
        }
    }
    return false;
}

static std::uint32_t MaterialPluginKey(std::uint32_t formID) {
    // Full plugins use the high byte.  ESL compacted plugins also need their
    // twelve-bit light index so unrelated light plugins are never grouped.
    if ((formID & 0xFF000000u) == 0xFE000000u) return formID & 0xFFFFF000u;
    return formID & 0xFF000000u;
}

static bool IsOfficialGamePlugin(const RE::TESForm* form) {
    if (!form) return false;
    auto* file = form->GetFile(0);
    const auto filename = Lower(file ? std::string(file->GetFilename()) : std::string{});
    static const std::unordered_set<std::string> official{
        "fallout4.esm",
        "dlcrobot.esm",
        "dlccoast.esm",
        "dlcworkshop01.esm",
        "dlcworkshop02.esm",
        "dlcworkshop03.esm",
        "dlcnukaworld.esm",
        "dlcultrahighresolution.esm"
    };
    return official.contains(filename);
}

static std::unordered_set<std::uint32_t> MaterialPluginKeys(const OE& item) {
    std::unordered_set<std::uint32_t> result{ MaterialPluginKey(item.fid) };
    for (const auto& saved : item.mods) {
        if (!saved.disabled && saved.fid != 0) result.insert(MaterialPluginKey(saved.fid));
    }
    return result;
}

static const RE::ExtraDataList* FindLiveMaterialProbeExtra(const StudioItemRef& entry) {
    std::array<RE::Actor*, 2> owners{};
    auto previewRef = g_preview.actor.get();
    owners[0] = previewRef ? previewRef->As<RE::Actor>() : nullptr;
    owners[1] = RE::PlayerCharacter::GetSingleton();

    auto* form = RE::TESForm::GetFormByID(entry.item.fid);
    auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
    if (!object) return nullptr;

    const RE::ExtraDataList* fallback = nullptr;
    for (auto* owner : owners) {
        if (!owner) continue;
        auto* inventoryItem = FindInventoryItem(owner, object);
        if (!inventoryItem) continue;

        // The UI reference is only accepted after proving it still belongs to
        // a live stack.  This avoids the stale ExtraData pointer that made the
        // older engine compatibility scan crash on some outfits.
        if (entry.ref.extra && FindStackID(inventoryItem, entry.ref.extra, false) >= 0) {
            return entry.ref.extra;
        }

        for (auto* stack = inventoryItem->stackData.get(); stack; stack = stack->nextStack.get()) {
            auto* extra = stack->extra.get();
            if (!extra) continue;
            auto* objectExtra = extra->GetByType<RE::BGSObjectInstanceExtra>();
            if (!SameSavedMods(CollectInstanceMods(objectExtra), entry.item.mods)) continue;
            if (stack->IsEquipped()) return extra;
            if (!fallback) fallback = extra;
        }
    }
    return fallback;
}

static bool CanMaterialModBeUsedOnLiveStack(
    RE::BGSMod::Attachment::Mod* mod,
    RE::TESForm* form,
    const RE::ExtraDataList* extra)
{
    if (!mod || !form || !extra) return false;
    // SAM uses the engine's CanBeUsedOnForm(mod, form, extraData) routine.
    // The verified Address Library ID below is for the original runtime line.
    // NG/AE keep the structural adapter until their distinct IDs are
    // verified instead of risking a cross-runtime call into the wrong address.
    if (!REX::FModule::IsRuntimeOG()) return true;
    using func_t = bool (*)(RE::BGSMod::Attachment::Mod*, RE::TESForm*, RE::ExtraDataList*);
    static REL::Relocation<func_t> canBeUsedOnForm{ REL::ID(509890) };
    return canBeUsedOnForm(mod, form, const_cast<RE::ExtraDataList*>(extra));
}

static std::string NormalizeEditorID(const char* editorID) {
    std::string result;
    if (!editorID) return result;
    for (const auto ch : std::string_view(editorID)) {
        const auto byte = static_cast<unsigned char>(ch);
        if (std::isalnum(byte)) result.push_back(static_cast<char>(std::tolower(byte)));
    }
    return result;
}

static std::string ArmorOwnershipKey(const RE::TESObjectARMO* armor) {
    auto result = NormalizeEditorID(armor ? armor->GetFormEditorID() : nullptr);
    if (result.starts_with("armor") && result.size() > 8) result.erase(0, 5);
    return result;
}

static bool IsBestEditorIDOwner(
    const RE::BGSMod::Attachment::Mod* mod,
    const RE::TESObjectARMO* armor,
    RE::TESDataHandler* dataHandler)
{
    if (!mod || !armor || !dataHandler) return false;
    auto modKey = NormalizeEditorID(mod->GetFormEditorID());
    if (modKey.starts_with("omod")) modKey.erase(0, 4);
    const auto currentKey = ArmorOwnershipKey(armor);
    if (modKey.empty() || currentKey.size() < 5) return false;
    // Reject unrelated OMODs before the expensive best-owner walk.  Without
    // this guard a vanilla armor asks us to rescan every ARMO for thousands
    // of unrelated Fallout4.esm material mods.
    if (modKey.find(currentKey) == std::string::npos) return false;

    std::size_t bestLength = 0;
    std::uint32_t bestFormID = 0;
    for (auto* candidate : dataHandler->GetFormArray<RE::TESObjectARMO>()) {
        if (!candidate) continue;
        const auto candidateKey = ArmorOwnershipKey(candidate);
        if (candidateKey.size() < 5 || modKey.find(candidateKey) == std::string::npos) continue;
        if (candidateKey.size() > bestLength) {
            bestLength = candidateKey.size();
            bestFormID = candidate->GetFormID();
        }
    }
    return bestFormID == armor->GetFormID();
}

static const std::vector<MaterialChoice>& GetMaterialChoices(int token) {
    if (auto it = g_materialCache.find(token); it != g_materialCache.end()) return it->second;
    auto& result = g_materialCache[token];
    auto* entry = FindStudioItem(token);
    const std::uint32_t armorID = entry ? entry->item.fid : 0;
    auto* form = RE::TESForm::GetFormByID(armorID);
    auto* armor = form ? form->As<RE::TESObjectARMO>() : nullptr;
    auto* dataHandler = RE::TESDataHandler::GetSingleton();
    if (!entry || !armor || !dataHandler) return result;

    int armorMods = 0;
    int namedMods = 0;
    int materialMods = 0;
    int formalMatches = 0;
    int editorMatches = 0;
    int engineMatches = 0;
    int narrowedMods = 0;
    const auto allowedPluginKeys = MaterialPluginKeys(entry->item);
    const auto armorPluginKey = MaterialPluginKey(armorID);
    // Plugin-wide fallback is useful for focused outfit plugins such as Vtaw,
    // but Fallout4.esm contains thousands of unrelated armor OMODs.  Treating
    // all of them as candidates causes multi-second stalls and still produces
    // no valid choices for the Vault 111 suit.
    const bool allowPluginFallback = armorPluginKey != 0 && !IsOfficialGamePlugin(armor);
    const auto* probeExtra = FindLiveMaterialProbeExtra(*entry);
    for (auto* mod : dataHandler->GetFormArray<RE::BGSMod::Attachment::Mod>()) {
        if (!mod || mod->targetFormType != RE::ENUM_FORM_ID::kARMO) continue;
        ++armorMods;
        if (!IsArmorObjectModCandidate(mod)) continue;
        ++namedMods;
        if (!HasMaterialSwapProperty(mod)) continue;
        ++materialMods;
        // APPR is often shared by every component in an outfit (the inspected
        // Vtaw package uses the same four parents for all ten ARMO records).
        // MNAM/filter keyword -> ARMO KWDA is the discriminating family link.
        const bool formalMatch = MatchesExplicitArmorFilter(mod, armor);
        const bool editorMatch = IsBestEditorIDOwner(mod, armor, dataHandler);
        const bool relatedPlugin = allowPluginFallback &&
            allowedPluginKeys.contains(MaterialPluginKey(mod->GetFormID()));
        if (!relatedPlugin && !formalMatch && !editorMatch) continue;
        if (probeExtra && !CanMaterialModBeUsedOnLiveStack(mod, form, probeExtra)) continue;
        if (formalMatch) ++formalMatches;
        if (editorMatch) ++editorMatches;
        if (probeExtra && REX::FModule::IsRuntimeOG()) ++engineMatches;
        ++narrowedMods;
        const char* name = mod->GetFullName();
        if (!name || !name[0]) continue;
        result.push_back({ mod->GetFormID(), name, mod->attachPoint.keywordIndex, true });
    }
    std::stable_sort(result.begin(), result.end(), [](const MaterialChoice& lhs, const MaterialChoice& rhs) {
        if (lhs.exact != rhs.exact) return lhs.exact > rhs.exact;
        return Lower(lhs.name) < Lower(rhs.name);
    });
    result.erase(std::unique(result.begin(), result.end(), [](const MaterialChoice& lhs, const MaterialChoice& rhs) {
        return lhs.fid == rhs.fid;
    }), result.end());
    if (result.size() > 256) result.resize(256);
    LogLine("2.0 material scan armor=" + FormIDHex(armorID) +
        " token=" + std::to_string(token) +
        " armorOMOD=" + std::to_string(armorMods) +
        " namedOMOD=" + std::to_string(namedMods) +
        " materialOMOD=" + std::to_string(materialMods) +
        " formal=" + std::to_string(formalMatches) +
        " editor=" + std::to_string(editorMatches) +
        " engine=" + std::to_string(engineMatches) +
        " liveStack=" + (probeExtra ? "1" : "0") +
        " narrowed=" + std::to_string(narrowedMods) +
        " result=" + std::to_string(result.size()));
    for (std::size_t i = 0; i < result.size() && i < 32; ++i) {
        LogLine("2.0 material candidate token=" + std::to_string(token) +
            " omod=" + FormIDHex(result[i].fid) +
            " attach=" + std::to_string(result[i].attachPoint) +
            " name=" + result[i].name);
    }
    return result;
}

static std::string BuildMaterialChoicesJson(int token) {
    auto* entry = FindStudioItem(token);
    std::string result = "{\"token\":" + std::to_string(token) + ",\"itemName\":\"";
    result += entry ? json::esc(entry->displayName) : "";
    result += "\",\"choices\":[";
    if (entry) {
        const auto& choices = GetMaterialChoices(token);
        for (std::size_t i = 0; i < choices.size(); ++i) {
            if (i) result += ",";
            const bool current = std::any_of(entry->item.mods.begin(), entry->item.mods.end(), [&](const SavedMod& mod) {
                return !mod.disabled && mod.fid == choices[i].fid;
            });
            result += "{\"id\":\"" + FormIDHex(choices[i].fid) +
                "\",\"name\":\"" + json::esc(choices[i].name) +
                "\",\"current\":" + (current ? "true" : "false") +
                ",\"fallback\":" + (choices[i].exact ? "false" : "true") + "}";
        }
    }
    result += "]}";
    return result;
}

static std::optional<OE> MakeMaterialItem(int token, std::uint32_t modID) {
    auto* entry = FindStudioItem(token);
    auto* form = RE::TESForm::GetFormByID(modID);
    auto* selectedMod = form ? form->As<RE::BGSMod::Attachment::Mod>() : nullptr;
    if (!entry || !selectedMod || !IsArmorObjectModCandidate(selectedMod)) return std::nullopt;
    const auto& compatible = GetMaterialChoices(token);
    if (std::none_of(compatible.begin(), compatible.end(), [modID](const MaterialChoice& choice) {
            return choice.fid == modID;
        })) {
        return std::nullopt;
    }
    OE result = entry->item;
    result.modSnapshot = true;
    const auto selectedPoint = selectedMod->attachPoint.keywordIndex;
    const auto isCompatibleMaterial = [&compatible](std::uint32_t fid) {
        return std::any_of(compatible.begin(), compatible.end(), [fid](const MaterialChoice& choice) {
            return choice.fid == fid;
        });
    };
    std::uint8_t selectedIndex = 1;
    std::uint8_t selectedRank = 1;
    result.mods.erase(std::remove_if(result.mods.begin(), result.mods.end(), [&](const SavedMod& saved) {
        if (isCompatibleMaterial(saved.fid)) {
            selectedIndex = saved.index;
            selectedRank = saved.rank;
            return true;
        }
        auto* savedForm = RE::TESForm::GetFormByID(saved.fid);
        auto* savedObjectMod = savedForm ? savedForm->As<RE::BGSMod::Attachment::Mod>() : nullptr;
        if (!savedObjectMod || !IsArmorObjectModCandidate(savedObjectMod)) return false;
        const bool sameSlot = selectedPoint != 0 && savedObjectMod->attachPoint.keywordIndex == selectedPoint;
        if (sameSlot) {
            selectedIndex = saved.index;
            selectedRank = saved.rank;
        }
        return sameSlot;
    }), result.mods.end());
    result.mods.push_back({ modID, selectedIndex, selectedRank, false });
    LogLine("2.0 material build token=" + std::to_string(token) +
        " omod=" + FormIDHex(modID) + " attach=" + std::to_string(selectedPoint) +
        " index=" + std::to_string(selectedIndex) +
        " rank=" + std::to_string(selectedRank));
    return result;
}

static std::string BuildMaterialItemName(int token, std::uint32_t modID, const StudioItemRef& entry) {
    auto* baseForm = RE::TESForm::GetFormByID(entry.item.fid);
    std::string result = GetBaseFormName(baseForm);
    if (result.empty()) result = entry.item.name.empty() ? entry.displayName : entry.item.name;

    const auto& choices = GetMaterialChoices(token);
    const auto selected = std::find_if(choices.begin(), choices.end(), [modID](const MaterialChoice& choice) {
        return choice.fid == modID;
    });
    if (selected != choices.end() && !selected->name.empty()) {
        result += " [" + selected->name + "]";
    }
    return result;
}

static int InventoryItemTotalCount(RE::BGSInventoryItem* inventoryItem) {
    if (!inventoryItem) return 0;
    int result = 0;
    for (auto* stack = inventoryItem->stackData.get(); stack; stack = stack->nextStack.get()) {
        result += static_cast<int>((std::max)(stack->GetCount(), 1u));
    }
    return result;
}

static int FindMaterialSourceStack(
    RE::Actor* actor,
    RE::BGSInventoryItem* inventoryItem,
    const StudioItemRef& entry)
{
    if (!actor || !inventoryItem) return -1;
    if (actor == RE::PlayerCharacter::GetSingleton() && entry.ref.extra) {
        const int exact = FindStackID(inventoryItem, entry.ref.extra, false);
        if (exact >= 0) return exact;
    }

    int equippedFallback = -1;
    int appearanceFallback = -1;
    int stackID = 0;
    for (auto* stack = inventoryItem->stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
        auto* objectExtra = stack->extra ? stack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
        const bool sameAppearance = SameSavedMods(CollectInstanceMods(objectExtra), entry.item.mods);
        if (stack->IsEquipped()) {
            if (sameAppearance) return stackID;
            if (equippedFallback < 0) equippedFallback = stackID;
        } else if (sameAppearance && appearanceFallback < 0) {
            appearanceFallback = stackID;
        }
    }
    return equippedFallback >= 0 ? equippedFallback : appearanceFallback;
}

static int FindChangedMaterialStack(
    RE::BGSInventoryItem* inventoryItem,
    const std::vector<SavedMod>& wantedMods,
    const RE::ExtraDataList* sourceExtra)
{
    if (!inventoryItem) return -1;
    int sourceMatch = -1;
    int stackID = 0;
    for (auto* stack = inventoryItem->stackData.get(); stack; stack = stack->nextStack.get(), ++stackID) {
        auto* objectExtra = stack->extra ? stack->extra->GetByType<RE::BGSObjectInstanceExtra>() : nullptr;
        if (!SameSavedMods(CollectInstanceMods(objectExtra), wantedMods)) continue;
        if (stack->IsEquipped()) return stackID;
        if (sourceExtra && stack->extra.get() == sourceExtra) sourceMatch = stackID;
    }
    return sourceMatch;
}

static void UpdateTrackedMaterialInstance(
    RE::Actor* actor,
    const RE::ExtraDataList* sourceExtra,
    const ManagedInstanceRef& changedRef)
{
    if (!actor || !sourceExtra || !changedRef.extra) return;
    for (auto& temporary : g_preview.temporary) {
        if (temporary.ref.fid == changedRef.fid && temporary.ref.extra == sourceExtra) {
            temporary.ref = changedRef;
        }
    }
    for (auto& persistent : g_preview.persistentPreview) {
        if (persistent.fid == changedRef.fid && persistent.extra == sourceExtra) {
            persistent = changedRef;
        }
    }
    std::lock_guard<std::mutex> lock(g_mx);
    if (auto found = g_managedInstances.find(actor->GetFormID()); found != g_managedInstances.end()) {
        for (auto& tracked : found->second) {
            if (tracked.fid == changedRef.fid && tracked.extra == sourceExtra) tracked = changedRef;
        }
    }
}

static int PreviewMaterialItem(
    int token,
    std::uint32_t modID,
    RE::Actor* actor,
    std::string* outChangedName)
{
    auto item = MakeMaterialItem(token, modID);
    auto* entry = FindStudioItem(token);
    if (!item || !entry || !actor || !BeginPreview(actor)) return 0;

    // E changes the currently worn item's material. Rebuild the display name
    // from the base form every time so repeated changes replace the material
    // label instead of stacking suffixes such as [Red] [Blue].
    const std::string materialName = BuildMaterialItemName(token, modID, *entry);
    item->name = materialName;

    auto* form = RE::TESForm::GetFormByID(item->fid);
    auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
    auto* selectedForm = RE::TESForm::GetFormByID(modID);
    auto* selectedMod = selectedForm ? selectedForm->As<RE::BGSMod::Attachment::Mod>() : nullptr;
    auto* inventoryItem = FindInventoryItem(actor, object);
    if (!object || !selectedMod) return 0;

    // A few outfit/template actors expose a worn biped object without a live
    // inventory stack. There is nothing to duplicate in that case, so retain
    // the generated-instance fallback solely for that representation.
    if (!inventoryItem) {
        const std::string generatedName = AozoraGeneratedItemName(materialName);
        item->name = generatedName;
        const int result = PreviewStudioOE(*item, entry->slots, actor, &generatedName);
        if (result > 0 && outChangedName) *outChangedName = generatedName;
        LogLine("2.0 material change used biped-only fallback item=" + FormIDHex(item->fid) +
            " target=" + FormIDHex(actor->GetFormID()));
        return result;
    }

    const int sourceStackID = FindMaterialSourceStack(actor, inventoryItem, *entry);
    auto* sourceStack = sourceStackID >= 0 ?
        inventoryItem->GetStackByID(static_cast<std::uint32_t>(sourceStackID)) : nullptr;
    if (!sourceStack) {
        LogLine("2.0 material change refused: no source stack item=" + FormIDHex(item->fid));
        return 0;
    }

    const auto* sourceExtra = sourceStack->extra.get();
    const int countBefore = InventoryItemTotalCount(inventoryItem);
    const auto selectedSavedMod = std::find_if(item->mods.begin(), item->mods.end(), [modID](const SavedMod& mod) {
        return !mod.disabled && mod.fid == modID;
    });
    if (selectedSavedMod == item->mods.end()) return 0;

    bool writerResult = false;
    RE::BGSInventoryItem::CheckStackIDFunctor comparer(static_cast<std::uint32_t>(sourceStackID));
    RE::BGSInventoryItem::ModifyModDataFunctor writer(
        selectedMod, static_cast<std::int8_t>(selectedSavedMod->index), true, &writerResult);
    writer.shouldSplitStacks = true;
    writer.transferEquippedToSplitStack = sourceStack->IsEquipped();
    actor->FindAndWriteStackDataForInventoryItem(object, comparer, writer);
    NotifyInventoryItemModified(actor, object);

    inventoryItem = FindInventoryItem(actor, object);
    const int changedStackID = FindChangedMaterialStack(inventoryItem, item->mods, sourceExtra);
    auto* changedStack = changedStackID >= 0 && inventoryItem ?
        inventoryItem->GetStackByID(static_cast<std::uint32_t>(changedStackID)) : nullptr;
    if (!changedStack || !changedStack->extra) {
        LogLine("2.0 material change verification failed item=" + FormIDHex(item->fid) +
            " wanted=" + SavedModsDebug(item->mods));
        return 0;
    }

    const auto* changedExtra = changedStack->extra.get();
    const bool changedWasEquipped = changedStack->IsEquipped();
    changedStack->extra->SetOverrideName(materialName.c_str());
    NotifyInventoryItemModified(actor, object);
    inventoryItem = FindInventoryItem(actor, object);
    const int countAfter = InventoryItemTotalCount(inventoryItem);
    if (!changedWasEquipped) {
        EquipExistingStack(actor, { item->fid, changedExtra });
    }

    const ManagedInstanceRef changedRef{ item->fid, changedExtra, materialName, item->mods };
    UpdateTrackedMaterialInstance(actor, sourceExtra, changedRef);
    AddDraftItem(*item, entry->slots);
    RefreshActorAppearance(actor, true);
    if (outChangedName) *outChangedName = materialName;
    LogLine("2.0 material changed in place item=" + FormIDHex(item->fid) +
        " target=" + FormIDHex(actor->GetFormID()) +
        " sourceStack=" + std::to_string(sourceStackID) +
        " changedStack=" + std::to_string(changedStackID) +
        " writer=" + (writerResult ? "1" : "0") +
        " count=" + std::to_string(countBefore) + "->" + std::to_string(countAfter));
    return countBefore == countAfter ? 1 : 0;
}

static int CommitMaterialItem(int token, std::uint32_t modID, RE::Actor* actor, std::string* outGeneratedName) {
    auto item = MakeMaterialItem(token, modID);
    auto* entry = FindStudioItem(token);
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!g_menuOpen || !item || !entry || !player) return 0;

    if (actor == player && BeginPreview(player)) RemovePreviewTemps(player, entry->slots, item->fid);

    bool added = false;
    ManagedInstanceRef permanentRef;
    const bool equipPlayer = actor == player;
    const std::string generatedName = AozoraGeneratedItemName(
        BuildMaterialItemName(token, modID, *entry));
    OE committedItem = *item;
    committedItem.name = generatedName;
    // F always creates one more permanent copy. Identical generated variants
    // may be grouped by the inventory UI, but their quantity must increase
    // instead of reusing the copy that already exists.
    const int result = AddAndEquipSavedItem(
        player, *item, added, false, &permanentRef, equipPlayer, &generatedName, false);
    if (result > 0) {
        if (equipPlayer && g_preview.active) {
            g_preview.persistentPreview.push_back(permanentRef);
            AddDraftItem(committedItem, entry->slots);
            UpdatePreviewActor(player);
        } else if (actor) {
            PreviewStudioOE(committedItem, entry->slots, actor, &generatedName);
        }
        if (outGeneratedName) *outGeneratedName = generatedName;
    }
    LogLine("2.0 material commit item=" + FormIDHex(item->fid) + " omod=" + FormIDHex(modID) +
        " target=" + (actor ? FormIDHex(actor->formID) : "None") + " result=" + std::to_string(result));
    return result;
}

class DeferredOutfitEquipCallback final : public RE::IMessageBoxCallback {
public:
    DeferredOutfitEquipCallback(RE::Actor* actor, int slot) :
        actor_(actor ? actor->GetHandle() : RE::ObjectRefHandle{}),
        slot_(slot)
    {}

    void operator()(std::uint8_t buttonIndex) override {
        if (buttonIndex != 0) return;
        const auto actorHandle = actor_;
        const int slot = slot_;
        QueueGameTask([actorHandle, slot] {
            auto ref = actorHandle ? actorHandle.get() : RE::NiPointer<RE::TESObjectREFR>{};
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            if (!actor) return;
            ClearExpiredBusyState();
            if (g_eqBusy || g_randBusy) {
                RE::SendHUDMessage::ShowHUDMessage("An outfit operation is busy. Try again shortly", nullptr, true, true);
                return;
            }
            g_eqBusy = true;
            g_eqBusySinceMs = GetTickCount64();
            const int result = EquipOutfitForActor(slot, actor, true, false);
            g_eqBusy = false;
            g_eqBusySinceMs = 0;
            const std::string message = result > 0 ?
                "Equipped：" + (ReadSlotName(slot).empty() ?
                    ("Outfit " + std::to_string(slot)) : ReadSlotName(slot)) :
                "Outfit Change Failed";
            RE::SendHUDMessage::ShowHUDMessage(message.c_str(), nullptr, true, result <= 0);
        });
    }

private:
    RE::ObjectRefHandle actor_;
    int slot_ = 0;
};

class DeferredMissingOutfitCallback final : public RE::IMessageBoxCallback {
public:
    DeferredMissingOutfitCallback(RE::Actor* actor, int slot, bool allowSharedTemplate) :
        actor_(actor ? actor->GetHandle() : RE::ObjectRefHandle{}),
        slot_(slot),
        allowSharedTemplate_(allowSharedTemplate)
    {}

    void operator()(std::uint8_t buttonIndex) override {
        const auto actorHandle = actor_;
        const int slot = slot_;
        const bool continueEquip = buttonIndex == 0;
        const bool allowSharedTemplate = allowSharedTemplate_;
        QueueGameTask([actorHandle, slot, continueEquip, allowSharedTemplate] {
            int slotGender = -1;
            std::vector<OE> resolvedItems;
            std::string summary;
            SlotReadStatus status;
            const bool read = ReadSlot(slot, slotGender, resolvedItems, summary, &status);
            if (!read && status.RemovedCount() <= 0) {
                RE::SendHUDMessage::ShowHUDMessage(
                    "Unable to read this outfit; the original record was not changed",
                    nullptr,
                    true,
                    true);
                return;
            }
            if (status.RemovedCount() > 0 &&
                !PersistResolvedSlotRecords(slot, slotGender, resolvedItems, status)) {
                RE::SendHUDMessage::ShowHUDMessage(
                    "Outfit backup or cleanup failed; the original record was not changed",
                    nullptr,
                    true,
                    true);
                return;
            }

            if (!continueEquip) {
                RE::SendHUDMessage::ShowHUDMessage(
                    "Missing records were removed from this outfit",
                    nullptr,
                    true,
                    false);
                return;
            }
            if (resolvedItems.empty()) {
                RE::SendHUDMessage::ShowHUDMessage(
                    "Missing records were removed; this outfit has no wearable items left",
                    nullptr,
                    true,
                    true);
                return;
            }

            auto ref = actorHandle ? actorHandle.get() : RE::NiPointer<RE::TESObjectREFR>{};
            auto* actor = ref ? ref->As<RE::Actor>() : nullptr;
            if (!actor) return;
            ClearExpiredBusyState();
            if (g_eqBusy || g_randBusy) {
                RE::SendHUDMessage::ShowHUDMessage("An outfit operation is busy. Try again shortly", nullptr, true, true);
                return;
            }
            g_eqBusy = true;
            g_eqBusySinceMs = GetTickCount64();
            const int result = EquipOutfitForActor(slot, actor, allowSharedTemplate, true);
            g_eqBusy = false;
            g_eqBusySinceMs = 0;
            const std::string message = result > 0 ?
                "Equipped：" + (ReadSlotName(slot).empty() ?
                    ("Outfit " + std::to_string(slot)) : ReadSlotName(slot)) :
                "Outfit Change Failed";
            RE::SendHUDMessage::ShowHUDMessage(message.c_str(), nullptr, true, result <= 0);
        });
    }

private:
    RE::ObjectRefHandle actor_;
    int slot_ = 0;
    bool allowSharedTemplate_ = false;
};

static bool ShowDeferredOutfitPrompt(
    RE::Actor* actor,
    int slot,
    std::string_view title,
    std::string_view body,
    std::string_view accept,
    std::string_view cancel)
{
    auto* manager = RE::MessageMenuManager::GetSingleton();
    if (!manager || !actor) return false;
    manager->Create(
        std::string(title).c_str(),
        std::string(body).c_str(),
        new DeferredOutfitEquipCallback(actor, slot),
        RE::WARNING_TYPES::kInGameMessage,
        std::string(accept).c_str(),
        std::string(cancel).c_str());
    return true;
}

static bool ShowDeferredMissingOutfitPrompt(
    RE::Actor* actor,
    int slot,
    const SlotReadStatus& status,
    bool allowSharedTemplate)
{
    auto* manager = RE::MessageMenuManager::GetSingleton();
    if (!manager || !actor) return false;
    const std::string body =
        std::to_string(status.RemovedCount()) +
        " missing clothing or modification records were found.\n"
        "Either choice removes only missing records from this outfit and keeps a recovery backup.\n"
        "Continuing may leave the character wearing only the remaining items.";
    manager->Create(
        "Outfit Content Missing",
        body.c_str(),
        new DeferredMissingOutfitCallback(actor, slot, allowSharedTemplate),
        RE::WARNING_TYPES::kInGameMessage,
        "Continue",
        "Don't Equip");
    return true;
}

static int EquipOutfitForActor(
    int slot,
    RE::Actor* actor,
    bool allowSharedTemplate,
    bool acknowledgeMissing)
{
    const auto targetState = CheckOutfitTargetState(actor, allowSharedTemplate);
    if (targetState == OutfitTargetState::kSharedTemplate && !allowSharedTemplate) {
        ShowDeferredOutfitPrompt(
            actor,
            slot,
            "Shared NPC Warning",
            "This NPC uses a shared template. Changing their outfit may also affect similar NPCs.",
            "Continue",
            "Cancel");
        return static_cast<int>(targetState);
    }
    if (targetState != OutfitTargetState::kAllowed) {
        LogLine("2.0 outfit target blocked target=" +
            FormIDHex(actor ? actor->GetFormID() : 0) +
            " reason=" + std::to_string(static_cast<int>(targetState)));
        return static_cast<int>(targetState);
    }

    std::vector<OE> items;
    std::vector<OE> resolvedSlotItems;
    int slotGender = -1;
    SlotReadStatus status;
    const int validation = ValidateOutfitForActor(
        slot,
        actor,
        items,
        &status,
        &resolvedSlotItems,
        &slotGender);
    if (status.RemovedCount() > 0 && !acknowledgeMissing) {
        ShowDeferredMissingOutfitPrompt(actor, slot, status, allowSharedTemplate);
        return -5;
    }
    if (status.RemovedCount() > 0) {
        if (!PersistResolvedSlotRecords(slot, slotGender, resolvedSlotItems, status)) return -9;
    }
    if (validation <= 0) return validation;

    const auto oldEquipped = CollectEquippedArmor(actor);
    const auto oldEquippedStacks = CaptureEquippedStacks(actor);

    {
        std::lock_guard<std::mutex> lk(g_mx);
        auto& managed = g_mi[actor->GetFormID()];
        if (actor == RE::PlayerCharacter::GetSingleton()) {
            managed.d.clear();
            managed.dr = false;
        } else if (!managed.dr && managed.m.empty()) {
            for (const auto& item : oldEquipped) managed.d.push_back(item.fid);
            managed.dr = true;
        }
    }

    std::vector<std::uint32_t> previousManaged;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        previousManaged = g_mi[actor->GetFormID()].m;
        g_mi[actor->GetFormID()].m.clear();
    }
    // A companion keeps the current vanilla outfit until the player equips the
    // generated item through the normal trade menu. Never remove that outfit
    // before the inventory-only operation has completed.
    if (!IsPlayerTeammateNative(actor)) {
        // Match the workbench preview path: fully clear the old worn stacks
        // before equipping the next outfit.
        for (const auto& oldStack : oldEquippedStacks) {
            UnequipExistingStack(actor, { oldStack.fid, oldStack.extra });
        }
    }
    RemovePreviousManagedItems(actor, previousManaged);

    int equippedCount = 0;
    std::vector<std::uint32_t> newlyManaged;
    for (const auto& item : items) {
        bool managed = false;
        equippedCount += AddAndEquipSavedItem(
            actor,
            item,
            managed,
            true,
            nullptr,
            !IsPlayerTeammateNative(actor),
            nullptr,
            true);
        if (managed) newlyManaged.push_back(item.fid);
    }

    {
        std::lock_guard<std::mutex> lk(g_mx);
        auto& managed = g_mi[actor->GetFormID()];
        for (auto id : newlyManaged) {
            if (std::find(managed.m.begin(), managed.m.end(), id) == managed.m.end()) managed.m.push_back(id);
        }
        WriteActors();
    }
    if (equippedCount > 0 && !IsPlayerTeammateNative(actor)) {
        SetActiveSlotForActor(actor, slot);
        IncrementSlotUsageCount(slot);
    }
    if (equippedCount > 0 && IsPlayerTeammateNative(actor) && !g_menuOpen) {
        RE::SendHUDMessage::ShowHUDMessage(
            "The outfit has been added to the target's inventory. Please equip it through the trade menu.",
            nullptr,
            true,
            false);
    }
    if (actor != RE::PlayerCharacter::GetSingleton()) {
        RefreshActorAppearance(actor, true);
    } else if (equippedCount > 0 && g_menuOpen && !IsLightweightMenuMode() &&
        (REX::FModule::IsRuntimeNG() || REX::FModule::IsRuntimeAE())) {
        LogLine("1.1 full-menu player outfit refresh requested slot=" + std::to_string(slot));
        RefreshActorAppearance(actor, true);
        LogLine("1.1 full-menu player outfit refresh submitted slot=" + std::to_string(slot));
    }
    LogLine("2.0 native outfit apply target=" + FormIDHex(actor->formID) + " slot=" + std::to_string(slot) + " placed=" + std::to_string(equippedCount) + "/" + std::to_string(items.size()));
    return equippedCount;
}
int OM_EquipMenuActionTargetOutfit(std::monostate, int slot) {
    EnsureCacheLoaded();
    return EquipOutfitForActor(slot, ResolveMenuActionActor());
}
int OM_EquipOutfitNative(std::monostate, int slot, RE::Actor* actor) {
    EnsureCacheLoaded();
    return EquipOutfitForActor(slot, actor);
}
int OM_ResetOutfitUsageCounts(std::monostate) {
    EnsureCacheLoaded();
    int resetCount = 0;
    for (const auto& info : g_index) {
        if (info.usageCount <= 0) continue;
        if (WriteSlotUsageCount(info.slot, 0)) ++resetCount;
    }
    WriteIndex();
    LogLine("2.0 outfit usage reset slots=" + std::to_string(resetCount));
    return resetCount;
}
int OM_ResetMenuActionTargetOutfit(std::monostate) {
    EnsureCacheLoaded();
    auto* actor = ResolveMenuActionActor();
    if (!actor) return -3;
    if (IsActorInPowerArmor(actor)) return -4;
    const auto targetState = CheckOutfitTargetState(actor, true);
    if (targetState != OutfitTargetState::kAllowed) return static_cast<int>(targetState);

    std::vector<std::uint32_t> defaultIDs;
    std::vector<std::uint32_t> managedIDs;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_mi.find(actor->GetFormID());
        if (it != g_mi.end()) {
            defaultIDs = it->second.d;
            managedIDs = it->second.m;
        }
    }

    if (actor == RE::PlayerCharacter::GetSingleton()) {
        const auto equippedItems = CollectEquippedArmor(actor);
        RemovePreviousManagedItems(actor, managedIDs);
        auto* equipManager = RE::ActorEquipManager::GetSingleton();
        int unequippedCount = 0;
        for (const auto& item : equippedItems) {
            auto* form = RE::TESForm::GetFormByID(item.fid);
            auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
            if (!object || !equipManager) continue;
            RE::BGSObjectInstance instance(object, nullptr);
            if (equipManager->UnequipObject(actor, &instance, 1, nullptr, 0, false, true, false, true, nullptr)) {
                ++unequippedCount;
            }
        }
        {
            std::lock_guard<std::mutex> lk(g_mx);
            auto& managed = g_mi[actor->GetFormID()];
            managed.m.clear();
            managed.d.clear();
            managed.dr = false;
            g_lastAppliedSlots.erase(actor->GetFormID());
            WriteActors();
        }

        constexpr std::uint32_t kVault111SuitFormID = 0x0001EED7;
        bool resetSuitEquipped = false;
        auto* suitForm = RE::TESForm::GetFormByID(kVault111SuitFormID);
        auto* suitObject = suitForm ? suitForm->As<RE::TESBoundObject>() : nullptr;
        if (suitObject && equipManager) {
            std::uint32_t suitCount = 0;
            actor->GetItemCount(suitCount, suitObject, false);
            if (suitCount == 0) {
                actor->AddObjectToContainer(suitObject, {}, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
            }
            auto* inventoryItem = FindInventoryItem(actor, suitObject);
            const int stackID = FindStackID(inventoryItem, nullptr, false);
            if (inventoryItem && stackID >= 0) {
                if (auto* stack = inventoryItem->GetStackByID(static_cast<std::uint32_t>(stackID)); stack) {
                    stack->flags.reset(RE::BGSInventoryItem::Stack::Flag::kEquipStateLocked);
                }
            }
            auto* instanceData = inventoryItem && stackID >= 0 ?
                inventoryItem->GetInstanceData(static_cast<std::uint32_t>(stackID)) : nullptr;
            RE::BGSObjectInstance instance(suitObject, instanceData);
            resetSuitEquipped = equipManager->EquipObject(
                actor,
                instance,
                stackID >= 0 ? static_cast<std::uint32_t>(stackID) : 0,
                1,
                nullptr,
                false,
                ShouldForceEquip(actor),
                false,
                true,
                false);
        }
        constexpr std::uint32_t kAppearanceRefresh = 0x13;
        if (auto* taskQueue = RE::TaskQueueInterface::GetSingleton()) taskQueue->QueueUpdate3D(actor, kAppearanceRefresh);
        LogLine("2.0 player reset removed managed=" + std::to_string(managedIDs.size()) +
            " unequipped=" + std::to_string(unequippedCount) +
            " vault111=" + (resetSuitEquipped ? "1" : "0"));
        return (std::max)(resetSuitEquipped ? 1 : 0,
            (std::max)(unequippedCount, static_cast<int>(managedIDs.size())));
    }

    if (defaultIDs.empty()) {
        return -1;
    }

    auto* equipManager = RE::ActorEquipManager::GetSingleton();
    const auto oldEquipped = CollectEquippedArmor(actor);
    RemovePreviousManagedItems(actor, managedIDs);
    int equippedCount = 0;
    for (auto id : defaultIDs) {
        auto* form = RE::TESForm::GetFormByID(id);
        if (IsIgnoredOutfitArmor(form)) continue;
        auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
        if (!object) continue;
        std::uint32_t count = 0;
        actor->GetItemCount(count, object, false);
        if (count == 0) actor->AddObjectToContainer(object, {}, 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
        RE::BGSObjectInstance instance(object, nullptr);
        if (equipManager && equipManager->EquipObject(actor, instance, 0, 1, nullptr, false, false, false, true, false)) ++equippedCount;
    }
    for (const auto& item : oldEquipped) {
        if (std::find(defaultIDs.begin(), defaultIDs.end(), item.fid) != defaultIDs.end()) continue;
        auto* form = RE::TESForm::GetFormByID(item.fid);
        auto* object = form ? form->As<RE::TESBoundObject>() : nullptr;
        if (!object || !equipManager) continue;
        RE::BGSObjectInstance instance(object, nullptr);
        equipManager->UnequipObject(actor, &instance, 1, nullptr, 0, false, true, false, true, nullptr);
    }
    {
        std::lock_guard<std::mutex> lk(g_mx);
        g_mi[actor->GetFormID()].m.clear();
        g_lastAppliedSlots.erase(actor->GetFormID());
        WriteActors();
    }
    actor->UpdateReference3D();
    LogLine("Native menu reset target=" + FormIDHex(actor->formID) + " equipped=" + std::to_string(equippedCount));
    return equippedCount;
}

int OM_SaveOutfit(std::monostate, int slot, RE::Actor* t) {
    EnsureCacheLoaded();
    if (!t || slot < 1 || slot > MAX_SLOTS) return 0;
    int sex = GetSex(t); if (sex < 0) return 0;
    auto items = CollectEquippedArmor(t);
    if (SaveWeaponsEnabled()) AppendEquippedWeapons(t, items);
    if (items.empty()) return 0;
    for (const auto& item : items) {
        LogLine("2.0 save item=" + FormIDHex(item.fid) + " mods=" + std::to_string(item.mods.size()) + " color=" + (item.color ? std::to_string(*item.color) : "none"));
    }
    if (!WriteSlot(slot, sex, items)) return 0;
    WriteIndex();
    SetActiveSlotForActor(t, slot);
    return static_cast<int>(items.size());
}

bool OM_BeginSaveOutfit(std::monostate, int slot, int gender) {
    EnsureCacheLoaded();
    if (slot < 1 || slot > MAX_SLOTS || gender < 0) return false;
    std::lock_guard<std::mutex> lk(g_mx);
    auto& draft = g_saveDrafts[slot];
    draft.gender = gender;
    draft.items.clear();
    return true;
}

bool OM_AddSaveOutfitItem(std::monostate, int slot, RE::TESForm* item) {
    EnsureCacheLoaded();
    if (slot < 1 || slot > MAX_SLOTS || !IsManagedOutfitArmor(item)) return false;
    std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_saveDrafts.find(slot);
    if (it == g_saveDrafts.end()) return false;
    AddOutfitEntry(it->second.items, item);
    return true;
}

int OM_CommitSaveOutfit(std::monostate, int slot) {
    EnsureCacheLoaded();
    if (slot < 1 || slot > MAX_SLOTS) return 0;

    SaveDraft draft;
    {
        std::lock_guard<std::mutex> lk(g_mx);
        auto it = g_saveDrafts.find(slot);
        if (it == g_saveDrafts.end()) return 0;
        draft = it->second;
        g_saveDrafts.erase(it);
    }

    if (draft.gender < 0 || draft.items.empty()) return 0;
    if (!WriteSlot(slot, draft.gender, draft.items)) return 0;
    WriteIndex();
    return static_cast<int>(draft.items.size());
}

int OM_LoadOutfit(std::monostate, int slot, RE::Actor* t) {
    EnsureCacheLoaded();
    if (!t) return 0;
    int sg = -1; std::vector<OE> items; std::string sum;
    if (!ReadSlot(slot, sg, items, sum)) return -1;
    int ts = GetSex(t);
    if (ts >= 0 && sg >= 0 && ts != sg) return -2;
    int resolvedCount = 0;
    for (auto& item : items) {
        auto* form = RE::TESForm::GetFormByID(item.fid);
        if (IsManagedOutfitArmor(form)) {
            ++resolvedCount;
            if (resolvedCount >= 44) break;
        }
    }
    return resolvedCount > 0 ? resolvedCount : -1;
}

RE::TESForm* OM_GetSavedOutfitItem(std::monostate, int s, int idx) {
    (void)s;
    (void)idx;
    return nullptr;
}
int OM_GetSavedOutfitItemID(std::monostate, int s, int idx) {
    EnsureCacheLoaded();
    int g = -1; std::vector<OE> items; std::string sum;
    if (!ReadSlot(s, g, items, sum)) return 0;
    if (idx < 0) return 0;
    int resolvedIndex = 0;
    for (auto& item : items) {
        auto* form = RE::TESForm::GetFormByID(item.fid);
        if (!IsManagedOutfitArmor(form)) continue;
        if (resolvedIndex == idx) return static_cast<int>(form->GetFormID());
        ++resolvedIndex;
    }
    return 0;
}
RE::BSFixedString OM_GetSavedOutfitDebug(std::monostate, int s) {
    EnsureCacheLoaded();
    int g = -1; std::vector<OE> items; std::string sum;
    if (!ReadSlot(s, g, items, sum)) return "(Empty)";
    std::string r = "Slot " + std::to_string(s) + " G:" + std::to_string(g) + "\n";
    for (size_t i = 0; i < items.size(); ++i) {
        char b[32]; snprintf(b, 32, "%08X", items[i].fid);
        r += "  [" + std::to_string(i) + "] " + items[i].name + " (0x" + b + ")\n";
    } return r.c_str();
}
RE::BSFixedString OM_GetSavedOutfitSummary(std::monostate, int s) { EnsureCacheLoaded(); return Summ(s).c_str(); }
bool OM_ClearSlotData(std::monostate, int s) {
    EnsureCacheLoaded();
    const bool ok = DelSlot(s);
    if (ok) {
        WriteIndex();
        ClearActiveSlotReferences(s);
    }
    if (g_menuOpen) SendUiResult("clear", ok, s, ok ? "Managed slot cleared" : "Failed to clear managed slot");
    return ok;
}

bool OM_EnsureDefaultOutfitRecorded(std::monostate, RE::Actor* t) {
    EnsureCacheLoaded();
    if (!t) return false;
    std::lock_guard<std::mutex> lk(g_mx);
    auto& mi = g_mi[t->GetFormID()];
    if (mi.dr) return true;
    auto equipped = CollectEquippedArmor(t);
    for (auto& item : equipped) {
        uint32_t fid = item.fid;
        bool dup = false;
        for (auto id : mi.d) { if (id == fid) { dup = true; break; } }
        if (!dup) mi.d.push_back(fid);
    }
    mi.dr = true; return true;
}
bool OM_RecordManagedItem(std::monostate, RE::Actor* t, RE::TESForm* item) {
    EnsureCacheLoaded();
    if (!t || !item) return false;
    std::lock_guard<std::mutex> lk(g_mx); auto& mi = g_mi[t->GetFormID()];
    uint32_t iid = item->GetFormID();
    for (auto id : mi.m) if (id == iid) return false;
    mi.m.push_back(iid); WriteActors(); return true;
}
int OM_GetManagedItemCount(std::monostate, RE::Actor* t) {
    EnsureCacheLoaded();
    if (!t) return 0; std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_mi.find(t->GetFormID()); return it == g_mi.end() ? 0 : (int)it->second.m.size();
}
RE::TESForm* OM_GetManagedItem(std::monostate, RE::Actor* t, int idx) {
    (void)t;
    (void)idx;
    return nullptr;
}
int OM_GetManagedItemID(std::monostate, RE::Actor* t, int idx) {
    EnsureCacheLoaded();
    if (!t) return 0; std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_mi.find(t->GetFormID()); if (it == g_mi.end()) return 0;
    if (idx < 0 || idx >= (int)it->second.m.size()) return 0;
    return static_cast<int>(it->second.m[idx]);
}
void OM_ClearManagedItems(std::monostate, RE::Actor* t) { EnsureCacheLoaded(); if (t) { std::lock_guard<std::mutex> lk(g_mx); g_mi.erase(t->GetFormID()); WriteActors(); } }
int OM_GetDefaultOutfitCount(std::monostate, RE::Actor* t) {
    EnsureCacheLoaded();
    if (!t) return 0; std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_mi.find(t->GetFormID()); return it == g_mi.end() ? 0 : (int)it->second.d.size();
}
RE::TESForm* OM_GetDefaultOutfitItem(std::monostate, RE::Actor* t, int idx) {
    (void)t;
    (void)idx;
    return nullptr;
}
int OM_GetDefaultOutfitItemID(std::monostate, RE::Actor* t, int idx) {
    EnsureCacheLoaded();
    if (!t) return 0; std::lock_guard<std::mutex> lk(g_mx);
    auto it = g_mi.find(t->GetFormID()); if (it == g_mi.end()) return 0;
    if (idx < 0 || idx >= (int)it->second.d.size()) return 0;
    return static_cast<int>(it->second.d[idx]);
}
RE::BSFixedString OM_DebugGetInventoryInfo(std::monostate, RE::Actor* t) {
    if (!t) return "No target";
    std::string r = "Inv: " + GetName(t) + "\n";
    auto* inv = t->inventoryList;
    if (!inv) return (r + "(no inv)").c_str();
    for (auto& item : inv->data) {
        auto* form = item.object;
        if (IsManagedOutfitArmor(form)) {
            bool equipped = false;
            for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
                if (stack->IsEquipped()) { equipped = true; break; }
            }
            if (equipped) {
                auto* ar = form->As<RE::TESObjectARMO>();
                auto* fn = ar ? ar->GetFullName() : nullptr;
                r += "  " + std::string(fn ? fn : "?") + "\n";
            }
        }
    }
    return r.c_str();
}
void OM_RefreshActorModel(std::monostate, RE::Actor*) { /* No-op */ }

// ======== Registration ========
bool RegisterPapyrus(RE::BSScript::IVirtualMachine* vm) {
    if (!vm) return false;
    #define B(name) vm->BindNativeMethod("OMNative", #name, OM_##name)
    B(GetPluginVersion); B(GetSlotPath); B(GetActorName); B(LogText);
    B(GetSavedOutfitDebug); B(GetSavedOutfitSummary); B(GetSavedOutfitName); B(DebugGetInventoryInfo);
    B(IsMenuAvailable); B(PreparePlayerPreview); B(OpenMenu); B(OpenQuickSaveMenu); B(OpenQuickOutfitMenu); B(OpenStudioMenu); B(CloseMenu); B(IsMenuOpen);
    B(GetMenuOpenCooldownRemaining); B(BeginMenuOpen);
    B(GetMenuActionCooldownRemaining); B(BeginMenuAction);
    B(IsValidTarget); B(IsValidOutfitTarget); B(StartQuickTargetEffect); B(RestoreQuickTargetEffect); B(IsOutfitItem); B(IsPowerArmorItem); B(AreOutfitNotificationsEnabled); B(IsEquipBusy);
    B(GetEquipCooldownRemaining); B(BeginEquipAction);
    B(IsRandomBusy); B(GetRandomCooldownRemaining); B(BeginRandom);
    B(HasSlotData); B(ClearSlotData); B(EnsureDefaultOutfitRecorded);
    B(ResetOutfitUsageCounts);
    B(RecordManagedItem); B(GetMenuAction); B(GetMenuActionSlot);
    B(GetCurrentSlot); B(GetActiveOutfitSlot); B(FindFirstEmptyOutfitSlot);
    B(GetLastRandomSlot); B(GetLastEquippedSlot); B(ChooseRandomSlot);
    B(FindMatchingSlot); B(GetSlotGender); B(GetSavedOutfitItemID); B(GetManagedItemCount);
    B(GetDefaultOutfitCount); B(SaveOutfit); B(BeginSaveOutfit);
    B(AddSaveOutfitItem); B(CommitSaveOutfit); B(LoadOutfit);
    B(GetMenuTarget); B(GetMenuActionTarget); B(GetMenuActionTargetRef); B(GetCameraTarget);
    B(HasMenuActionTarget); B(GetMenuActionTargetSex); B(GetMenuActionTargetName);
    B(IsMenuActionTargetPowerArmorBlocked); B(IsMenuActionTargetPlayerTeammate); B(SaveMenuActionTargetOutfit);
    B(LoadMenuActionTargetOutfit); B(IsMenuActionTargetWearingOutfit); B(EquipMenuActionTargetOutfit);
    B(ResetMenuActionTargetOutfit);
    B(IsWearingOutfitNative); B(EquipOutfitNative);
    B(GetManagedItemID); B(GetDefaultOutfitItemID);
    B(ClearMenuTarget); B(ClearMenuAction); B(SetCurrentSlot); B(SetLastEquippedSlot);
    B(FinishEquipAction); B(FinishRandom); B(ClearManagedItems);
    B(RefreshActorModel);
    #undef B
    return true;
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_intfc) {
    F4SE::Init(a_intfc, { .log = false, .hook = false });
    LogLine("2.0 plugin load begin");
    if (auto* messaging = F4SE::GetMessagingInterface()) {
        messaging->RegisterListener(F4SEMessageHandler);
    }
    if (auto* papyrus = F4SE::GetPapyrusInterface()) {
        papyrus->Register(RegisterPapyrus);
    }
    if (auto* serialization = F4SE::GetSerializationInterface()) {
        serialization->SetUniqueID(MakeRecordType('A', 'O', 'M', '2'));
        serialization->SetSaveCallback(SaveActorState);
        serialization->SetLoadCallback(LoadActorState);
        serialization->SetRevertCallback(RevertActorState);
    }
    std::error_code ec;
    fs::create_directories(PrimaryDataRoot(), ec);
    LoadState();
    RegisterInputSink();
    LogLine("2.0 plugin load complete");
    return true;
}
OUTFITMANAGER_EXPORT bool F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info) {
    a_info->infoVersion = F4SE::PluginInfo::kVersion;
    a_info->name = "OutfitManager"; a_info->version = 1;
    if (a_f4se->IsEditor()) return false;
    return a_f4se->RuntimeVersion() >= F4SE::RUNTIME_1_10_162;
}
