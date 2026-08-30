// SPDX-License-Identifier: GPL-3.0-or-later
#include "kcm_dragonperch.h"

#include "dragonperch/pack_library.hpp"
#include "settings_file.hpp"

#include <KLocalizedString>
#include <KPluginFactory>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>

#include <algorithm>
#include <filesystem>

K_PLUGIN_CLASS_WITH_JSON(DragonPerchKcm, "kcm_dragonperch.json")

namespace {

constexpr const char* bus_name = "org.dragonperch";
constexpr const char* object_path = "/org/dragonperch/Control";
constexpr const char* interface_name = "org.dragonperch.Control1";

/// The pack ids beside the daemon, not a list written out here.
///
/// Somebody who drops a fourth mascot into the artwork directory should be able to turn it
/// on without this having been rebuilt, which is the same rule the daemon and the Windows
/// settings window both follow.
///
/// Where to start looking is the daemon itself: find_sprite_packs takes a directory and
/// tries `assets` and `../share/dragonperch/assets` below it, so the answer for an
/// installed copy falls out of asking where dragonperch-wl is. If it is not on the path --
/// somebody running from an unpacked tarball -- there is nothing to find and the page says
/// so rather than inventing three names.
QStringList findInstalledMascots()
{
    const QString daemon = QStandardPaths::findExecutable(QStringLiteral("dragonperch-wl"));
    if (daemon.isEmpty()) {
        return {};
    }

    const std::filesystem::path beside =
        std::filesystem::path{daemon.toStdString()}.parent_path();

    QStringList ids;
    for (const std::filesystem::path& definition : dp::find_sprite_packs(beside)) {
        // assets/<id>/<id>.ini, so the id is the directory the file sits in.
        ids.append(QString::fromStdString(definition.stem().string()));
    }
    ids.sort();
    return ids;
}

/// The connector names the compositor is using: DP-1, eDP-1, HDMI-A-1.
///
/// QScreen::name is the connector on Wayland, which is what the daemon puts in
/// OutputInfo::name and therefore what the file has to contain. This is exactly the place
/// the Windows settings window got wrong -- it saved a monitor handle the daemon could
/// never match, and the effect of unticking one screen was that the pets vanished from all
/// of them -- so it is worth being explicit that these two names are the same name.
QStringList findOutputs()
{
    QStringList names;
    const QList<QScreen*> screens = QGuiApplication::screens();
    for (const QScreen* screen : screens) {
        if (screen != nullptr && !screen->name().isEmpty()) {
            names.append(screen->name());
        }
    }
    names.sort();
    return names;
}

QStringList toStringList(const std::vector<std::string>& items)
{
    QStringList out;
    out.reserve(static_cast<qsizetype>(items.size()));
    for (const std::string& item : items) {
        out.append(QString::fromStdString(item));
    }
    return out;
}

std::vector<std::string> toVector(const QStringList& items)
{
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(items.size()));
    for (const QString& item : items) {
        out.push_back(item.toStdString());
    }
    return out;
}

/// One tick box's answer, given that an empty list means every one of them.
bool wants(const std::vector<std::string>& chosen, const QString& name)
{
    if (chosen.empty()) {
        return true;
    }
    const std::string wanted = name.toStdString();
    return std::find(chosen.begin(), chosen.end(), wanted) != chosen.end();
}

/// Turning one off when the list is empty has to write down the others; turning the last
/// one back on has to give the empty list back, so that a mascot installed later is
/// included rather than left out of a list written before it existed.
void setWanted(std::vector<std::string>& chosen, const QStringList& all, const QString& name,
               bool wanted)
{
    QStringList current = chosen.empty() ? all : toStringList(chosen);

    if (wanted) {
        if (!current.contains(name)) {
            current.append(name);
        }
    } else {
        current.removeAll(name);
    }

    current.sort();
    QStringList sortedAll = all;
    sortedAll.sort();

    chosen = current == sortedAll ? std::vector<std::string>{} : toVector(current);
}

} // namespace

DragonPerchKcm::DragonPerchKcm(QObject* parent, const KPluginMetaData& data)
    : KQuickConfigModule(parent, data)
    , installedMascots_(findInstalledMascots())
    , knownOutputs_(findOutputs())
{
}

void DragonPerchKcm::load()
{
    settings_ = dp::wl::load_settings();
    saved_ = settings_;
    ++revision_;
    setNeedsSave(false);
    setRepresentsDefaults(settings_ == dp::Settings{});
    Q_EMIT settingsChanged();
}

void DragonPerchKcm::save()
{
    if (!dp::wl::save_settings(settings_)) {
        return;
    }
    saved_ = settings_;
    setNeedsSave(false);

    // And ask whoever is running to read it again, so a change is on the screen before this
    // page has finished closing. Nothing to do if there is nobody: the file is saved either
    // way and is read at startup.
    if (daemonRunning()) {
        QDBusConnection::sessionBus().call(
            QDBusMessage::createMethodCall(QString::fromLatin1(bus_name),
                                           QString::fromLatin1(object_path),
                                           QString::fromLatin1(interface_name),
                                           QStringLiteral("Reload")),
            QDBus::NoBlock);
    }
}

void DragonPerchKcm::defaults()
{
    settings_ = dp::Settings{};
    noteChange();
}

bool DragonPerchKcm::daemonRunning() const
{
    QDBusConnectionInterface* bus = QDBusConnection::sessionBus().interface();
    return bus != nullptr && bus->isServiceRegistered(QString::fromLatin1(bus_name)).value();
}

void DragonPerchKcm::noteChange()
{
    ++revision_;
    setNeedsSave(!(settings_ == saved_));
    setRepresentsDefaults(settings_ == dp::Settings{});
    Q_EMIT settingsChanged();
}

void DragonPerchKcm::setPetsPerMascot(int value)
{
    if (settings_.pets_per_mascot != value) {
        settings_.pets_per_mascot = value;
        noteChange();
    }
}

void DragonPerchKcm::setWalkSpeed(double value)
{
    if (settings_.walk_speed != value) {
        settings_.walk_speed = value;
        noteChange();
    }
}

void DragonPerchKcm::setIdleInterval(double value)
{
    if (settings_.idle_interval != value) {
        settings_.idle_interval = value;
        noteChange();
    }
}

void DragonPerchKcm::setPauseForFullscreen(bool value)
{
    if (settings_.pause_for_fullscreen != value) {
        settings_.pause_for_fullscreen = value;
        noteChange();
    }
}

bool DragonPerchKcm::wantsMascot(const QString& id) const
{
    return wants(settings_.mascots, id);
}

void DragonPerchKcm::setMascotWanted(const QString& id, bool wanted)
{
    setWanted(settings_.mascots, installedMascots_, id, wanted);
    noteChange();
}

bool DragonPerchKcm::wantsOutput(const QString& name) const
{
    return wants(settings_.outputs, name);
}

void DragonPerchKcm::setOutputWanted(const QString& name, bool wanted)
{
    setWanted(settings_.outputs, knownOutputs_, name, wanted);
    noteChange();
}

#include "kcm_dragonperch.moc"
