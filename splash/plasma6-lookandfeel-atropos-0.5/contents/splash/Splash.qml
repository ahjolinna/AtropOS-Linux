/*
    Plasma 6 port of the KSplashX theme by Michail Vourlakos.
    Sprite layout matches 1600x1200/description.txt:

        ANIM_REL 1 CC -500 300 CC 100 animation.png 35 1
        WAIT_STATE wm
        ANIM_REL 2 CC -200 400 CC 72 experiencefreedom.png 30 1

    Positions match that script. Playback is 1.1 s then 0.9 s so it
    finishes during a Plasma 6 login. The background is the AtropOS 02
    wallpaper, cropped to fill. The gear and the slogan are unchanged.

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick

Image {
    id: root
    source: "file:///usr/share/wallpapers/atropos-02/contents/images/1376x768.jpg"
    fillMode: Image.PreserveAspectCrop
    horizontalAlignment: Image.AlignHCenter
    verticalAlignment: Image.AlignVCenter
    smooth: true
    mipmap: true
    cache: true

    property int stage

    // Coordinates in description.txt are for the 1600x1200 sheet.
    readonly property real designWidth: 1600
    readonly property real designHeight: 1200
    readonly property real sceneScale: Math.max(width / designWidth, height / designHeight)
    readonly property real sceneX: (width - designWidth * sceneScale) / 2
    readonly property real sceneY: (height - designHeight * sceneScale) / 2
    // Sprite offset kept from the Fuuten layout. The background is no
    // longer the poster sheet.
    readonly property int lift: 220

    // The KDE 4 delays (35 ms and 30 ms) outlast a Plasma 6 login.
    // Gear, then the slogan, and both are finished in 2 seconds.
    readonly property int iconDuration: 1100
    readonly property int textDuration: 900
    property bool clockOn: false
    property double t0: 0
    property double nowMs: 0
    readonly property real elapsed: clockOn ? (nowMs - t0) : 0

    Timer {
        interval: 16
        repeat: true
        running: iconAnim.status === Image.Ready
                 && (!root.clockOn || root.elapsed < root.iconDuration + root.textDuration)
        onTriggered: {
            var t = Date.now()
            if (!root.clockOn) {
                root.t0 = t
                root.clockOn = true
            }
            root.nowMs = t
        }
    }

    Image {
        id: iconAnim
        readonly property int frames: 100
        readonly property int frameWidth: 400
        readonly property int frameHeight: 300
        readonly property int columns: 10
        readonly property int designX: Math.round(root.designWidth / 2 - 500 - frameWidth / 2)
        readonly property int designY: Math.round(root.designHeight / 2 + 300 - frameHeight / 2 - root.lift)
        readonly property int frame: {
            if (!root.clockOn)
                return 0
            return Math.min(frames - 1, Math.floor(root.elapsed / root.iconDuration * frames))
        }

        source: "images/animation.png"
        x: root.sceneX + designX * root.sceneScale
        y: root.sceneY + designY * root.sceneScale
        width: frameWidth * root.sceneScale
        height: frameHeight * root.sceneScale
        fillMode: Image.Stretch
        smooth: true
        mipmap: true
        cache: true
        sourceClipRect: Qt.rect((frame % columns) * frameWidth,
                                 Math.floor(frame / columns) * frameHeight,
                                 frameWidth, frameHeight)
    }

    Image {
        id: textAnim
        readonly property int frames: 72
        readonly property int frameWidth: 400
        readonly property int frameHeight: 175
        readonly property int columns: 10
        readonly property int designX: Math.round(root.designWidth / 2 - 200 - frameWidth / 2)
        readonly property int designY: Math.round(root.designHeight / 2 + 400 - Math.floor(frameHeight / 2) - root.lift)
        readonly property int frame: {
            if (!root.clockOn)
                return 0
            var t = root.elapsed - root.iconDuration
            if (t <= 0)
                return 0
            return Math.min(frames - 1, Math.floor(t / root.textDuration * frames))
        }

        source: "images/experiencefreedom.png"
        x: root.sceneX + designX * root.sceneScale
        y: root.sceneY + designY * root.sceneScale
        width: frameWidth * root.sceneScale
        height: frameHeight * root.sceneScale
        fillMode: Image.Stretch
        smooth: true
        mipmap: true
        cache: true
        sourceClipRect: Qt.rect((frame % columns) * frameWidth,
                                 Math.floor(frame / columns) * frameHeight,
                                 frameWidth, frameHeight)
    }
}
