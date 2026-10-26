// A sign-in panel: a web site's sign-in page in a framed panel inside one of
// the program's windows, which closes itself once the page goes to an
// address that starts as the caller says, keeping that address. A site that
// ends its sign-in on a page of its own, as GOG does, so hands its code over
// without the user copying it (docs/gog-download.md). On Windows the panel
// is the system's WebView2 browser, when the build has it (OPENDAY_WEBVIEW2);
// elsewhere available() is false and nothing opens.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace openday::platform::web_sign_in {

// True on Windows with the WebView2 runtime installed, when built with it.
bool available();

// The colours of the panel's frame, each 0xRRGGBB.
struct Look {
    std::uint32_t face = 0;      // behind the page while it loads
    std::uint32_t band = 0;      // the title band
    std::uint32_t rule = 0;      // the line under the band
    std::uint32_t edgeLight = 0; // the raised edge's top and left
    std::uint32_t edgeDark = 0;  // the raised edge's bottom and right
    std::uint32_t text = 0;      // the title; the close button's cross under the pointer
    std::uint32_t quiet = 0;     // the close button's cross
    std::uint32_t hover = 0;     // the close button under the pointer
    std::uint32_t held = 0;      // the close button while pressed
};

struct Request {
    std::string address; // the sign-in page, https
    // The panel closes when the page goes to an address starting with this,
    // before that page loads; the address is kept (Window::landedAddress).
    std::string landing;
    std::string title; // UTF-8, in the frame's band
    Look look;
    // The window the panel shows inside (an HWND on Windows); with none it
    // is a window of its own.
    void* parent = nullptr;
};

enum class State : std::uint8_t {
    Idle,    // not opened
    Opening, // the panel and its browser are starting
    Open,    // the page is showing
    Landed,  // the page went to the landing address; the panel has closed
    Closed,  // the user closed it, or close() was called
    Failed,  // the browser could not start; the panel has closed
};

// One sign-in panel. It lives on the thread that opens it, whose event loop
// (SDL's) delivers its messages and the browser's answers; call every member
// from that thread. Destroying it closes the panel.
class Window {
public:
    Window();
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Opens the panel in the middle of the parent window and loads the page.
    // The frame has a title band with a close button. A page's own window
    // (the sign-in with Google opens one) takes the first page's place until
    // it closes. Escape closes the page it is pressed in; in the first page
    // it closes the panel. The browser forgets its cookies as the page loads
    // and as it lands. Returns false when the panel cannot be shown.
    bool open(const Request& request);

    // Closes the panel; the state turns Closed unless it had landed.
    void close();

    State state() const;

    // The address the page went to when the state is Landed; else empty.
    std::string landedAddress() const;

private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
};

} // namespace openday::platform::web_sign_in
