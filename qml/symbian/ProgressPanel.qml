/*
  Copyright (C) 2014 Michal Kosciesza <michal@mkiol.net>

  This file is part of Kaktus.

  Kaktus is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Kaktus is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Kaktus.  If not, see <http://www.gnu.org/licenses/>.
*/

import QtQuick 1.1
import com.nokia.symbian 1.1

Item {
    id: root

    property string text
    property bool cancelable: true
    property bool open: false
    property real progress: 0.0
    signal closeClicked

    height: inPortrait ? privateStyle.toolBarHeightPortrait : privateStyle.toolBarHeightLandscape

    enabled: open
    opacity: open ? 1.0 : 0.0
    visible: opacity > 0.0

    //width: parent.width

    Behavior on opacity { NumberAnimation { duration: 300 } }

    Rectangle {
        id: background
        anchors.fill: parent
        color: Qt.rgba(0.1,0.1,0.1, 0.9)
    }

    function show(text) {
        root.text = text;
        root.open = true;
    }

    function hide() {
        root.open = false;
        root.progress = 0.0;
    }

    Rectangle {
        id: progressRect
        height: parent.height
        anchors.left: parent.left
        width: root.progress * parent.width
        color: Qt.rgba(0.3,0.3,0.3, 0.8)

        Behavior on width {
            enabled: root.opacity == 1.0
            SmoothedAnimation {
                velocity: 480; duration: 200
            }
        }
    }

    Image {
        id: icon
        //height: 60; width: 60
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: platformStyle.paddingMedium
        source: "graphic-busyindicator-small.png"
        RotationAnimation on rotation {
            loops: Animation.Infinite
            from: 0
            to: 360
            duration: 1200
            running: root.open && Qt.application.active
        }
    }

    onVisibleChanged: {
        if (!visible) {
            progress = 0;
        }
    }

    Label {
        id: titleBar
        height: icon.height
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: icon.right; anchors.right: closeButton.right
        anchors.leftMargin: platformStyle.paddingMedium
        font.pixelSize: platformStyle.fontSizeSmall
        text: root.text
        verticalAlignment: Text.AlignVCenter
    }

    ToolButton {
        id: closeButton
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        iconSource: "close.png"
        visible: root.cancelable
        onClicked: root.closeClicked()
    }
}