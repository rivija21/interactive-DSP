// Filter Lab for Windows: the window, the menus and the message loop (the counterpart of
// AppDelegate.swift and main.swift).
#include "app/Export.h"
#include "app/LabModel.h"
#include "app/Lessons.h"
#include "audio/AudioFile.h"
#include "platform/Dialogs.h"
#include "platform/Dispatch.h"
#include "platform/Settings.h"
#include "ui/MainView.h"
#include "util/Format.h"
#include "util/Text.h"
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <windowsx.h>
#include <memory>

#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

namespace {

constexpr UINT WM_APP_DISPATCH = WM_APP + 1;
constexpr UINT WM_APP_END_EDIT = WM_APP + 2;
constexpr UINT_PTR kFrameTimer = 1;

enum Command : UINT {
    cmdOpenAudio = 100,
    cmdExportAudio,
    cmdExportResponse,
    cmdExportZ,
    cmdExportLive,
    cmdClose,
    cmdExit,
    cmdUndo = 200,
    cmdRedo,
    cmdCut,
    cmdCopy,
    cmdPaste,
    cmdSelectAll,
    cmdCopyCode0 = 210,   // + language index
    cmdDeleteSelected = 220,
    cmdLive0 = 300,       // + live view index
    cmdResponse0 = 310,   // + response mode index
    cmdLogAxis = 320,
    cmdMap,
    cmdPrediction,
    cmdHold,
    cmdResetZoom,
    cmdTheory,
    cmdFullScreen,
    cmdListen = 400,
    cmdFilterOn,
    cmdPause,
    cmdSource0 = 410,     // + source index
    cmdRate0 = 430,       // + rate index
    cmdLessons = 500,
    cmdLesson0 = 510,     // + lesson index
    cmdExperiment0 = 540, // + experiment index
    cmdMinimize = 600,
    cmdZoom,
    cmdHelpLessons = 700,
    cmdAbout,
};

LabModel* gModel = nullptr;
MainView* gMain = nullptr;
HWND gWindow = nullptr;
HMENU gMenu = nullptr;
HACCEL gAccel = nullptr;
bool gFullScreen = false;
WINDOWPLACEMENT gPlacementBeforeFullScreen = {sizeof(WINDOWPLACEMENT)};
LONG_PTR gStyleBeforeFullScreen = 0;

std::wstring menuText(const std::string& title, const std::string& shortcut = "") {
    std::wstring w = widen(replaceAll(title, "&", "&&"));
    if (!shortcut.empty()) w += L"\t" + widen(shortcut);
    return w;
}

void add(HMENU menu, UINT id, const std::string& title, const std::string& shortcut = "") {
    AppendMenuW(menu, MF_STRING, id, menuText(title, shortcut).c_str());
}

void separator(HMENU menu) { AppendMenuW(menu, MF_SEPARATOR, 0, nullptr); }

/// Titles here may carry an "&" mnemonic, so they're passed through unescaped.
void submenu(HMENU parent, HMENU child, const std::string& title) {
    AppendMenuW(parent, MF_POPUP, UINT_PTR(child), widen(title).c_str());
}

HMENU buildMenu() {
    HMENU main = CreateMenu();

    HMENU file = CreatePopupMenu();
    add(file, cmdOpenAudio, "Open Audio File\xE2\x80\xA6", "Ctrl+O");
    add(file, cmdExportAudio, "Export Filtered Audio\xE2\x80\xA6", "Ctrl+E");
    separator(file);
    add(file, cmdExportResponse, "Export Response Figure\xE2\x80\xA6", "Ctrl+Shift+R");
    add(file, cmdExportZ, "Export Z-plane Figure\xE2\x80\xA6", "Ctrl+Shift+P");
    add(file, cmdExportLive, "Export Live View Figure\xE2\x80\xA6", "Ctrl+Shift+L");
    separator(file);
    add(file, cmdClose, "Close Window", "Ctrl+W");
    add(file, cmdExit, "Exit Filter Lab", "Ctrl+Q");
    submenu(main, file, "&File");

    HMENU edit = CreatePopupMenu();
    add(edit, cmdUndo, "Undo", "Ctrl+Z");
    add(edit, cmdRedo, "Redo", "Ctrl+Shift+Z");
    separator(edit);
    add(edit, cmdCut, "Cut", "Ctrl+X");
    add(edit, cmdCopy, "Copy", "Ctrl+C");
    add(edit, cmdPaste, "Paste", "Ctrl+V");
    add(edit, cmdSelectAll, "Select All", "Ctrl+A");
    separator(edit);
    const char* codeKeys[] = {"Ctrl+Alt+M", "Ctrl+Alt+Y", "Ctrl+Alt+K"};
    for (int i = 0; i < 3; i++)
        add(edit, cmdCopyCode0 + UINT(i), std::string("Copy Filter as ") + languageTitle(kAllLanguages[i]), codeKeys[i]);
    separator(edit);
    add(edit, cmdDeleteSelected, "Delete Selected Pole/Zero", "Del");
    submenu(main, edit, "&Edit");

    HMENU view = CreatePopupMenu();
    for (int i = 0; i < 3; i++) add(view, cmdLive0 + UINT(i), liveViewTitle(kAllLiveViews[i]), "Ctrl+" + std::to_string(i + 1));
    separator(view);
    for (int i = 0; i < 5; i++)
        add(view, cmdResponse0 + UINT(i), std::string("Response: ") + responseModeTitle(kAllResponseModes[i]),
            "Ctrl+Alt+" + std::to_string(i + 1));
    separator(view);
    add(view, cmdLogAxis, "Logarithmic Frequency Axis", "Ctrl+G");
    add(view, cmdMap, "|H(z)| Map in Z-plane");
    add(view, cmdPrediction, "Predicted Output on Spectrum");
    add(view, cmdHold, "Hold Live Display", "Ctrl+Shift+H");
    add(view, cmdResetZoom, "Reset Z-plane Zoom", "Ctrl+0");
    separator(view);
    add(view, cmdTheory, "Show Theory Panel", "Ctrl+T");
    add(view, cmdFullScreen, "Enter Full Screen", "F11");
    submenu(main, view, "&View");

    HMENU audio = CreatePopupMenu();
    add(audio, cmdListen, "Listen", "Ctrl+L");
    add(audio, cmdFilterOn, "Filter On (Space switches A/B)", "Ctrl+B");
    add(audio, cmdPause, "Pause Input", "Ctrl+.");
    separator(audio);
    HMENU sources = CreatePopupMenu();
    for (int i = 0; i < int(std::size(kAllSources)); i++) add(sources, cmdSource0 + UINT(i), sourceTitle(kAllSources[i]));
    submenu(audio, sources, "Source");
    HMENU rates = CreatePopupMenu();
    for (size_t i = 0; i < LabModel::sampleRates.size(); i++) add(rates, cmdRate0 + UINT(i), formatHz(LabModel::sampleRates[i]));
    submenu(audio, rates, "Sample Rate");
    submenu(main, audio, "&Audio");

    HMENU learn = CreatePopupMenu();
    add(learn, cmdLessons, "All Lessons");
    separator(learn);
    for (size_t i = 0; i < Lessons::all().size(); i++) add(learn, cmdLesson0 + UINT(i), Lessons::all()[i].title);
    separator(learn);
    HMENU experiments = CreatePopupMenu();
    for (size_t i = 0; i < Experiments::all().size(); i++) add(experiments, cmdExperiment0 + UINT(i), Experiments::all()[i].title);
    submenu(learn, experiments, "Experiments");
    submenu(main, learn, "&Learn");

    HMENU window = CreatePopupMenu();
    add(window, cmdMinimize, "Minimize", "Ctrl+M");
    add(window, cmdZoom, "Zoom");
    submenu(main, window, "&Window");

    HMENU help = CreatePopupMenu();
    add(help, cmdHelpLessons, "Filter Lab Lessons", "F1");
    separator(help);
    add(help, cmdAbout, "About Filter Lab");
    submenu(main, help, "&Help");
    return main;
}

HACCEL buildAccelerators() {
    std::vector<ACCEL> a;
    auto key = [&](BYTE mods, WORD vk, UINT cmd) { a.push_back({BYTE(FVIRTKEY | mods), vk, WORD(cmd)}); };
    const BYTE C = FCONTROL, S = FSHIFT, A = FALT;
    key(C, 'O', cmdOpenAudio);
    key(C, 'E', cmdExportAudio);
    key(C | S, 'R', cmdExportResponse);
    key(C | S, 'P', cmdExportZ);
    key(C | S, 'L', cmdExportLive);
    key(C, 'W', cmdClose);
    key(C, 'Q', cmdExit);
    key(C, 'Z', cmdUndo);
    key(C | S, 'Z', cmdRedo);
    key(C, 'Y', cmdRedo);
    key(C, 'C', cmdCopy);
    key(C, 'A', cmdSelectAll);
    key(C | A, 'M', cmdCopyCode0 + 0);
    key(C | A, 'Y', cmdCopyCode0 + 1);
    key(C | A, 'K', cmdCopyCode0 + 2);
    for (int i = 0; i < 3; i++) key(C, WORD('1' + i), cmdLive0 + UINT(i));
    for (int i = 0; i < 5; i++) key(C | A, WORD('1' + i), cmdResponse0 + UINT(i));
    key(C, 'G', cmdLogAxis);
    key(C | S, 'H', cmdHold);
    key(C, '0', cmdResetZoom);
    key(C, 'T', cmdTheory);
    key(0, VK_F11, cmdFullScreen);
    key(C, 'L', cmdListen);
    key(C, 'B', cmdFilterOn);
    key(C, VK_OEM_PERIOD, cmdPause);
    key(C, 'M', cmdMinimize);
    key(0, VK_F1, cmdHelpLessons);
    return CreateAcceleratorTableW(a.data(), int(a.size()));
}

void check(UINT id, bool on) { CheckMenuItem(gMenu, id, MF_BYCOMMAND | (on ? MF_CHECKED : MF_UNCHECKED)); }
void enable(UINT id, bool on) { EnableMenuItem(gMenu, id, MF_BYCOMMAND | (on ? MF_ENABLED : MF_GRAYED)); }

/// The counterpart of validateMenuItem: check marks and enabled states.
void updateMenus() {
    LabModel& m = *gModel;
    check(cmdLogAxis, m.logAxis());
    check(cmdMap, m.showHMap());
    check(cmdPrediction, m.showPrediction());
    check(cmdHold, m.hold());
    check(cmdListen, m.listen());
    check(cmdFilterOn, m.filterOn());
    check(cmdPause, m.paused());
    check(cmdTheory, gMain->theoryVisible());
    check(cmdFullScreen, gFullScreen);
    for (int i = 0; i < 3; i++) check(cmdLive0 + UINT(i), int(m.liveView()) == i);
    for (int i = 0; i < 5; i++) check(cmdResponse0 + UINT(i), int(m.responseMode()) == i);
    for (int i = 0; i < int(std::size(kAllSources)); i++) check(cmdSource0 + UINT(i), int(m.source()) == i);
    for (size_t i = 0; i < LabModel::sampleRates.size(); i++) check(cmdRate0 + UINT(i), LabModel::sampleRates[i] == m.fs());
    enable(cmdDeleteSelected, m.selectedItem().has_value() && m.spec().method == DesignMethod::poleZero);
    enable(cmdExportAudio, m.source() != SourceKind::microphone);
    Host& host = Host::shared();
    bool editing = host.isEditing();
    enable(cmdUndo, editing || m.canUndo());
    enable(cmdRedo, !editing && m.canRedo());
    ModifyMenuW(gMenu, cmdUndo, MF_BYCOMMAND | MF_STRING, cmdUndo,
                menuText(!editing && m.canUndo() ? "Undo Change Filter" : "Undo", "Ctrl+Z").c_str());
    ModifyMenuW(gMenu, cmdRedo, MF_BYCOMMAND | MF_STRING, cmdRedo,
                menuText(!editing && m.canRedo() ? "Redo Change Filter" : "Redo", "Ctrl+Shift+Z").c_str());
    enable(cmdUndo, editing || m.canUndo());
    enable(cmdRedo, !editing && m.canRedo());
    enable(cmdCut, editing);
    enable(cmdPaste, editing);
    enable(cmdCopy, editing || gMain->theory.text().hasSelection());
    ModifyMenuW(gMenu, cmdFullScreen, MF_BYCOMMAND | MF_STRING, cmdFullScreen,
                menuText(gFullScreen ? "Exit Full Screen" : "Enter Full Screen", "F11").c_str());
}

void ensureTheoryVisible() {
    if (!gMain->theoryVisible()) gMain->setTheoryVisible(true);
}

void toggleFullScreen() {
    if (!gFullScreen) {
        GetWindowPlacement(gWindow, &gPlacementBeforeFullScreen);
        gStyleBeforeFullScreen = GetWindowLongPtrW(gWindow, GWL_STYLE);
        MONITORINFO mi = {sizeof mi};
        GetMonitorInfoW(MonitorFromWindow(gWindow, MONITOR_DEFAULTTONEAREST), &mi);
        SetWindowLongPtrW(gWindow, GWL_STYLE, gStyleBeforeFullScreen & ~WS_OVERLAPPEDWINDOW);
        SetMenu(gWindow, nullptr);
        SetWindowPos(gWindow, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        gFullScreen = true;
    } else {
        SetWindowLongPtrW(gWindow, GWL_STYLE, gStyleBeforeFullScreen);
        SetMenu(gWindow, gMenu);
        SetWindowPlacement(gWindow, &gPlacementBeforeFullScreen);
        SetWindowPos(gWindow, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        gFullScreen = false;
    }
}

void showAbout() {
    MSGBOXPARAMSW p = {sizeof p};
    p.hwndOwner = gWindow;
    p.hInstance = GetModuleHandleW(nullptr);
    p.lpszCaption = L"About Filter Lab";
    std::wstring text = widen("Filter Lab 1.0\n\nDesign digital filters and hear them live.\nIIR (Butterworth, Chebyshev, Elliptic, "
                              "Bessel), FIR window method and hand-placed poles and zeros. Results match SciPy.");
    p.lpszText = text.c_str();
    p.dwStyle = MB_OK | MB_USERICON;
    p.lpszIcon = MAKEINTRESOURCEW(1);
    MessageBoxIndirectW(&p);
}

void copyCode(int index) {
    CodeLanguage lang = kAllLanguages[index];
    copyToClipboard(gWindow, exportCode(lang, gModel->spec(), gModel->filter()));
    gMain->showToast(std::string("Copied ") + languageTitle(lang) + " code to the clipboard");
}

void command(UINT id) {
    LabModel& m = *gModel;
    Host& host = Host::shared();
    HWND edit = host.editControl();
    switch (id) {
    case cmdOpenAudio: m.openAudioFile(); return;
    case cmdExportAudio:
        if (m.source() != SourceKind::microphone) exportFilteredAudio(m);
        return;
    case cmdExportResponse: gMain->exportResponse(); return;
    case cmdExportZ: gMain->exportZ(); return;
    case cmdExportLive: gMain->exportLive(); return;
    case cmdClose:
    case cmdExit: PostMessageW(gWindow, WM_CLOSE, 0, 0); return;
    case cmdUndo:
        if (edit) SendMessageW(edit, EM_UNDO, 0, 0);
        else m.undo();
        return;
    case cmdRedo:
        if (!edit) m.redo();
        return;
    case cmdCut:
        if (edit) SendMessageW(edit, WM_CUT, 0, 0);
        return;
    case cmdCopy:
        if (edit) SendMessageW(edit, WM_COPY, 0, 0);
        else if (gMain->theory.text().hasSelection()) copyToClipboard(gWindow, gMain->theory.text().selectedText());
        return;
    case cmdPaste:
        if (edit) SendMessageW(edit, WM_PASTE, 0, 0);
        return;
    case cmdSelectAll:
        if (edit) SendMessageW(edit, EM_SETSEL, 0, -1);
        else gMain->theory.text().selectAll();
        return;
    case cmdDeleteSelected: m.deleteSelectedItem(); return;
    case cmdLogAxis: m.setLogAxis(!m.logAxis()); return;
    case cmdMap: m.setShowHMap(!m.showHMap()); return;
    case cmdPrediction: m.setShowPrediction(!m.showPrediction()); return;
    case cmdHold: m.setHold(!m.hold()); return;
    case cmdResetZoom: gMain->zPlane.resetView(); return;
    case cmdTheory: gMain->setTheoryVisible(!gMain->theoryVisible()); return;
    case cmdFullScreen: toggleFullScreen(); return;
    case cmdListen: m.setListen(!m.listen()); return;
    case cmdFilterOn: m.setFilterOn(!m.filterOn()); return;
    case cmdPause: m.setPaused(!m.paused()); return;
    case cmdLessons:
    case cmdHelpLessons:
        ensureTheoryVisible();
        m.setTheoryPage(TheoryPage::lessons());
        return;
    case cmdMinimize: ShowWindow(gWindow, SW_MINIMIZE); return;
    case cmdZoom: ShowWindow(gWindow, IsZoomed(gWindow) ? SW_RESTORE : SW_MAXIMIZE); return;
    case cmdAbout: showAbout(); return;
    default: break;
    }
    if (id >= cmdCopyCode0 && id < cmdCopyCode0 + 3) copyCode(int(id - cmdCopyCode0));
    else if (id >= cmdLive0 && id < cmdLive0 + 3) m.setLiveView(LiveView(id - cmdLive0));
    else if (id >= cmdResponse0 && id < cmdResponse0 + 5) m.setResponseMode(ResponseMode(id - cmdResponse0));
    else if (id >= cmdSource0 && id < cmdSource0 + UINT(std::size(kAllSources))) m.setSource(kAllSources[id - cmdSource0]);
    else if (id >= cmdRate0 && id < cmdRate0 + UINT(LabModel::sampleRates.size())) m.setSampleRate(LabModel::sampleRates[id - cmdRate0]);
    else if (id >= cmdLesson0 && id < cmdLesson0 + UINT(Lessons::all().size())) {
        ensureTheoryVisible();
        m.setTheoryPage(TheoryPage::lesson(Lessons::all()[id - cmdLesson0].id));
    } else if (id >= cmdExperiment0 && id < cmdExperiment0 + UINT(Experiments::all().size())) {
        ensureTheoryVisible();
        m.setTheoryPage(TheoryPage::experiments());
        m.runExperiment(Experiments::all()[id - cmdExperiment0].id);
    }
}

/// Keys nobody else handled: Space is the A/B switch (LabWindow.keyDown).
bool windowKey(const KeyEvent& e) {
    if (e.ctrl || e.alt) return false;
    LabModel& m = *gModel;
    switch (e.vk) {
    case VK_SPACE: m.setFilterOn(!m.filterOn()); return true;
    case 'L': m.setListen(!m.listen()); return true;
    case VK_DELETE:
    case VK_BACK: m.deleteSelectedItem(); return true;
    case VK_ESCAPE: m.setZTool(ZTool::move); return true;
    default: return false;
    }
}

void minTrackSize(MINMAXINFO* info) {
    UINT dpi = GetDpiForWindow(gWindow);
    float scale = float(dpi) / 96.0f;
    float minW = std::max(1200.0f, gMain ? gMain->minimumWidth() : 1200.0f), minH = 740;
    RECT r = {0, 0, LONG(minW * scale), LONG(minH * scale)};
    AdjustWindowRectExForDpi(&r, DWORD(GetWindowLongPtrW(gWindow, GWL_STYLE)), GetMenu(gWindow) != nullptr,
                             DWORD(GetWindowLongPtrW(gWindow, GWL_EXSTYLE)), dpi);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(gWindow, MONITOR_DEFAULTTONEAREST), &mi);
    LONG maxW = mi.rcWork.right - mi.rcWork.left, maxH = mi.rcWork.bottom - mi.rcWork.top;
    info->ptMinTrackSize.x = std::min(r.right - r.left, maxW);
    info->ptMinTrackSize.y = std::min(r.bottom - r.top, maxH);
}

void saveWindowPlacement() {
    WINDOWPLACEMENT wp = {sizeof wp};
    if (gFullScreen) wp = gPlacementBeforeFullScreen;
    else GetWindowPlacement(gWindow, &wp);
    const RECT& r = wp.rcNormalPosition;
    Settings::shared().set("window", strf("%ld %ld %ld %ld %d", r.left, r.top, r.right, r.bottom, wp.showCmd == SW_SHOWMAXIMIZED ? 1 : 0));
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Host& host = Host::shared();
    switch (msg) {
    case WM_APP_DISPATCH: dispatchDrain(); return 0;
    case WM_APP_END_EDIT: host.endEditing(wp != 0); return 0;
    case WM_TIMER:
        if (wp == kFrameTimer && gMain) {
            gMain->tick();
            dispatchTimers();
            host.tick();
            if (host.renderPending()) host.render();
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        host.render();
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        if (gMain && wp != SIZE_MINIMIZED) {
            host.resize();
            host.render();
        }
        return 0;
    case WM_DPICHANGED: {
        host.setDpi(HIWORD(wp));
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        host.resize();
        return 0;
    }
    case WM_GETMINMAXINFO:
        if (gWindow && !gFullScreen) minTrackSize(reinterpret_cast<MINMAXINFO*>(lp));
        return 0;
    case WM_MOUSEMOVE: host.mouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp); return 0;
    case WM_MOUSELEAVE: host.mouseLeave(); return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: host.mouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp); return 0;
    case WM_LBUTTONUP: host.mouseUp(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp); return 0;
    case WM_RBUTTONDOWN: host.rightMouseDown(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), wp); return 0;
    case WM_MOUSEWHEEL: host.mouseWheel(GET_X_LPARAM(lp), GET_Y_LPARAM(lp), GET_WHEEL_DELTA_WPARAM(wp), GET_KEYSTATE_WPARAM(wp)); return 0;
    case WM_CAPTURECHANGED:
        if (HWND(lp) != hwnd) host.captureLost();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) return host.setCursor();
        break;
    case WM_KEYDOWN:
        if (host.keyDown(wp)) return 0;
        break;
    case WM_CTLCOLOREDIT: return host.editColors(HDC(wp));
    case WM_INITMENUPOPUP: updateMenus(); return 0;
    case WM_COMMAND:
        if (HIWORD(wp) == 0 || HIWORD(wp) == 1) command(LOWORD(wp));
        return 0;
    case WM_DROPFILES: {
        HDROP drop = HDROP(wp);
        wchar_t path[MAX_PATH * 4];
        if (DragQueryFileW(drop, 0, path, UINT(std::size(path))) && isAudioFileName(path)) gModel->loadFile(path);
        DragFinish(drop);
        return 0;
    }
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_DESTROY:
        KillTimer(hwnd, kFrameTimer);
        saveWindowPlacement();
        gModel->save();
        gModel->audio.shutdown();
        PostQuitMessage(0);
        return 0;
    default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/// The window frame restored from the last run, or a centred default.
RECT initialFrame(DWORD style, DWORD exStyle) {
    if (auto saved = Settings::shared().getString("window")) {
        long l, t, r, b;
        int maximized;
        if (sscanf(saved->c_str(), "%ld %ld %ld %ld %d", &l, &t, &r, &b, &maximized) == 5 && r - l > 200 && b - t > 200) {
            RECT rc = {l, t, r, b};
            if (MonitorFromRect(&rc, MONITOR_DEFAULTTONULL)) return rc;
        }
    }
    POINT origin = {0, 0};
    HMONITOR mon = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {sizeof mi};
    GetMonitorInfoW(mon, &mi);
    UINT dpiX = 96, dpiY = 96;
    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore) {
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        if (auto fn = reinterpret_cast<GetDpiForMonitorFn>(reinterpret_cast<void*>(GetProcAddress(shcore, "GetDpiForMonitor"))))
            fn(mon, 0, &dpiX, &dpiY);
    }
    float scale = float(dpiX) / 96.0f;
    float visW = float(mi.rcWork.right - mi.rcWork.left) / scale, visH = float(mi.rcWork.bottom - mi.rcWork.top) / scale;
    float w = std::min(1480.0f, visW - 20), h = std::min(940.0f, visH - 10);
    RECT r = {0, 0, LONG(w * scale), LONG(h * scale)};
    AdjustWindowRectExForDpi(&r, style, TRUE, exStyle, dpiX);
    LONG ww = std::min(r.right - r.left, mi.rcWork.right - mi.rcWork.left);
    LONG wh = std::min(r.bottom - r.top, mi.rcWork.bottom - mi.rcWork.top);
    LONG x = mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - ww) / 2;
    LONG y = mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - wh) / 2;
    return {x, y, x + ww, y + wh};
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    audioFileStartup();
    timeBeginPeriod(1);

    WNDCLASSEXW wc = {sizeof wc};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hIconSm = LoadIconW(instance, MAKEINTRESOURCEW(1));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"FilterLabWindow";
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    const DWORD exStyle = WS_EX_ACCEPTFILES;
    RECT frame = initialFrame(style, exStyle);
    gMenu = buildMenu();
    gWindow = CreateWindowExW(exStyle, wc.lpszClassName, L"Filter Lab", style, frame.left, frame.top, frame.right - frame.left,
                              frame.bottom - frame.top, nullptr, gMenu, instance, nullptr);
    if (!gWindow) return 1;

    // A caption in the panel colour, like the transparent title bar on macOS (Windows 11).
    COLORREF caption = RGB(0xFF, 0xFD, 0xF8), captionText = RGB(0x2B, 0x2A, 0x33);
    DwmSetWindowAttribute(gWindow, DWMWA_CAPTION_COLOR, &caption, sizeof caption);
    DwmSetWindowAttribute(gWindow, DWMWA_TEXT_COLOR, &captionText, sizeof captionText);

    dispatchInit(gWindow, WM_APP_DISPATCH);
    Host& host = Host::shared();
    host.background = Theme::app().background;
    host.attach(gWindow);
    host.onKeyDown = windowKey;

    gModel = new LabModel();      // lives for the whole run
    gModel->window = gWindow;
    gMain = new MainView(*gModel);
    host.root = gMain;
    host.resize();
    gAccel = buildAccelerators();

    bool maximized = false;
    if (auto saved = Settings::shared().getString("window")) maximized = !saved->empty() && saved->back() == '1';
    ShowWindow(gWindow, maximized ? SW_SHOWMAXIMIZED : show);
    UpdateWindow(gWindow);
    gModel->start();
    SetTimer(gWindow, kFrameTimer, 15, nullptr);

    // An audio file passed on the command line (or opened with Filter Lab from Explorer).
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 1) gModel->loadFile(argv[1]);
    if (argv) LocalFree(argv);

    MSG msg;
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            if (!host.isEditing() && TranslateAcceleratorW(gWindow, gAccel, &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running) break;
        if (host.renderPending()) host.render();
        HANDLE frame = host.frameWaitHandle();
        if (host.renderPending() && frame) {
            // A frame is due but the swap chain is busy: sleep until it is ready (or input).
            if (MsgWaitForMultipleObjectsEx(1, &frame, 100, QS_ALLINPUT, MWMO_INPUTAVAILABLE) == WAIT_OBJECT_0) host.frameReady();
        } else {
            WaitMessage();
        }
    }
    timeEndPeriod(1);
    Settings::shared().save();
    audioFileShutdown();
    CoUninitialize();
    // Skip global destructors: background workers may still hold references.
    ExitProcess(0);
}
