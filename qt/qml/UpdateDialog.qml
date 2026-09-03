import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Resultado del chequeo MANUAL de actualizaciones ("Acerca de" → Buscar
// actualizaciones). Modal sobre toda la ventana (Overlay.overlay), como el
// diálogo de solicitud entrante. Fase 1: solo enlaza al Release en GitHub,
// no descarga ni instala nada.
Dialog {
    id: updateDialog
    required property var app
    property string mode: ""            // "available" | "uptodate" | "failed"
    property string newVersion: ""
    property string currentVersion: ""
    property string releaseUrl: ""

    parent: Overlay.overlay
    modal: true; anchors.centerIn: parent
    width: Math.min(parent ? parent.width - 48 : 320, 360)
    padding: 22
    closePolicy: Popup.CloseOnEscape
    Material.background: app.cardColor
    Material.roundedScale: Material.LargeScale
    Overlay.modal: Rectangle { color: "#99000000" }

    function showAvailable(version, url, current) {
        mode = "available"; newVersion = version; releaseUrl = url; currentVersion = current
        open()
    }
    function showUpToDate(current) {
        mode = "uptodate"; currentVersion = current
        open()
    }
    function showFailed() {
        mode = "failed"
        open()
    }

    contentItem: ColumnLayout {
        spacing: 14
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            Icon {
                name: updateDialog.mode === "failed" ? "close"
                      : updateDialog.mode === "uptodate" ? "check" : "download"
                size: 30
                color: updateDialog.mode === "failed" ? "#ef4444" : app.accent
                Layout.alignment: Qt.AlignTop
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 2
                Label {
                    text: updateDialog.mode === "available"
                          ? qsTr("Nueva versión %1 disponible").arg(updateDialog.newVersion)
                          : updateDialog.mode === "uptodate"
                            ? qsTr("Ya tienes la última versión")
                            : qsTr("No se pudo comprobar")
                    color: app.textColor; font.pixelSize: 15; font.bold: true
                    wrapMode: Text.Wrap; Layout.fillWidth: true
                }
                Label {
                    text: updateDialog.mode === "available"
                          ? qsTr("Descarga la nueva versión desde GitHub.")
                          : updateDialog.mode === "uptodate"
                            ? qsTr("Versión %1 instalada.").arg(updateDialog.currentVersion)
                            : qsTr("Revisa tu conexión e inténtalo de nuevo.")
                    color: app.subtextColor; font.pixelSize: 13
                    wrapMode: Text.Wrap; Layout.fillWidth: true
                }
            }
        }
    }
    footer: DialogButtonBox {
        Material.background: "transparent"
        Button {
            flat: true; text: qsTr("Cerrar")
            icon.source: Qt.resolvedUrl("icons/close.svg"); icon.color: app.subtextColor
            icon.width: 14; icon.height: 14; display: AbstractButton.TextBesideIcon
            onClicked: updateDialog.close()
        }
        Button {
            visible: updateDialog.mode === "available"
            highlighted: true; text: qsTr("Ver")
            icon.source: Qt.resolvedUrl("icons/download.svg"); icon.color: app.accent
            icon.width: 14; icon.height: 14; display: AbstractButton.TextBesideIcon
            onClicked: { Qt.openUrlExternally(updateDialog.releaseUrl); updateDialog.close() }
        }
    }
}
