import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Elegir una o VARIAS apps instaladas para enviar sus APK (Android).
Item {
    id: ap
    required property var app
    property bool forWeb: false
    property string pageTitle: forWeb ? qsTr("Compartir apps") : qsTr("Enviar apps")
    property var apps: []
    property bool loading: true
    property string searchText: ""

    property var selApks: []
    property var selLabels: []
    function isSel(apk) { return selApks.indexOf(apk) >= 0 }
    function toggle(apk, label) {
        var a = selApks.slice(), l = selLabels.slice()
        var i = a.indexOf(apk)
        if (i >= 0) { a.splice(i, 1); l.splice(i, 1) }
        else { a.push(apk); l.push(label) }
        selApks = a; selLabels = l
    }

    property var filteredApps: {
        if (searchText.length === 0) return apps
        var q = searchText.toLowerCase()
        var result = []
        for (var i = 0; i < apps.length; i++) {
            if (apps[i].label.toLowerCase().indexOf(q) >= 0)
                result.push(apps[i])
        }
        return result
    }

    function selectAll() {
        var a = [], l = []
        for (var i = 0; i < filteredApps.length; i++) {
            a.push(filteredApps[i].apk)
            l.push(filteredApps[i].label)
        }
        selApks = a; selLabels = l
    }
    function deselectAll() { selApks = []; selLabels = [] }

    Component.onCompleted: loadTimer.start()
    Timer { id: loadTimer; interval: 420
            onTriggered: { ap.apps = JSON.parse(transfer.installedApps()); ap.loading = false } }

    ColumnLayout {
        anchors.centerIn: parent; spacing: 10
        visible: ap.loading
        BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: ap.loading }
        Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("Buscando aplicaciones…")
                color: app.subtextColor; font.pixelSize: 12 }
    }
    Label { anchors.centerIn: parent; visible: !ap.loading && ap.apps.length === 0
            text: qsTr("No hay aplicaciones"); color: app.subtextColor; font.pixelSize: 14 }

    // Barra de búsqueda.
    Rectangle {
        id: searchBar
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        height: 56; visible: !ap.loading && ap.apps.length > 0; color: "transparent"
        Rectangle {
            anchors.fill: parent; anchors.margins: 8; radius: 12; color: app.inputBg
            border.width: 1; border.color: searchField.activeFocus ? app.accent : app.divider
        }
        TextInput {
            id: searchField
            anchors.fill: parent; anchors.margins: 8
            leftPadding: 16; rightPadding: searchField.text.length > 0 ? 44 : 16
            color: app.textColor; font.pixelSize: 15
            clip: true
            verticalAlignment: TextInput.AlignVCenter
            cursorVisible: activeFocus && Qt.inputMethod.keyboardRectangle.height > 0
            Text {
                anchors.verticalCenter: parent.verticalCenter; anchors.left: parent.left; anchors.leftMargin: 16
                text: searchField.text.length === 0 ? qsTr("Buscar aplicación…") : ""
                color: app.subtextColor; font.pixelSize: 15
            }
            onTextChanged: ap.searchText = text
            Keys.onPressed: (e) => { if (e.key === Qt.Key_Escape) { text = ""; focus = false } }
        }
        Rectangle {
            visible: searchField.text.length > 0
            anchors.right: parent.right; anchors.rightMargin: 16; anchors.verticalCenter: parent.verticalCenter
            width: 28; height: 28; radius: 14; color: clearArea.containsMouse ? app.accentA(0.2) : "transparent"
            Icon { anchors.centerIn: parent; name: "close"; size: 16; color: app.textColor }
            MouseArea {
                id: clearArea; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor
                onClicked: { searchField.text = ""; searchField.forceActiveFocus() }
            }
        }
    }

    // Checkbox "Todo": solo visible cuando hay selección.
    Rectangle {
        id: selectBar
        anchors.top: searchBar.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: ap.selApks.length > 0 ? 44 : 0; visible: height > 0; color: "transparent"
        Behavior on height { NumberAnimation { duration: 150 } }
        property bool allSelected: ap.selApks.length > 0 && ap.selApks.length >= ap.filteredApps.length
        Label {
            anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 seleccionada(s)").arg(ap.selApks.length)
            color: app.accent; font.pixelSize: 13; font.bold: true
        }
        Label {
            anchors.right: todoCheck.left; anchors.rightMargin: 8; anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Todo")
            color: app.textColor; font.pixelSize: 13
        }
        Rectangle {
            id: todoCheck
            anchors.right: parent.right; anchors.rightMargin: 28; anchors.verticalCenter: parent.verticalCenter
            width: 24; height: 24; radius: 4
            color: selectBar.allSelected ? app.accent : "transparent"
            border.width: 2; border.color: app.accent
            Text {
                anchors.centerIn: parent; text: "✓"; font.pixelSize: 15; font.bold: true
                color: "white"; visible: selectBar.allSelected
            }
            MouseArea {
                anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                onClicked: selectBar.allSelected ? ap.deselectAll() : ap.selectAll()
            }
        }
    }

    ListView {
        id: appList
        anchors.fill: parent
        anchors.topMargin: searchBar.height + selectBar.height
        anchors.bottomMargin: sendBar.visible ? sendBar.height : 0
        visible: !ap.loading && ap.apps.length > 0
        keyNavigationEnabled: false
        clip: true; model: ap.filteredApps; spacing: 6
        topMargin: 6; bottomMargin: 10; leftMargin: 12; rightMargin: 12
        delegate: ItemDelegate {
            id: appRow
            activeFocusOnTab: true
            width: ListView.view.width - 24; height: 56
            property bool sel: ap.isSel(modelData.apk)
            onClicked: ap.toggle(modelData.apk, modelData.label)
            background: Rectangle {
                radius: 12
                color: appRow.sel ? app.accentA(0.18)
                     : appRow.hovered ? app.cardHover : app.cardColor
                border.width: appRow.sel ? 2 : 0
                border.color: app.accent
            }
            contentItem: RowLayout {
                spacing: 12
                Rectangle {
                    width: 40; height: 40; radius: 11; color: app.accentA(0.16)
                    Image {
                        anchors.centerIn: parent; width: 32; height: 32
                        source: (modelData.icon && modelData.icon.length > 0) ? "file://" + modelData.icon : ""
                        sourceSize.width: 64; sourceSize.height: 64
                        smooth: true; visible: status === Image.Ready
                    }
                    Icon { anchors.centerIn: parent; name: "smartphone"; size: 18; color: app.accent
                           visible: !(modelData.icon && modelData.icon.length > 0) }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 1
                    Label { text: modelData.label; color: app.textColor; font.pixelSize: 14; font.bold: true
                            elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: app.fmtSize(modelData.size); color: app.subtextColor; font.pixelSize: 11 }
                }
                // Checkbox cuadrado de selección.
                Rectangle {
                    width: 24; height: 24; radius: 4
                    color: appRow.sel ? app.accent : "transparent"
                    border.width: appRow.sel ? 0 : 2
                    border.color: app.subtextColor
                    Text {
                        anchors.centerIn: parent; text: "✓"; font.pixelSize: 16; font.bold: true
                        color: "white"; visible: appRow.sel
                    }
                }
            }
        }
    }

    // Barra inferior: enviar las apps seleccionadas.
    Rectangle {
        id: sendBar
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 64
        visible: ap.selApks.length > 0
        color: app.surface
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: app.divider }
        Button {
            anchors.centerIn: parent
            width: parent.width - 32; height: 44
            Material.roundedScale: Material.SmallScale
            highlighted: true
            text: (ap.forWeb ? qsTr("Compartir") : qsTr("Enviar")) + " (" + ap.selApks.length + ")"
            icon.source: "icons/send.svg"; icon.width: 18; icon.height: 18; icon.color: "white"
            onClicked: {
                if (ap.forWeb) {
                    transfer.addWebSharedApps(ap.selApks, ap.selLabels)
                    app.popPage()
                } else {
                    transfer.sendApps(app.pendingHost, app.pendingPort, app.pendingName,
                                      app.pendingPlatform, ap.selApks, ap.selLabels)
                    app.goHome()
                }
            }
        }
    }
}
