# Taskbar placement and move editor verification - 2026-10-04

Scope: local version **1.6.0**, using the compiler and engine libraries bundled
with Windhawk **1.7.3**. The metric collectors and provider behavior are outside
this change. These checks qualify the source and isolated fixtures; they do not
establish the live Explorer behavior listed below.

## Regression reproduced before the fix

The isolated XAML fixture first added a stretched TaskbarFrameRepeater with
300 DIPs of actual button content on a 1920-DIP panel, a 120-DIP tray,
Reserve space enabled, and offsets 0 and 2000. The previous scalar calculation
failed with `Stretched repeater hides widget with usable space`.

The corrected implementation measures actual button hit areas. The regression
now keeps a readable widget visible at both offsets. The offset-2000 case
places the 410-DIP widget at x=1384 with six DIPs before the tray; the unused width of the
container is available space.

## Implemented behavior

- A plain geometry snapshot and interval solver choose the nearest full-size
  gap before trying a smaller one. Scale remains at least 85%, with main text
  at least 9 DIPs. **Support the small taskbar** lowers both floors to 78% and
  8 DIPs for a short-button taskbar. An unreadable placement hides and restores
  automatically.
- XAML hit areas are transformed into the panel root's coordinates. Background
  containers, the widget subtree and hidden controls do not occupy space.
  Cached element references stay on the owning UI thread. Structural changes
  rediscover controls; ordinary metric text changes reuse the cache.
- Reservation removes the mod's previous margin from the baseline, preserves
  external margins, and verifies the arranged widget and button group. Failed
  reservation restores the baseline and uses a free gap without a layout loop.
- Ctrl+Alt+M opens a native preview; release keeps the candidate, Enter
  revalidates and confirms, Esc cancels, and Home stages a reset. Normal widget
  operation is click-through. Empty moveHotkey disables registration.
- The preview contains the live widget text, graphs and bars with a rounded
  frame and a hand cursor, with no instruction surface. The glass fill appears
  only while the mouse button is held for dragging. The normal widget and the
  idle, released and hovered preview have no visible fill. The native editor
  retains alpha 1/255 inside its rounded body so empty spaces can receive clicks;
  alpha-zero layered pixels pass clicks through under
  [Win32's layered-window rules](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows).
  Invalid destinations tint the
  drag surface and outline red. Six DIPs of inner side padding and clearance
  from mapped controls and taskbar edges remain in normal operation too. CPU/GPU
  cells use compact widths while retaining 100% and 100-degree readings at font
  size 13. Source opacity is restored on cancellation; normal
  metric collection continues while the source is hidden. High contrast uses
  an opaque system-color surface during dragging. A cached premultiplied bitmap avoids rerendering
  unchanged content on every pointer move.
- Display keys use QueryDisplayConfig monitor device paths. Confirmed positions
  are normalized against full-width logical travel and stored by Windhawk's
  local string API. Manual monitor/offset changes have the documented priority.
- Move application is a transaction: transfer, arranged-geometry verification,
  then persistence. Failed transfer, geometry, storage or cancellation restores
  the previous preference. Move teardown retains metric history and its rendered
  sequence, preventing replay of already rendered samples.

## Automated and visual checks

| Check | Result and boundary |
| --- | --- |
| `python tests/validate-source.py` | Pass: metadata version 1.6.0, 30 settings and source invariants. |
| `./tests/run-regression.ps1` | Pass: 262 behavioral checks. Pure placement, minimum side clearance, small-taskbar readability limits, DPI arithmetic, persistence and injected transaction failures; real native hidden-window callbacks for capture, Enter revalidation, Home/Esc, hotkey conflict/retry and teardown. Provider regression checks also remain passing. |
| `./tests/run-ui-smoke.ps1` | Pass: real isolated XAML Island, transformed/hidden controls, stretched-container regression, resize/hide/restore, tray/button changes, external margins, repeated layout, cache invalidation and history-preserving removal. Production preview paint, alpha pixels, hidden layered-window upload, live metric updates during dragging, glass only while dragging, transparent normal/idle/released/hovered states, hand cursor, red invalid surface, removal of instruction space, compact cells at font size 13, alignment with actual XAML layout slots, side padding and source opacity restoration. The 30-DIP render uses **Support the small taskbar** and stays visible. |
| `./tests/run-metrics-smoke.ps1` | Pass: live Windows counter and adapter reads on this workstation. AMD Radeon RX 7900 XTX was selected; GPU usage, VRAM and native GPU temperature were available. The fresh memory query worked after one collection. Windows thermal zones were unavailable. This does not test Explorer placement or physical display changes. |
| `./build.ps1 -Architecture x86_64` | Pass with the Windhawk 1.7.3 x64 engine library and `-Wall -Wextra`. |
| `./build.ps1 -Architecture aarch64` | Pass with the Windhawk 1.7.3 ARM64 engine library and `-Wall -Wextra`; cross-build only. |
| `git diff --check` | Pass. |

Rendered widget states inspected separately from coordinate checks: dark,
light, minimum width, larger font and unavailable readings. A 30-DIP panel
correctly hides the widget because it cannot maintain the readability floor.
The live valid/invalid/reset/hover editor previews, light/dark colors, minimum
width, larger font, high-contrast fixture and 200% preview render were inspected
for compact spacing, readable values, unclipped side padding and color feedback.
Generated PNGs are in `build-ui-smoke`.

The real Windhawk storage calls are replaced only in the regression executable
with an in-memory store that can reject writes. Fresh in-memory state reloads
the serialized target, but this is not a live Windhawk/Explorer restart test.
Successful transaction callbacks exercise commit order and rollback, not an
actual Explorer transfer between physical displays.

## Explorer verification remains incomplete

The workstation has Windows 11 Pro build 26300 and Windhawk 1.7.3. A current
read of `HKLM\SOFTWARE\Windhawk\Engine\Mods\local@taskbar-system-info` reports
version **1.6.0**, enabled, with library
`local@taskbar-system-info_1.6.0_314882.dll`. That registry state does not prove
which source revision is loaded or the behavior of the final drag-only glass
change. The test runs did not install or replace the Explorer mod.

These scenarios still require the new source installed into Windhawk and live
Explorer verification:

- Top and bottom taskbars, left and centered alignment, Reserve space on/off.
- Start, Search, Task View, Widgets/weather, growing app groups, overflow,
  tray/clock size changes and clicks after leaving the editor.
- Real dragging and confirmation between displays with different DPI, including
  negative desktop coordinates, missing XAML roots and unplug/reconnect.
- Saved device-path selection after restarting Explorer/Windhawk and changes to
  manual settings while editing.
- Coexistence with an actual Taskbar Styler preset and any separately hosted
  third-party taskbar elements.
- Unload while editing, final keyboard/mouse cleanup and ARM64 hardware.

The isolated tests do not satisfy these physical acceptance checks. No claim
of complete Explorer compatibility is made for version 1.6.0 yet.
