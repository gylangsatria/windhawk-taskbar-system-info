# Review resolution and verification - 2026-09-17

Scope: the full historical review audit of PRs [#5175](https://github.com/ramensoftware/windhawk-mods/pull/5175) and [#5299](https://github.com/ramensoftware/windhawk-mods/pull/5299). This records local verification before publication, on top of standalone `4405ad8` and catalog `6f802e4`. It does not claim upstream approval or a release update. Version remains **1.5.0**.

The preceding audit read all 18 substantive AI reviews and all three maintainer comments. Repeated findings were grouped by behavior. Previously accepted fixes were preserved; this pass closes the confirmed regressions and explains conflicting or product-level suggestions instead of claiming that every historical proposal should coexist.

## Changes applied

| Finding | Resolution | Verification |
| --- | --- | --- |
| Cached HWiNFO index can silently become another valid sensor | Shared Memory checks sensor ID, sensor instance and reading ID; Registry checks exact Sensor/Label. Mismatch reselects in the same sample. | Real reader functions consume synthetic swapped tables/registry values. |
| Missing second temperature waits a full scan interval | Four short five-second discovery retries, then normal 60-second SM / 30-second Registry scans. Completion resets the short retry window. | Backoff state and cache behavior tested. |
| Sparse Registry indices stop discovery prematurely | Enumerate actual SensorN values instead of stopping at 16 missing indices or index 1023. | A valid sensor at index 7001 is found. |
| Rejected Shared Memory layout gives no explanation | Bounded one-shot layout diagnostic, including truncated mappings; reset after a valid layout. | Mapped-size offset/bounds rejection and recovery tested. |
| Locale can alter decimals | Locale-independent display formatting and Registry numeric parsing. | Ukrainian numeric locale reproduced the old parsing defect; dot/comma inputs now pass. |
| GPU recovery resets unrelated CPU thermal counter | Separate CPU and GPU PDH queries. CPU/RAM remain publishable while GPU primes. | Recovery closes only the GPU query; the same CPU thermal counter still returns a value. |
| Same-LUID empty-success arrays never recover | Probe a fresh memory gauge at most once per minute. Rebuild only when it proves that the old GPU query is stale. | Synthetic old-empty/fresh-valid case recovers. Healthy live GPU also provides a valid memory reading after one collect. |
| Parked GPU must not create a reset storm | No rebuild when the fresh query is also empty. | Fifty unavailable samples cause one probe and zero GPU-query rebuilds. |
| Unavailable GPU is disguised as idle 0% | Idle zero requires working memory data and no matching engine instances. Invalid matched engine readings remain unavailable. | Empty, invalid and genuine-idle cases tested separately. |
| Buffer churn treated as a broken query | Exhausted bounded PDH_MORE_DATA retries skip the sample rather than resetting the query. | Growing and continuously growing arrays tested. |
| Hard-error streak survives an intervening soft absence | Every non-hard result resets the consecutive-hard-error streak. | Mixed hard/soft sequence tested; three actual consecutive hard failures still recover. |
| One missing sample erases the graph | Timestamped history with explicit gaps; independent PathGeometry runs do not bridge missing data. | Missing sample and stalled-collector scenarios, plus actual XAML renders. |
| History's time axis includes accumulated collection drift | Deadline-based sampling and timestamp-derived X positions; missed deadlines do not create a catch-up burst. | Timing arithmetic and history-window pruning tested. |
| Recreate every graph point on every sample | Reuse PathFigure/LineSegment objects and update changed coordinates. | Actual XAML graph rendering, including a visible gap. |
| Monitor reorder is not detected when window/count stays unchanged | UI-thread window subclass invalidates placement on WM_DISPLAYCHANGE/WM_SETTINGCHANGE. | Real native hidden-window message test verifies invalidation/backoff reset; physical topology change remains untested. |
| UI timer ignores updateInterval | New snapshots post pointer-free numeric messages. The fallback timer scales with updateInterval; startup/placement retry remains fast. Theme/placement notifications remain immediate. | Timing tests, real subclass detach, and queued-message-after-detach test. |
| Short taskbar clips fixed 38-DIP content | Uniform down-only Viewbox with a root SizeChanged handler; both handlers explicitly revoked. | Actual XAML rendering at height 30 and 48; no host-height overflow. |
| Duplicate metric teardown | Worker alone closes providers before returning; redundant Uninit calls removed. | Both architectures compile; worker join and UI cleanup structural checks retained. |
| PR description and evidence no longer match source | README and embedded guide updated; accurate replacement PR text prepared locally. | Documentation/source validation. Nothing posted to GitHub. |

## Previous fixes retained

This pass did not undo the existing fixes for:

- XAML no_destroy ownership, Loaded-revoker cleanup, remembered UI thread and failed-injection teardown;
- synchronous window-thread marshaling, no timeout payload, no global mutex held over SendMessage, bounded hook removal and explicit registry destruction;
- one-time loader hooking, LastError preservation, required taskbar symbols, conservative x64/ARM64 discovery and no blind fallback offset;
- worker-only metric I/O, recovery after WAIT_FAILED, lazy PDH initialization, missing-counter retry, settings-change rebaselining and bounded snapshot buffering;
- packed HWiNFO ABI, local header copies, mapped-view bounds, short mutex ownership, raw-byte temperature units and Fahrenheit conversion;
- native D3DKMT GPU temperature, cached native handles, adapter identity matching, shared-memory semantics and small/fractional capacity formatting;
- configured primary/secondary placement, actual HWND tracking, fallback/backoff, reuse of known XAML frames and preservation of externally changed taskbar margins;
- cached styles/brushes, high-contrast opacity, strict color parsing, text trimming, minimum graph width and documented setting clamps.

The regression suite additionally exercises an invalid cached native KMT handle: both handle and adapter identity are invalidated, a fresh handle supplies the next temperature, and cleanup closes it. This is synthetic fault injection, not a physical driver reset.

## Deliberate decisions on conflicting suggestions

These are **not** presented as implemented feature requests:

1. **HWiNFO packing remains one byte.** The earlier natural-alignment suggestion contradicted the actual layout and was later accepted as incorrect. ABI assertions remain.
2. **No stale-temperature holdover.** An unavailable provider produces a gap/`--` unless another configured provider succeeds. Showing an old hot/cold value as current would weaken this administrator-oriented monitor.
3. **Automatic iGPU classification stays flag + memory shape, with explicit override.** A legacy 512 MiB discrete GPU with an 8 GiB shared pool is ambiguous. The original difficult fixture is restored, and the Dedicated override is tested. A growing device-name list would contradict the later simplification request; we do not claim perfect automatic classification.
4. **Keep the six existing temperature modes.** They are established diagnostic/provider-selection settings. Removing them would break configurations; Automatic remains the documented default.
5. **Do not add per-metric GPU switches or dynamic per-engine subscriptions in this corrective pass.** These were exploratory performance/product suggestions, not a demonstrated remaining bug. The accepted dashboard always contains CPU/GPU/RAM/VRAM. Independent on/off controls would need an explicit display contract, and replacing PDH wildcard subscriptions would add a new instance-lifecycle subsystem. Longer update intervals already reduce collection work.
6. **Keep the small callback-token registry.** It prevents arbitrary message lParam values from becoming dereferenced pointers and prevents multiple active thread hooks from dispatching the same stack context twice. The callback runs outside the short registry lock; no dispatch mutex, heap payload, timeout or permanent module pin is reintroduced.
7. **Keep bounded retries for degraded placement.** The alternative suspension/fingerprint machinery was removed at the reviewer's later request. A slow retry permits recovery without disabling the feature forever; normal topology changes now use events.
8. **Keep remembered UI ownership.** Later lifecycle findings require it for safe cleanup, despite an earlier suggestion to remove that bookkeeping.

## Verification record

All tests below run against the local corrected source, not a previously published DLL.

- Source validation: pass, version 1.5.0, 28 settings.
- Windhawk x64 build: pass with -Wall/-Wextra; no compiler warnings reported.
- Windhawk ARM64 cross-build: pass with -Wall/-Wextra; not an ARM64 hardware test.
- Production-code regression executable: **70 behavioral checks passed**. Only the test build injects provider APIs; no synthetic self-test code is shipped in the mod.
- Isolated native XAML Island: six rendered states visually inspected - dark, light, 30-DIP height, minimum 330-DIP width, maximum supported font size 13, and unavailable metrics. The layout is click-through and has no interactive controls. Rendered graph gaps and fractional 0.4/0.5G capacity are visible.
- Healthy live read-only smoke on this workstation: AMD Radeon RX 7900 XTX; GPU 5.07%, dedicated memory 2.18/23.94 GiB, native temperature 56 C at that snapshot. Fresh single-collect memory gauge available. CPU utility available; Windows ACPI thermal zones unavailable. Values are a point-in-time sample, not thresholds or benchmarks.
- Both repository copies: byte-identical source, SHA-256 `B9FA61A429C03818EF1BF071CB7A33D52AF525E421DD57A5B9AC721AAF65E9FE`.
- git diff --check: pass.
- At the end of this local verification pass: no installed Windhawk source edited, no Explorer/driver restart, no commit, no push, no release mutation and no /ai-review or other PR comment. Subsequent publication is separate from these test results.

### Not physically reproduced in this pass

Driver restart/TDR, real LUID replacement, live HWiNFO table replacement, physical monitor reorder/unplug/replug, mixed-DPI transitions, live high-contrast switching, and ARM64-device behavior. The synthetic tests cover specific branches, not every operating-system failure mode. Isolated XAML rendering validates the widget, not private Explorer symbol discovery on every Windows build.

## Reproduce

```powershell
python -B .\tests\validate-source.py
.\tests\run-regression.ps1
.\tests\run-ui-smoke.ps1
.\tests\run-metrics-smoke.ps1
.\build.ps1
.\build.ps1 -Architecture aarch64 -OutputDirectory .\build-arm64
```

UI captures are generated under build-ui-smoke. Builds/test outputs are ignored by Git. Upstream CI and reviewer approval must refer to the resulting catalog HEAD; neither is implied by these local tests.
