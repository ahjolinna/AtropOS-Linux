/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * First-login notice. plasma-welcome loads this from
 * extra-pages/ before Get Involved. The page is skippable.
 * It does not start the download. atropos-seed-flatpaks.service does.
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts

import org.kde.kirigami as Kirigami
import org.kde.plasma.welcome as Welcome
import org.kde.plasma.workspace.dbus as DBus

Welcome.Page {
    id: page

    property bool statusKnown: false
    property string activeState: ""
    property string conditionResult: ""
    property string serviceResult: ""
    property string exitStamp: "0"
    property int pollId: 0

    readonly property string phase: {
        if (!statusKnown) {
            return "downloading"
        }
        if (activeState === "activating" || activeState === "active") {
            return "downloading"
        }
        // ConditionPathExists=!marker failed, so the files are already installed.
        if (conditionResult === "false") {
            return "ready"
        }
        if (serviceResult === "success" && Number(exitStamp) > 0) {
            return "ready"
        }
        if (serviceResult !== "" && serviceResult !== "success") {
            return "failed"
        }
        return "downloading"
    }
    readonly property bool downloading: phase === "downloading"
    readonly property int bodyPx: {
        const font = Kirigami.Theme.defaultFont
        const base = font.pixelSize > 0 ? font.pixelSize : (font.pointSize > 0 ? font.pointSize * 1.33 : 14)
        return Math.round(base * 1.35)
    }

    heading: phase === "failed"
        ? i18nc("@info:window", "Applications could not be installed")
        : downloading
            ? i18nc("@info:window", "Applications are still installing")
            : i18nc("@info:window", "Applications are installed")
    description: ""

    function unwrap(v) {
        if (v === undefined || v === null) {
            return ""
        }
        if (typeof v === "boolean") {
            return v ? "true" : "false"
        }
        if (typeof v === "number") {
            return String(v)
        }
        if (typeof v === "string") {
            return v
        }
        if (v.value !== undefined) {
            return unwrap(v.value)
        }
        if (typeof v.toString === "function") {
            const text = String(v.toString())
            if (text !== "" && text.indexOf("object") === -1) {
                return text
            }
        }
        return ""
    }

    function ask(iface, key, apply, id) {
        DBus.SystemBus.asyncCall({
            "service": "org.freedesktop.systemd1",
            "path": "/org/freedesktop/systemd1/unit/atropos_2dseed_2dflatpaks_2eservice",
            "iface": "org.freedesktop.DBus.Properties",
            "member": "Get",
            "signature": "(ss)",
            "arguments": [iface, key]
        }, function (reply) {
            if (id !== page.pollId) {
                return
            }
            apply(page.unwrap(reply.value))
        }, function (reply) {
            if (id !== page.pollId) {
                return
            }
            apply("")
        })
    }

    function beginPoll() {
        const id = ++page.pollId
        const box = {
            "n": 0,
            "active": "",
            "condition": "",
            "result": "",
            "stamp": "0"
        }
        function got(field, value) {
            if (id !== page.pollId) {
                return
            }
            box[field] = value
            box.n += 1
            if (box.n < 4) {
                return
            }
            page.activeState = box.active
            page.conditionResult = box.condition
            page.serviceResult = box.result
            page.exitStamp = box.stamp
            page.statusKnown = true
        }
        page.ask("org.freedesktop.systemd1.Unit", "ActiveState", function (v) { got("active", v) }, id)
        page.ask("org.freedesktop.systemd1.Unit", "ConditionResult", function (v) { got("condition", v) }, id)
        page.ask("org.freedesktop.systemd1.Service", "Result", function (v) { got("result", v) }, id)
        page.ask("org.freedesktop.systemd1.Service", "ExecMainExitTimestamp", function (v) { got("stamp", v) }, id)
    }

    Timer {
        interval: 1500
        repeat: true
        running: page.downloading
        triggeredOnStart: true
        onTriggered: page.beginPoll()
    }

    topContent: [
        Item {
            id: iconBox
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Kirigami.Units.largeSpacing
            implicitWidth: Kirigami.Units.iconSizes.huge
            implicitHeight: Kirigami.Units.iconSizes.huge + Kirigami.Units.gridUnit * 2

            Kirigami.Icon {
                id: statusIcon
                anchors.horizontalCenter: parent.horizontalCenter
                width: Kirigami.Units.iconSizes.huge
                height: Kirigami.Units.iconSizes.huge
                source: page.phase === "failed" ? "dialog-warning" : page.downloading ? "download" : "checkmark"
                transform: Translate {
                    id: downloadShift
                    y: 0
                }
            }

            SequentialAnimation {
                running: page.downloading
                loops: Animation.Infinite
                onRunningChanged: if (!running) downloadShift.y = 0
                NumberAnimation {
                    target: downloadShift
                    property: "y"
                    from: 0
                    to: Kirigami.Units.gridUnit * 1.7
                    duration: 650
                    easing.type: Easing.InOutQuad
                }
                NumberAnimation {
                    target: downloadShift
                    property: "y"
                    from: Kirigami.Units.gridUnit * 1.7
                    to: 0
                    duration: 650
                    easing.type: Easing.InOutQuad
                }
            }
        },
        Item {
            id: track
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: Kirigami.Units.gridUnit * 24
            Layout.topMargin: Kirigami.Units.largeSpacing
            implicitHeight: Math.max(8, Math.round(Kirigami.Units.gridUnit * 0.55))

            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.14)
            }

            Rectangle {
                id: chunk
                height: track.height
                radius: height / 2
                color: Kirigami.Theme.highlightColor
                width: page.downloading ? Math.max(28, track.width * 0.36) : track.width
                x: 0

                SequentialAnimation on x {
                    running: page.downloading && track.width > chunk.width
                    loops: Animation.Infinite
                    onRunningChanged: if (!running) chunk.x = 0
                    NumberAnimation {
                        from: 0
                        to: Math.max(0, track.width - chunk.width)
                        duration: 850
                        easing.type: Easing.InOutQuad
                    }
                    NumberAnimation {
                        from: Math.max(0, track.width - chunk.width)
                        to: 0
                        duration: 850
                        easing.type: Easing.InOutQuad
                    }
                }
            }
        },
        QQC2.Label {
            Layout.fillWidth: true
            Layout.topMargin: Kirigami.Units.largeSpacing
            Layout.leftMargin: Kirigami.Units.largeSpacing
            Layout.rightMargin: Kirigami.Units.largeSpacing
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font.pixelSize: page.bodyPx
            font.weight: Font.Medium
            text: page.phase === "failed"
                ? i18nc("@info", "The download did not finish. It will try again the next time the computer starts.")
                : page.downloading
                    ? i18nc("@info", "Firefox and the other Flatpak applications are downloading in the background. They appear in the menu when the download finishes. You can keep using the desktop.")
                    : i18nc("@info", "Firefox and the other Flatpak applications are installed. Open them from the application menu.")
        }
    ]
}
