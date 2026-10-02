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
    ModSettings settings;
    settings.width = 410;
    settings.leftOffset = 2000;
    auto p = ResolveTaskbarPlacement(settings, 1920);
    Check(Near(p.left, 1510) && Near(p.width, 410), "1080p offset must fit the whole widget");
    p = ResolveTaskbarPlacement(settings, 2560.0 / 1.5);
    Check(Near(p.left, 2560.0 / 1.5 - 410), "150 percent uses logical panel width");
    settings.leftOffset = 3500;
    p = ResolveTaskbarPlacement(settings, 5120);
    Check(Near(p.left, 3500), "ultrawide offsets must not have an arbitrary 2000 limit");
    p = ResolveTaskbarPlacement(settings, 300);
    Check(Near(p.left, 0) && Near(p.width, 300), "very narrow panel must shrink the host");
    p = ResolveTaskbarPlacement(settings, 0);
    Check(Near(p.left, 0) && Near(p.reserved, 0), "unmeasured panel must defer a large offset");
    p = ResolveTaskbarPlacement(settings, std::numeric_limits<double>::quiet_NaN());
    Check(Near(p.left, 0) && std::isfinite(p.width), "invalid measurement must remain finite");

    settings.reserveSpace = true;
    settings.reserveGap = 8;
    settings.leftOffset = 2000;
    p = ResolveTaskbarPlacement(settings, 1700, 500, 10, 20);
    Check(Near(p.left, 752) && Near(p.reserved, 1170),
          "reservation must preserve buttons and external margins");
    p = ResolveTaskbarPlacement(settings, 700, 500);
    Check(Near(p.left, 0) && Near(p.width, 200) && Near(p.reserved, 200),
          "buttons have priority when only part of the widget fits");
    p = ResolveTaskbarPlacement(settings, 400, 500);
    Check(Near(p.width, 0) && Near(p.reserved, 0), "no room must not displace taskbar buttons");
    p = ResolveTaskbarPlacement(settings, 417, 0);
    Check(Near(p.width, 410) && Near(p.reserved, 417), "reduce gap before shrinking widget");

    for (double physicalWidth : {1280.0, 1920.0, 2560.0, 3840.0, 5120.0}) {
        for (double scale : {1.0, 1.25, 1.5, 2.0}) {
            for (bool reserve : {false, true}) {
                settings.reserveSpace = reserve;
                double available = physicalWidth / scale - 180;
                p = ResolveTaskbarPlacement(settings, available, 480, 10, 8);
                Check(p.left >= 0 && p.width >= 0 &&
                          p.left + p.width <= available + 0.001 &&
                          (!reserve || p.reserved <= std::max(0.0, available - 480 - 18) + 0.001),
                      "placement matrix must stay inside panel and preserve buttons");
            }
        }
    }
    Check(settings.leftOffset == 2000, "clamping must not rewrite user settings");
    settings.reserveSpace = false;
    auto small = ResolveTaskbarPlacement(settings, 960);
    auto large = ResolveTaskbarPlacement(settings, 3840);
    Check(small.left < large.left && Near(large.left, 2000),
          "requested offset must return after moving to a wider panel");
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
        SensorIdentity();
        std::cout << "PASS: production HWiNFO mapping and registry cache reordering\n";
        PdhRecovery();
        std::cout << "PASS: production PDH stale/parked/LUID/error/priming paths\n";
        NativeTemperatureRecovery();
        std::cout << "PASS: native temperature refusal/backoff/open failure/same-LUID recovery\n";
        WindowNotifications();
        std::cout << "PASS: native hidden-window notification and detach lifecycle\n";
        std::cout << checks << " behavioral checks passed (synthetic providers, no Explorer injection).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
