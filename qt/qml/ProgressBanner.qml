import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Effects

// Banner global de envío/recepción (visible en ambas pestañas).
// La solicitud entrante se maneja por separado en Main.qml (incomingDialog).
Pane {
    id: bannerPane
    required property var app

    Material.background: app.surface
    Material.elevation: 2
    padding: 14

    background: Rectangle {
        color: app.surface
        radius: 12

        layer.enabled: bannerPane.Material.elevation > 0
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "#20000000"
            shadowBlur: 0.4
            shadowVerticalOffset: bannerPane.Material.elevation
        }
    }
    contentItem: ColumnLayout {
        id: bannerLayout
        Layout.fillWidth: true
        spacing: 12

        // Estado: en curso (busy) o resultado persistente.
        RowLayout {
            id: statusRow
            readonly property bool result: !transfer.busy && transfer.lastResult !== ""
            Layout.fillWidth: true; spacing: 8
            Icon {
                size: 17
                color: transfer.lastResult === "error" ? "#ef4444" : app.accent
                name: statusRow.result
                      ? (transfer.lastResult === "error" ? "info" : "check")
                      : (transfer.state === "sending" ? "send" : "download")
            }
            Label {
                Layout.fillWidth: true; elide: Text.ElideRight
                color: app.textColor; font.pixelSize: 14; font.bold: true
                text: statusRow.result
                      ? (transfer.lastResult === "received" ? qsTr("Recibido")
                         : transfer.lastResult === "sent" ? qsTr("Enviado") : qsTr("Error"))
                      : (transfer.state === "sending"
                          ? (transfer.statusCount !== "" ? qsTr("Enviando %1…").arg(transfer.statusCount) : qsTr("Enviando…"))
                          : (transfer.statusCount !== "" ? qsTr("Recibiendo %1…").arg(transfer.statusCount) : qsTr("Recibiendo…")))
            }
            Button { Material.roundedScale: Material.ExtraSmallScale; visible: transfer.busy; activeFocusOnTab: visible; text: qsTr("Cancelar"); flat: true; onClicked: transfer.cancel() }
            Button { Material.roundedScale: Material.ExtraSmallScale; visible: !transfer.busy && transfer.lastResult !== ""; activeFocusOnTab: visible; text: qsTr("Limpiar"); flat: true; onClicked: { transfer.clearLastResult(); transfer.clearFinishedSends(); } }
        }
        RowLayout {
            visible: transfer.busy || transfer.lastResult !== ""
            Layout.fillWidth: true; spacing: 8
            Label {
                Layout.fillWidth: true; elide: Text.ElideMiddle
                text: (!transfer.busy && transfer.lastResult !== "") ? transfer.lastResultText : transfer.statusName
                color: app.subtextColor; font.pixelSize: 12
                visible: text !== ""
            }
            Label {
                text: (!transfer.busy && transfer.lastResult !== "") ? "" : transfer.statusSize
                color: app.subtextColor; font.pixelSize: 12
                Layout.alignment: Qt.AlignRight
                horizontalAlignment: Text.AlignRight
            }
        }
        ProgressBar {
            visible: transfer.busy || transfer.lastResult !== ""
            Layout.fillWidth: true; from: 0; to: 1
            value: transfer.busy ? transfer.progress : 1
        }
    }
}
