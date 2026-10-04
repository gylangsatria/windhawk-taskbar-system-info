#include "regression-fakes.h"
#include "../taskbar-system-info.wh.cpp"
#undef PdhOpenQueryW
#undef PdhCloseQuery
#undef PdhAddEnglishCounterW
#undef PdhRemoveCounter
#undef PdhCollectQueryData
#undef PdhGetFormattedCounterValue
#undef PdhGetFormattedCounterArrayW
#undef OpenFileMappingW
#undef OpenMutexW
#undef WaitForSingleObject
#undef MapViewOfFile
#undef VirtualQuery
#undef UnmapViewOfFile
#undef ReleaseMutex
#undef CloseHandle
#undef RegOpenKeyExW
#undef RegCloseKey
#undef RegQueryValueExW
#undef RegEnumValueW
#include <iostream>
#include <limits>
#include <stdexcept>
#include <clocale>

namespace {
int checks = 0;
void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) throw std::runtime_error(description);
}
bool Near(double a, double b) { return std::abs(a - b) < 0.001; }

void HistoryAndScheduling() {
    using namespace std::chrono;
    std::deque<HistorySample> history;
    SampleTime t{};
    for (int i = 0; i <= 6; ++i) {
        ApplyHistorySample(history, i != 3, i * 10.0, t + seconds(i), 60);
    }
    auto runs = BuildSparklineRuns(history, 60, 1, 120, 12);
    Check(history.size() == 7, "a missing sample must not erase history");
    Check(runs.size() == 2 && runs[0].size() == 3 && runs[1].size() == 3,
          "a missing sample must split graph geometry");
    Check(Near(runs[0][0].x, 108) && Near(runs[1].back().x, 120),
          "x coordinates must represent elapsed seconds");
    ApplyHistorySample(history, true, 25, t + seconds(20), 60);
    ApplyHistorySample(history, true, 30, t + seconds(21), 60);
    Check(BuildSparklineRuns(history, 60, 1, 120, 12).size() == 3,
          "collector stalls must produce gaps even with no explicit missing sample");
    ApplyHistorySample(history, false, 0, t + seconds(68), 60);
    Check(history.front().time == t + seconds(20), "history must expire by timestamp");
    ApplyHistorySample(history, true, std::numeric_limits<double>::quiet_NaN(),
                       t + seconds(69), 60);
    Check(!history.back().value, "NaN must create a gap");
    Check(AdvanceSampleDeadline(t + seconds(1), t + milliseconds(1250), seconds(1)) ==
              t + seconds(2), "collection time must not accumulate as timer drift");
    Check(AdvanceSampleDeadline(t + seconds(1), t + milliseconds(3250), seconds(1)) ==
              t + seconds(4), "slow collects must skip missed deadlines without a burst");
    Check(UiTimerInterval(true, true, 10) == seconds(10), "UI watchdog must scale");
    Check(UiTimerInterval(true, false, 10) == milliseconds(250), "startup stays responsive");
    Check(UiTimerInterval(false, false, 10) == seconds(1), "failed placement still retries");
    Check(Near(WidgetHeightForTaskbar(30), 30), "short taskbar must fit");
    Check(Near(WidgetHeightForTaskbar(48), 38), "normal taskbar must preserve size");
    Check(Near(WidgetHeightForTaskbar(0), 38), "unmeasured layout needs a sane fallback");
    Check(Near(WidgetHeightForTaskbar(std::numeric_limits<double>::quiet_NaN()), 38),
          "invalid layout measurement must not propagate");
    Check(FormatCapacity(0.4, 0.5, true) == L"0.4/0.5G", "512 MiB capacity must not show zero");
    const char* locale = std::setlocale(LC_NUMERIC, nullptr);
    std::string originalLocale = locale ? locale : "C";
    // Windows locale name; if unavailable, the ordinary checks still run.
    std::setlocale(LC_NUMERIC, "Ukrainian_Ukraine.1251");
    Check(FormatFixed(12.75, 1) == L"12.8", "display decimal separator must be stable");
    Check(Near(*ParseLocalizedDouble(L"12,75"), 12.75), "registry comma decimal");
    Check(Near(*ParseLocalizedDouble(L"12.75"), 12.75), "registry dot decimal");
    Check(!ParseLocalizedDouble(L"NaN"), "reject non-finite temperatures");
    Check(!ParseLocalizedDouble(L""), "reject empty temperatures");
    std::setlocale(LC_NUMERIC, originalLocale.c_str());

    GpuAdapterInfo ambiguous{L"AMD Radeon HD 6450", {}, {}, 512ull * 1024 * 1024,
                             8ull * 1024 * 1024 * 1024, false};
    ModSettings settings;
    Check(UseSharedGpuMemory(ambiguous, settings), "retain documented memory-shape auto mode");
    settings.gpuMemoryMode = GpuMemoryMode::Dedicated;
    Check(!UseSharedGpuMemory(ambiguous, settings),
          "explicit dedicated override must resolve ambiguous legacy 512 MiB GPUs");
}

void AdaptivePlacement() {
    ModSettings settings; settings.width = 410;
    TaskbarGeometry geometry{1920, 48, {{0, 300, true}, {1800, 1920, false}}, true};
    auto p = ResolveTaskbarPlacement(settings, geometry, {2000, 0});
    Check(Near(p.left, 1384) && Near(p.width, 410) && Near(p.reserved, 0), "prefer the far-right free slot with breathing room before tray");
    settings.reserveSpace = true;
    p = ResolveTaskbarPlacement(settings, geometry, {0, 0});
    Check(Near(p.left, 6) && Near(p.width, 410) && Near(p.reserved, 424), "reserve before actual buttons with an inset at the panel edge");
    Check(PlacementFits(geometry, p), "reservation must leave the widget and shifted buttons separate");
    p = ResolveTaskbarPlacement(settings, geometry, {2000, 0});
    Check(Near(p.left, 1384) && Near(p.reserved, 0), "reservation need not move buttons when a nearer gap exists");
    settings.reserveSpace = false;
    geometry.occupied = {{0, 100}, {550, 700}, {1150, 1920}};
    p = ResolveTaskbarPlacement(settings, geometry, {600, 0});
    Check(Near(p.left, 706) && Near(p.width, 410), "choose nearest full-width gap between independent elements with clearance");
    geometry = {800, 48, {{0, 100}, {472, 800}}, true};
    p = ResolveTaskbarPlacement(settings, geometry, {200, 200});
    Check(Near(p.left, 106) && Near(p.width, 360), "shrink only when no full size slot exists, preserving side clearance");
    geometry.occupied[1].left = 452;
    Check(ResolveTaskbarPlacement(settings, geometry, {0, 0}).width == 0, "hide below 85 percent scale");
    geometry.occupied[1].left = 472; settings.fontSize = 9;
    Check(ResolveTaskbarPlacement(settings, geometry, {0, 0}).width == 0, "nine-DIP text must not become smaller");
    settings.fontSize = 11; geometry.height = 30;
    Check(ResolveTaskbarPlacement(settings, geometry, {0, 0}).width == 0, "respect readability when height constrains scale");
    geometry.height = 48; geometry.ready = false;
    Check(ResolveTaskbarPlacement(settings, geometry, {0, 0}).width == 0, "unknown layout is not a free slot");
    geometry = {1212, 48, {{0, 100}, {522, 690}, {1112, 1212}}, true};
    p = ResolveTaskbarPlacement(settings, geometry, {401, 696});
    Check(Near(p.left, 696), "equal-distance choices preserve the preceding slot");
    Check(!PlacementFits(geometry, {100, 410, 0}) && PlacementFits(geometry, {106, 410, 0}),
          "touching a button is invalid while a six-DIP gap is accepted");
    Check(!PlacementFits({500, 48, {}, true}, {0, 410, 0}) &&
          PlacementFits({500, 48, {}, true}, {6, 410, 0}),
          "panel edges keep the same minimum clearance without any mapped controls");
    auto free = FreeTaskbarIntervals(1000, {{100, 300}, {250, 400}, {-30, 10}, {900, 1200}});
    Check(free.size() == 2 && Near(free[0].left, 10) && Near(free[0].right, 100) &&
          Near(free[1].left, 400) && Near(free[1].right, 900), "merge overlaps and clip to the panel");
    for (double physical : {1280., 1920., 2560., 3840., 5120.}) {
        for (double scale : {1., 1.25, 1.5, 2.}) {
            for (bool reserve : {false, true}) {
                settings.reserveSpace = reserve;
                double width = physical / scale;
                geometry = {width, 48, {{20, 300, true}, {width - 180, width}}, true};
                p = ResolveTaskbarPlacement(settings, geometry, {2000, 0});
                Check(p.width == 0 || (PlacementFits(geometry, p) && p.width >= 348.5),
                      "DPI matrix must preserve mapped elements and readability");
            }
        }
    }
    geometry = {1920, 48, {{0, 300, true}, {1800, 1920}}, true};
    settings.reserveSpace = true;
    p = ResolveTaskbarPlacement(settings, geometry, {0, 0}, false);
    Check(p.reserved == 0 && Near(p.left, 306), "rejected reservation falls back to an existing gap");
    Check(!PlacementFits(geometry, {0, 410, 0}), "independent intersection check detects a button overlap");
    TaskbarGeometry arranged{1920, 48, {{1450, 1850, true}, {1800, 1920}}, true};
    Check(PlacementFits(arranged, {6, 410, 0}) && !ReservedControlsFit(arranged),
          "arranged buttons touching the tray reject reservation even when the widget itself is clear");
    arranged.occupied[0] = {1418, 1718, true};
    Check(ReservedControlsFit(arranged), "actual reservation keeps all moved buttons within their own gap");
    arranged.occupied[0] = {1900, 2100, true};
    Check(PlacementFits(arranged, {6, 410, 0}) && !ReservedControlsFit(arranged),
          "unreserved clipped buttons do not hide a free widget but cannot be reserved beyond the panel");
    settings.reserveGap = 0;
    p = ResolveTaskbarPlacement(settings, geometry, {0, 0});
    Check(Near(p.left, 6) && Near(p.reserved, 422),
          "a zero configured reservation gap still retains six DIP visual clearance");
}

void MovePreferences() {
    TaskbarProjection projection;
    projection.origin = {-1920, -900}; projection.scale = 1.5;
    Check(Near(ScreenPointToPreferredLeft(projection, {-1125, -850}, .25, 410), 427.5),
          "negative display origins and 150 percent DPI convert physical cursor coordinates");
    projection.origin = {2560, 0}; projection.scale = 2;
    Check(Near(ScreenPointToPreferredLeft(projection, {3560, 20}, .5, 410), 295),
          "200 percent destination uses its own logical coordinate scale");
    MoveEditorState editor;
    editor.visual.ready = true; editor.visual.width = 410;
    editor.target.origin = {-1920, -1080}; editor.target.scale = 1.5;
    editor.target.geometry = {1280, 48, {}, true}; editor.candidate = {100, 410, 0};
    auto preview = ResolveMovePreviewLayout(editor, {-1920, -1080, 0, 0});
    Check(preview.bounds.left + preview.content.left == -1770 && preview.content.right - preview.content.left == 615,
          "glass padding preserves actual widget coordinates at a negative-origin 150 percent display");
    Check(preview.bounds.top == -1078 && preview.bounds.bottom == -1011 &&
          preview.bounds.right - preview.bounds.left == 615 && preview.body.top == 0,
          "top taskbar preview contains only the widget with no instruction surface");
    editor.target.origin = {0, 1032}; editor.target.scale = 1;
    editor.target.geometry = {1920, 48, {}, true}; editor.candidate.left = 300;
    preview = ResolveMovePreviewLayout(editor, {0, 0, 1920, 1080});
    Check(preview.bounds.top == 1034 && preview.bounds.bottom == 1078 &&
          preview.bounds.top + preview.content.top == 1037,
          "bottom taskbar preview stays within the panel without moving the proposed widget");
    editor.candidate.width = 348.5;
    preview = ResolveMovePreviewLayout(editor, {0, 0, 1920, 1080});
    Check(Near(preview.contentScale, .85) && preview.content.bottom - preview.content.top == 32,
          "live preview follows the accepted readable widget scale");
    auto hotkey = ParseMoveHotkey(L"Ctrl+Alt+M");
    Check(hotkey && hotkey->key == 'M' && hotkey->modifiers == (MOD_CONTROL | MOD_ALT), "default move combination");
    Check(ParseMoveHotkey(L" shift + WIN + f24 ")->key == VK_F24, "case and whitespace in hotkeys");
    Check(ParseMoveHotkey(L"")->key == 0, "empty disables the shortcut");
    for (auto invalid : {L"Ctrl+Ctrl+M", L"Ctrl+", L"A+B", L"F25", L"Ctrl+Mouse1", L"Ctrl", L"+M", L"F01", L"F 2", L"F-2"}) {
        std::wstring reason;
        Check(!ParseMoveHotkey(invalid, &reason) && !reason.empty(), "malformed hotkeys have a specific rejection reason");
    }
    PlacementProfiles profiles{2, 2000, L"display-b", {{L"display-a", .25}, {L"display-b", .75}}};
    auto saved = SerializeProfiles(profiles); auto parsed = ParseProfiles(saved);
    Check(parsed && parsed->target == L"display-b" && parsed->monitor == 2 && parsed->offset == 2000 &&
          Near(parsed->positions.at(L"display-a"), .25), "per-display positions survive serialization");
    Check(SerializeProfiles(*parsed) == saved, "profile serialization is deterministic");
    for (auto invalid : {L"", L"2\n1 10\n\n", L"1\n33 10\n\n", L"1\n1 -10\n\n", L"1\n1 10\nx\n",
                         L"1\n1 10\n\nx\tNaN\n", L"1\n1 10\n\nx\t1.1\n", L"1\n1 10\n\nx\t.1\nx\t.2\n",
                         L"1\n1 \n\n", L"1\n1 2147483648\n\n", L"1\n1  10\n\n", L"1\n1 10junk\n\n"})
        Check(!ParseProfiles(invalid), "corrupt saved data must not become a placement");
    ModSettings settings; settings.monitor = 2; settings.leftOffset = 2000;
    auto reconciled = profiles;
    settings.fontSize = 16; settings.width = 600;
    Check(!ReconcileProfiles(reconciled, settings, L"display-b") && SerializeProfiles(reconciled) == saved,
          "appearance settings preserve target and every display preference");
    settings.monitor = 1;
    Check(ReconcileProfiles(reconciled, settings, L"display-a") && reconciled.target.empty() && reconciled.positions.size() == 2,
          "manual monitor change releases dragged target and retains per-display positions");
    settings.leftOffset = 20;
    Check(ReconcileProfiles(reconciled, settings, L"display-a") && !reconciled.positions.contains(L"display-a") &&
          Near(reconciled.positions.at(L"display-b"), .75), "manual offset change clears only the current display position");
    auto confirmed = ConfirmPlacementPreference(profiles, settings, L"display-c", 700, 2000, false);
    Check(confirmed && confirmed->target == L"display-c" && Near(confirmed->positions.at(L"display-c"), .5) &&
          confirmed->positions.size() == 3, "confirmed move saves a fraction of full-width logical travel");
    auto reset = ConfirmPlacementPreference(profiles, settings, L"", 0, 2000, true);
    Check(reset && reset->target.empty() && reset->positions.empty() && reset->monitor == 1 && reset->offset == 20,
          "confirmed Home clears all drag preferences and uses current manual settings");
    Check(profiles.target == L"display-b" && profiles.positions.size() == 2,
          "drafting a move or reset leaves the preceding saved preference intact");
    Check(!ConfirmPlacementPreference(profiles, settings, L"", 0, 2000, false), "display without stable identity cannot be saved");
    auto limited = profiles;
    for (int i = 0; limited.positions.size() < 32; ++i) limited.positions[L"screen-" + std::to_wstring(i)] = .5;
    Check(!ConfirmPlacementPreference(limited, settings, L"new-screen", 0, 2000, false) &&
          ConfirmPlacementPreference(limited, settings, L"display-b", 0, 2000, false).has_value(),
          "profile bound rejects only new displays and still allows updating an existing one");
    SetPlacementProfiles(profiles);
    Check(PlacementProfilesSnapshot().target == L"display-b", "profile snapshots are copied safely");
    for (int failure = 0; failure < 5; ++failure) {
        fake::localSaveFails = false; SavePlacementProfiles(profiles);
        int applied = 0, verified = 0, restored = 0;
        auto epoch = g_moveEpoch.load();
        fake::localSaveFails = failure == 2;
        bool success = CompleteMoveTransaction(profiles, *confirmed, epoch, [&] {
            ++applied;
            if (failure == 3) CancelMoveEditor();
            if (failure == 4) throw std::runtime_error("injected transfer exception");
            return failure != 0;
        }, [&] { ++verified; return failure != 1; }, [&] { ++restored; return true; });
        Check(!success && applied == 1 && restored == 1 && !g_moveCommitting &&
              PlacementProfilesSnapshot().target == profiles.target &&
              ParseProfiles(fake::localStorage[L"placement.v1"])->target == profiles.target,
              "failed transfer, layout, storage, cancellation or exception restores preference and releases commit state");
        Check(verified == (failure == 0 || failure == 4 ? 0 : 1), "unapplied or throwing transfers are never verified or saved");
    }
    fake::localSaveFails = false; int restored = 0;
    Check(CompleteMoveTransaction(profiles, *confirmed, g_moveEpoch.load(), [] { return true; },
          [] { return true; }, [&] { ++restored; return true; }) && !restored && !g_moveCommitting &&
          ParseProfiles(fake::localStorage[L"placement.v1"])->target == L"display-c",
          "successful transfer and verification persist the confirmed target without rollback");
    Check(CompleteMoveTransaction(*confirmed, *reset, g_moveEpoch.load(), [] { return true; },
          [] { return true; }, [] { return true; }) && PlacementProfilesSnapshot().positions.empty() &&
          ParseProfiles(fake::localStorage[L"placement.v1"])->target.empty(), "confirmed Home reset is persisted only after successful placement");
    fake::localStorage.clear();
    SetPlacementProfiles({});
}

void SetMapping(std::vector<HwInfoSensorPrefix> sensors,
                std::vector<HwInfoReadingPrefix> readings) {
    HwInfoHeader header{};
    header.signature = kHwInfoSignature;
    header.version = 2;
    header.sensorOffset = sizeof(header);
    header.sensorStride = sizeof(HwInfoSensorPrefix);
    header.sensorCount = static_cast<uint32_t>(sensors.size());
    header.readingOffset = header.sensorOffset + header.sensorStride * header.sensorCount;
    header.readingStride = sizeof(HwInfoReadingPrefix);
    header.readingCount = static_cast<uint32_t>(readings.size());
    fake::mappingOffset = 16;
    fake::mapping.resize(fake::mappingOffset + header.readingOffset +
                         header.readingStride * header.readingCount);
    BYTE* bytes = fake::mapping.data() + fake::mappingOffset;
    std::memcpy(bytes, &header, sizeof(header));
    std::memcpy(bytes + header.sensorOffset, sensors.data(), sensors.size() * sizeof(sensors[0]));
    std::memcpy(bytes + header.readingOffset, readings.data(), readings.size() * sizeof(readings[0]));
}
HwInfoSensorPrefix Sensor(uint32_t id, const char* name) {
    HwInfoSensorPrefix sensor{};
    sensor.sensorId = id;
    std::strcpy(sensor.originalName, name);
    return sensor;
}
HwInfoReadingPrefix Reading(uint32_t sensor, uint32_t id, const char* label, double value) {
    HwInfoReadingPrefix reading{};
    reading.readingType = kHwInfoTemperatureType;
    reading.sensorIndex = sensor;
    reading.readingId = id;
    reading.value = value;
    std::strcpy(reading.originalLabel, label);
    std::strcpy(reading.unit, "C");
    return reading;
}
MetricsSnapshot SharedSnapshot() {
    MetricsSnapshot snapshot;
    HwInfoTemperatureDiagnostics diagnostics;
    ReadHwInfoSharedMemory(snapshot, ModSettings{}, std::nullopt, diagnostics);
    return snapshot;
}
void SensorIdentity() {
    g_hwInfoSharedMemoryCache = {};
    auto sensors = std::vector{Sensor(1, "CPU"), Sensor(2, "GPU")};
    auto package = Reading(0, 101, "CPU Package", 72);
    auto core = Reading(0, 102, "Core 3", 42);
    auto gpu = Reading(1, 201, "GPU Temperature", 55);
    SetMapping(sensors, {package, core, gpu});
    Check(SharedSnapshot().cpuTemp == 72, "initial shared-memory discovery");
    auto deadline = g_hwInfoSharedMemoryCache.nextFullScan;
    SetMapping(sensors, {core, package, gpu});
    Check(SharedSnapshot().cpuTemp == 72, "reordered valid readings must not substitute a core");
    Check(g_hwInfoSharedMemoryCache.cpuReadingIndex == 1,
          "identity mismatch must rescan in the same sample");
    Check(deadline > SampleTime::clock::now(), "test must exercise cache, not scheduled rescan");

    sensors[0].sensorInstance = 7;
    SetMapping(sensors, {core, package, gpu});
    Check(SharedSnapshot().cpuTemp == 72 &&
          g_hwInfoSharedMemoryCache.cpuIdentity->sensorInstance == 7,
          "sensor-instance changes must refresh identity");
    fake::mutexTimeout = true;
    Check(!SharedSnapshot().cpuTemp, "timeout must not display a stale temperature as live");
    fake::mutexTimeout = false;
    auto* header = reinterpret_cast<HwInfoHeader*>(fake::mapping.data() + fake::mappingOffset);
    header->readingOffset = static_cast<uint32_t>(fake::mapping.size() - 8);
    Check(!SharedSnapshot().cpuTemp && g_hwInfoLayoutRejectedLogged,
          "mapped-size bounds failure must reject layout and latch diagnostic");
    SetMapping(sensors, {core, package, gpu});
    Check(SharedSnapshot().cpuTemp == 72 && !g_hwInfoLayoutRejectedLogged,
          "a valid layout must recover and reset the diagnostic");
    unsigned fast = 0;
    for (int i = 0; i < 4; ++i) {
        Check(HwInfoRescanDelay(false, fast, std::chrono::seconds(60)) ==
                  std::chrono::seconds(5), "partial discovery window");
    }
    Check(HwInfoRescanDelay(false, fast, std::chrono::seconds(60)) ==
              std::chrono::seconds(60), "partial rescans must back off");
    HwInfoRescanDelay(true, fast, std::chrono::seconds(60));
    Check(fast == 0, "completed discovery must reset the short retry window");

    auto populateRegistry = [](bool swapped) {
        fake::registry.clear();
        for (int i = 0; i < 3; ++i) {
            int record = i < 2 && swapped ? 1 - i : i;
            auto suffix = std::to_wstring(i);
            fake::registry[L"Sensor" + suffix] = record == 2 ? L"GPU" : L"CPU";
            fake::registry[L"Label" + suffix] =
                record == 2 ? L"GPU Temperature" : record ? L"Core 3" : L"CPU Package";
            fake::registry[L"ValueRaw" + suffix] = record == 2 ? L"55" : record ? L"42" : L"72";
            fake::registry[L"Value" + suffix] = L"72 \u00B0C";
        }
    };
    auto registrySnapshot = [] {
        MetricsSnapshot snapshot;
        HwInfoTemperatureDiagnostics diagnostics;
        ReadHwInfoGadgetRegistry(snapshot, ModSettings{}, std::nullopt, diagnostics);
        return snapshot;
    };
    g_hwInfoGadgetRegistryCache = {};
    populateRegistry(false);
    Check(registrySnapshot().cpuTemp == 72, "registry discovery");
    populateRegistry(true);
    Check(registrySnapshot().cpuTemp == 72 && g_hwInfoGadgetRegistryCache.cpuIndex == 1,
          "registry Sensor/Label reorder must rescan immediately");
    for (const auto& prefix : {L"Sensor", L"Label", L"ValueRaw", L"Value"}) {
        fake::registry[std::wstring(prefix) + L"7001"] = fake::registry[std::wstring(prefix) + L"1"];
        fake::registry.erase(std::wstring(prefix) + L"1");
    }
    Check(registrySnapshot().cpuTemp == 72 && g_hwInfoGadgetRegistryCache.cpuIndex == 7001,
          "sparse registry indices beyond the old scan cap must be discovered");
    fake::mapping.clear();
    fake::registry.clear();
}

GpuAdapterInfo TestAdapter() {
    return {L"Test GPU", L"0x00000000_0x00000042", {0x42, 0},
            4ull * 1024 * 1024 * 1024, 8ull * 1024 * 1024 * 1024, false};
}
void CacheAdapter() {
    g_cachedGpuAdapterInfo = TestAdapter();
    g_cachedGpuAdapterFilter = L"";
    g_cachedGpuAdapterResolved = true;
    g_nextGpuAdapterResolve = SampleTime::clock::now() + std::chrono::hours(1);
}
void ResetPdh() {
    CloseMetricSources();
    fake::queries.clear();
    fake::counters.clear();
    fake::stale = nullptr;
    fake::freshMemory = true;
    fake::memoryAvailable = true;
    fake::enginesAvailable = true;
    fake::hardArrays = false;
    fake::invalidEngine = false;
    fake::growArrayAttempts = 0;
    fake::opens = 0;
    CacheAdapter();
}
MetricsSnapshot PdhSnapshot() {
    MetricsSnapshot snapshot;
    snapshot.cpu = 10; snapshot.cpuAvailable = true;
    snapshot.ram = 50; snapshot.ramAvailable = true;
    ReadPdhMetrics(snapshot, ModSettings{});
    return snapshot;
}
void PdhRecovery() {
    ResetPdh();
    auto first = PdhSnapshot();
    Check(first.cpuAvailable && first.ramAvailable && !first.gpuAvailable,
          "GPU priming must retain CPU/RAM without publishing a bogus GPU rate");
    auto* cpu = reinterpret_cast<fake::Query*>(g_cpuPdhQuery);
    auto* gpu = reinterpret_cast<fake::Query*>(g_pdhQuery);
    Check(cpu != gpu, "CPU and GPU queries must be independent");
    auto normal = PdhSnapshot();
    Check(normal.gpuAvailable && normal.vramAvailable, "healthy production PDH path");
    fake::invalidEngine = true;
    Check(!PdhSnapshot().gpuAvailable,
          "an invalid engine sample must not be disguised as idle");
    fake::invalidEngine = false;
    fake::stale = gpu;
    auto missing = PdhSnapshot();
    Check(!missing.gpuAvailable && !missing.vramAvailable, "empty stale arrays must not fake idle");
    Check(gpu->closed && !cpu->closed && !g_pdhQuery,
          "fresh memory probe must recover stale GPU without resetting CPU");
    MetricsSnapshot thermal;
    ReadWindowsThermalZones(thermal, ModSettings{});
    Check(thermal.cpuTemp && Near(*thermal.cpuTemp, 50),
          "CPU temperature must remain available during GPU recovery");
    g_nextPdhCounterRetry = {};
    CacheAdapter();
    Check(!PdhSnapshot().gpuAvailable, "rebuilt GPU query must prime before publishing");
    Check(PdhSnapshot().vramAvailable, "VRAM must recover on the next sample");

    ResetPdh();
    PdhSnapshot();
    gpu = reinterpret_cast<fake::Query*>(g_pdhQuery);
    fake::stale = gpu;
    fake::freshMemory = false;
    fake::hardArrays = true;
    PdhSnapshot(); PdhSnapshot();
    Check(g_consecutivePdhReadFailures == 2, "track consecutive hard errors");
    fake::hardArrays = false;
    int before = fake::opens;
    for (int i = 0; i < 50; ++i) PdhSnapshot();
    Check(!gpu->closed && fake::opens == before + 1,
          "a parked GPU must keep its query and throttle fresh probes");
    Check(g_consecutivePdhReadFailures == 0,
          "soft absence must break the consecutive-hard-error streak");
    fake::stale = nullptr;
    fake::enginesAvailable = false;
    auto idle = PdhSnapshot();
    Check(idle.gpuAvailable && idle.gpu == 0 && idle.vramAvailable,
          "healthy VRAM plus no engine instances may report idle");
    g_gpuAdapterIdentityChanged = true;
    PdhSnapshot();
    Check(gpu->closed, "confirmed LUID change must rebuild immediately");

    ResetPdh();
    PdhSnapshot();
    gpu = reinterpret_cast<fake::Query*>(g_pdhQuery);
    gpu->fail = true;
    PdhSnapshot(); PdhSnapshot();
    Check(!gpu->closed, "isolated hard errors must not churn queries");
    PdhSnapshot();
    Check(gpu->closed, "three hard errors must recover GPU");
    ResetPdh();
    PdhSnapshot();
    fake::growArrayAttempts = 2;
    PDH_STATUS status{};
    auto bytes = ReadVramUsedBytes(g_vramCounter, TestAdapter(), status);
    Check(bytes.has_value() && status == ERROR_SUCCESS,
          "growing wildcard arrays must retry safely");
    fake::growArrayAttempts = 20;
    ReadVramUsedBytes(g_vramCounter, TestAdapter(), status);
    Check(status == static_cast<PDH_STATUS>(PDH_MORE_DATA) &&
          !IsHardPdhArrayFailure(status),
          "buffer churn beyond the retry bound must not reset the query");
    CloseMetricSources();
}

int kmtOpens = 0;
int kmtCloses = 0;
int kmtQueries = 0;
int kmtEnumerations = 0;
LONG kmtQueryStatus = 0;
LONG kmtOpenStatus = 0;
bool kmtEmptyHandle = false;
ULONG kmtTemperature = 535;
GpuAdapterInfo kmtAdapter;
LONG WINAPI TestKmtEnumerate(D3DKMT_ENUMADAPTERS2* request) {
    ++kmtEnumerations;
    request->NumAdapters = 1;
    request->pAdapters[0] = {99, kmtAdapter.luidValue, 1, FALSE};
    return 0;
}
LONG WINAPI TestKmtOpen(D3DKMT_OPENADAPTERFROMLUID* request) {
    ++kmtOpens;
    request->hAdapter = kmtOpenStatus || kmtEmptyHandle
                           ? 0 : static_cast<D3DKMT_HANDLE>(kmtOpens);
    return kmtOpenStatus;
}
LONG WINAPI TestKmtClose(const D3DKMT_CLOSEADAPTER*) {
    ++kmtCloses;
    return 0;
}
LONG WINAPI TestKmtQuery(D3DKMT_QUERYADAPTERINFO* request) {
    if (request->Type == kAdapterRegistryInfoQueryType) {
        auto* info = static_cast<D3DKMT_ADAPTERREGISTRYINFO*>(request->pPrivateDriverData);
        std::wcscpy(info->AdapterString, L"Test GPU");
        return 0;
    }
    if (request->Type == kAdapterSegmentSizeQueryType) {
        auto* info = static_cast<D3DKMT_SEGMENTSIZEINFO*>(request->pPrivateDriverData);
        info->DedicatedVideoMemorySize = kmtAdapter.dedicatedVideoMemory;
        info->SharedSystemMemorySize = kmtAdapter.sharedSystemMemory;
        return 0;
    }
    if (request->Type == kAdapterTypeQueryType) {
        *static_cast<D3DKMT_ADAPTERTYPE*>(request->pPrivateDriverData) = {};
        return 0;
    }
    ++kmtQueries;
    if (kmtQueryStatus) return kmtQueryStatus;
    auto* data = static_cast<D3DKMT_ADAPTER_PERFDATA*>(request->pPrivateDriverData);
    data->Temperature = kmtTemperature;
    return 0;
}
void ResetNativeTemperature() {
    ResetPdh();
    kmtOpens = kmtCloses = kmtQueries = kmtEnumerations = 0;
    kmtQueryStatus = kmtOpenStatus = 0;
    kmtEmptyHandle = false;
    kmtTemperature = 535;
    kmtAdapter = TestAdapter();
    g_d3dkmtEnumAdapters2 = TestKmtEnumerate;
    g_d3dkmtOpenAdapterFromLuid = TestKmtOpen;
    g_d3dkmtCloseAdapter = TestKmtClose;
    g_d3dkmtQueryAdapterInfo = TestKmtQuery;
}
MetricsSnapshot NativeTemperatureSnapshot() {
    MetricsSnapshot snapshot;
    ReadWindowsGpuTemperature(snapshot, ModSettings{});
    return snapshot;
}
void CheckTemperatureRetryDelay(int seconds) {
    auto remaining = g_gpuTemperatureRetry.nextAttempt - SampleTime::clock::now();
    Check(remaining > std::chrono::seconds(seconds - 1) &&
              remaining <= std::chrono::seconds(seconds),
          "native temperature retry deadline must use the bounded delay");
}
void NativeTemperatureRecovery() {
    ResetNativeTemperature();
    Check(NativeTemperatureSnapshot().gpuTemp == 53.5,
          "native GPU temperature must convert tenths of a degree");
    Check(NativeTemperatureSnapshot().gpuTemp == 53.5 && kmtOpens == 1,
          "native handle must be reused between samples");
    kmtQueryStatus = kStatusInvalidHandle;
    auto adapterDeadline = g_nextGpuAdapterResolve;
    Check(!NativeTemperatureSnapshot().gpuTemp && kmtCloses == 1 && !g_cachedD3dkmtAdapterHandle &&
              g_cachedGpuAdapterResolved && g_cachedGpuAdapterInfo,
          "a temperature failure must not invalidate the shared GPU adapter cache");
    Check(g_nextGpuAdapterResolve == adapterDeadline,
          "temperature failure must preserve normal adapter refresh scheduling");
    CheckTemperatureRetryDelay(5);
    kmtQueryStatus = 0;
    Check(!NativeTemperatureSnapshot().gpuTemp && kmtOpens == 1,
          "a failed native handle must not be reopened on every sample");
    g_gpuTemperatureRetry.nextAttempt = {};
    Check(NativeTemperatureSnapshot().gpuTemp == 53.5 && kmtOpens == 2,
          "native temperature must resume with a fresh handle");
    Check(g_gpuTemperatureRetry.failures == 0 &&
              g_gpuTemperatureRetry.nextAttempt == SampleTime{},
          "successful temperature recovery must clear the backoff");
    CloseMetricSources();
    Check(kmtCloses == 2, "shutdown must close the recovered native handle");
    Check(!g_gpuTemperatureRetry.adapterLuid,
          "provider shutdown must clear native temperature retry state");

    for (LONG unsupported : {kStatusNotImplemented, kStatusNotSupported}) {
        ResetNativeTemperature();
        kmtQueryStatus = unsupported;
        PdhSnapshot();
        auto healthy = PdhSnapshot();
        auto* gpuQuery = g_pdhQuery;
        int queryOpens = fake::opens;
        adapterDeadline = g_nextGpuAdapterResolve;
        ReadTemperatures(healthy, ModSettings{}); // Default Automatic fallback.
        Check(!healthy.gpuTemp && kmtQueries == 1 && kmtOpens == 1,
              "Automatic must try the native fallback once when HWiNFO is absent");
        CheckTemperatureRetryDelay(60);
        bool otherMetricsStayedAvailable = true;
        for (int i = 0; i < 80; ++i) {
            auto snapshot = PdhSnapshot();
            ReadTemperatures(snapshot, ModSettings{});
            otherMetricsStayedAvailable &= snapshot.cpuAvailable && snapshot.ramAvailable &&
                snapshot.gpuAvailable && snapshot.vramAvailable && !snapshot.gpuTemp;
        }
        Check(otherMetricsStayedAvailable,
              "persistent temperature refusal must preserve CPU/RAM/GPU/VRAM");
        Check(fake::opens == queryOpens && g_pdhQuery == gpuQuery,
              "persistent temperature refusal must preserve the independent PDH queries");
        Check(kmtQueries == 1 && kmtOpens == 1 && kmtEnumerations == 0 &&
                  g_nextGpuAdapterResolve == adapterDeadline,
              "unsupported temperature must not churn handles or enumerate adapters every sample");
        g_nextGpuAdapterResolve = {};
        Check(GetGpuAdapterInfo(L"").has_value() && kmtEnumerations == 1,
              "the independent periodic adapter refresh must still run");
        Check(!NativeTemperatureSnapshot().gpuTemp && kmtQueries == 1,
              "same-LUID cache refresh must not reset temperature unavailability");
        kmtQueryStatus = 0;
        g_gpuTemperatureRetry.nextAttempt = {};
        Check(NativeTemperatureSnapshot().gpuTemp == 53.5 && kmtQueries == 2,
              "periodic reprobe must recover when a new driver preserves the LUID");

        kmtQueryStatus = unsupported;
        NativeTemperatureSnapshot();
        ++kmtAdapter.luidValue.LowPart;
        kmtAdapter.luid = FormatAdapterLuid(kmtAdapter.luidValue);
        g_nextGpuAdapterResolve = {};
        kmtQueryStatus = 0;
        Check(NativeTemperatureSnapshot().gpuTemp == 53.5 &&
                  SameLuid(*g_gpuTemperatureRetry.adapterLuid, kmtAdapter.luidValue),
              "a newly resolved LUID must bypass the old adapter's cooldown");
    }

    ResetNativeTemperature();
    kmtQueryStatus = static_cast<LONG>(0xC000000Du); // Ambiguous INVALID_PARAMETER.
    for (int delay : {5, 10, 20, 40, 60, 60}) {
        g_gpuTemperatureRetry.nextAttempt = {};
        Check(!NativeTemperatureSnapshot().gpuTemp && g_cachedGpuAdapterResolved,
              "unknown persistent failures must not invalidate adapter identity");
        CheckTemperatureRetryDelay(delay);
    }
    kmtQueryStatus = 0;
    g_gpuTemperatureRetry.nextAttempt = {};
    Check(NativeTemperatureSnapshot().gpuTemp == 53.5,
          "unknown errors must never permanently disable native temperature");

    ResetNativeTemperature();
    kmtOpenStatus = kStatusInvalidHandle;
    Check(!NativeTemperatureSnapshot().gpuTemp && g_cachedGpuAdapterResolved &&
              kmtQueries == 0 && kmtCloses == 0,
          "failed open must not invalidate adapter identity or query an invalid handle");
    CheckTemperatureRetryDelay(5);
    kmtOpenStatus = 0;
    Check(!NativeTemperatureSnapshot().gpuTemp && kmtOpens == 1,
          "open failures must honor the retry interval");
    g_gpuTemperatureRetry.nextAttempt = {};
    Check(NativeTemperatureSnapshot().gpuTemp == 53.5,
          "native open must recover with the same cached adapter identity");

    ResetNativeTemperature();
    kmtEmptyHandle = true;
    Check(!NativeTemperatureSnapshot().gpuTemp && kmtQueries == 0 &&
              g_cachedGpuAdapterResolved,
          "a successful open returning no handle must be treated as transient");
    CheckTemperatureRetryDelay(5);

    for (ULONG invalidTemperature : {0ul, 2001ul}) {
        ResetNativeTemperature();
        kmtTemperature = invalidTemperature;
        Check(!NativeTemperatureSnapshot().gpuTemp, "invalid native temperatures must stay unavailable");
        CheckTemperatureRetryDelay(60);
        Check(!NativeTemperatureSnapshot().gpuTemp && kmtQueries == 1,
              "invalid readings must not be polled on every sample");
    }
    CloseMetricSources();
    g_d3dkmtEnumAdapters2 = nullptr;
    g_d3dkmtOpenAdapterFromLuid = nullptr;
    g_d3dkmtCloseAdapter = nullptr;
    g_d3dkmtQueryAdapterInfo = nullptr;
}

void MoveWindowLifecycle() {
    auto settings = std::make_shared<ModSettings>();
    settings->moveHotkey = L"Ctrl+Alt+Shift+F24";
    { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
    g_unloading = false;
    EnsurePlacementControl(*settings);
    Check(g_placementControlWindow != nullptr, "create production placement control on the owning thread");
    Check(g_hotkeyRegistered, "register the test move combination");
    HWND control = g_placementControlWindow;
    Check(!RegisterHotKey(control, 991, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24) &&
          GetLastError() == ERROR_HOTKEY_ALREADY_REGISTERED, "a occupied global combination really conflicts");
    for (int i = 0; i < 10; ++i) QueueTaskbarPlacement();
    MSG message{}; int queued = 0;
    while (PeekMessageW(&message, control, kGeometryMessage, kGeometryMessage, PM_REMOVE)) {
        ++queued; DispatchMessageW(&message);
    }
    Check(queued == 1 && !g_geometryQueued, "layout notifications coalesce into one native message");
    settings->moveHotkey.clear(); EnsurePlacementControl(*settings);
    Check(!g_hotkeyRegistered && RegisterHotKey(control, 991, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24),
          "disabling the shortcut releases its registration");
    UnregisterHotKey(control, 991);
    Check(RegisterHotKey(control, 991, MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_F24), "occupy a key before mod registration");
    settings->moveHotkey = L"Ctrl+Alt+Shift+F24"; EnsurePlacementControl(*settings);
    Check(!g_hotkeyRegistered, "mod leaves an occupied key unregistered");
    UnregisterHotKey(control, 991);
    EnsurePlacementControl(*settings);
    Check(!g_hotkeyRegistered, "layout refresh does not repeatedly register a rejected key");
    g_hotkeyRefreshPending = true; EnsurePlacementControl(*settings);
    Check(g_hotkeyRegistered && !g_hotkeyRefreshPending, "settings reload retries a previously occupied unchanged key once");
    settings->moveHotkey.clear(); EnsurePlacementControl(*settings);
    WNDCLASSW parentClass{}; parentClass.hInstance = GetModuleHandleW(nullptr);
    parentClass.lpfnWndProc = DefWindowProcW; parentClass.lpszClassName = L"PrivateMoveTestHost";
    Check(RegisterClassW(&parentClass) != 0, "register an isolated hidden editor host");
    HWND parent = CreateWindowExW(0, parentClass.lpszClassName, L"", WS_OVERLAPPED,
                                  0, 0, 500, 100, nullptr, nullptr, parentClass.hInstance, nullptr);
    WNDCLASSW cls{}; cls.hInstance = PlacementModule(); cls.lpfnWndProc = MoveEditorProc;
    cls.lpszClassName = kMoveWindowClass;
    Check(RegisterClassW(&cls) != 0, "register the real preview callback");
    g_moveEditor = std::make_shared<MoveEditorState>();
    HWND preview = CreateWindowExW(0, cls.lpszClassName, L"", WS_CHILD,
                                   0, 0, 410, 38, parent, nullptr, cls.hInstance, nullptr);
    Check(preview != nullptr, "create preview under a hidden parent without desktop interaction");
    g_moveEditor->window = preview; g_moveEditorWindow = preview;
    PlacementProfiles profiles{1, 10, L"test-display", {{L"test-display", .5}}};
    SetPlacementProfiles(profiles);
    SendMessageW(preview, WM_KEYDOWN, VK_RETURN, 0);
    Check(IsWindow(preview) && PlacementProfilesSnapshot().target == L"test-display",
          "Enter on an invalid candidate leaves saved state and editor intact");
    g_moveEditor->candidate = {0, 410, 0}; g_moveEditor->target.window = parent;
    SendMessageW(preview, WM_KEYDOWN, VK_RETURN, 0);
    Check(IsWindow(preview) && !g_moveEditor->candidate.width && PlacementProfilesSnapshot().target == L"test-display",
          "Enter revalidates a stale candidate and rejects a target without a taskbar root");
    SendMessageW(preview, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(80, 10));
    Check(GetCapture() == preview && g_moveEditor->dragging, "drag captures the pointer in the real callback");
    Check(g_previewGraphicsToken != 0 && g_moveEditor->surface && g_moveEditor->surface->bitmap,
          "drag uses the real premultiplied-alpha preview renderer");
    SendMessageW(preview, WM_LBUTTONUP, 0, 0);
    Check(GetCapture() != preview && !g_moveEditor->dragging, "mouse release retains preview and releases capture");
    SendMessageW(preview, WM_KEYDOWN, VK_HOME, 0);
    Check(g_moveEditor && g_moveEditor->reset && PlacementProfilesSnapshot().target == L"test-display",
          "Home stages a reset without clearing persistence");
    SendMessageW(preview, WM_KEYDOWN, VK_ESCAPE, 0);
    Check(!IsWindow(preview) && !g_moveEditorWindow && !g_moveEditor &&
          PlacementProfilesSnapshot().target == L"test-display", "Esc closes the preview and cancels Home");
    Check(g_previewGraphicsToken == 0, "closing the preview releases its graphics runtime");
    Check(SavePlacementProfiles(profiles), "save a confirmed profile through the local storage API");
    SetPlacementProfiles({}); LoadPlacementProfiles();
    Check(PlacementProfilesSnapshot().target == L"test-display", "reload persisted target after a fresh in-memory state");
    fake::localSaveFails = true;
    Check(!SavePlacementProfiles({}) && ParseProfiles(fake::localStorage[L"placement.v1"])->target == L"test-display",
          "storage failure preserves the previously persisted target");
    fake::localSaveFails = false;
    RemovePlacementControl();
    Check(!IsWindow(control) && !g_placementControlWindow && !g_hotkeyRegistered && !g_geometryQueued,
          "teardown releases hidden control, registrations and queued state");
    DestroyWindow(parent); UnregisterClassW(parentClass.lpszClassName, parentClass.hInstance);
    SetPlacementProfiles({}); fake::localStorage.clear();
}

void WindowNotifications() {
    WNDCLASSW cls{};
    cls.lpfnWndProc = DefWindowProcW;
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpszClassName = L"TaskbarSystemInfoRegression";
    Check(RegisterClassW(&cls) != 0, "register hidden test window");
    HWND window = CreateWindowExW(0, cls.lpszClassName, L"", WS_OVERLAPPED,
                                  0, 0, 100, 100, nullptr, nullptr, cls.hInstance, nullptr);
    Check(window != nullptr, "create hidden test window");
    g_unloading = false;
    g_notificationWindow = window;
    g_taskbarRefreshMessage = RegisterWindowMessageW(L"TaskbarSystemInfoRegressionRefresh");
    Check(SetWindowSubclass(window, TaskbarNotificationsProc, 1, 0), "attach production subclass");
    g_placementApplyPending = false;
    g_placementFailures = 7;
    SendMessageW(window, WM_DISPLAYCHANGE, 32, 0);
    Check(g_placementApplyPending && g_placementFailures == 0,
          "same-count display change must invalidate placement and retry backoff");
    Check(RemoveTaskbarNotifications() && !g_notificationWindow,
          "notification callback must be detached synchronously");
    g_placementApplyPending = false;
    SendMessageW(window, WM_DISPLAYCHANGE, 32, 0);
    Check(!g_placementApplyPending, "detached callback must never run again");
    MSG message;
    while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
    Check(!g_placementApplyPending, "queued numeric refresh must be harmless after detach");
    DestroyWindow(window);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
}
} // namespace

int main() {
    try {
        HistoryAndScheduling();
        std::cout << "PASS: timestamped history, scheduling, formatting, layout bounds\n";
        AdaptivePlacement();
        std::cout << "PASS: adaptive placement, DPI matrix, reservation bounds and restoration\n";
        MovePreferences();
        std::cout << "PASS: move hotkeys, per-display profiles and corruption rejection\n";
        SensorIdentity();
        std::cout << "PASS: production HWiNFO mapping and registry cache reordering\n";
        PdhRecovery();
        std::cout << "PASS: production PDH stale/parked/LUID/error/priming paths\n";
        NativeTemperatureRecovery();
        std::cout << "PASS: native temperature refusal/backoff/open failure/same-LUID recovery\n";
        MoveWindowLifecycle();
        std::cout << "PASS: native move preview, capture, cancel/reset, hotkey conflict, persistence and teardown\n";
        WindowNotifications();
        std::cout << "PASS: native hidden-window notification and detach lifecycle\n";
        std::cout << checks << " behavioral checks passed (synthetic providers, no Explorer injection).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
