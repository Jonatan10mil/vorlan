import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

// Elegir QUÉ ofrece este equipo para descargar desde el navegador.
// Mismas opciones que al enviar por LAN (archivos, carpeta, texto, portapapeles
// y, en Android, las apps instaladas).
Item {
    id: wsp
    required property var app
    property string pageTitle: qsTr("Compartir")
    property bool dropHover: false

    FileDialog {
        id: filesDialog
        title: qsTr("Elegir archivos para compartir")
        fileMode: FileDialog.OpenFiles
        onAccepted: { transfer.addWebShared(selectedFiles); app.popPage() }
    }
    FolderDialog {
        id: folderDialog
        title: qsTr("Elegir carpeta para compartir")
        onAccepted: { transfer.addWebShared([selectedFolder]); app.popPage() }
    }

    // Arrastrar archivos sobre la página → añadir a la lista de compartidos
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onEntered: (drag) => { wsp.dropHover = true; drag.accept(Qt.CopyAction) }
        onExited: wsp.dropHover = false
        onDropped: (drop) => {
            wsp.dropHover = false
            if (drop.hasUrls) {
                transfer.addWebShared(drop.urls)
                app.popPage()
            }
        }
    }

    Component { id: textComp; ComposePage { app: wsp.app; forWeb: true } }
    Component { id: appsComp; AppsPage { app: wsp.app; forWeb: true } }

    Component.onCompleted: transfer.refreshClipboard()

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 10

        Label {
            text: qsTr("¿Qué quieres compartir?")
            color: app.subtextColor; font.pixelSize: 12
        }

        Repeater {
            model: {
                var m = [ {t: qsTr("Archivos"),     ic: "file",      a: "files"},
                          {t: qsTr("Carpeta"),      ic: "folder",    a: "folder"},
                          {t: qsTr("Texto"),        ic: "message",   a: "text"},
                          {t: qsTr("Portapapeles"), ic: "clipboard", a: "clipboard"} ]
                if (Qt.platform.os === "android")
                    m.push({t: qsTr("Aplicaciones"), ic: "smartphone", a: "apps"})
                return m
            }
            ItemDelegate {
                activeFocusOnTab: enabled
                readonly property bool isClip: modelData.a === "clipboard"
                // El portapapeles solo comparte texto → deshabilitado si está vacío.
                enabled: !isClip || transfer.clipboardHasText
                opacity: enabled ? 1 : 0.45
                Layout.fillWidth: true; Layout.preferredHeight: 48
                text: isClip && !enabled ? modelData.t + "  ·  " + qsTr("vacío") : modelData.t
                icon.source: "icons/" + modelData.ic + ".svg"
                icon.width: 20; icon.height: 20; icon.color: app.accent
                background: Rectangle { radius: 12; color: parent.hovered ? app.cardHover : app.cardColor }
                onClicked: {
                    if (modelData.a === "files") filesDialog.open()
                    else if (modelData.a === "folder") folderDialog.open()
                    else if (modelData.a === "text") app.pushPage(textComp)
                    else if (modelData.a === "clipboard") {
                        transfer.shareClipboardWeb()
                        app.popPage()
                    }
                    else app.pushPage(appsComp)
                }
            }
        }

        Item { Layout.fillHeight: true; Layout.fillWidth: true }
    }

    // Feedback al arrastrar sobre la página (escritorio): toda la vista es zona de soltar.
    Rectangle {
        anchors.fill: parent; anchors.margins: 8
        visible: wsp.dropHover; radius: 14
        color: app.accentA(0.15)
        border.width: 2; border.color: app.accent
        ColumnLayout {
            anchors.centerIn: parent; spacing: 8
            Icon { Layout.alignment: Qt.AlignHCenter; name: "download"; size: 34; color: app.accent }
            Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("Soltar para compartir")
                    color: app.textColor; font.pixelSize: 15; font.bold: true }
        }
    }
}
