// Isolated XAML Island: renders the actual widget without injecting Explorer.
#include <windhawk_api.h>
#include <windows.ui.xaml.hosting.desktopwindowxamlsource.h>
#include "../taskbar-system-info.wh.cpp"
#include <winrt/Windows.UI.Xaml.Hosting.h>
#include <winrt/Windows.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Streams.h>
#include <iostream>
#include <stdexcept>

void Pump() {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}
template <class Operation>
auto Finish(Operation operation) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (operation.Status() == AsyncStatus::Started) {
        Pump();
        if (std::chrono::steady_clock::now() > deadline) {
            throw std::runtime_error("XAML rendering timed out");
        }
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
    }
    return operation.GetResults();
}
void SaveImage(FrameworkElement element, const std::wstring& folderPath, PCWSTR name) {
    using namespace Windows::Storage;
    using namespace Windows::Storage::Streams;
    using namespace Windows::Graphics::Imaging;
    Windows::UI::Xaml::Media::Imaging::RenderTargetBitmap bitmap;
    Finish(bitmap.RenderAsync(element));
    if (!bitmap.PixelWidth() || !bitmap.PixelHeight()) {
        throw std::runtime_error("XAML rendered an empty image");
    }
    auto buffer = Finish(bitmap.GetPixelsAsync());
    std::vector<uint8_t> pixels(buffer.Length());
    DataReader::FromBuffer(buffer).ReadBytes(pixels);
    auto folder = Finish(StorageFolder::GetFolderFromPathAsync(folderPath));
    auto file = Finish(folder.CreateFileAsync(name, CreationCollisionOption::ReplaceExisting));
    auto stream = Finish(file.OpenAsync(FileAccessMode::ReadWrite));
    auto encoder = Finish(BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream));
    encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied,
                         bitmap.PixelWidth(), bitmap.PixelHeight(), 96, 96, pixels);
    Finish(encoder.FlushAsync());
    stream.Close();
}
void SaveNativePreview(HWND window, const std::wstring& folderPath, PCWSTR name) {
    using namespace Windows::Storage;
    using namespace Windows::Storage::Streams;
    using namespace Windows::Graphics::Imaging;
    RECT rect; GetClientRect(window, &rect);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = rect.right; info.bmiHeader.biHeight = -rect.bottom;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr; HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!dc || !bitmap || !pixels) throw std::runtime_error("Native preview bitmap allocation failed");
    HGDIOBJ old = SelectObject(dc, bitmap);
    HBRUSH backdrop = CreateSolidBrush(g_moveEditor->visual.light ? RGB(225, 233, 245) : RGB(17, 28, 50));
    FillRect(dc, &rect, backdrop); DeleteObject(backdrop);
    SendMessageW(window, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
    std::vector<uint8_t> copy(static_cast<uint8_t*>(pixels),
                              static_cast<uint8_t*>(pixels) + rect.right * rect.bottom * 4);
    SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc);
    auto folder = Finish(StorageFolder::GetFolderFromPathAsync(folderPath));
    auto file = Finish(folder.CreateFileAsync(name, CreationCollisionOption::ReplaceExisting));
    auto stream = Finish(file.OpenAsync(FileAccessMode::ReadWrite));
    auto encoder = Finish(BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId(), stream));
    encoder.SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Ignore, rect.right, rect.bottom,
                         96, 96, copy);
    Finish(encoder.FlushAsync()); stream.Close();
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    init_apartment(apartment_type::single_threaded);
    Windows::UI::Xaml::Hosting::WindowsXamlManager manager{nullptr};
    Windows::UI::Xaml::Hosting::DesktopWindowXamlSource island{nullptr};
    HWND window = nullptr;
    int result = 0;
    try {
        manager = Windows::UI::Xaml::Hosting::WindowsXamlManager::InitializeForCurrentThread();
        WNDCLASSW cls{};
        cls.lpfnWndProc = DefWindowProcW;
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpszClassName = L"TaskbarSystemInfoVisualCheck";
        RegisterClassW(&cls);
        window = CreateWindowExW(0, cls.lpszClassName, L"", WS_POPUP,
                                  0, 0, 520, 64, nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) throw std::runtime_error("Creating test host failed");
        island = Windows::UI::Xaml::Hosting::DesktopWindowXamlSource();
        auto native = island.as<IDesktopWindowXamlSourceNative>();
        check_hresult(native->AttachToWindow(window));
        HWND child = nullptr;
        check_hresult(native->get_WindowHandle(&child));
        SetWindowPos(child, nullptr, 0, 0, 520, 64, SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        Grid frame;
        Grid root;
        root.Name(L"RootGrid");
        frame.Children().Append(root);
        island.Content(frame);
        auto initialSettings = std::make_shared<ModSettings>();
        initialSettings->moveHotkey.clear();
        initialSettings->fontFamily = L"Segoe UI Variable Text";
        initialSettings->graphColor = kDefaultGraphColor;
        initialSettings->warningColor = kDefaultWarningColor;
        initialSettings->criticalColor = kDefaultCriticalColor;
        g_settings = initialSettings;
        // Satisfy StartMetricsWorker's already-running branch: no providers are
        // started in this fixture, and only synthetic snapshots are published.
        g_metricsWorker.emplace();
        if (!InjectWidget(frame)) throw std::runtime_error("InjectWidget failed");
        struct Scenario { PCWSTR name; int width; int height; int font; bool light; bool unavailable; };
        const Scenario cases[] = {
            {L"dark-410x48.png", 410, 48, 11, false, false},
            {L"light-410x48.png", 410, 48, 11, true, false},
            {L"short-410x30.png", 410, 30, 11, false, false},
            {L"narrow-330x48.png", 330, 48, 11, false, false},
            {L"large-font-430x48.png", 430, 48, 13, false, false},
            {L"unavailable-410x48.png", 410, 48, 11, false, true},
        };
        for (const auto& scenario : cases) {
            auto settings = std::make_shared<ModSettings>(*initialSettings);
            settings->width = scenario.width;
            settings->fontSize = scenario.font;
            settings->shortTaskbar = scenario.height <= 30;
            settings->leftOffset = 0;
            {
                std::lock_guard lock(g_settingsMutex);
                g_settings = settings;
            }
            root.RequestedTheme(scenario.light ? ElementTheme::Light : ElementTheme::Dark);
            root.Background(SolidColorBrush(scenario.light ? Color{255, 242, 242, 242}
                                                          : Color{255, 32, 32, 32}));
            frame.Width(scenario.width);
            frame.Height(scenario.height);
            root.Width(scenario.width);
            root.Height(scenario.height);
            frame.Measure(Size{static_cast<float>(scenario.width), static_cast<float>(scenario.height)});
            frame.Arrange(Rect{0, 0, static_cast<float>(scenario.width), static_cast<float>(scenario.height)});
            frame.UpdateLayout();
            Pump();
            ApplyWidgetSettings();
            if (g_widget.Background())
                throw std::runtime_error("Normal widget acquired a background instead of staying transparent");
            {
                std::lock_guard lock(g_metricsMutex);
                g_publishedMetrics.clear();
            }
            g_cpuHistory.clear(); g_gpuHistory.clear();
            auto now = SampleTime::clock::now();
            for (int i = 0; i <= 60; ++i) {
                MetricsSnapshot sample;
                sample.capturedAt = now - std::chrono::seconds(60 - i);
                sample.cpuAvailable = !scenario.unavailable;
                sample.gpuAvailable = !scenario.unavailable && (i < 25 || i > 30);
                sample.ramAvailable = sample.vramAvailable = !scenario.unavailable;
                sample.cpu = i % 17; sample.gpu = i % 11;
                sample.ram = 52; sample.ramUsedGb = 16.7; sample.ramTotalGb = 32;
                sample.vram = 80; sample.vramUsedGb = 0.4; sample.vramTotalGb = 0.5;
                if (!scenario.unavailable) { sample.cpuTemp = 72; sample.gpuTemp = 56; }
                PublishMetrics(sample);
            }
            UpdateWidgetText(true);
            frame.UpdateLayout();
            Pump();
            if (g_widgetHost.Visibility() == Visibility::Visible && g_widgetHost.ActualHeight() > scenario.height + 0.1) {
                throw std::runtime_error("Widget overflows short taskbar");
            }
            if (scenario.height == 30 && g_widgetHost.Visibility() != Visibility::Visible)
                throw std::runtime_error("Small taskbar support must keep the widget visible");
            SaveImage(frame, argv[1], scenario.name);
            std::wcout << L"RENDERED " << scenario.name << L" host-height="
                       << g_widgetHost.ActualHeight() << L"\n";
        }
        // Model taskbar buttons and tray inside the same selected XAML root.
        // Exercise the production size handlers without refreshing settings.
        Grid buttons;
        buttons.Name(L"TaskbarFrameRepeater");
        Button buttonContent;
        buttonContent.Width(300);
        buttons.Children().Append(buttonContent);
        buttons.Height(38);
        buttons.HorizontalAlignment(HorizontalAlignment::Left);
        buttons.VerticalAlignment(VerticalAlignment::Center);
        buttons.Background(SolidColorBrush(Color{255, 70, 70, 70}));
        Grid tray;
        tray.Name(L"SystemTrayFrame");
        tray.Width(120);
        tray.Height(38);
        tray.HorizontalAlignment(HorizontalAlignment::Right);
        tray.VerticalAlignment(VerticalAlignment::Center);
        tray.Background(SolidColorBrush(Color{255, 90, 90, 90}));
        root.Children().Append(buttons);
        root.Children().Append(tray);
        // A real repeater may stretch even when its buttons occupy only 300 DIPs.
        // Both reported offsets must stay visible with reservation enabled.
        buttonContent.HorizontalAlignment(HorizontalAlignment::Left);
        buttons.HorizontalAlignment(HorizontalAlignment::Stretch);
        frame.Width(1920); root.Width(1920);
        frame.Height(48); root.Height(48);
        auto regressionSettings = std::make_shared<ModSettings>(*initialSettings);
        regressionSettings->reserveSpace = true;
        for (int offset : {0, 2000}) {
            regressionSettings->leftOffset = offset;
            { std::lock_guard lock(g_settingsMutex); g_settings = regressionSettings; }
            if (!RemoveWidget() || !InjectWidget(frame))
                throw std::runtime_error("Stretch regression reinjection failed");
            frame.Measure(Size{1920, 48}); frame.Arrange(Rect{0, 0, 1920, 48});
            frame.UpdateLayout(); Pump(); ApplyWidgetSettings();
            frame.UpdateLayout(); Pump();
            if (g_widgetHost.Visibility() != Visibility::Visible ||
                g_widgetHost.Width() < 348.5)
                throw std::runtime_error("Stretched repeater hides widget with usable space");
        }
        buttons.HorizontalAlignment(HorizontalAlignment::Left);
        if (!RemoveWidget() || !InjectWidget(frame))
            throw std::runtime_error("Adaptive reinjection failed");
        auto settings = std::make_shared<ModSettings>(*initialSettings);
        settings->leftOffset = 3500;
        settings->reserveSpace = true;
        { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
        root.RequestedTheme(ElementTheme::Dark);
        MetricsSnapshot populated;
        populated.capturedAt = SampleTime::clock::now();
        populated.cpuAvailable = populated.gpuAvailable = true;
        populated.ramAvailable = populated.vramAvailable = true;
        populated.cpu = 17; populated.gpu = 25;
        populated.ram = 52; populated.ramUsedGb = 16.7; populated.ramTotalGb = 32;
        populated.vram = 9; populated.vramUsedGb = 2.1; populated.vramTotalGb = 24;
        populated.cpuTemp = 72; populated.gpuTemp = 56;
        PublishMetrics(populated); UpdateWidgetText(true);
        const int widths[] = {1920, 1280, 2560, 700, 400, 1920};
        bool firstLayout = true;
        for (int width : widths) {
            frame.Width(width); root.Width(width);
            frame.Height(48); root.Height(48);
            frame.Measure(Size{static_cast<float>(width), 48});
            frame.Arrange(Rect{0, 0, static_cast<float>(width), 48});
            frame.UpdateLayout(); Pump();
            // Settings are applied once; subsequent layouts use SizeChanged.
            if (firstLayout) { ApplyWidgetSettings(); firstLayout = false; }
            frame.UpdateLayout(); Pump();
            double available = width - tray.ActualWidth();
            TaskbarPlacement expected{};
            if (available - 312 >= 410) expected = {available - 416, 410, 0};
            else if (available - 312 >= 348.5) expected = {306, available - 312, 0};
            std::wcout << L"LAYOUT " << width << L" root=" << root.ActualWidth()
                       << L" available=" << TaskbarAvailableWidth()
                       << L" buttons=" << buttons.ActualWidth()
                       << L" desired=" << buttons.DesiredSize().Width
                       << L" margin=" << buttons.Margin().Left
                       << L" offset=" << g_widgetHost.Margin().Left
                       << L" width=" << g_widgetHost.Width()
                       << L" reserved=" << g_reservedMargin
                       << L" expected=" << expected.left << L"," << expected.width
                       << L"," << expected.reserved << L"\n";
            if (!std::isfinite(g_widgetHost.Width()) ||
                !std::isfinite(g_widgetHost.Height()) ||
                std::abs(g_widgetHost.Margin().Left - expected.left) > 0.1 ||
                std::abs(g_reservedMargin - expected.reserved) > 0.1 ||
                std::abs(g_widgetHost.Width() - expected.width) > 0.1 ||
                (expected.width == 0 && g_widgetHost.Visibility() != Visibility::Collapsed)) {
                throw std::runtime_error("Adaptive resize did not preserve widget/buttons/tray");
            }
            if (g_widgetHost.Visibility() == Visibility::Visible) {
                auto position = g_widgetHost.TransformToVisual(root).TransformPoint({0, 0});
                auto labelPoint = g_cpuLabel.TransformToVisual(g_widget).TransformPoint({0, 0});
                auto memoryPoint = g_ramTrack.TransformToVisual(g_widget).TransformPoint({0, 0});
                if (position.X < buttonContent.ActualWidth() + 5.9 ||
                    position.X + g_widgetHost.ActualWidth() > available - 5.9 ||
                    g_widgetHost.ActualHeight() > root.ActualHeight() + 0.1)
                    throw std::runtime_error("Rendered widget loses minimum clearance from panel controls");
                if (std::abs(labelPoint.X - 6) > .1 ||
                    std::abs(memoryPoint.X + g_ramTrack.ActualWidth() - (settings->width - 6)) > .1)
                    throw std::runtime_error("Rendered widget loses its internal side padding");
            }
            double before = g_reservedMargin;
            ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump();
            if (std::abs(before - g_reservedMargin) > 0.1)
                throw std::runtime_error("Reservation accumulated after repeated placement");
            std::wcout << L"RESIZED " << width << L" offset=" << expected.left
                       << L" width=" << expected.width << L" reserved=" << g_reservedMargin << L"\n";
            if (width == 1280 || width == 700) {
                SaveImage(frame, argv[1], width == 1280 ? L"adaptive-1280.png" : L"adaptive-700.png");
            }
        }
        // Expanding the tray and adding buttons must reclaim space immediately.
        tray.Width(240); buttonContent.Width(600);
        frame.UpdateLayout(); Pump();
        TaskbarPlacement crowded{1264, 410, 0};
        if (std::abs(g_reservedMargin - crowded.reserved) > 0.1)
            throw std::runtime_error("Tray/button size changes did not update reservation");
        // Moving a tray without changing its size must update the usable width.
        tray.Margin(Thickness{0, 0, 20, 0});
        frame.UpdateLayout(); Pump();
        crowded = {1244, 410, 0};
        if (std::abs(g_reservedMargin - crowded.reserved) > 0.1)
            throw std::runtime_error("Tray movement without resize did not update placement");
        tray.Margin(Thickness{}); frame.UpdateLayout(); Pump();
        // Reservation uses the external base margin exactly once.
        buttons.Margin(Thickness{20, 0, 10, 0});
        ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump();
        crowded = {1264, 410, 0};
        if (std::abs(buttons.Margin().Left - (20 + crowded.reserved)) > 0.1)
            throw std::runtime_error("External repeater margins were not preserved");
        // Fractional measurements must settle without accumulating margins.
        buttonContent.Width(600.125);
        tray.Margin(Thickness{0, 0, 20.125, 0});
        buttons.Margin(Thickness{20.125, 0, 10.125, 0});
        ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump();
        double stableMargin = buttons.Margin().Left;
        for (int i = 0; i < 100; ++i) {
            ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump();
        }
        if (std::abs(buttons.Margin().Left - stableMargin) > 0.01)
            throw std::runtime_error("Fractional placement margins drifted");
        settings = std::make_shared<ModSettings>(*settings);
        settings->reserveSpace = false;
        { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
        ApplyWidgetSettings(); frame.UpdateLayout(); Pump();
        if (std::abs(buttons.Margin().Left - 20.125) > 0.1 || g_reservedMargin != 0)
            throw std::runtime_error("Disabling reservation did not restore external margin");
        // Independent fixed controls, transforms and hidden controls must form
        // separate obstacles rather than treating their parent grid as full.
        Button search;
        search.Width(180); search.Height(38); search.HorizontalAlignment(HorizontalAlignment::Left);
        search.Margin(Thickness{850, 0, 0, 0});
        TranslateTransform translation; translation.X(40); search.RenderTransform(translation);
        Button hidden; hidden.Width(1920); hidden.Visibility(Visibility::Collapsed);
        root.Children().Append(search); root.Children().Append(hidden);
        settings->leftOffset = 900; settings->reserveSpace = false;
        { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
        ApplyWidgetSettings(); frame.UpdateLayout(); Pump();
        for (int i = 0; i < 3; ++i) {
            ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump();
        }
        auto searchPoint = search.TransformToVisual(root).TransformPoint({0, 0});
        auto hostPoint = g_widgetHost.TransformToVisual(root).TransformPoint({0, 0});
        if (g_widgetHost.Visibility() != Visibility::Visible ||
            (hostPoint.X < searchPoint.X + search.ActualWidth() - .1 &&
             hostPoint.X + g_widgetHost.Width() > searchPoint.X + .1))
            throw std::runtime_error("Transformed search control was not avoided");
        SaveImage(frame, argv[1], L"mapped-search.png");
        size_t cachedRebuilds = g_geometryCache.rebuilds;
        for (int i = 0; i < 10; ++i) {
            g_cpuUsageText.Text(i % 2 ? L"25%" : L"26%");
            frame.UpdateLayout(); Pump(); ApplyTaskbarPlacement(*settings);
        }
        if (g_geometryCache.rebuilds != cachedRebuilds)
            throw std::runtime_error("Metric text changes rediscovered the entire XAML tree");
        hidden.Height(38); hidden.Visibility(Visibility::Visible);
        frame.UpdateLayout(); Pump();
        if (g_widgetHost.Visibility() != Visibility::Collapsed || g_geometryCache.rebuilds != cachedRebuilds)
            throw std::runtime_error("Cached hidden control did not become an obstacle");
        hidden.Visibility(Visibility::Collapsed); frame.UpdateLayout(); Pump();
        if (g_widgetHost.Visibility() != Visibility::Visible)
            throw std::runtime_error("Hiding a cached obstacle did not restore placement");
        // Replace a child at the same count: stale detached objects must cause
        // rediscovery, not remain in the occupied-area map.
        root.Children().RemoveAtEnd();
        Button replacement; replacement.Width(1920); replacement.Height(38);
        root.Children().Append(replacement); frame.UpdateLayout(); Pump();
        if (g_widgetHost.Visibility() != Visibility::Collapsed || g_geometryCache.rebuilds <= cachedRebuilds)
            throw std::runtime_error("Same-count child replacement escaped cache invalidation");
        root.Children().RemoveAtEnd(); root.Children().RemoveAtEnd();
        // Centered layout does not necessarily translate by Margin.Left. A
        // failed reservation must restore its base once, then remain stable.
        tray.Margin(Thickness{}); tray.Width(120);
        buttonContent.Width(300); buttons.Margin(Thickness{});
        buttons.HorizontalAlignment(HorizontalAlignment::Center);
        settings->leftOffset = 750; settings->reserveSpace = true;
        { std::lock_guard lock(g_settingsMutex); g_settings = settings; }
        frame.UpdateLayout(); Pump(); ApplyWidgetSettings();
        for (int i = 0; i < 20; ++i) { frame.UpdateLayout(); Pump(); }
        if (g_reservedMargin != 0 || g_widgetHost.Visibility() != Visibility::Visible)
            throw std::runtime_error("Rejected centered reservation did not fall back");
        double settledLeft = g_widgetHost.Margin().Left;
        for (int i = 0; i < 100; ++i) { ApplyTaskbarPlacement(*settings); frame.UpdateLayout(); Pump(); }
        if (g_reservedMargin != 0 || std::abs(g_widgetHost.Margin().Left - settledLeft) > .1)
            throw std::runtime_error("Rejected reservation retried in a feedback loop");
        auto buttonPoint = buttonContent.TransformToVisual(root).TransformPoint({0, 0});
        if (settledLeft < buttonPoint.X + buttonContent.ActualWidth() - .1 &&
            settledLeft + g_widgetHost.Width() > buttonPoint.X + .1)
            throw std::runtime_error("Centered fallback overlaps a real button");
        SaveImage(frame, argv[1], L"centered-fallback.png");
        // Render the production native editor without showing any desktop UI.
        StopMetricsWorker(); // Keep these illustrative render samples deterministic.
        g_lastRenderedMetricsSequence = 0;
        g_cpuHistory.clear(); g_gpuHistory.clear();
        { std::lock_guard lock(g_metricsMutex); g_publishedMetrics.clear(); }
        auto previewTime = SampleTime::clock::now();
        for (int i = 0; i <= 60; ++i) {
            MetricsSnapshot sample; sample.capturedAt = previewTime - std::chrono::seconds(60 - i);
            sample.cpuAvailable = sample.gpuAvailable = sample.ramAvailable = sample.vramAvailable = true;
            sample.cpu = 8 + i * .1 + 4 * std::sin(i / 7.0); sample.gpu = 5 + 3 * std::sin(i / 5.0);
            sample.ram = 52; sample.ramUsedGb = 16.7; sample.ramTotalGb = 32;
            sample.vram = 42; sample.vramUsedGb = 10.1; sample.vramTotalGb = 24;
            sample.cpuTemp = 72; sample.gpuTemp = 56; PublishMetrics(sample);
        }
        UpdateWidgetText(true); frame.UpdateLayout(); Pump();
        WNDCLASSW editorClass{}; editorClass.hInstance = PlacementModule();
        editorClass.lpfnWndProc = MoveEditorProc; editorClass.lpszClassName = kMoveWindowClass;
        if (!RegisterClassW(&editorClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
            throw std::runtime_error("Preview test class registration failed");
        g_moveEditor = std::make_shared<MoveEditorState>();
        HWND preview = CreateWindowExW(0, kMoveWindowClass, L"", WS_CHILD, 0, 0, 410, 38,
                                       window, nullptr, editorClass.hInstance, nullptr);
        if (!preview) throw std::runtime_error("Preview render window failed");
        g_moveEditor->window = preview; g_moveEditorWindow = preview; g_moveEditor->source = g_taskbarWindow.load();
        g_moveEditor->target.geometry = {1920, 48, {}, true};
        g_moveEditor->candidate = {500, 410, 0};
        RefreshMovePreviewVisual(); RenderMovePreview();
        if (!g_moveEditor->visual.ready || g_moveEditor->visual.texts.size() != 12 ||
            g_moveEditor->visual.graphs.size() != 2 || g_moveEditor->visual.bars.size() != 4)
            throw std::runtime_error("Move preview does not contain the real widget content");
        auto usageSlot = Controls::Primitives::LayoutInformation::GetLayoutSlot(g_cpuUsageText);
        auto usageParent = g_cpuUsageText.Parent().as<FrameworkElement>();
        auto usageRight = usageParent.TransformToVisual(g_widget).TransformPoint(
            {usageSlot.X + usageSlot.Width, usageSlot.Y});
        const auto& previewUsage = g_moveEditor->visual.texts[1];
        if (std::abs(previewUsage.bounds.X + previewUsage.bounds.Width - usageRight.X) > .1)
            throw std::runtime_error("Native preview utilization lost the right edge of its actual XAML cell");
        for (int index : {0, 3, 6, 9}) {
            const auto& label = g_moveEditor->visual.texts[index]; auto font = MovePreviewFont(label);
            Gdiplus::Graphics measure(g_moveEditor->surface->dc); Gdiplus::RectF bounds;
            measure.MeasureString(label.text.c_str(), static_cast<int>(label.text.size()), font.get(),
                                  Gdiplus::PointF(0, 0), Gdiplus::StringFormat::GenericTypographic(), &bounds);
            if (bounds.Width > label.bounds.Width + .1f || font->GetUnit() != Gdiplus::UnitPixel) {
                std::wcerr << L"Preview label " << label.text << L": glyph width=" << bounds.Width
                           << L", cell=" << label.bounds.Width << L", source font=" << label.font
                           << L", size=" << label.fontSize << L", native size=" << font->GetSize()
                           << L", unit=" << font->GetUnit() << L"\n";
                throw std::runtime_error("DPI/font conversion trims a native preview label");
            }
        }
        auto bodyLayout = CurrentMovePreviewLayout(*g_moveEditor);
        if (bodyLayout.bounds.bottom - bodyLayout.bounds.top != 44 ||
            bodyLayout.bounds.right - bodyLayout.bounds.left != 410)
            throw std::runtime_error("Move preview retained space for removed instructions");
        SendMessageW(preview, WM_SETCURSOR, reinterpret_cast<WPARAM>(preview), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        if (GetCursor() != LoadCursorW(nullptr, IDC_HAND))
            throw std::runtime_error("Move mode did not expose the hand cursor");
        auto* surface = g_moveEditor->surface.get();
        unsigned tintAlpha = surface->pixels[(bodyLayout.body.top + 2) * surface->width + surface->width / 2] >> 24;
        if ((surface->pixels[0] >> 24) != 0 || tintAlpha != 1)
            throw std::runtime_error("Armed move mode lost its minimal hit area or painted glass before dragging");
        HWND layered = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW, L"STATIC", L"", WS_POPUP,
                                        0, 0, 1, 1, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!layered) throw std::runtime_error("Hidden layered preview upload host could not be created");
        bool uploaded = UploadMovePreviewSurface(layered, bodyLayout, *surface);
        RECT uploadedBounds{}; GetWindowRect(layered, &uploadedBounds);
        bool stayedHidden = !IsWindowVisible(layered); DestroyWindow(layered);
        if (!uploaded || !stayedHidden || uploadedBounds.right - uploadedBounds.left != surface->width)
            throw std::runtime_error("Real layered-window alpha upload failed or showed its private test host");
        SourceWidgetOpacityContext opacity{g_moveEditor->source, true};
        OnPreviewSourceThread(opacity.source, SetSourceWidgetOpacity, &opacity);
        g_moveEditor->sourceHidden = opacity.changed; g_moveEditor->sourceOpacity = opacity.original;
        if (!opacity.changed || g_widgetHost.Opacity() != 0)
            throw std::runtime_error("Editing did not hide the duplicate source widget");
        SaveNativePreview(preview, argv[1], L"move-valid.png");
        uint64_t oldSequence = g_moveEditor->visual.sequence;
        MetricsSnapshot live; live.capturedAt = SampleTime::clock::now();
        live.cpuAvailable = live.gpuAvailable = live.ramAvailable = live.vramAvailable = true;
        live.cpu = 61; live.gpu = 33; live.ram = 67; live.ramUsedGb = 21.4; live.ramTotalGb = 32;
        live.vram = 42; live.vramUsedGb = 10.1; live.vramTotalGb = 24;
        live.cpuTemp = 72; live.gpuTemp = 56;
        PublishMetrics(live); UpdateWidgetText(true);
        g_moveEditor->dragging = true;
        SendMessageW(preview, WM_TIMER, 1, 0); // Real callback keeps updating during a drag.
        if (g_moveEditor->visual.sequence <= oldSequence || g_moveEditor->visual.texts[1].text != L"61%" ||
            g_moveEditor->visual.texts[4].text != L"67%" || g_widgetHost.Opacity() != 0)
            throw std::runtime_error("Live preview stopped following metrics while dragging");
        tintAlpha = surface->pixels[2 * surface->width + surface->width / 2] >> 24;
        if (tintAlpha < 100 || tintAlpha >= 255)
            throw std::runtime_error("Active dragging lost its translucent background");
        SaveNativePreview(preview, argv[1], L"move-live-drag.png");
        g_moveEditor->dragging = false; g_moveEditor->hovered = true; g_moveEditor->dirty = true; RenderMovePreview();
        if ((surface->pixels[2 * surface->width + surface->width / 2] >> 24) != 1)
            throw std::runtime_error("Released/hovered preview lost its minimal hit area or retained glass");
        SaveNativePreview(preview, argv[1], L"move-hover.png");
        g_moveEditor->candidate = {}; g_moveEditor->dragging = true; g_moveEditor->dirty = true; RenderMovePreview();
        auto invalidPixel = g_moveEditor->surface->pixels[2 * g_moveEditor->surface->width + g_moveEditor->surface->width / 2];
        if (((invalidPixel >> 16) & 255) <= (invalidPixel & 255))
            throw std::runtime_error("Invalid placement has no red surface feedback");
        SaveNativePreview(preview, argv[1], L"move-invalid.png");
        g_moveEditor->dragging = false; g_moveEditor->dirty = true; RenderMovePreview();
        if ((surface->pixels[2 * surface->width + surface->width / 2] >> 24) != 1)
            throw std::runtime_error("Invalid idle position lost its minimal hit area or retained glass");
        g_moveEditor->candidate = {500, 410, 0}; g_moveEditor->reset = true;
        g_moveEditor->dirty = true; RenderMovePreview(); SaveNativePreview(preview, argv[1], L"move-reset.png");
        g_moveEditor->reset = false;
        g_moveEditor->dragging = true; g_moveEditor->dirty = true;
        root.RequestedTheme(ElementTheme::Light); frame.UpdateLayout(); ApplyWidgetSettings();
        RefreshMovePreviewVisual(); RenderMovePreview(); SaveNativePreview(preview, argv[1], L"move-light.png");
        settings->width = 330; settings->fontSize = 11; ApplyWidgetSettings(); frame.UpdateLayout();
        g_moveEditor->visual.ready = false; g_moveEditor->candidate.width = 330;
        RefreshMovePreviewVisual(); RenderMovePreview(); SaveNativePreview(preview, argv[1], L"move-narrow.png");
        settings->width = 430; settings->fontSize = 13; ApplyWidgetSettings(); frame.UpdateLayout();
        g_cpuUsageText.Text(L"100%"); g_cpuTempText.Text(L"100\u00B0C");
        g_gpuUsageText.Text(L"100%"); g_gpuTempText.Text(L"100\u00B0C"); frame.UpdateLayout();
        g_moveEditor->visual.ready = false; g_moveEditor->candidate.width = 430;
        RefreshMovePreviewVisual(); RenderMovePreview(); SaveNativePreview(preview, argv[1], L"move-large-font.png");
        for (int index : {0, 1, 2, 6, 7, 8}) {
            const auto& text = g_moveEditor->visual.texts[index]; auto font = MovePreviewFont(text);
            Gdiplus::Graphics measure(g_moveEditor->surface->dc); Gdiplus::RectF bounds;
            measure.MeasureString(text.text.c_str(), static_cast<int>(text.text.size()), font.get(),
                                  Gdiplus::PointF(0, 0), Gdiplus::StringFormat::GenericTypographic(), &bounds);
            if (bounds.Width > text.bounds.Width + .1f)
                throw std::runtime_error("Compact compute cells trim maximum values at the largest supported font");
        }
        g_moveEditor->target.scale = 2; RenderMovePreview();
        SaveNativePreview(preview, argv[1], L"move-200pct.png");
        g_moveEditor->target.scale = 1; g_moveEditor->visual.highContrast = true;
        for (auto& text : g_moveEditor->visual.texts) text.color = ColorFromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        for (auto& bar : g_moveEditor->visual.bars) bar.color = ColorFromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        for (auto& graph : g_moveEditor->visual.graphs) graph.color = ColorFromColorRef(GetSysColor(COLOR_WINDOWTEXT));
        g_moveEditor->dirty = true; RenderMovePreview(); SaveNativePreview(preview, argv[1], L"move-high-contrast.png");
        DestroyWindow(preview); UnregisterClassW(kMoveWindowClass, editorClass.hInstance);
        if (g_widgetHost.Opacity() != opacity.original || g_previewGraphicsToken != 0)
            throw std::runtime_error("Closing the preview lost source opacity or retained its graphics runtime");
        StopMetricsWorker();
        auto historyTime = std::chrono::steady_clock::now();
        g_cpuHistory = {{historyTime, 25}, {historyTime + std::chrono::seconds(1), 26}};
        g_gpuHistory = {{historyTime, 10}, {historyTime + std::chrono::seconds(1), std::nullopt}};
        g_lastRenderedMetricsSequence = 42;
        RemoveWidgetForMoveContext moveRemoval; RemoveWidgetForMove(&moveRemoval);
        if (!moveRemoval.succeeded || g_cpuHistory.size() != 2 || g_gpuHistory.size() != 2 ||
            g_cpuHistory.back().value != 26 || g_gpuHistory.back().value ||
            g_lastRenderedMetricsSequence != 42 || !g_geometryCache.elements.empty())
            throw std::runtime_error("Move teardown lost metric history/sequence or retained XAML cache");
        std::wcout << L"PASS: XAML resize/restore, mapping/cache, margins, reservation rollback, preview renders, history-preserving teardown\n";

    } catch (const hresult_error& error) {
        std::wcerr << L"XAML failure 0x" << std::hex << static_cast<uint32_t>(error.code())
                   << L": " << error.message().c_str() << L"\n";
        result = 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << "\n";
        result = 1;
    }
    g_unloading = true;
    StopMetricsWorker();
    RemoveTaskbarUiContext cleanup;
    RemoveFromCurrentTaskbar(&cleanup);
    if (!cleanup.succeeded || g_widget || g_widgetHost || g_timer ||
        g_rootSizeChangedToken.value || g_actualThemeChangedToken.value ||
        g_rootLayoutUpdatedToken.value || g_systemTrayFrame) {
        std::cerr << "XAML cleanup did not release all widget resources\n";
        result = 1;
    }
    if (island) { island.Content(nullptr); island.Close(); island = nullptr; }
    if (window) DestroyWindow(window);
    if (manager) { manager.Close(); manager = nullptr; Pump(); }
    return result;
}
