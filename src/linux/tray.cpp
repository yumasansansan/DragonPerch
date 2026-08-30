// SPDX-License-Identifier: GPL-3.0-or-later
#include "tray.hpp"

#include "dragonperch/text.hpp"
#include "errno_text.hpp"
#include "log.hpp"
#include "png.hpp"
#include "session_bus.hpp"

#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <ranges>
#include <string_view>
#include <system_error>
#include <utility>

#include <sys/wait.h>
#include <systemd/sd-bus.h>
#include <unistd.h>

namespace dp::wl {
namespace {

constexpr const char* item_path = "/StatusNotifierItem";
constexpr const char* item_interface = "org.kde.StatusNotifierItem";
constexpr const char* menu_path = "/MenuBar";
constexpr const char* menu_interface = "com.canonical.dbusmenu";

constexpr const char* watcher_name = "org.kde.StatusNotifierWatcher";
constexpr const char* watcher_path = "/StatusNotifierWatcher";

/// Menu item ids. Zero is the root, which dbusmenu reserves.
enum MenuId : int {
    menu_root = 0,
    menu_pause = 1,
    menu_separator_a = 2,
    menu_settings = 3,
    menu_separator_b = 4,
    menu_quit = 5,
};

/// Is there a `kcmshell6` to open the settings module with?
///
/// Asked once, at startup, and the answer decides whether the Settings item is offered at
/// all. An item that is there and does nothing is worse than one that is visibly not
/// available: the module is a separate build (-DDRAGONPERCH_BUILD_KCM=ON) and a package
/// that ships the daemon without it is a perfectly ordinary thing.
bool settings_available()
{
    static const bool found = [] {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        const char* path = std::getenv("PATH");
        if (path == nullptr) {
            return false;
        }

        for (const auto part : std::views::split(std::string_view{path}, ':')) {
            const std::string_view directory{part.begin(), part.end()};
            if (directory.empty()) {
                continue;
            }

            // Executable, not merely present. A directory called kcmshell6 exists too.
            const std::filesystem::path candidate = std::filesystem::path{directory} / "kcmshell6";
            if (::access(candidate.c_str(), X_OK) == 0) {
                return true;
            }
        }
        return false;
    }();
    return found;
}

/// Starts the settings module, and does not wait for it.
///
/// Forked twice. Nothing in this program ever calls wait(), so a single fork would leave a
/// zombie behind every time somebody opened the settings; the middle child exits at once
/// and init inherits the one that matters.
///
/// This runs on the bus thread, so the child does nothing between the fork and the exec
/// but the exec: fork in a process with more than one thread gives the child only this
/// thread, and anything holding a lock in another one is holding it forever.
void open_settings()
{
    const pid_t middle = fork();
    if (middle < 0) {
        log_line("settings: could not fork");
        return;
    }

    if (middle == 0) {
        if (fork() == 0) {
            // The cast is not decoration. execlp reads its arguments as a variadic list
            // terminated by a null *pointer*, and a bare nullptr is a std::nullptr_t --
            // which happens to be passed the same way on the platforms this builds for and
            // is not required to be.
            execlp("kcmshell6", "kcmshell6", "kcm_dragonperch", static_cast<char*>(nullptr));
            _exit(127);
        }
        _exit(0);
    }

    int status = 0;
    (void)waitpid(middle, &status, 0);
}

int append_string_entry(sd_bus_message* m, const char* key, const char* value)
{
    int failed = sd_bus_message_open_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
    if (failed >= 0) {
        failed = sd_bus_message_append(m, "s", key);
    }
    if (failed >= 0) {
        failed = sd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "s");
    }
    if (failed >= 0) {
        failed = sd_bus_message_append(m, "s", value);
    }
    if (failed >= 0) {
        failed = sd_bus_message_close_container(m);
    }
    if (failed >= 0) {
        failed = sd_bus_message_close_container(m);
    }
    return failed;
}

int append_bool_entry(sd_bus_message* m, const char* key, bool value)
{
    const int on = value ? 1 : 0;

    int failed = sd_bus_message_open_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
    if (failed >= 0) {
        failed = sd_bus_message_append(m, "s", key);
    }
    if (failed >= 0) {
        failed = sd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "b");
    }
    if (failed >= 0) {
        failed = sd_bus_message_append(m, "b", on);
    }
    if (failed >= 0) {
        failed = sd_bus_message_close_container(m);
    }
    if (failed >= 0) {
        failed = sd_bus_message_close_container(m);
    }
    return failed;
}

int append_int_entry(sd_bus_message* m, const char* key, int value)
{
    int failed = sd_bus_message_open_container(m, SD_BUS_TYPE_DICT_ENTRY, "sv");
    if (failed >= 0) {
        failed = sd_bus_message_append(m, "s", key);
    }
    if (failed >= 0) {
        failed = sd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "i");
    }
    if (failed >= 0) {
        failed = sd_bus_message_append(m, "i", value);
    }
    if (failed >= 0) {
        failed = sd_bus_message_close_container(m);
    }
    if (failed >= 0) {
        failed = sd_bus_message_close_container(m);
    }
    return failed;
}

/// One item's `a{sv}` of properties. Everything the menu says about itself lives here --
/// dbusmenu has no separate notion of a checkbox or a separator, only properties.
int append_item_properties(sd_bus_message* m, int id, bool paused)
{
    int failed = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "{sv}");
    if (failed < 0) {
        return failed;
    }

    switch (id) {
    case menu_root:
        failed = append_string_entry(m, "children-display", "submenu");
        break;

    case menu_pause:
        failed = append_string_entry(m, "label", "Pause");
        if (failed >= 0) {
            failed = append_string_entry(m, "toggle-type", "checkmark");
        }
        if (failed >= 0) {
            failed = append_int_entry(m, "toggle-state", paused ? 1 : 0);
        }
        break;

    case menu_separator_a:
    case menu_separator_b:
        failed = append_string_entry(m, "type", "separator");
        break;

    case menu_settings:
        failed = append_string_entry(m, "label", "Settings...");
        // Greyed when there is no kcmshell6 to open the module with. Present either way,
        // because leaving it out and adding it later moves everything else, and people
        // learn where an item is by where it sits.
        if (failed >= 0) {
            failed = append_bool_entry(m, "enabled", settings_available());
        }
        break;

    case menu_quit:
        failed = append_string_entry(m, "label", "Quit DragonPerch");
        break;

    default:
        break;
    }

    if (failed < 0) {
        return failed;
    }
    return sd_bus_message_close_container(m);
}

/// One `(ia{sv}av)`: an id, its properties, and its children. Only the root has children
/// here, so the recursion is one level deep however deep the caller asked to go.
int append_item(sd_bus_message* m, int id, bool paused, bool with_children)
{
    int failed = sd_bus_message_open_container(m, SD_BUS_TYPE_STRUCT, "ia{sv}av");
    if (failed >= 0) {
        failed = sd_bus_message_append(m, "i", id);
    }
    if (failed >= 0) {
        failed = append_item_properties(m, id, paused);
    }
    if (failed >= 0) {
        failed = sd_bus_message_open_container(m, SD_BUS_TYPE_ARRAY, "v");
    }
    if (failed < 0) {
        return failed;
    }

    if (with_children) {
        for (const int child : {menu_pause, menu_separator_a, menu_settings, menu_separator_b,
                                menu_quit}) {
            failed = sd_bus_message_open_container(m, SD_BUS_TYPE_VARIANT, "(ia{sv}av)");
            if (failed >= 0) {
                failed = append_item(m, child, paused, false);
            }
            if (failed >= 0) {
                failed = sd_bus_message_close_container(m);
            }
            if (failed < 0) {
                return failed;
            }
        }
    }

    failed = sd_bus_message_close_container(m); // av
    if (failed >= 0) {
        failed = sd_bus_message_close_container(m); // struct
    }
    return failed;
}

std::filesystem::path executable_directory()
{
    std::error_code failed;
    const std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", failed);
    return failed ? std::filesystem::current_path() : self.parent_path();
}

/// The icon file, looked for the way the artwork and the KWin script are: beside the
/// executable, in the install prefix, in the system directory, then up the source tree.
std::filesystem::path find_icon()
{
    std::error_code failed;

    for (const std::filesystem::path& candidate : {
             executable_directory() / "dragonperch.png",
             executable_directory() / ".." / "share" / "dragonperch" / "dragonperch.png",
             std::filesystem::path{"/usr/share/dragonperch/dragonperch.png"},
             std::filesystem::path{"/usr/local/share/dragonperch/dragonperch.png"},
         }) {
        if (std::filesystem::exists(candidate, failed)) {
            return candidate;
        }
    }

    std::filesystem::path directory = executable_directory();
    for (int i = 0; i < 8; ++i) {
        const std::filesystem::path candidate = directory / "packaging" / "dragonperch.png";
        if (std::filesystem::exists(candidate, failed)) {
            return candidate;
        }
        if (!directory.has_parent_path() || directory.parent_path() == directory) {
            break;
        }
        directory = directory.parent_path();
    }

    return {};
}

} // namespace

bool TrayIcon::paused() const
{
    return paused_query_ && paused_query_();
}

int TrayIcon::on_item_property(sd_bus*, const char*, const char*, const char* property,
                               sd_bus_message* reply, void* userdata, sd_bus_error*)
{
    auto* self = static_cast<TrayIcon*>(userdata);
    const std::string_view name{property};

    if (name == "Category") {
        return sd_bus_message_append(reply, "s", "ApplicationStatus");
    }
    if (name == "Id") {
        return sd_bus_message_append(reply, "s", "dragonperch");
    }
    if (name == "Title") {
        return sd_bus_message_append(reply, "s", "DragonPerch");
    }
    if (name == "Status") {
        return sd_bus_message_append(reply, "s", "Active");
    }
    if (name == "IconName") {
        // Set as well as the pixels, so a desktop that would rather resolve a themed name
        // can. It only works once the icon is installed into hicolor, which is why the
        // pixels below are what this actually relies on.
        return sd_bus_message_append(reply, "s", "dragonperch");
    }
    if (name == "Menu") {
        return sd_bus_message_append(reply, "o", menu_path);
    }
    if (name == "ItemIsMenu") {
        // Both buttons open the menu, as on the other platform. A tray icon whose two
        // buttons do different things is one whose users find the second by accident.
        return sd_bus_message_append(reply, "b", 1);
    }

    if (name == "IconPixmap") {
        int failed = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "(iiay)");
        if (failed >= 0 && !self->pixels_.empty()) {
            failed = sd_bus_message_open_container(reply, SD_BUS_TYPE_STRUCT, "iiay");
            if (failed >= 0) {
                failed = sd_bus_message_append(reply, "ii", self->width_, self->height_);
            }
            if (failed >= 0) {
                failed = sd_bus_message_append_array(reply, 'y', self->pixels_.data(),
                                                     self->pixels_.size());
            }
            if (failed >= 0) {
                failed = sd_bus_message_close_container(reply);
            }
        }
        if (failed < 0) {
            return failed;
        }
        return sd_bus_message_close_container(reply);
    }

    if (name == "ToolTip") {
        int failed = sd_bus_message_open_container(reply, SD_BUS_TYPE_STRUCT, "sa(iiay)ss");
        if (failed >= 0) {
            failed = sd_bus_message_append(reply, "s", "dragonperch");
        }
        if (failed >= 0) {
            failed = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "(iiay)");
        }
        if (failed >= 0) {
            failed = sd_bus_message_close_container(reply);
        }
        if (failed >= 0) {
            failed = sd_bus_message_append(reply, "ss", "DragonPerch",
                                           "Konqi and friends, on your title bars");
        }
        if (failed >= 0) {
            failed = sd_bus_message_close_container(reply);
        }
        return failed;
    }

    return sd_bus_message_append(reply, "s", "");
}

int TrayIcon::on_item_method(sd_bus_message* message, void*, sd_bus_error*)
{
    // Activate, SecondaryActivate, ContextMenu and Scroll all exist because the interface
    // says so. With ItemIsMenu the host shows the menu itself and calls none of them, and
    // answering rather than erroring is what stops a host that does call one from deciding
    // the item is broken.
    return sd_bus_reply_method_return(message, "");
}

int TrayIcon::on_menu_layout(sd_bus_message* message, void* userdata, sd_bus_error*)
{
    auto* self = static_cast<TrayIcon*>(userdata);

    int parent = 0;
    int depth = 0;
    if (const int failed = sd_bus_message_read(message, "ii", &parent, &depth); failed < 0) {
        return failed;
    }

    // Read because the arguments have to be consumed in order, and then ignored: this menu
    // is one level deep, so every depth a host can ask for gets the same answer.
    (void)depth;
    // The property filter is read and ignored: this menu has five items and sending all of
    // their properties is cheaper than deciding which to leave out.
    if (const int failed = sd_bus_message_skip(message, "as"); failed < 0) {
        return failed;
    }

    sd_bus_message* reply = nullptr;
    int failed = sd_bus_message_new_method_return(message, &reply);
    if (failed < 0) {
        return failed;
    }

    failed = sd_bus_message_append(reply, "u", self->menu_revision_);
    if (failed >= 0) {
        failed = append_item(reply, parent, self->paused(), parent == menu_root);
    }
    if (failed >= 0) {
        failed = sd_bus_send(nullptr, reply, nullptr);
    }

    sd_bus_message_unref(reply);
    return failed < 0 ? failed : 1;
}

int TrayIcon::on_menu_group_properties(sd_bus_message* message, void* userdata, sd_bus_error*)
{
    auto* self = static_cast<TrayIcon*>(userdata);
    const bool paused = self->paused();

    // The requested ids are ignored and every item is answered. Five items is not worth the
    // bookkeeping, and a host that asked for a subset is not harmed by a superset.
    if (const int failed = sd_bus_message_skip(message, "aias"); failed < 0) {
        return failed;
    }

    sd_bus_message* reply = nullptr;
    int failed = sd_bus_message_new_method_return(message, &reply);
    if (failed < 0) {
        return failed;
    }

    failed = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "(ia{sv})");

    if (failed >= 0) {
        for (const int id : {menu_pause, menu_separator_a, menu_settings, menu_separator_b,
                             menu_quit}) {
            failed = sd_bus_message_open_container(reply, SD_BUS_TYPE_STRUCT, "ia{sv}");
            if (failed >= 0) {
                failed = sd_bus_message_append(reply, "i", id);
            }
            if (failed >= 0) {
                failed = append_item_properties(reply, id, paused);
            }
            if (failed >= 0) {
                failed = sd_bus_message_close_container(reply);
            }
            if (failed < 0) {
                break;
            }
        }
    }

    if (failed >= 0) {
        failed = sd_bus_message_close_container(reply);
    }
    if (failed >= 0) {
        failed = sd_bus_send(nullptr, reply, nullptr);
    }

    sd_bus_message_unref(reply);
    return failed < 0 ? failed : 1;
}

int TrayIcon::on_menu_property(sd_bus*, const char*, const char*, const char* property,
                               sd_bus_message* reply, void*, sd_bus_error*)
{
    const std::string_view name{property};

    if (name == "Version") {
        return sd_bus_message_append(reply, "u", 3U);
    }
    if (name == "TextDirection") {
        return sd_bus_message_append(reply, "s", "ltr");
    }
    if (name == "Status") {
        return sd_bus_message_append(reply, "s", "normal");
    }
    if (name == "IconThemePath") {
        int failed = sd_bus_message_open_container(reply, SD_BUS_TYPE_ARRAY, "s");
        if (failed < 0) {
            return failed;
        }
        return sd_bus_message_close_container(reply);
    }

    return sd_bus_message_append(reply, "s", "");
}

int TrayIcon::on_menu_about_to_show(sd_bus_message* message, void*, sd_bus_error*)
{
    // False: nothing needs updating before the menu is shown, because the properties are
    // built from the live pause state every time they are asked for.
    return sd_bus_reply_method_return(message, "b", 0);
}

int TrayIcon::on_menu_event(sd_bus_message* message, void* userdata, sd_bus_error*)
{
    auto* self = static_cast<TrayIcon*>(userdata);

    int id = 0;
    const char* event = nullptr;
    if (const int failed = sd_bus_message_read(message, "is", &id, &event); failed < 0) {
        return failed;
    }

    if (event != nullptr && std::string_view{event} == "clicked" && self->handler_) {
        switch (id) {
        case menu_pause:
            self->handler_(Command::toggle_pause);
            break;
        case menu_settings:
            // Not a command for the daemon: opening a window is this side's own business,
            // and the module talks to the daemon over the control interface once it has
            // something to say.
            open_settings();
            break;
        case menu_quit:
            self->handler_(Command::quit);
            break;
        default:
            break;
        }
    }

    return sd_bus_reply_method_return(message, "");
}

int TrayIcon::on_watcher_appeared(sd_bus_message* message, void* userdata, sd_bus_error*)
{
    const char* name = nullptr;
    const char* was = nullptr;
    const char* now = nullptr;
    if (sd_bus_message_read(message, "sss", &name, &was, &now) < 0) {
        return 0;
    }
    (void)name;   // the match already narrowed it to the watcher
    (void)was;

    // Appearing, not going. An owner arriving where there was none is a tray that has just
    // started, and every item in the session has to introduce itself again.
    if (now == nullptr || *now == '\0') {
        return 0;
    }

    log_line("tray: the status notifier watcher appeared, registering again");
    (void)static_cast<TrayIcon*>(userdata)->register_with_watcher();
    return 0;
}

void TrayIcon::publish(SessionBus& bus, Handler handler, PausedQuery paused)
{
    handler_ = std::move(handler);
    paused_query_ = std::move(paused);

    const std::filesystem::path icon = find_icon();
    if (icon.empty()) {
        log_line("tray: no dragonperch.png found; falling back to the themed icon name");
    } else {
        try {
            const dp::DecodedImage image = decode_image(icon);
            width_ = image.size.width;
            height_ = image.size.height;

            // Premultiplied BGRA in, ARGB32 in network byte order out -- which is what
            // StatusNotifierItem's a(iiay) means and what QImage::Format_ARGB32 expects at
            // the other end, so the multiply has to be undone rather than reordered.
            pixels_.resize(image.pixels.size());
            for (std::size_t i = 0; i + 3 < image.pixels.size(); i += 4) {
                const auto blue = std::to_integer<unsigned>(image.pixels[i]);
                const auto green = std::to_integer<unsigned>(image.pixels[i + 1]);
                const auto red = std::to_integer<unsigned>(image.pixels[i + 2]);
                const auto alpha = std::to_integer<unsigned>(image.pixels[i + 3]);

                const auto straight = [alpha](unsigned channel) -> std::uint8_t {
                    if (alpha == 0) {
                        return 0;
                    }
                    const unsigned value = (channel * 255U + alpha / 2U) / alpha;
                    return static_cast<std::uint8_t>(value > 255U ? 255U : value);
                };

                pixels_[i] = static_cast<std::uint8_t>(alpha);
                pixels_[i + 1] = straight(red);
                pixels_[i + 2] = straight(green);
                pixels_[i + 3] = straight(blue);
            }
        } catch (const std::exception& error) {
            log_line(cat("tray: cannot read ", icon.string(), ": ", error.what()));
        }
    }

    static const sd_bus_vtable item_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_PROPERTY("Category", "s", &TrayIcon::on_item_property, 0,
                        SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("Id", "s", &TrayIcon::on_item_property, 0, SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("Title", "s", &TrayIcon::on_item_property, 0,
                        SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("Status", "s", &TrayIcon::on_item_property, 0,
                        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("IconName", "s", &TrayIcon::on_item_property, 0,
                        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("IconPixmap", "a(iiay)", &TrayIcon::on_item_property, 0,
                        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("ToolTip", "(sa(iiay)ss)", &TrayIcon::on_item_property, 0,
                        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("Menu", "o", &TrayIcon::on_item_property, 0,
                        SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("ItemIsMenu", "b", &TrayIcon::on_item_property, 0,
                        SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_METHOD("Activate", "ii", "", &TrayIcon::on_item_method,
                      SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("SecondaryActivate", "ii", "", &TrayIcon::on_item_method,
                      SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("ContextMenu", "ii", "", &TrayIcon::on_item_method,
                      SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("Scroll", "is", "", &TrayIcon::on_item_method, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_SIGNAL("NewIcon", "", 0),
        SD_BUS_SIGNAL("NewStatus", "s", 0),
        SD_BUS_SIGNAL("NewToolTip", "", 0),
        SD_BUS_VTABLE_END,
    };

    static const sd_bus_vtable menu_vtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_PROPERTY("Version", "u", &TrayIcon::on_menu_property, 0,
                        SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("TextDirection", "s", &TrayIcon::on_menu_property, 0,
                        SD_BUS_VTABLE_PROPERTY_CONST),
        SD_BUS_PROPERTY("Status", "s", &TrayIcon::on_menu_property, 0,
                        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_PROPERTY("IconThemePath", "as", &TrayIcon::on_menu_property, 0,
                        SD_BUS_VTABLE_PROPERTY_EMITS_CHANGE),
        SD_BUS_METHOD("GetLayout", "iias", "u(ia{sv}av)", &TrayIcon::on_menu_layout,
                      SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("GetGroupProperties", "aias", "a(ia{sv})",
                      &TrayIcon::on_menu_group_properties, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("Event", "isvu", "", &TrayIcon::on_menu_event, SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_METHOD("AboutToShow", "i", "b", &TrayIcon::on_menu_about_to_show,
                      SD_BUS_VTABLE_UNPRIVILEGED),
        SD_BUS_SIGNAL("ItemsPropertiesUpdated", "a(ia{sv})a(ias)", 0),
        SD_BUS_SIGNAL("LayoutUpdated", "ui", 0),
        SD_BUS_SIGNAL("ItemActivationRequested", "iu", 0),
        SD_BUS_VTABLE_END,
    };

    bus.add_object(item_path, item_interface, item_vtable, this);
    bus.add_object(menu_path, menu_interface, menu_vtable, this);

    // The watcher is part of the shell and a program started with the session can be ready
    // before it is. This is the same problem Explorer restarting causes on Windows.
    bus.add_match("type='signal',sender='org.freedesktop.DBus',"
                  "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
                  "arg0='org.kde.StatusNotifierWatcher'",
                  &TrayIcon::on_watcher_appeared, this);
}

bool TrayIcon::register_with_watcher()
{
    // A connection of its own. The bus thread owns the other one, and a blocking call from
    // here while it is inside sd_bus_process is a race -- the same reason kwin_script.cpp
    // opens one to ask KWin to re-run the script.
    sd_bus* bus = nullptr;
    if (sd_bus_open_user(&bus) < 0) {
        return false;
    }

    sd_bus_error error{};
    sd_bus_message* reply = nullptr;

    // The well-known name, not the object path: the watcher then goes looking for
    // /StatusNotifierItem on it, which is where the object above is published.
    const int failed = sd_bus_call_method(bus, watcher_name, watcher_path, watcher_name,
                                          "RegisterStatusNotifierItem", &error, &reply, "s",
                                          "org.dragonperch");

    if (failed < 0) {
        log_line(cat("tray: no status notifier watcher (",
                     error.message != nullptr ? error.message : errno_text(-failed),
                     "). No icon; use the control interface or Ctrl+C."));
    }

    sd_bus_error_free(&error);
    if (reply != nullptr) {
        sd_bus_message_unref(reply);
    }
    sd_bus_flush_close_unref(bus);

    return failed >= 0;
}

} // namespace dp::wl
