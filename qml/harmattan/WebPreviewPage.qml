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
import com.nokia.meego 1.0
import QtWebKit 1.0

import "Theme.js" as Theme

Page {
    id: root

    property string title
    property string entryId
    property string offlineUrl
    property string onlineUrl
    property bool stared
    property bool read
    property int index
    property int feedindex
    property bool cached
    property int markAsReadTime: 4000

    property bool isPortrait: screen.currentOrientation==Screen.Portrait || screen.currentOrientation==Screen.PortraitInverted

    signal updateViewPort

    ActiveDetector {}

    tools:  WebToolbar {
        stared: root.stared

        onStarClicked: {
            if (stared) {
                stared=false;
                entryModel.setData(root.index, "readlater", 0);
            } else {
                stared=true;
                entryModel.setData(root.index, "readlater", 1);
            }
        }

        onBrowserClicked: {
            notification.show(qsTr("Launching an external browser..."));
            Qt.openUrlExternally(onlineUrl);
        }

        onOfflineClicked: {
            if (settings.offlineMode) {
                if (dm.online)
                    settings.offlineMode = false;
                else
                    notification.show(qsTr("Cannot switch to Online mode\nNetwork connection is unavailable"));
            } else {
                if (root.cached)
                    settings.offlineMode = true;
                else
                    notification.show(qsTr("Offline version not available"));
            }
        }
    }

    orientationLock: {
        switch (settings.allowedOrientations) {
        case 1:
            return PageOrientation.LockPortrait;
        case 2:
            return PageOrientation.LockLandscape;
        }
        return PageOrientation.Automatic;
    }

    onUpdateViewPort: {
        //console.log("onUpdateViewPort");
        var viewport = 1;
        if (settings.fontSize==1)
            viewport = 1.5;
        if (settings.fontSize==2)
            viewport = 2.0;

        /*console.log(view.webview.evaluateJavaScript(
        "(function(){var viewport = document.querySelector('meta[name=\"viewport\"]');
        if (viewport){viewport.content = 'initial-scale="+viewport+", maximum-scale=2.0, user-scalable=no';
        return 1;} document.getElementsByTagName('head')[0].appendChild('<meta name=\"viewport\"
        content=\"initial-scale="+viewport+"\">');return 0;})()"));*/

        view.webview.settings.defaultFontSize = 18*viewport;
         view.webview.settings.minimumFontSize = 18*viewport;
    }

    FlickableWebView {
        id: view

        property int imgWidth: view.width

        /*onImgWidthChanged: {
            console.log("imgWidth:", imgWidth);
        }*/

        url: settings.offlineMode ? offlineUrl+"?width="+imgWidth+"px" : onlineUrl

        onProgressChanged: {
            //console.log("progress:"+progress);
            proggressPanel.progress = progress;

            if (progress<1) {
                proggressPanel.text = qsTr("Loading page content...");
                proggressPanel.open = true;
            } else {
                proggressPanel.open = false;

                // Start timer to mark as read
                if (!root.read)
                    timer.start();
            }
        }

        onLoadFailed: {
            //console.log("LoadFailed");
            proggressPanel.open = false;
        }

        onLoadFinished: {
            //console.log("LoadFinished");
            proggressPanel.open = false;

        }

        onLoadStarted: {
            //console.log("LoadStarted");
            root.updateViewPort();
        }

        anchors.fill: parent
    }

    ProgressPanel {
        id: proggressPanel
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        cancelable: true
        onCloseClicked: view.stop()
    }

    Timer {
        id: timer
        interval: root.markAsReadTime
        onTriggered: {
            if (!root.read) {
                read=true;
                entryModel.setData(root.index, "read", 1);
            }
        }
    }

    Connections {
        target: fetcher
        onBusyChanged: pageStack.pop()
    }

    Connections {
        target: dm
        onBusyChanged: pageStack.pop()
    }

    // Workaround for 'High Power Consumption' webkit bug
    Connections {
        target: Qt.application
        onActiveChanged: {
            if(!Qt.application.active && settings.powerSaveMode) {
                pageStack.pop();
            }
        }
    }
}
