/*
    SPDX-FileCopyrightText: 2026 ahjolinna <ahjolinna@yahoo.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami

Kirigami.FormLayout {
    id: root
    twinFormLayouts: parentLayout

    property alias cfg_VideoFile: pathField.text

    QQC2.TextField {
        id: pathField
        Kirigami.FormData.label: "Video file:"
        implicitWidth: Kirigami.Units.gridUnit * 24
    }
}
