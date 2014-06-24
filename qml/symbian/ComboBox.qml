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

    property alias label: label.text
    property variant menu
    property int currentIndex

    onCurrentIndexChanged: {
        comboboxButton.text = comboboxDialog.model.get(currentIndex).text;
    }

    Component.onCompleted: {
        comboboxButton.text = comboboxDialog.model.get(currentIndex).text;
    }

    anchors {
        left: parent.left;
        right: parent.right;
    }

    height: Math.max(comboboxButton.height,label.height)

    Label {
        id: label
        wrapMode: Text.WordWrap
        anchors { left: parent.left; right: comboboxButton.left;
            verticalCenter: parent.verticalCenter; rightMargin: platformStyle.paddingMedium}
    }

    Button {
        id: comboboxButton

        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter

        text: comboboxDialog.model.get(currentIndex).name
        onClicked: comboboxDialog.open();

        width: text!=="" ? parent.width/2 : 0

        ToolButton {
            id: filterImage
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            iconSource: "combobox-arrow.png"
        }

        SelectionDialog {
            id: comboboxDialog
            titleText: qsTr("Select")

            model: root.menu
            onAccepted: { currentIndex = comboboxDialog.selectedIndex }
        }

    }
}
