// Systems and builds without a browser to embed: no panel opens.

#include "platform/web_sign_in.hpp"

namespace openday::platform::web_sign_in {

struct Window::Shared {};

bool available() { return false; }

Window::Window() = default;
Window::~Window() = default;

bool Window::open(const Request&) { return false; }
void Window::close() {}
State Window::state() const { return State::Idle; }
std::string Window::landedAddress() const { return {}; }

} // namespace openday::platform::web_sign_in
