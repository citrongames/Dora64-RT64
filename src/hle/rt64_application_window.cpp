//
// RT64
//

#include "rt64_application_window.h"

#include <algorithm>
#include <cassert>
#include <stdio.h>
#include <SDL.h>

#if defined(_WIN32)
#   include <Windows.h>
#   include <ShellScalingAPI.h>
#elif defined(__linux__)
#   define Status int
#   if !defined(RT64_SDL_WINDOW_VULKAN)
#      include <X11/extensions/Xrandr.h>
#   endif
#elif defined(__APPLE__)
#   include "rt64_application.h"
#   include "apple/rt64_apple.h"
#endif

#include "common/rt64_common.h"

namespace RT64 {
    // ApplicationWindow

    ApplicationWindow *ApplicationWindow::HookedApplicationWindow = nullptr;

    ApplicationWindow::ApplicationWindow() {
        // Empty.
    }

    ApplicationWindow::~ApplicationWindow() {
        if (HookedApplicationWindow == this) {
            HookedApplicationWindow = nullptr;
        }

#   ifdef _WIN32
        if (windowHook != nullptr) {
            UnhookWindowsHookEx(windowHook);
        }
#   endif
    }

    void ApplicationWindow::setup(RenderWindow window, Listener *listener, uint32_t threadId) {
        assert(listener != nullptr);

        this->listener = listener;

        windowHandle = window;

#   if defined(RT64_SDL_WINDOW_VULKAN)
        sdlWindow = window;
#   endif

#   if defined(__APPLE__)
        windowWrapper = std::make_unique<CocoaWindow>(window.window);
#   endif

        if (listener->usesWindowMessageFilter()) {
            if ((sdlWindow == nullptr) && SDL_WasInit(SDL_INIT_VIDEO)) {
                // We'd normally install the event filter here, but Mupen does not set its own event filter
                // until much later. Instead, we delegate this to the first time a screen update is sent.
                // FIXME: We attempt to get the first window created by SDL2. This can be improved later
                // by actually passing the SDL_Window handle through as a parameter.
                sdlWindow = SDL_GetWindowFromID(1);
            }

            if (sdlWindow == nullptr) {
#           ifdef _WIN32
                assert(HookedApplicationWindow == nullptr);
                assert(threadId != 0);
                windowHook = SetWindowsHookEx(WH_GETMESSAGE, &windowHookCallback, NULL, threadId);
                HookedApplicationWindow = this;
#           endif
            }
        }
    }

    void ApplicationWindow::setup(const char *windowTitle, Listener *listener) {
        assert(windowTitle != nullptr);

        // Find the right window dimension and placement.
        const int Width = 1280;
        const int Height = 720;
        struct {
            uint32_t left, top, width, height;
        } bounds{};

#   if defined(_WIN32)
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

        RECT rect;
        UINT dwStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
        rect.left = (GetSystemMetrics(SM_CXSCREEN) - Width) / 2;
        rect.top = (GetSystemMetrics(SM_CYSCREEN) - Height) / 2;
        rect.right = rect.left + Width;
        rect.bottom = rect.top + Height;
        AdjustWindowRectEx(&rect, dwStyle, 0, 0);
        bounds.left = rect.left;
        bounds.top = rect.top;
        bounds.width = rect.right - rect.left;
        bounds.height = rect.bottom - rect.top;
#   elif defined(__ANDROID__)
        static_assert(false && "Android unimplemented");
#   elif defined(__linux__) || defined(__APPLE__)
        if (SDL_VideoInit(nullptr) != 0) {
            printf("Failed to init SDL2 video: %s\n", SDL_GetError());
            assert(false && "Failed to init SDL2 video");
            return;
        }
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(0, &dm) != 0) {
            printf("Failed to get SDL2 desktop display mode: %s\n", SDL_GetError());
            assert(false && "Failed to get SDL2 desktop display mode");
            return;
        }
        bounds.left = (dm.w - Width) / 2;
        bounds.top = (dm.h - Height) / 2;
        bounds.width = Width;
        bounds.height = Height;
#   else
        static_assert(false && "Unimplemented");
#   endif
        uint32_t createFlags = SDL_WINDOW_RESIZABLE;
#   if defined(__APPLE__)
        createFlags |= SDL_WINDOW_METAL;
#   endif

        // Create window.
        uint32_t flags = SDL_WINDOW_RESIZABLE;
        # if defined(__APPLE__)
        flags |= SDL_WINDOW_METAL;
        # elif defined(RT64_SDL_WINDOW_VULKAN)
        flags |= SDL_WINDOW_VULKAN;
        #endif
        sdlWindow = SDL_CreateWindow(windowTitle, bounds.left, bounds.top, bounds.width, bounds.height, flags);
        assert((sdlWindow != nullptr) && "Failed to open window with SDL");

        // Get native window handles from the window.
        SDL_SysWMinfo wmInfo;
        SDL_VERSION(&wmInfo.version);
        SDL_GetWindowWMInfo(sdlWindow, &wmInfo);
#   if defined(_WIN32)
        windowHandle = wmInfo.info.win.window;
#   elif defined(RT64_SDL_WINDOW_VULKAN)
        windowHandle = sdlWindow;
#   elif defined(__ANDROID__)
        static_assert(false && "Android unimplemented");
#   elif defined(__linux__)
        windowHandle.display = wmInfo.info.x11.display;
        windowHandle.window = wmInfo.info.x11.window;
#   elif defined(__APPLE__)
        windowHandle.window = sdlWindow;
        SDL_MetalView view = SDL_Metal_CreateView(sdlWindow);
        windowHandle.view = SDL_Metal_GetLayer(view);
#   else
        static_assert(false && "Unimplemented");
#   endif

#   ifdef _WIN32
        setup(windowHandle, listener, GetCurrentThreadId());
#   elif defined(__APPLE__)
        uint64_t tid;
        pthread_threadid_np(nullptr, &tid);
        setup(windowHandle, listener, tid);
#   else
        setup(windowHandle, listener, pthread_self());
#   endif
    }

    void ApplicationWindow::setFullScreen(bool newFullScreen) {
        if (newFullScreen == fullScreen) {
            return;
        }

#   ifdef _WIN32
        if (newFullScreen) {
            // Save if window is maximized or not
            WINDOWPLACEMENT windowPlacement;
            windowPlacement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(windowHandle, &windowPlacement);
            lastMaximizedState = windowPlacement.showCmd == SW_SHOWMAXIMIZED;

            // Save window position and size if the window is not maximized
            GetWindowRect(windowHandle, &lastWindowRect);

            // Get in which monitor the window is
            HMONITOR hmonitor = MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST);

            // Get info from that monitor
            MONITORINFOEX monitorInfo;
            monitorInfo.cbSize = sizeof(MONITORINFOEX);
            GetMonitorInfo(hmonitor, &monitorInfo);
            RECT r = monitorInfo.rcMonitor;

            // Set borderless full screen to that monitor
            LONG_PTR lStyle = GetWindowLongPtr(windowHandle, GWL_STYLE);
            lStyle = (lStyle | WS_VISIBLE | WS_POPUP) & ~WS_OVERLAPPEDWINDOW;
            SetWindowLongPtr(windowHandle, GWL_STYLE, lStyle);
            SetWindowPos(windowHandle, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED);
            if (windowMenu != nullptr) {
                SetMenu(windowHandle, nullptr);
            }
        }
        else {
            // Set in window mode with the last saved position and size.
            LONG_PTR lStyle = GetWindowLongPtr(windowHandle, GWL_STYLE);
            lStyle = (lStyle | WS_VISIBLE | WS_OVERLAPPEDWINDOW) & ~WS_POPUP;
            SetWindowLongPtr(windowHandle, GWL_STYLE, lStyle);

            if (lastMaximizedState) {
                SetWindowPos(windowHandle, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE);
                ShowWindow(windowHandle, SW_MAXIMIZE);
            }
            else {
                const RECT &r = lastWindowRect;
                SetWindowPos(windowHandle, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED);
                ShowWindow(windowHandle, SW_RESTORE);
            }

            if (windowMenu != nullptr) {
                SetMenu(windowHandle, windowMenu);
            }
        }

        fullScreen = newFullScreen;
#   elif defined(__APPLE__)
        windowWrapper->toggleFullscreen();
#   elif defined(RT64_SDL_WINDOW_VULKAN)
        if (newFullScreen) {
            SDL_SetWindowFullscreen(windowHandle, SDL_WINDOW_FULLSCREEN_DESKTOP);
        } else {
            SDL_SetWindowFullscreen(windowHandle, 0);
        }
        fullScreen = newFullScreen;
#   endif
    }

    bool ApplicationWindow::setDisplayConfig(DisplayMode mode, int32_t displayIndex, int32_t width, int32_t height, int32_t requestedRefreshRate) {
        if (sdlWindow == nullptr) {
            setFullScreen(mode == DisplayMode::BorderlessFullscreen);
            return true;
        }

        const int displayCount = SDL_GetNumVideoDisplays();
        if (displayCount <= 0) {
            fprintf(stderr, "SDL_GetNumVideoDisplays failed: %s\n", SDL_GetError());
            return false;
        }

        displayIndex = std::clamp(displayIndex, 0, displayCount - 1);
        width = std::max(width, 320);
        height = std::max(height, 240);
        (void)requestedRefreshRate;

        const uint32_t currentFlags = SDL_GetWindowFlags(sdlWindow);
        if ((mode == DisplayMode::Windowed) ||
            ((currentFlags & SDL_WINDOW_FULLSCREEN) != 0)) {
            if (SDL_SetWindowFullscreen(sdlWindow, 0) != 0) {
                fprintf(stderr, "Unable to leave fullscreen mode: %s\n", SDL_GetError());
                return false;
            }
        }

        if (SDL_SetWindowDisplayMode(sdlWindow, nullptr) != 0) {
            fprintf(stderr, "Unable to reset the SDL display mode: %s\n", SDL_GetError());
            return false;
        }

        const int centered = SDL_WINDOWPOS_CENTERED_DISPLAY(displayIndex);
        SDL_SetWindowPosition(sdlWindow, centered, centered);

        if (mode == DisplayMode::Windowed) {
#       ifdef _WIN32
            // SDL can leave an externally hooked HWND with the popup style
            // after fullscreen. Restore the normal window chrome
            // explicitly before applying the requested client size.
            LONG_PTR windowStyle = GetWindowLongPtr(windowHandle, GWL_STYLE);
            windowStyle = (windowStyle | WS_VISIBLE | WS_OVERLAPPEDWINDOW) & ~WS_POPUP;
            SetWindowLongPtr(windowHandle, GWL_STYLE, windowStyle);
            SetWindowPos(
                windowHandle,
                HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (windowMenu != nullptr) {
                SetMenu(windowHandle, windowMenu);
            }
#       endif
            SDL_RestoreWindow(sdlWindow);
            SDL_SetWindowSize(sdlWindow, width, height);

            SDL_Rect usableBounds{};
            if (SDL_GetDisplayUsableBounds(displayIndex, &usableBounds) == 0) {
#           ifdef _WIN32
                // SDL positions the client origin for this externally hooked
                // HWND, which puts the non-client title bar above Y=0 for an
                // oversized window. Position the outer Win32 rectangle directly
                // so the title bar always remains inside the monitor work area.
                RECT outerRect{};
                GetWindowRect(windowHandle, &outerRect);
                const int outerWidth = outerRect.right - outerRect.left;
                const int outerHeight = outerRect.bottom - outerRect.top;
                const int windowX = usableBounds.x +
                    std::max((usableBounds.w - outerWidth) / 2, 0);
                const int windowY = usableBounds.y +
                    std::max((usableBounds.h - outerHeight) / 2, 0);
                SetWindowPos(
                    windowHandle,
                    nullptr,
                    windowX,
                    windowY,
                    0,
                    0,
                    SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
#           else
                int borderTop = 0;
                int borderLeft = 0;
                int borderBottom = 0;
                int borderRight = 0;
                SDL_GetWindowBordersSize(
                    sdlWindow,
                    &borderTop,
                    &borderLeft,
                    &borderBottom,
                    &borderRight);

                const int outerWidth = width + borderLeft + borderRight;
                const int outerHeight = height + borderTop + borderBottom;
                const int outerX = usableBounds.x +
                    std::max((usableBounds.w - outerWidth) / 2, 0);
                const int outerY = usableBounds.y +
                    std::max((usableBounds.h - outerHeight) / 2, 0);
                const int windowX = outerX + borderLeft;
                const int windowY = outerY + borderTop;
                SDL_SetWindowPosition(sdlWindow, windowX, windowY);
#           endif
            }
            else {
                fprintf(stderr, "Unable to query display usable bounds: %s\n", SDL_GetError());
                SDL_SetWindowPosition(sdlWindow, centered, centered);
            }
            fullScreen = false;
            detectRefreshRate();
            return true;
        }

        if (SDL_SetWindowFullscreen(sdlWindow, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
            fprintf(stderr, "Unable to enter fullscreen mode: %s\n", SDL_GetError());
            SDL_SetWindowDisplayMode(sdlWindow, nullptr);
            SDL_SetWindowSize(sdlWindow, width, height);
            SDL_SetWindowPosition(sdlWindow, centered, centered);
            fullScreen = false;
            return false;
        }

        fullScreen = true;
        detectRefreshRate();
        return true;
    }

    void ApplicationWindow::makeResizable() {
#   ifdef _WIN32
        LONG_PTR lStyle = GetWindowLongPtr(windowHandle, GWL_STYLE);
        windowMenu = GetMenu(windowHandle);
        lStyle |= WS_THICKFRAME | WS_MAXIMIZEBOX;
        SetWindowLongPtr(windowHandle, GWL_STYLE, lStyle);
#   endif
    }

    void ApplicationWindow::detectRefreshRate() {
#   if defined(_WIN32)
        HMONITOR monitor = MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEX info = {};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfo(monitor, &info)) {
            fprintf(stderr, "GetMonitorInfo failed.\n");
            return;
        }

        DEVMODE displayMode = {};
        displayMode.dmSize = sizeof(DEVMODE);
        if (!EnumDisplaySettings(info.szDevice, ENUM_CURRENT_SETTINGS, &displayMode)) {
            fprintf(stderr, "EnumDisplaySettings failed.\n");
            return;
        }

        if (displayMode.dmDisplayFlags & DM_INTERLACED) {
            // Interlaced modes report the refresh rate as half of what the actual refresh rate is.
            refreshRate = displayMode.dmDisplayFrequency * 2;
        }
        else {
            refreshRate = displayMode.dmDisplayFrequency;
        }

        // FIXME: This function truncates refresh rates that'd otherwise round to the correct rate in most cases.
        // This hack will fix most common cases where refresh rates divisble by 10 are truncated to the wrong value.
        // This can be removed when a more accurate way to query the refresh rate of the monitor is found.
        if ((refreshRate % 10) == 9) {
            refreshRate++;
        }
#   elif defined(RT64_SDL_WINDOW_VULKAN)
        int displayIndex = SDL_GetWindowDisplayIndex(windowHandle);
        if (displayIndex < 0) {
            fprintf(stderr, "SDL_GetWindowDisplayIndex failed. Error: %s.\n", SDL_GetError());
            return;
        }

        SDL_DisplayMode displayMode = {};
        int modeResult = SDL_GetCurrentDisplayMode(displayIndex, &displayMode);
        if (modeResult != 0) {
            fprintf(stderr, "SDL_GetCurrentDisplayMode failed. Error: %s.\n", SDL_GetError());
            return;
        }

        refreshRate = displayMode.refresh_rate;
#   elif defined(__linux__)
        // Sourced from: https://stackoverflow.com/a/66865623
        XRRScreenResources *screenResources = XRRGetScreenResources(windowHandle.display, windowHandle.window);
        if (screenResources == nullptr) {
            fprintf(stderr, "XRRGetScreenResources failed.\n");
            return;
        }

        RRMode activeModeID = 0;
        for (int i = 0; i < screenResources->ncrtc; ++i) {
            XRRCrtcInfo *crtcInfo = XRRGetCrtcInfo(windowHandle.display, screenResources, screenResources->crtcs[i]);
            if (crtcInfo != nullptr) {
                if (crtcInfo->mode != 0L) {
                    activeModeID = crtcInfo->mode;
                }

                XRRFreeCrtcInfo(crtcInfo);
            }
        }

        if (activeModeID == 0L) {
            fprintf(stderr, "Unable to find active mode through XRRGetScreenResources and XRRGetCrtcInfo.\n");
            XRRFreeScreenResources(screenResources);
            return;
        }

        for (int i = 0; i < screenResources->nmode; ++i) {
            XRRModeInfo modeInfo = screenResources->modes[i];
            if (modeInfo.id == activeModeID) {
                refreshRate = std::lround(modeInfo.dotClock / double(modeInfo.hTotal * modeInfo.vTotal));
                break;
            }
        }

        XRRFreeScreenResources(screenResources);
#   elif defined(__APPLE__)
        refreshRate = windowWrapper->getRefreshRate();
#   endif
    }

    uint32_t ApplicationWindow::getRefreshRate() const {
        return refreshRate;
    }

    bool ApplicationWindow::detectWindowMoved() {
        int32_t newWindowLeft = INT32_MAX;
        int32_t newWindowTop = INT32_MAX;

#   if defined(_WIN64)
        RECT rect;
        GetWindowRect(windowHandle, &rect);
        newWindowLeft = rect.left;
        newWindowTop = rect.top;
#   elif defined(RT64_SDL_WINDOW_VULKAN)
        SDL_GetWindowPosition(windowHandle, &newWindowLeft, &newWindowTop);
#   elif defined(__linux__)
        XWindowAttributes attributes;
        XGetWindowAttributes(windowHandle.display, windowHandle.window, &attributes);
        newWindowLeft = attributes.x;
        newWindowTop = attributes.y;
#   elif defined(__APPLE__)
        CocoaWindowAttributes attributes;
        windowWrapper->getWindowAttributes(&attributes);
        newWindowLeft = attributes.x;
        newWindowTop = attributes.y;
#   endif

        if ((windowLeft != newWindowLeft) || (windowTop != newWindowTop)) {
            windowLeft = newWindowLeft;
            windowTop = newWindowTop;
            return true;
        }
        else {
            return false;
        }
    }

#ifdef _WIN32
    void ApplicationWindow::windowMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        if (listener->windowMessageFilter(message, wParam, lParam)) {
            return;
        }

        switch (message) {
        case WM_CLOSE:
            PostQuitMessage(0);
            break;
        };
    }

    LRESULT CALLBACK ApplicationWindow::windowHookCallback(int nCode, WPARAM wParam, LPARAM lParam) {
        if ((nCode >= 0) && (wParam == PM_REMOVE)) {
            const MSG &msg = *reinterpret_cast<MSG *>(lParam);
            HookedApplicationWindow->windowMessage(msg.message, msg.wParam, msg.lParam);
        }

        return CallNextHookEx(NULL, nCode, wParam, lParam);
    }
#endif

    void ApplicationWindow::sdlCheckFilterInstallation() {
        if (listener->usesWindowMessageFilter() && !sdlEventFilterInstalled && (sdlWindow != nullptr)) {
            if (!SDL_GetEventFilter(&sdlEventFilterStored, &sdlEventFilterUserdata)) {
                sdlEventFilterStored = nullptr;
                sdlEventFilterUserdata = nullptr;
            }

            SDL_SetEventFilter(&ApplicationWindow::sdlEventFilter, this);
            sdlEventFilterInstalled = true;
        }
    }

    int ApplicationWindow::sdlEventFilter(void *userdata, SDL_Event *event) {
        // Run it through the listener's event filter. If it's processed by the listener, the event should be filtered.
        ApplicationWindow *appWindow = reinterpret_cast<ApplicationWindow *>(userdata);
        if (appWindow->listener->sdlEventFilter(event)) {
            return 0;
        }

        // Pass to the event filter that was stored if it exists. Let the original filter determine the result.
        if (appWindow->sdlEventFilterStored != nullptr) {
            return appWindow->sdlEventFilterStored(appWindow->sdlEventFilterUserdata, event);
        }
        // The event should not be filtered.
        else {
            return 1;
        }
    }
};
