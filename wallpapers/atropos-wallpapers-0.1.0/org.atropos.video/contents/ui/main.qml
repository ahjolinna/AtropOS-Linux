/*
    SPDX-FileCopyrightText: 2026 ahjolinna <ahjolinna@yahoo.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtMultimedia
import org.kde.plasma.plasmoid

WallpaperItem {
    id: root

    readonly property string packagedVideo: "file:///usr/share/atropos/wallpaper-animated.webm"
    readonly property string videoSource: {
        const configured = String(root.configuration.VideoFile || "")
        return configured.length > 0 ? configured : root.packagedVideo
    }

    MediaPlayer {
        id: player
        source: root.videoSource
        videoOutput: output
        loops: MediaPlayer.Infinite
        autoPlay: true

        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.EndOfMedia)
                play()
        }
    }

    VideoOutput {
        id: output
        anchors.fill: parent
        fillMode: VideoOutput.PreserveAspectCrop
    }

    onVisibleChanged: {
        if (visible)
            player.play()
        else
            player.pause()
    }
}
