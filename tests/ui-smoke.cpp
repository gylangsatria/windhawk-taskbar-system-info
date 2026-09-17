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
            if (g_widgetHost.ActualHeight() > scenario.height + 0.1) {
                throw std::runtime_error("Widget overflows short taskbar");
            }
            SaveImage(frame, argv[1], scenario.name);
            std::wcout << L"RENDERED " << scenario.name << L" host-height="
                       << g_widgetHost.ActualHeight() << L"\n";
        }
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
        g_rootSizeChangedToken.value || g_actualThemeChangedToken.value) {
        std::cerr << "XAML cleanup did not release all widget resources\n";
        result = 1;
    }
    if (island) { island.Content(nullptr); island.Close(); island = nullptr; }
    if (window) DestroyWindow(window);
    if (manager) { manager.Close(); manager = nullptr; Pump(); }
    return result;
}
