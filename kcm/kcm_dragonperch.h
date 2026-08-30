// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "dragonperch/settings.hpp"

#include <KQuickConfigModule>

#include <QString>
#include <QStringList>

/// Milestone 10's Linux half: the settings, as a KDE Config Module.
///
/// A KCM rather than a window of its own, because `kcmshell6 kcm_dragonperch` gives the
/// standalone case for free -- which is what the tray's Settings item opens -- while the
/// same target also appears in System Settings, where somebody using KDE would look first.
///
/// It reads and writes the file through the core's own parse_settings and write_settings
/// rather than through KConfig, which is a deliberate departure from what docs/plan.md
/// first proposed. The Windows head has a second implementation of this format in C#, and
/// it cost two real bugs: a monitor identifier the daemon could never match, and a
/// disagreement about what a file with one bad line means. A third implementation here
/// would be a third chance at the same mistake, and the core is already portable, tested
/// and fuzzed. KConfig's cascading and defaults are not wanted for six settings.
class DragonPerchKcm : public KQuickConfigModule
{
    Q_OBJECT

    Q_PROPERTY(int petsPerMascot READ petsPerMascot WRITE setPetsPerMascot NOTIFY settingsChanged)
    Q_PROPERTY(double walkSpeed READ walkSpeed WRITE setWalkSpeed NOTIFY settingsChanged)
    Q_PROPERTY(double idleInterval READ idleInterval WRITE setIdleInterval NOTIFY settingsChanged)
    Q_PROPERTY(bool pauseForFullscreen READ pauseForFullscreen WRITE setPauseForFullscreen
                   NOTIFY settingsChanged)

    /// Bumped whenever anything below changes, and read by the tick boxes.
    ///
    /// They ask wantsMascot and wantsOutput, and a method call is not something QML can
    /// watch: a binding that only calls one is evaluated once and never again. Pressing
    /// Defaults changed the answer and left every tick exactly where it was. Reading this
    /// property in the same binding gives it something to depend on.
    Q_PROPERTY(int revision READ revision NOTIFY settingsChanged)

    /// What is installed, and what is on the screen. Neither is a setting; both are what
    /// the settings are about.
    Q_PROPERTY(QStringList installedMascots READ installedMascots CONSTANT)
    Q_PROPERTY(QStringList knownOutputs READ knownOutputs CONSTANT)

public:
    explicit DragonPerchKcm(QObject* parent, const KPluginMetaData& data);

    void load() override;
    void save() override;
    void defaults() override;

    [[nodiscard]] int petsPerMascot() const { return settings_.pets_per_mascot; }
    [[nodiscard]] double walkSpeed() const { return settings_.walk_speed; }
    [[nodiscard]] double idleInterval() const { return settings_.idle_interval; }
    [[nodiscard]] bool pauseForFullscreen() const { return settings_.pause_for_fullscreen; }

    void setPetsPerMascot(int value);
    void setWalkSpeed(double value);
    void setIdleInterval(double value);
    void setPauseForFullscreen(bool value);

    [[nodiscard]] int revision() const { return revision_; }

    [[nodiscard]] QStringList installedMascots() const { return installedMascots_; }
    [[nodiscard]] QStringList knownOutputs() const { return knownOutputs_; }

    /// An empty list in the file means every one of them, which is why these are questions
    /// rather than a list QML can bind a checkbox to directly: unticking the first of three
    /// has to turn "all" into the other two.
    Q_INVOKABLE [[nodiscard]] bool wantsMascot(const QString& id) const;
    Q_INVOKABLE void setMascotWanted(const QString& id, bool wanted);
    Q_INVOKABLE [[nodiscard]] bool wantsOutput(const QString& name) const;
    Q_INVOKABLE void setOutputWanted(const QString& name, bool wanted);

    /// Whether a DragonPerch is listening on the session bus, so the page can say whether
    /// what was saved has been picked up or is waiting for the next start.
    Q_INVOKABLE [[nodiscard]] bool daemonRunning() const;

    /// One string, in the language somebody reads.
    ///
    /// Named `text` rather than `tr` because QObject already has a `tr` and it means
    /// something else. It goes through the same catalogue as the daemon and the Windows
    /// shell -- see docs/translating.md -- rather than through ki18n, so that the settings
    /// here and the settings window on Windows cannot end up saying different things.
    ///
    /// The English is the second argument and is the fallback, so a build with no
    /// catalogue reads exactly as it did before there was one.
    Q_INVOKABLE [[nodiscard]] QString text(const QString& id, const QString& english) const;

Q_SIGNALS:
    void settingsChanged();

private:
    void noteChange();

    dp::Settings settings_;
    dp::Settings saved_;
    int revision_ = 0;
    QStringList installedMascots_;
    QStringList knownOutputs_;
};
