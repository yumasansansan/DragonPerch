// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

KCM.SimpleKCM {
    id: root

    // No Apply button of our own: a KCM is given one, and the framework decides when it is
    // enabled from setNeedsSave. Everything below therefore reports a change and nothing
    // writes a file.
    Kirigami.FormLayout {
        anchors.fill: parent

        QQC2.SpinBox {
            Kirigami.FormData.label: i18n("Dragons per mascot:")
            from: 0
            to: 64
            value: kcm.petsPerMascot
            onValueModified: kcm.petsPerMascot = value
        }

        QQC2.Label {
            Kirigami.FormData.label: ""
            text: i18n("Three mascots at two each is six dragons.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        Item { Kirigami.FormData.isSection: true }

        ColumnLayout {
            Kirigami.FormData.label: i18n("Mascots:")
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: kcm.installedMascots

                QQC2.CheckBox {
                    // Capitalised for the eye only; what is saved is the pack id.
                    text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                    // kcm.revision is read so the binding has something to depend on;
                    // see the property's own comment. Without it, Defaults moved the
                    // setting and left the tick behind.
                    checked: kcm.revision, kcm.wantsMascot(modelData)
                    onToggled: kcm.setMascotWanted(modelData, checked)
                }
            }

            QQC2.Label {
                visible: kcm.installedMascots.length === 0
                text: i18n("No sprite packs found beside the daemon.")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }

            QQC2.Label {
                visible: kcm.installedMascots.length > 0
                text: i18n("Turning all of them off is the same as turning all of them on.")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }
        }

        Item { Kirigami.FormData.isSection: true }

        RowLayout {
            Kirigami.FormData.label: i18n("Walking speed:")
            spacing: Kirigami.Units.largeSpacing

            QQC2.Slider {
                Layout.fillWidth: true
                from: 1
                to: 200
                stepSize: 1
                value: kcm.walkSpeed
                onMoved: kcm.walkSpeed = value
            }

            QQC2.Label {
                text: Math.round(kcm.walkSpeed)
                Layout.minimumWidth: Kirigami.Units.gridUnit * 2
                horizontalAlignment: Text.AlignRight
            }
        }

        QQC2.Label {
            Kirigami.FormData.label: ""
            text: i18n("Pixels per second. The walk cycle was drawn against 42: much faster and the feet slide, much slower and it moonwalks.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
            wrapMode: Text.Wrap
            Layout.maximumWidth: Kirigami.Units.gridUnit * 24
        }

        QQC2.SpinBox {
            Kirigami.FormData.label: i18n("Pause now and then:")
            from: 0
            to: 3600
            value: Math.round(kcm.idleInterval)
            onValueModified: kcm.idleInterval = value
        }

        QQC2.Label {
            Kirigami.FormData.label: ""
            text: i18n("Mean seconds between spontaneous pauses. Zero stops them happening at all.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        Item { Kirigami.FormData.isSection: true }

        ColumnLayout {
            Kirigami.FormData.label: i18n("Monitors:")
            spacing: Kirigami.Units.smallSpacing

            Repeater {
                model: kcm.knownOutputs

                QQC2.CheckBox {
                    text: modelData
                    checked: kcm.revision, kcm.wantsOutput(modelData)
                    onToggled: kcm.setOutputWanted(modelData, checked)
                }
            }

            QQC2.Label {
                text: i18n("Where the dragons are allowed. Turning all of them off is the same as turning all of them on.")
                font: Kirigami.Theme.smallFont
                opacity: 0.7
            }
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.Switch {
            Kirigami.FormData.label: i18n("Get out of the way of full-screen apps:")
            checked: kcm.pauseForFullscreen
            onToggled: kcm.pauseForFullscreen = checked
        }

        QQC2.Label {
            Kirigami.FormData.label: ""
            text: i18n("Hide the dragons on a monitor showing something full screen.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }
    }

    // Said once, at the bottom, rather than after saving: whether anything is listening is
    // a fact about the session, not a result of pressing Apply.
    footer: Kirigami.InlineMessage {
        visible: !kcm.daemonRunning()
        position: Kirigami.InlineMessage.Position.Footer
        type: Kirigami.MessageType.Information
        text: i18n("DragonPerch is not running. What is saved here will be read the next time it starts.")
    }
}
