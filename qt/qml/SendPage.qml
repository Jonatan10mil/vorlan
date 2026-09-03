import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

// Elegir qué enviar al dispositivo destino (archivos, carpeta, texto, portapapeles, apps).
Item {
    id: sp
    required property var app
    property string targetName: ""
    property string targetPlatform: ""
    property string targetAddress: ""
    property string pageTitle: qsTr("Enviar a %1").arg(targetName)
    property bool dropHover: false
    property string activeTab: "files"
    Component.onCompleted: transfer.refreshClipboard()   // re-evaluar estado del portapapeles

    // Diálogos exclusivos de esta página.
    FileDialog {
        id: fileDialog; title: qsTr("Elegir archivos"); fileMode: FileDialog.OpenFiles
        onAccepted: {
            transfer.enqueueSend(app.pendingHost, app.pendingPort,
                                 sp.targetName, sp.targetPlatform, selectedFiles)
            app.popPage()
        }
    }
    FolderDialog {
        id: folderDialog; title: qsTr("Elegir carpeta")
        onAccepted: {
            transfer.enqueueSend(app.pendingHost, app.pendingPort,
                                 sp.targetName, sp.targetPlatform, [selectedFolder])
            app.popPage()
        }
    }
    // El selector de archivos de Android devolvió una selección → cerrar esta página
    // (igual que hace el diálogo de carpeta al aceptar).
    Connections {
        target: transfer
        function onAndroidFilesPicked() { app.popPage() }
    }

    // Sub-páginas que se abren desde aquí.
    Component { id: composePageComp; ComposePage { app: sp.app } }
    Component { id: appsPageComp; AppsPage { app: sp.app } }

    // Soltar archivos en cualquier parte de la página → enviar
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        onEntered: (drag) => { sp.dropHover = true; drag.accept(Qt.CopyAction) }
        onExited: sp.dropHover = false
        onDropped: (drop) => {
            sp.dropHover = false
            if (!drop.hasUrls) return
            // Detectar si hay carpetas en el drop.
            var hasFolders = false
            for (var i = 0; i < drop.urls.length; i++) {
                var p = drop.urls[i].toString()
                if (p.endsWith('/') || p.endsWith('\\')) { hasFolders = true; break }
            }
            // En pestaña "Archivos": rechazar carpetas.
            if (hasFolders && sp.activeTab === "files") return
            transfer.enqueueSend(app.pendingHost, app.pendingPort,
                                 sp.targetName, sp.targetPlatform, drop.urls)
            app.popPage()
        }
    }

    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 12

        // Tarjeta del destino
        Rectangle {
            Layout.fillWidth: true; height: 60; radius: 14; color: app.cardColor
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 12
                Rectangle {
                    width: 40; height: 40; radius: 13
                    gradient: Gradient {
                        GradientStop { position: 0; color: Qt.lighter(app.accent, 1.25) }
                        GradientStop { position: 1; color: Qt.darker(app.accent, 1.35) }
                    }
                    Icon { anchors.centerIn: parent
                           name: app.platformIconName(sp.targetPlatform); size: 20; color: "white" }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 1
                    Label { text: sp.targetName; color: app.textColor; font.pixelSize: 15; font.bold: true
                            elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: sp.targetAddress + " · " + app.platformName(sp.targetPlatform)
                            color: app.subtextColor; font.pixelSize: 11
                            elide: Text.ElideRight; Layout.fillWidth: true }
                }
            }
        }

        Label { text: qsTr("¿Qué quieres enviar?"); color: app.subtextColor; font.pixelSize: 12 }

        Repeater {
            model: {
                var m = [ {t: qsTr("Archivos"), ic: "file", a: "files"}, {t: qsTr("Carpeta"), ic: "folder", a: "folder"},
                          {t: qsTr("Texto"), ic: "message", a: "text"}, {t: qsTr("Portapapeles"), ic: "clipboard", a: "clipboard"} ]
                if (Qt.platform.os === "android")
                    m.push({t: qsTr("Aplicaciones"), ic: "smartphone", a: "apps"})
                return m
            }
            ItemDelegate {
                activeFocusOnTab: enabled
                readonly property bool isClip: modelData.a === "clipboard"
                // El portapapeles solo envía texto → deshabilitar si no hay texto.
                enabled: !isClip || transfer.clipboardHasText
                opacity: enabled ? 1 : 0.45
                Layout.fillWidth: true; Layout.preferredHeight: 48
                text: isClip && !enabled ? modelData.t + "  ·  " + qsTr("vacío") : modelData.t
                icon.source: "icons/" + modelData.ic + ".svg"
                icon.width: 20; icon.height: 20; icon.color: app.accent
                background: Rectangle { radius: 12; color: parent.hovered ? app.cardHover : app.cardColor }
                onClicked: {
                    sp.activeTab = modelData.a
                    if (modelData.a === "files") {
                        // Android: selector propio (el de Qt no lee ClipData y con
                        // selección múltiple devolvía lista vacía → no se enviaba nada).
                        if (Qt.platform.os === "android") {
                            // No cerrar aquí: se cierra cuando el selector devuelve
                            // una selección (señal androidFilesPicked). Si se cierra
                            // ahora, se ve el salto a la página principal.
                            transfer.pickAndroidFiles(app.pendingHost, app.pendingPort,
                                                      sp.targetName, sp.targetPlatform)
                        } else {
                            fileDialog.open()
                        }
                    }
                    else if (modelData.a === "folder") folderDialog.open()
                    else if (modelData.a === "clipboard") {
                        transfer.enqueueClipboard(app.pendingHost, app.pendingPort,
                                                  sp.targetName, sp.targetPlatform)
                        app.popPage()
                    }
                    else if (modelData.a === "apps") app.pushPage(appsPageComp)
                    else app.pushPage(composePageComp)
                }
            }
        }

        // Relleno: empuja las opciones arriba (toda la página acepta soltar).
        Item { Layout.fillHeight: true; Layout.fillWidth: true }
    }

    // Feedback al arrastrar sobre la página (escritorio): toda la vista es zona de soltar.
    Rectangle {
        anchors.fill: parent; anchors.margins: 8
        visible: sp.dropHover; radius: 14
        color: app.accentA(0.15)
        border.width: 2; border.color: app.accent
        ColumnLayout {
            anchors.centerIn: parent; spacing: 8
            Icon { Layout.alignment: Qt.AlignHCenter; name: "download"; size: 34; color: app.accent }
            Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("Suelta para enviar")
                    color: app.textColor; font.pixelSize: 15; font.bold: true }
        }
    }
}
