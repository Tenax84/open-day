// The sign-in panel on Windows: a child window of the program's own, with a
// title band, a close button and a raised edge, and below the band a pane
// with the system's WebView2 browser. It lives on the thread that opens it,
// whose event loop (SDL's) delivers its messages and the browser's answers,
// so that thread must be a COM single-threaded apartment, as SDL's is. A
// navigation to the landing address is cancelled before it loads and its
// address kept. A page's own window, such as the one a sign-in with Google
// opens, is a second pane in the first one's place with a browser of its own
// that the page can still talk to; the first is hidden until it closes.

#include "platform/web_sign_in.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <knownfolders.h>
#include <objbase.h>
#include <shlobj.h>

#include <WebView2.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace openday::platform::web_sign_in {
namespace {

// The window classes of the framed panel and of each browser's pane in it.
constexpr wchar_t kPanelClass[] = L"OpenDaySignIn";
constexpr wchar_t kPaneClass[] = L"OpenDaySignInPane";
// The browser's size at 96 dots per inch, before the parent window limits it.
constexpr int kBrowserWidth = 560;
constexpr int kBrowserHeight = 720;
// The room left between the panel and the edges of the parent, at 96 dpi.
constexpr int kMargin = 12;
// The frame at 96 dpi: the raised edge, the title band, the room before the
// title, the title's letters and the close button's width.
constexpr int kEdge = 1;
constexpr int kBandHeight = 32;
constexpr int kBandPadding = 12;
constexpr int kTitleHeight = 15;
constexpr int kCloseWidth = 44;
// The close button's cross: a glyph of the system's icon font, and its size.
constexpr wchar_t kCloseGlyph[] = L"\xE8BB";
constexpr wchar_t kGlyphFont[] = L"Segoe MDL2 Assets";
constexpr int kGlyphHeight = 10;
// The timer that keeps the panel fitted to the parent as it changes size.
constexpr UINT_PTR kFitTimer = 1;
constexpr UINT kFitEveryMs = 250;
// Where the browser keeps its files, under the user's local application data.
constexpr wchar_t kProfileFolder[] = L"\\OpenDay\\Sign-in browser";

std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(std::max(n, 0)), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), n);
    return out;
}

std::string utf8(const wchar_t* text) {
    if (text == nullptr || *text == L'\0') return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
    out.resize(static_cast<std::size_t>(n - 1));
    return out;
}

COLORREF colour(std::uint32_t rgb) { return RGB((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff); }

void fill(HDC dc, const RECT& area, std::uint32_t rgb) {
    HBRUSH brush = CreateSolidBrush(colour(rgb));
    FillRect(dc, &area, brush);
    DeleteObject(brush);
}

// A COM handler that calls a function; `Args` are its Invoke's.
template <class Interface, class... Args>
class Handler final : public Interface {
public:
    explicit Handler(std::function<HRESULT(Args...)> call) : call_(std::move(call)) {}

    ULONG STDMETHODCALLTYPE AddRef() override { return ++count_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = --count_;
        if (left == 0) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (out == nullptr) return E_POINTER;
        if (id == __uuidof(IUnknown) || id == __uuidof(Interface)) {
            *out = static_cast<Interface*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return call_(args...); }

private:
    ~Handler() = default;

    std::atomic<ULONG> count_{1};
    std::function<HRESULT(Args...)> call_;
};

// Makes a handler; the caller passes it on and then releases it.
template <class Interface, class... Args>
Handler<Interface, Args...>* handler(std::function<HRESULT(Args...)> call) {
    return new Handler<Interface, Args...>(std::move(call));
}

// The browser's profile folder; empty for the default one.
std::wstring profilePath() {
    PWSTR local = nullptr;
    std::wstring path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local)) && local != nullptr)
        path = std::wstring(local) + kProfileFolder;
    CoTaskMemFree(local);
    return path;
}

// Forgets every cookie the browser holds, so no sign-in outlasts a panel.
void forgetCookies(ICoreWebView2* view) {
    ICoreWebView2_2* view2 = nullptr;
    if (FAILED(view->QueryInterface(__uuidof(ICoreWebView2_2), reinterpret_cast<void**>(&view2))) || view2 == nullptr)
        return;
    ICoreWebView2CookieManager* cookies = nullptr;
    if (SUCCEEDED(view2->get_CookieManager(&cookies)) && cookies != nullptr) {
        cookies->DeleteAllCookies();
        cookies->Release();
    }
    view2->Release();
}

struct Session;

// One browser and the pane it fills.
struct Pane {
    HWND window = nullptr;
    ICoreWebView2Controller* controller = nullptr;
    ICoreWebView2* view = nullptr;
    Session* owner = nullptr;
    bool pageWindow = false; // a page's own window: its end leaves the sign-in open
};

void releasePane(Pane& pane) {
    if (pane.view != nullptr) {
        pane.view->Release();
        pane.view = nullptr;
    }
    if (pane.controller != nullptr) {
        pane.controller->Close();
        pane.controller->Release();
        pane.controller = nullptr;
    }
}

void fitBrowser(const Pane& pane) {
    if (pane.controller == nullptr || pane.window == nullptr) return;
    RECT bounds{};
    GetClientRect(pane.window, &bounds);
    pane.controller->put_Bounds(bounds);
}

int scaled(int measure, UINT dpi) { return MulDiv(measure, static_cast<int>(dpi), 96); }

// The panel's rectangle in the parent's client area: the browser and the
// frame scaled to the window's dpi, within the client area less a margin,
// centred.
RECT placement(HWND parent, DWORD style) {
    RECT client{};
    GetClientRect(parent, &client);
    const UINT dpi = std::max(GetDpiForWindow(parent), 96U);
    RECT outer{0, 0, scaled(kBrowserWidth + 2 * kEdge, dpi), scaled(kBrowserHeight + kBandHeight + 1 + 2 * kEdge, dpi)};
    AdjustWindowRectExForDpi(&outer, style, FALSE, 0, dpi);
    const int margin = scaled(kMargin, dpi);
    const int width = std::min<int>(outer.right - outer.left, std::max<int>(client.right - 2 * margin, 1));
    const int height = std::min<int>(outer.bottom - outer.top, std::max<int>(client.bottom - 2 * margin, 1));
    const int left = (client.right - width) / 2;
    const int top = (client.bottom - height) / 2;
    return RECT{left, top, left + width, top + height};
}

LRESULT CALLBACK panelProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);
LRESULT CALLBACK paneProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

bool registerClasses(HINSTANCE instance) {
    static const bool registered = [instance] {
        auto add = [instance](const wchar_t* name, WNDPROC proc) {
            WNDCLASSEXW type{};
            type.cbSize = sizeof(type);
            type.lpfnWndProc = proc;
            type.hInstance = instance;
            type.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
            type.lpszClassName = name;
            return RegisterClassExW(&type) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
        };
        return add(kPanelClass, panelProc) && add(kPaneClass, paneProc);
    }();
    return registered;
}

// One sign-in: its panel, its panes and their browsers.
struct Session {
    State state = State::Idle;
    std::string landed;
    HWND parent = nullptr; // none: the panel is a window of its own
    HWND panel = nullptr;
    SIZE fitted{};         // the parent's client size the panel was fitted to
    DWORD style = 0;
    std::wstring title;
    std::wstring landing;
    Look look;
    HFONT titleFont = nullptr;
    HFONT glyph = nullptr;
    UINT dpi = 0; // the fonts were made for this
    bool closeHovered = false;
    bool closeHeld = false;
    // Counts the sign-ins; an answer from the browser for an earlier one is
    // let go.
    unsigned session = 0;
    bool com = false; // open() initialised COM on this thread; finish() ends it
    Pane pane;
    std::vector<std::unique_ptr<Pane>> pageWindows;
    ICoreWebView2Environment* browser = nullptr;
    ICoreWebView2NavigationStartingEventHandler* starting = nullptr;
    ICoreWebView2NewWindowRequestedEventHandler* newWindow = nullptr;
    ICoreWebView2AcceleratorKeyPressedEventHandler* key = nullptr;

    // Moves to `next` unless the sign-in has already ended.
    void settle(State next) {
        if (state == State::Landed || state == State::Failed || state == State::Closed) return;
        state = next;
    }

    // The pane showing: the last page window, else the first pane.
    Pane& showing() { return pageWindows.empty() ? pane : *pageWindows.back(); }

    UINT panelDpi() const { return std::max(GetDpiForWindow(panel), 96U); }

    void makeFonts() {
        const UINT now = panelDpi();
        if (now == dpi && titleFont != nullptr) return;
        dropFonts();
        dpi = now;
        titleFont = CreateFontW(-scaled(kTitleHeight, dpi), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        glyph = CreateFontW(-scaled(kGlyphHeight, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, kGlyphFont);
    }

    void dropFonts() {
        for (HFONT font : {titleFont, glyph})
            if (font != nullptr) DeleteObject(font);
        titleFont = nullptr;
        glyph = nullptr;
    }

    RECT band() const {
        RECT client{};
        GetClientRect(panel, &client);
        const UINT at = panelDpi();
        const int edge = scaled(kEdge, at);
        return RECT{edge, edge, client.right - edge, edge + scaled(kBandHeight, at)};
    }

    RECT closeButton() const {
        RECT button = band();
        button.left = std::max<LONG>(button.right - scaled(kCloseWidth, panelDpi()), button.left);
        return button;
    }

    // Where the panes go: below the band and its rule, inside the edge.
    RECT content() const {
        RECT client{};
        GetClientRect(panel, &client);
        const int edge = scaled(kEdge, panelDpi());
        const RECT over = band();
        return RECT{edge, over.bottom + 1, client.right - edge, std::max<LONG>(client.bottom - edge, over.bottom + 1)};
    }

    void paint(HDC dc) {
        makeFonts();
        RECT client{};
        GetClientRect(panel, &client);
        fill(dc, client, look.face);
        const RECT over = band();
        fill(dc, over, look.band);
        fill(dc, RECT{over.left, over.bottom, over.right, over.bottom + 1}, look.rule);
        const RECT button = closeButton();
        SetBkMode(dc, TRANSPARENT);
        const int padding = scaled(kBandPadding, dpi);
        RECT words{over.left + padding, over.top, button.left - padding, over.bottom};
        HGDIOBJ was = SelectObject(dc, titleFont);
        SetTextColor(dc, colour(look.text));
        DrawTextW(dc, title.c_str(), static_cast<int>(title.size()), &words,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        if (closeHovered || closeHeld) fill(dc, button, closeHeld ? look.held : look.hover);
        RECT cross = button;
        SelectObject(dc, glyph);
        SetTextColor(dc, colour(closeHovered || closeHeld ? look.text : look.quiet));
        DrawTextW(dc, kCloseGlyph, -1, &cross, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, was);
        const int edge = scaled(kEdge, dpi);
        fill(dc, RECT{0, 0, client.right, edge}, look.edgeLight);
        fill(dc, RECT{0, 0, edge, client.bottom}, look.edgeLight);
        fill(dc, RECT{0, client.bottom - edge, client.right, client.bottom}, look.edgeDark);
        fill(dc, RECT{client.right - edge, 0, client.right, client.bottom}, look.edgeDark);
    }

    void hoverClose(bool over) {
        if (over == closeHovered) return;
        closeHovered = over;
        const RECT button = closeButton();
        InvalidateRect(panel, &button, FALSE);
    }

    bool makePanel() {
        HINSTANCE instance = GetModuleHandleW(nullptr);
        if (!registerClasses(instance)) return false;
        RECT place{};
        if (parent != nullptr) {
            place = placement(parent, style);
        } else {
            RECT work{};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            const int width = kBrowserWidth + 2 * kEdge;
            const int height = kBrowserHeight + kBandHeight + 1 + 2 * kEdge;
            place = RECT{work.left, work.top, work.left + width, work.top + height};
            OffsetRect(&place, (work.right - work.left - width) / 2, (work.bottom - work.top - height) / 2);
        }
        const DWORD extended = parent != nullptr ? WS_EX_NOPARENTNOTIFY : 0;
        panel = CreateWindowExW(extended, kPanelClass, title.c_str(), style, place.left, place.top,
                                place.right - place.left, place.bottom - place.top, parent, nullptr, instance, this);
        if (panel == nullptr) return false;
        ShowWindow(panel, SW_SHOWNORMAL);
        SetWindowPos(panel, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        if (parent == nullptr) SetForegroundWindow(panel);
        return true;
    }

    HWND makePane(Pane& made) {
        const RECT place = content();
        made.window = CreateWindowExW(WS_EX_NOPARENTNOTIFY, kPaneClass, L"", WS_CHILD | WS_CLIPSIBLINGS, place.left,
                                      place.top, place.right - place.left, place.bottom - place.top, panel, nullptr,
                                      GetModuleHandleW(nullptr), &made);
        if (made.window != nullptr) ShowWindow(made.window, SW_SHOWNORMAL);
        return made.window;
    }

    void layPanes() {
        const RECT place = content();
        auto move = [&place](const Pane& held) {
            if (held.window != nullptr)
                SetWindowPos(held.window, nullptr, place.left, place.top, place.right - place.left,
                             place.bottom - place.top, SWP_NOZORDER | SWP_NOACTIVATE);
        };
        move(pane);
        for (const auto& page : pageWindows) move(*page);
    }

    // Keeps the panel in the middle of the parent as it changes size.
    void fitToParent() {
        if (parent == nullptr || !IsWindow(parent) || panel == nullptr) return;
        RECT client{};
        GetClientRect(parent, &client);
        if (client.right == fitted.cx && client.bottom == fitted.cy) return;
        fitted = SIZE{client.right, client.bottom};
        const RECT place = placement(parent, style);
        SetWindowPos(panel, nullptr, place.left, place.top, place.right - place.left, place.bottom - place.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // Closes a page's own window and shows the pane it stood in front of.
    void closePageWindow(Pane* page) {
        auto found = std::find_if(pageWindows.begin(), pageWindows.end(),
                                  [page](const auto& held) { return held.get() == page; });
        if (found == pageWindows.end()) return;
        std::unique_ptr<Pane> ending = std::move(*found);
        pageWindows.erase(found);
        releasePane(*ending);
        if (ending->window != nullptr && IsWindow(ending->window)) DestroyWindow(ending->window);
        if (HWND back = showing().window) {
            ShowWindow(back, SW_SHOWNORMAL);
            SetFocus(back);
        }
    }

    // Ends the sign-in: the panel closes, the browsers and handlers are let
    // go, and the state turns `next` unless it had already ended.
    void finish(State next) {
        settle(next);
        ++session;
        if (parent != nullptr && IsWindow(parent) && panel != nullptr) SetFocus(parent);
        for (auto& page : pageWindows) releasePane(*page);
        releasePane(pane);
        if (panel != nullptr) {
            HWND ending = panel;
            panel = nullptr;
            KillTimer(ending, kFitTimer);
            if (IsWindow(ending)) DestroyWindow(ending); // the panes go with it
        }
        pageWindows.clear();
        pane.window = nullptr;
        dropFonts();
        dpi = 0;
        closeHovered = false;
        closeHeld = false;
        for (IUnknown* held : {static_cast<IUnknown*>(starting), static_cast<IUnknown*>(newWindow),
                               static_cast<IUnknown*>(key), static_cast<IUnknown*>(browser)})
            if (held != nullptr) held->Release();
        starting = nullptr;
        newWindow = nullptr;
        key = nullptr;
        browser = nullptr;
        if (com) {
            com = false;
            CoUninitialize();
        }
    }

    // Gives a new browser the sign-in's handlers.
    void attach(Pane& held, ICoreWebView2Controller* controller) {
        controller->AddRef();
        held.controller = controller;
        fitBrowser(held);
        controller->put_IsVisible(TRUE);
        EventRegistrationToken token{};
        held.view->add_NavigationStarting(starting, &token);
        held.view->add_NewWindowRequested(newWindow, &token);
        controller->add_AcceleratorKeyPressed(key, &token);
    }

    bool start(const Request& request);
};

LRESULT CALLBACK panelProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* owner = reinterpret_cast<Session*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (owner == nullptr || owner->panel != window) {
        if (message == WM_CLOSE) {
            DestroyWindow(window);
            return 0;
        }
        return DefWindowProcW(window, message, wparam, lparam);
    }
    const POINT at{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
    switch (message) {
    case WM_SIZE:
        owner->layPanes();
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(window, &ps);
        owner->paint(dc);
        EndPaint(window, &ps);
        return 0;
    }
    case WM_SETFOCUS:
        if (HWND showing = owner->showing().window) SetFocus(showing);
        return 0;
    case WM_TIMER:
        if (wparam == kFitTimer) owner->fitToParent();
        return 0;
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
        TrackMouseEvent(&tracking);
        const RECT button = owner->closeButton();
        owner->hoverClose(PtInRect(&button, at) != FALSE);
        return 0;
    }
    case WM_MOUSELEAVE:
        owner->hoverClose(false);
        return 0;
    case WM_LBUTTONDOWN: {
        const RECT button = owner->closeButton();
        if (PtInRect(&button, at)) {
            owner->closeHeld = true;
            SetCapture(window);
            InvalidateRect(window, &button, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!owner->closeHeld) return 0;
        owner->closeHeld = false;
        ReleaseCapture();
        const RECT button = owner->closeButton();
        InvalidateRect(window, &button, FALSE);
        if (PtInRect(&button, at)) PostMessageW(window, WM_CLOSE, 0, 0); // closes every page
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (owner->closeHeld) {
            owner->closeHeld = false;
            const RECT button = owner->closeButton();
            InvalidateRect(window, &button, FALSE);
        }
        return 0;
    case WM_CLOSE:
        owner->finish(State::Closed);
        return 0;
    case WM_DESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

LRESULT CALLBACK paneProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* pane = reinterpret_cast<Pane*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_SIZE:
        if (pane != nullptr) fitBrowser(*pane);
        return 0;
    case WM_ERASEBKGND: {
        if (pane == nullptr || pane->owner == nullptr) return DefWindowProcW(window, message, wparam, lparam);
        RECT client{};
        GetClientRect(window, &client);
        fill(reinterpret_cast<HDC>(wparam), client, pane->owner->look.face);
        return 1;
    }
    case WM_SETFOCUS:
        if (pane != nullptr && pane->controller != nullptr)
            pane->controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        return 0;
    case WM_CLOSE:
        if (pane == nullptr || pane->owner == nullptr) DestroyWindow(window);
        else if (pane->pageWindow) pane->owner->closePageWindow(pane);
        else pane->owner->finish(State::Closed);
        return 0;
    case WM_DESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return 0;
    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

bool Session::start(const Request& request) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return false;
    com = true;
    parent = static_cast<HWND>(request.parent);
    if (parent != nullptr && !IsWindow(parent)) parent = nullptr;
    style = parent != nullptr ? WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN : WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    title = wide(request.title);
    landing = wide(request.landing);
    look = request.look;
    fitted = SIZE{};
    pane = Pane{};
    pane.owner = this;
    if (!makePanel() || makePane(pane) == nullptr) return false;
    if (parent != nullptr) {
        RECT client{};
        GetClientRect(parent, &client);
        fitted = SIZE{client.right, client.bottom};
        SetTimer(panel, kFitTimer, kFitEveryMs, nullptr);
    }
    SetFocus(pane.window);

    const unsigned current = session;
    // A navigation to the landing address ends the sign-in.
    starting = handler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*,
                       ICoreWebView2NavigationStartingEventArgs*>(
        [this, current](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) {
            if (session != current) return S_OK;
            LPWSTR uri = nullptr;
            if (FAILED(args->get_Uri(&uri)) || uri == nullptr) return S_OK;
            const std::wstring_view to(uri);
            if (!landing.empty() && to.starts_with(landing)) {
                args->put_Cancel(TRUE);
                forgetCookies(sender);
                if (state == State::Opening || state == State::Open) {
                    state = State::Landed;
                    landed = utf8(uri);
                }
                if (panel != nullptr) PostMessageW(panel, WM_CLOSE, 0, 0);
            }
            CoTaskMemFree(uri);
            return S_OK;
        });
    // Escape closes the pane it is pressed in; in the first pane, the panel.
    key = handler<ICoreWebView2AcceleratorKeyPressedEventHandler, ICoreWebView2Controller*,
                  ICoreWebView2AcceleratorKeyPressedEventArgs*>(
        [this, current](ICoreWebView2Controller* sender, ICoreWebView2AcceleratorKeyPressedEventArgs* args) {
            if (session != current) return S_OK;
            COREWEBVIEW2_KEY_EVENT_KIND kind{};
            UINT pressed = 0;
            if (FAILED(args->get_KeyEventKind(&kind)) || FAILED(args->get_VirtualKey(&pressed)) ||
                kind != COREWEBVIEW2_KEY_EVENT_KIND_KEY_DOWN || pressed != VK_ESCAPE)
                return S_OK;
            args->put_Handled(TRUE);
            HWND closing = pane.window;
            for (const auto& page : pageWindows)
                if (page->controller == sender) closing = page->window;
            if (closing != nullptr) PostMessageW(closing, WM_CLOSE, 0, 0);
            return S_OK;
        });
    // A page asking for a window of its own gets a pane in place of the one
    // showing, which is hidden until it closes. The page that opened it still
    // hears from it, and may close it.
    newWindow = handler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*,
                        ICoreWebView2NewWindowRequestedEventArgs*>(
        [this, current](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) {
            if (session != current || browser == nullptr || panel == nullptr) return S_OK;
            auto madePane = std::make_unique<Pane>();
            madePane->owner = this;
            madePane->pageWindow = true;
            HWND before = showing().window;
            if (makePane(*madePane) == nullptr) return S_OK;
            ICoreWebView2Deferral* deferral = nullptr;
            if (FAILED(args->GetDeferral(&deferral)) || deferral == nullptr) {
                DestroyWindow(madePane->window);
                return S_OK;
            }
            Pane* held = madePane.get();
            pageWindows.push_back(std::move(madePane));
            if (before != nullptr) ShowWindow(before, SW_HIDE);
            args->AddRef();
            auto* made = handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT,
                                 ICoreWebView2Controller*>(
                [this, current, held, args, deferral](HRESULT result, ICoreWebView2Controller* controller) {
                    const bool kept = session == current &&
                                      std::any_of(pageWindows.begin(), pageWindows.end(),
                                                  [held](const auto& page) { return page.get() == held; });
                    if (kept && SUCCEEDED(result) && controller != nullptr &&
                        SUCCEEDED(controller->get_CoreWebView2(&held->view)) && held->view != nullptr) {
                        attach(*held, controller);
                        auto* closeAsked = handler<ICoreWebView2WindowCloseRequestedEventHandler, ICoreWebView2*,
                                                   IUnknown*>([page = held->window](ICoreWebView2*, IUnknown*) {
                            PostMessageW(page, WM_CLOSE, 0, 0);
                            return S_OK;
                        });
                        EventRegistrationToken token{};
                        held->view->add_WindowCloseRequested(closeAsked, &token);
                        closeAsked->Release();
                        args->put_NewWindow(held->view);
                        args->put_Handled(TRUE);
                        SetFocus(held->window);
                    } else {
                        if (controller != nullptr && !kept) controller->Close();
                        if (kept) closePageWindow(held);
                    }
                    deferral->Complete();
                    deferral->Release();
                    args->Release();
                    return S_OK;
                });
            if (FAILED(browser->CreateCoreWebView2Controller(held->window, made))) {
                closePageWindow(held);
                deferral->Complete();
                deferral->Release();
                args->Release();
            }
            made->Release();
            return S_OK;
        });
    auto* controllerMade =
        handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
            [this, current, address = wide(request.address)](HRESULT result, ICoreWebView2Controller* controller) {
                if (session != current) {
                    if (controller != nullptr) controller->Close();
                    return S_OK;
                }
                if (FAILED(result) || controller == nullptr || FAILED(controller->get_CoreWebView2(&pane.view)) ||
                    pane.view == nullptr) {
                    finish(State::Failed);
                    return S_OK;
                }
                attach(pane, controller);
                forgetCookies(pane.view);
                pane.view->Navigate(address.c_str());
                if (GetFocus() == pane.window) controller->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                settle(State::Open);
                return S_OK;
            });
    auto* environmentMade =
        handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(
            [this, current, controllerMade](HRESULT result, ICoreWebView2Environment* environment) {
                if (session != current) return S_OK;
                if (SUCCEEDED(result) && environment != nullptr) {
                    environment->AddRef();
                    browser = environment;
                }
                if (FAILED(result) || environment == nullptr ||
                    FAILED(environment->CreateCoreWebView2Controller(pane.window, controllerMade)))
                    finish(State::Failed);
                return S_OK;
            });
    const std::wstring profile = profilePath();
    const HRESULT created =
        CreateCoreWebView2EnvironmentWithOptions(nullptr, profile.empty() ? nullptr : profile.c_str(), nullptr,
                                                 environmentMade);
    // The browser holds the completions it will call for as long as it needs them.
    environmentMade->Release();
    controllerMade->Release();
    return SUCCEEDED(created);
}

} // namespace

struct Window::Shared : Session {};

bool available() {
    LPWSTR version = nullptr;
    const bool found = SUCCEEDED(GetAvailableCoreWebView2BrowserVersionString(nullptr, &version)) && version != nullptr;
    CoTaskMemFree(version);
    return found;
}

Window::Window() : shared_(std::make_shared<Shared>()) {}

Window::~Window() { close(); }

bool Window::open(const Request& request) {
    close();
    if (!available()) return false;
    shared_->state = State::Opening;
    shared_->landed.clear();
    if (!shared_->start(request)) {
        shared_->finish(State::Failed);
        return false;
    }
    return true;
}

void Window::close() {
    if (shared_->panel != nullptr || shared_->com) shared_->finish(State::Closed);
}

State Window::state() const { return shared_->state; }

std::string Window::landedAddress() const { return shared_->state == State::Landed ? shared_->landed : std::string(); }

} // namespace openday::platform::web_sign_in
