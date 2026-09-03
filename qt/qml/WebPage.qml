import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Modo web: cualquier dispositivo con navegador (sin instalar VorLAN) puede
// enviar archivos a este equipo y descargar los que se compartan.
Item {
    id: wp
    required property var app
    property string pageTitle: qsTr("Modo web")
    property bool dropHover: false
    property alias pinDialogItem: pinDialog

    Component { id: webShareComp; WebSharePage { app: wp.app } }

    function consumePendingShare() {
        if (app.pendingShare.length > 0) {
            transfer.addWebSharedPaths(app.pendingShare)
            app.pendingShare = []
        }
        if (app.pendingShareText.length > 0) {
            transfer.setWebText(app.pendingShareText)
            app.pendingShareText = ""
        }
    }

    // Poner/cambiar el PIN de acceso al modo web.
    Dialog {
        id: pinDialog
        parent: Overlay.overlay
        modal: true; anchors.centerIn: parent
        width: Math.min(Overlay.overlay ? Overlay.overlay.width - 48 : 340, 340); padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Material.background: app.cardColor
        Material.roundedScale: Material.LargeScale
        Overlay.modal: Rectangle { color: "#99000000" }
        onOpened: { pinField.text = transfer.webPin; pinField.forceActiveFocus() }
        contentItem: ColumnLayout {
            spacing: 10
            Label { text: transfer.webPinEnabled ? qsTr("Cambiar PIN") : qsTr("Poner un PIN")
                    color: app.textColor; font.family: app.titleFont
                    font.pixelSize: 18; font.bold: true }
            TextField {
                id: pinField
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhDigitsOnly
                echoMode: TextInput.Normal
                horizontalAlignment: TextInput.AlignHCenter
                font.pixelSize: 22; font.letterSpacing: 4
                maximumLength: 12
                placeholderText: "1234"
                onAccepted: if (text.length > 0) { transfer.setWebPin(text); pinDialog.close() }
            }
        }
        footer: DialogButtonBox {
            Material.background: "transparent"
            Button { flat: true; text: qsTr("Cancelar"); icon.source: Qt.resolvedUrl("icons/close.svg"); icon.color: app.accent; icon.width: 14; icon.height: 14; display: AbstractButton.TextBesideIcon; onClicked: { Qt.inputMethod.hide(); pinDialog.close() } }
            Button { highlighted: true; text: qsTr("Guardar"); icon.source: Qt.resolvedUrl("icons/check.svg"); icon.color: app.accent; icon.width: 14; icon.height: 14; display: AbstractButton.TextBesideIcon
                     enabled: pinField.text.length > 0
                     onClicked: { Qt.inputMethod.hide(); transfer.setWebPin(pinField.text); pinDialog.close() } }
        }
    }

    // Arrastrar archivos sobre la página → añadir a la lista de compartidos
    DropArea {
        anchors.fill: parent
        enabled: transfer.webEnabled
        keys: ["text/uri-list"]
        onEntered: (drag) => { wp.dropHover = true; drag.accept(Qt.CopyAction) }
        onExited: wp.dropHover = false
        onDropped: (drop) => {
            wp.dropHover = false
            if (drop.hasUrls) {
                transfer.addWebShared(drop.urls)
                drop.accept(Qt.CopyAction)
            }
        }
    }

    // Flickable para permitir el efecto de rebote al arrastrar, aunque quepa en pantalla.
    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: col.implicitHeight + 28
        boundsBehavior: Flickable.DragAndOvershootBounds
        clip: true

        ColumnLayout {
            id: col
            width: parent.width - 28   // 14px margen izq + 14px margen der
            x: 14; y: 14
            spacing: 6

        // Banner de archivos pendientes (compartidos desde otra app).
        Pane {
            Layout.fillWidth: true
            visible: app.hasPendingShare
            padding: 10
            background: Rectangle { radius: 12; color: app.accentA(0.15)
                                    border.width: 1; border.color: app.accent }
            contentItem: RowLayout {
                spacing: 10
                Icon { name: app.pendingShareText.length > 0 ? "message" : "send"
                       size: 18; color: app.accent }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 1
                    Label { text: app.pendingShareText.length > 0
                                  ? qsTr("Texto para enviar")
                                  : (app.pendingShare.length === 1
                                     ? qsTr("1 archivo para enviar")
                                     : qsTr("%1 archivos para enviar").arg(app.pendingShare.length))
                            color: app.textColor; font.pixelSize: 13; font.bold: true
                            elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: qsTr("Pulsa \"Compartir por web\" para añadir")
                            color: app.subtextColor; font.pixelSize: 11
                            elide: Text.ElideRight; Layout.fillWidth: true }
                }
                ToolButton { implicitWidth: 32; implicitHeight: 32
                             onClicked: { app.pendingShare = []; app.pendingShareText = "" }
                             contentItem: Icon { name: "close"; size: 15; color: app.subtextColor } }
            }
        }

        // ====== 1. INTERRUPTOR PRINCIPAL ======
        Rectangle {
            Layout.fillWidth: true
            radius: 14; color: app.cardColor
            Layout.preferredHeight: 52
            Switch {
                id: webSw
                width: 60
                padding: 0
                anchors.right: parent.right; anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                activeFocusOnTab: true
                scale: 0.9; transformOrigin: Item.Right
                checked: transfer.webEnabled
                onToggled: transfer.webEnabled = checked
            }
            // Icono
            Rectangle {
                x: 14; width: 36; height: 36; radius: 12
                anchors.verticalCenter: parent.verticalCenter
                color: transfer.webEnabled ? app.accent : app.accentA(0.15)
                Icon { anchors.centerIn: parent; name: "globe"; size: 18
                       color: transfer.webEnabled ? "white" : app.accent }
            }
            // Texto (alineado absolutamente en la misma columna que el resto)
            Column {
                anchors.left: parent.left; anchors.leftMargin: 62
                anchors.right: parent.right; anchors.rightMargin: 72
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Label { text: qsTr("Activar modo web"); color: app.textColor
                        font.pixelSize: 14; font.bold: true
                        elide: Text.ElideRight; width: parent.width }
                Label { text: transfer.webEnabled ? qsTr("Activo") : qsTr("Desactivado")
                        color: transfer.webEnabled ? app.accent : app.subtextColor
                        font.pixelSize: 11 }
            }
        }

        // ====== 1b. Selector HTTP / HTTPS (dentro del Modo Web) ======
        Rectangle {
            Layout.fillWidth: true
            radius: 14; color: app.cardColor
            Layout.preferredHeight: 52
            enabled: transfer.webEnabled
            opacity: transfer.webEnabled ? 1 : 0.45
            Switch {
                id: tlsSwitch
                padding: 0
                anchors.right: parent.right; anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                activeFocusOnTab: transfer.webEnabled
                scale: 0.9; transformOrigin: Item.Right
                checked: transfer.webTls
                onToggled: transfer.webTls = checked
            }
            // Icono
            Rectangle {
                x: 14; width: 36; height: 36; radius: 12
                anchors.verticalCenter: parent.verticalCenter
                color: transfer.webTls ? app.accent : app.accentA(0.15)
                Icon { anchors.centerIn: parent; name: transfer.webTls ? "lock" : "unlock"; size: 18
                       color: transfer.webTls ? "white" : app.accent }
            }
            // Texto
            Column {
                anchors.left: parent.left; anchors.leftMargin: 62
                anchors.right: parent.right; anchors.rightMargin: 72
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Label { text: qsTr("Cifrado HTTPS"); color: app.textColor
                        font.pixelSize: 14; font.bold: true
                        elide: Text.ElideRight; width: parent.width }
                Label { text: transfer.webTls ? qsTr("HTTPS · canal encriptado")
                                              : qsTr("HTTP · sin encriptar")
                        color: transfer.webTls ? app.accent : app.subtextColor
                        font.pixelSize: 11 }
            }
        }

        // Motivo del fallo (p.ej. puerto ocupado), en vez de apagarse sin más.
        Rectangle {
            Layout.fillWidth: true
            visible: transfer.webError.length > 0
            radius: 10; color: Qt.rgba(0.85, 0.25, 0.2, 0.12)
            border.width: 1; border.color: Qt.rgba(0.85, 0.25, 0.2, 0.5)
            Layout.preferredHeight: errLbl.implicitHeight + 12
            Label {
                id: errLbl
                anchors.centerIn: parent; width: parent.width - 20
                text: transfer.webError
                color: app.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap
            }
        }

        // ====== 2. QR + URL (Vertical) ======
        Rectangle {
            Layout.fillWidth: true
            visible: transfer.webEnabled && transfer.webUrl.length > 0
            radius: 14; color: app.cardColor
            Layout.preferredHeight: qrCol.implicitHeight + 16

            ColumnLayout {
                id: qrCol
                anchors.centerIn: parent
                width: parent.width - 32
                spacing: 2

                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 148; Layout.preferredHeight: 148
                    radius: 12; color: "white"
                    Image {
                        anchors.centerIn: parent
                        width: 138; height: 138
                        smooth: false
                        source: transfer.webEnabled ? transfer.qrSvgUri(transfer.webUrl) : ""
                        sourceSize.width: 300; sourceSize.height: 300
                    }
                }

                Label {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.fillWidth: true
                    text: transfer.webUrl
                    color: app.textColor; font.pixelSize: 14; font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    fontSizeMode: Text.Fit
                    minimumPixelSize: 10
                    elide: Text.ElideMiddle
                }

                ItemDelegate {
                    Layout.alignment: Qt.AlignHCenter
                    visible: transfer.webAddresses.length > 1
                    implicitHeight: 28
                    activeFocusOnTab: visible
                    background: null
                    onClicked: transfer.webAddressIndex =
                               (transfer.webAddressIndex + 1) % transfer.webAddresses.length
                    contentItem: RowLayout {
                        spacing: 6
                        Icon { name: "reset"; size: 12; color: app.accent }
                        Label {
                            text: qsTr("Otra dirección (%1)").arg(transfer.webAddresses.length)
                            color: app.accent; font.pixelSize: 11
                        }
                    }
                }
            }
        }

        // ====== 3. Compartir por web ======
        ItemDelegate {
            activeFocusOnTab: true
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            enabled: transfer.webEnabled
            opacity: transfer.webEnabled ? 1 : 0.45
            onClicked: {
                if (app.hasPendingShare) { wp.consumePendingShare(); return }
                app.pushPage(webShareComp)
            }
            background: Rectangle {
                radius: 14
                color: wp.dropHover ? app.accentA(0.25) : (parent.hovered ? app.cardHover : app.cardColor)
                border.width: wp.dropHover ? 2 : 0
                border.color: app.accent
            }
            contentItem: RowLayout {
                spacing: 12
                Item {
                    width: 36; height: 36
                    Icon { anchors.centerIn: parent; name: transfer.webSharedCount > 0 || transfer.webText.length > 0
                                 ? "share" : "inbox"
                           size: 16; color: app.accent }
                }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 0
                    Label { text: qsTr("Compartir por web")
                            color: app.textColor
                            font.pixelSize: 13; font.bold: true
                            elide: Text.ElideRight; Layout.fillWidth: true }
                    Label {
                        text: {
                            var n = transfer.webSharedCount
                            var t = transfer.webText.length > 0
                            if (n === 0 && !t) return qsTr("Nada compartido")
                            var partes = []
                            if (n === 1) partes.push(qsTr("1 elemento"))
                            else if (n > 1) partes.push(qsTr("%1 elementos").arg(n))
                            if (t) partes.push(qsTr("texto"))
                            return partes.join(" · ")
                        }
                        color: transfer.webSharedCount > 0 || transfer.webText.length > 0
                               ? app.accent : app.subtextColor
                        font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true
                    }
                }
                ToolButton {
                    visible: transfer.webSharedCount > 0 || transfer.webText.length > 0
                    implicitWidth: 28; implicitHeight: 28
                    onClicked: transfer.clearWebShared()
                    contentItem: Icon { name: "close"; size: 14; color: app.subtextColor }
                }
                Icon { name: "back"; size: 14; color: app.subtextColor; rotation: 180 }
            }
        }

        // ====== 4. Seguridad (PIN) — deshabilitado cuando web está off ======
        Rectangle {
            Layout.fillWidth: true
            radius: 14; color: app.cardColor
            Layout.preferredHeight: 50
            enabled: transfer.webEnabled
            opacity: transfer.webEnabled ? 1 : 0.45
            Switch {
                id: pinSwitch
                padding: 0
                anchors.right: parent.right; anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter
                activeFocusOnTab: transfer.webEnabled
                scale: 0.90; transformOrigin: Item.Right
                checked: transfer.webPinEnabled
                onClicked: {
                    checked = Qt.binding(function() { return transfer.webPinEnabled })
                    if (transfer.webPinEnabled) {
                        transfer.setWebPinEnabled(false)
                    } else {
                        if (!transfer.setWebPinEnabled(true))
                            pinDialog.open()
                    }
                }
            }
            MouseArea {
                anchors.left: parent.left; anchors.right: pinSwitch.left
                anchors.top: parent.top; anchors.bottom: parent.bottom
                enabled: transfer.webPinEnabled
                onClicked: pinDialog.open()
            }
            // Icono
            Rectangle {
                x: 14; width: 36; height: 36; radius: 12
                anchors.verticalCenter: parent.verticalCenter
                color: transfer.webPinEnabled ? app.accent : app.accentA(0.15)
                Icon { anchors.centerIn: parent; name: "shield"; size: 16
                       color: transfer.webPinEnabled ? "white" : app.accent }
            }
            // Texto
            Column {
                anchors.left: parent.left; anchors.leftMargin: 62
                anchors.right: parent.right; anchors.rightMargin: 72
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1
                Label { text: qsTr("Proteger con PIN"); color: app.textColor
                        font.pixelSize: 13; font.bold: true
                        elide: Text.ElideRight; width: parent.width }
                Label { text: {
                            if (!transfer.webEnabled) return qsTr("Activa antes el modo web")
                            if (!transfer.webPinEnabled && transfer.webPin.length > 0) return qsTr("PIN guardado")
                            if (transfer.webPinEnabled) return qsTr("PIN: %1 · toca para cambiar").arg(transfer.webPin)
                            return qsTr("Sin PIN")
                        }
                        color: transfer.webPinEnabled ? app.accent : app.subtextColor
                        font.pixelSize: 10; elide: Text.ElideRight; width: parent.width }
            }
        }

        // Relleno flexible para que todo lo anterior se agrupe arriba sin estirarse.
        Item { Layout.fillWidth: true; Layout.fillHeight: true }
    }
}
}
