import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

// Perfil: nombre + avatar del dispositivo.
Item {
    required property var app
    property string pageTitle: qsTr("Tu dispositivo")

    FileDialog {
        id: avatarDialog
        title: qsTr("Elegir imagen de avatar")
        nameFilters: [qsTr("Imágenes (*.png *.jpg *.jpeg *.webp *.bmp *.gif)")]
        onAccepted: transfer.setAvatarImage(selectedFile)
    }

    Flickable {
        anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: profBtnBar.top
        contentHeight: profCol.implicitHeight + 24; clip: true
        ColumnLayout {
            id: profCol
            x: 18; width: parent.width - 36; y: 16
            spacing: 16

            // Avatar (toca para cambiar la imagen)
            ColumnLayout {
                Layout.alignment: Qt.AlignHCenter; spacing: 6
                Item {
                    Layout.alignment: Qt.AlignHCenter
                    width: 88; height: 88
                    activeFocusOnTab: true
                    Keys.onPressed: (e) => {
                        if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter
                            || e.key === Qt.Key_Space || e.key === Qt.Key_Select) {
                            avatarDialog.open(); e.accepted = true
                        }
                    }
                    AvatarView { anchors.fill: parent; size: 88; imageUrl: transfer.avatarImage; accent: app.accent }
                    Rectangle {
                        width: 28; height: 28; radius: 14
                        anchors.right: parent.right; anchors.bottom: parent.bottom
                        color: app.accent; border.width: 2; border.color: app.bgColor
                        Icon { anchors.centerIn: parent; name: "camera"; size: 15; color: "white" }
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                onClicked: avatarDialog.open() }
                }
                Label { Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Toca para cambiar la imagen"); color: app.subtextColor; font.pixelSize: 11 }
            }

            // Nombre (editable)
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Label { text: qsTr("Nombre"); color: app.subtextColor; font.pixelSize: 12 }
                TextField { Material.roundedScale: Material.ExtraSmallScale;
                    id: profileName; activeFocusOnTab: true; Layout.fillWidth: true; Layout.preferredHeight: 40
                    topPadding: 0; bottomPadding: 0
                    Component.onCompleted: text = transfer.deviceName
                    onAccepted: { transfer.deviceName = text; app.goHome() }
                }
            }
            // Nombre del equipo (solo lectura)
            ColumnLayout {
                Layout.fillWidth: true; spacing: 4
                Label { text: qsTr("Nombre del equipo"); color: app.subtextColor; font.pixelSize: 12 }
                Label { text: transfer.hostName; color: app.textColor; font.pixelSize: 14 }
            }
            // Direcciones IP (solo lectura, una por fila)
            ColumnLayout {
                Layout.fillWidth: true; spacing: 3
                Label { text: qsTr("Direcciones IP"); color: app.subtextColor; font.pixelSize: 12 }
                Repeater {
                    model: transfer.localIp !== "" ? transfer.localIp.split(", ") : ["—"]
                    Label { text: modelData; color: app.textColor; font.pixelSize: 14
                            Layout.fillWidth: true }
                }
            }
        }
    }

    // Barra inferior fija con los botones
    Rectangle {
        id: profBtnBar
        anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right
        height: 60 + SafeArea.margins.bottom; color: app.surface
        Rectangle { anchors.top: parent.top; width: parent.width; height: 1; color: app.divider }
        RowLayout {
            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
            anchors.leftMargin: 16; anchors.rightMargin: 16; height: 60; spacing: 8
            Button { Material.roundedScale: Material.ExtraSmallScale; activeFocusOnTab: true; text: qsTr("Restablecer"); flat: true
                     icon.source: "icons/reset.svg"; icon.width: 15; icon.height: 15; icon.color: app.accent
                     onClicked: { transfer.resetProfile(); profileName.text = transfer.deviceName } }
            Item { Layout.fillWidth: true }
            Button { Material.roundedScale: Material.ExtraSmallScale; activeFocusOnTab: true; text: qsTr("Guardar"); highlighted: true
                     icon.source: "icons/check.svg"; icon.width: 15; icon.height: 15; icon.color: "#fff"
                     onClicked: { transfer.deviceName = profileName.text; app.goHome() } }
        }
    }
}
