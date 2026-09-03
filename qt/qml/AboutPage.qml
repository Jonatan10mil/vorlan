import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: aboutPage
    required property var app
    property string pageTitle: qsTr("Acerca de")
    property alias updateDialogItem: updateDialog   // para el botón atrás (Main.onClosing)
    contentHeight: aboutCol.implicitHeight + 24
    clip: true
    ColumnLayout {
        id: aboutCol
        x: 14; width: parent.width - 28; y: 12
        spacing: 5

        // Icono + nombre + versión
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter; spacing: 8
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 54; height: 54; radius: 15
                color: app.accent
                Icon { anchors.centerIn: parent; name: "vortex"; size: 28; color: "white" }
            }
            Label { Layout.alignment: Qt.AlignHCenter; text: "VorLAN"
                    color: app.textColor; font.family: app.titleFont
                    font.pixelSize: 18; font.bold: true }
            Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("Versión") + " " + transfer.appVersion
                    color: app.subtextColor; font.pixelSize: 12 }
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Comparte archivos, carpetas y texto entre dispositivos de tu red local, sin internet ni cuentas.")
            color: app.subtextColor; font.pixelSize: 13
            wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
        }

        // Datos técnicos
        Rectangle {
            Layout.fillWidth: true; radius: 10; color: app.cardColor
            Layout.preferredHeight: infoCol.implicitHeight
            ColumnLayout {
                id: infoCol; width: parent.width; spacing: 0
                RowLayout {
                    id: platRow
                    Layout.fillWidth: true
                    Layout.leftMargin: 12; Layout.rightMargin: 12
                    Layout.topMargin: 12; Layout.bottomMargin: 12; spacing: 12
                    readonly property var platModel: [
                        {n: "android", t: "Android"},
                        {n: "windows", t: "Windows"},
                        {n: "linux",   t: "Linux"},
                        {n: "apple",   t: "macOS"}
                    ]
                    Label { id: platLabel
                            text: qsTr("Plataformas"); color: app.subtextColor; font.pixelSize: 13
                            Layout.alignment: Qt.AlignTop; Layout.topMargin: 2 }
                    Item {
                        id: platBox
                        Layout.fillWidth: true
                        Layout.preferredHeight: fits ? chipsRow.implicitHeight : chipsFlow.implicitHeight
                        readonly property bool fits: width > 0 && chipsRow.implicitWidth <= width
                        Row {
                            id: chipsRow
                            visible: platBox.fits
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 12
                            Repeater {
                                model: platRow.platModel
                                Row { spacing: 4
                                    Icon { name: modelData.n; size: 13; color: app.textColor
                                           anchors.verticalCenter: parent.verticalCenter }
                                    Label { text: modelData.t; color: app.textColor; font.pixelSize: 13 }
                                }
                            }
                        }
                        Flow {
                            id: chipsFlow
                            visible: !platBox.fits
                            width: parent.width; spacing: 12
                            Repeater {
                                model: platRow.platModel
                                Row { spacing: 4
                                    Icon { name: modelData.n; size: 13; color: app.textColor
                                           anchors.verticalCenter: parent.verticalCenter }
                                    Label { text: modelData.t; color: app.textColor; font.pixelSize: 13 }
                                }
                            }
                        }
                    }
                }
                Repeater {
                    model: [
                        {k: qsTr("Descubrimiento"), v: "UDP 51888"},
                        {k: qsTr("Transferencia"), v: "TCP 51889"}
                    ]
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 0
                        Rectangle { Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                                    height: 1; color: app.divider }
                        RowLayout {
                            Layout.fillWidth: true; Layout.preferredHeight: 40
                            Layout.leftMargin: 12; Layout.rightMargin: 12; spacing: 8
                            Label { text: modelData.k; color: app.subtextColor; font.pixelSize: 13 }
                            Label { text: modelData.v; color: app.textColor; font.pixelSize: 13
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignRight; elide: Text.ElideRight }
                        }
                    }
                }
                // Código fuente
                Rectangle { Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                            height: 1; color: app.divider }
                ItemDelegate {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    Layout.leftMargin: 0; Layout.rightMargin: 0
                    padding: 0; topPadding: 0; bottomPadding: 0
                    leftPadding: 0; rightPadding: 0
                    onClicked: Qt.openUrlExternally("https://github.com/Jonatan10mil/vorlan")
                    background: Rectangle { color: "transparent"
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 12; anchors.rightMargin: 12 }
                    contentItem: RowLayout {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 12; anchors.rightMargin: 12
                        spacing: 8
                        Label { text: qsTr("Código fuente"); color: app.subtextColor; font.pixelSize: 13
                                Layout.fillWidth: true }
                        Icon { name: "github"; size: 13; color: app.accent }
                        Icon { name: "link"; size: 13; color: app.subtextColor }
                    }
                }
                // Donación
                Rectangle { Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                            height: 1; color: app.divider }
                ItemDelegate {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    Layout.leftMargin: 0; Layout.rightMargin: 0
                    padding: 0; topPadding: 0; bottomPadding: 0
                    leftPadding: 0; rightPadding: 0
                    onClicked: app.pushPage(donatePageComp)
                    background: Rectangle { color: "transparent"
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 12; anchors.rightMargin: 12 }
                    contentItem: RowLayout {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 12; anchors.rightMargin: 12
                        spacing: 8
                        Label { text: qsTr("Donación"); color: app.subtextColor; font.pixelSize: 13
                                Layout.fillWidth: true }
                        Icon { name: "heart"; size: 13; color: app.accent }
                        Icon { name: "back"; size: 13; color: app.subtextColor; rotation: 180 }
                    }
                }
                // Buscar actualizaciones (chequeo manual contra GitHub Releases)
                Rectangle { Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12
                            height: 1; color: app.divider }
                ItemDelegate {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 40
                    Layout.leftMargin: 0; Layout.rightMargin: 0
                    padding: 0; topPadding: 0; bottomPadding: 0
                    leftPadding: 0; rightPadding: 0
                    enabled: !transfer.updateChecking
                    onClicked: transfer.checkForUpdates()
                    background: Rectangle { color: "transparent"
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 12; anchors.rightMargin: 12 }
                    contentItem: RowLayout {
                        anchors.left: parent.left; anchors.right: parent.right
                        anchors.leftMargin: 12; anchors.rightMargin: 12
                        spacing: 8
                        Label { text: qsTr("Buscar actualizaciones"); color: app.subtextColor; font.pixelSize: 13
                                Layout.fillWidth: true }
                        BusyIndicator {
                            implicitWidth: 16; implicitHeight: 16
                            running: transfer.updateChecking
                            visible: transfer.updateChecking
                        }
                        Icon { name: "download"; size: 13; color: app.accent
                               visible: !transfer.updateChecking }
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter; spacing: 4
            Label { text: qsTr("Impulsado por"); color: app.subtextColor; font.pixelSize: 11 }
            Icon { name: "qt_logo"; size: 12; tinted: false }
        }

        // Diálogo modal con el resultado + escucha de las señales de transfer.
        // Ojo: `app: aboutPage.app` (cualificado), no `app: app`, que se
        // auto-sombrearía con la propia propiedad del diálogo (ver Main.qml).
        UpdateDialog { id: updateDialog; app: aboutPage.app }
        Connections {
            target: transfer
            function onUpdateAvailable(version, url) {
                updateDialog.showAvailable(version, url, transfer.appVersion)
            }
            function onUpdateUpToDate() { updateDialog.showUpToDate(transfer.appVersion) }
            function onUpdateCheckFailed() { updateDialog.showFailed() }
        }
    }
}
