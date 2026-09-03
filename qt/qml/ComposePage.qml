import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Escribir/pegar texto para enviarlo al destino elegido.
Item {
    required property var app
    // true = el texto se COMPARTE por el modo web en vez de enviarse a un equipo.
    property bool forWeb: false
    property string pageTitle: forWeb ? qsTr("Compartir texto") : qsTr("Enviar texto")
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 12
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 10; color: app.inputBg; border.width: 1; border.color: app.divider
            // El TextArea llena y desplaza todo el recuadro → se puede tocar
            // en cualquier parte para escribir (antes solo enfocaba arriba).
            Flickable {
                anchors.fill: parent; anchors.margins: 10; clip: true
                TextArea.flickable: TextArea {
                    id: composeArea
                    focus: true
                    activeFocusOnTab: true
                    wrapMode: TextArea.Wrap; background: null
                    placeholderText: qsTr("Escribe o pega texto…")
                    color: app.textColor; font.pixelSize: 15
                }
                ScrollBar.vertical: ScrollBar { }
            }
        }
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            Button { Material.roundedScale: Material.ExtraSmallScale; activeFocusOnTab: true; text: qsTr("Pegar"); flat: true
                     icon.source: "icons/clipboard.svg"; icon.width: 16; icon.height: 16; icon.color: app.accent
                     onClicked: composeArea.paste() }
            Item { Layout.fillWidth: true }
            Button { Material.roundedScale: Material.ExtraSmallScale; activeFocusOnTab: true
                     text: forWeb ? qsTr("Compartir") : qsTr("Enviar"); highlighted: true
                     icon.source: forWeb ? "icons/globe.svg" : "icons/send.svg"
                     icon.width: 16; icon.height: 16; icon.color: "white"
                     enabled: composeArea.text.length > 0
                     onClicked: {
                         if (forWeb) { transfer.setWebText(composeArea.text); app.popPage() }
                         else { transfer.enqueueText(app.pendingHost, app.pendingPort,
                                    app.pendingName, app.pendingPlatform, composeArea.text)
                                app.goHome() }
                     } }
        }
    }
    // Enfocar y abrir el teclado al entrar → se puede escribir sin tocar el área.
    Component.onCompleted: { if (forWeb) composeArea.text = transfer.webText; focusTimer.start() }
    Timer {
        id: focusTimer; interval: 120
        onTriggered: { composeArea.forceActiveFocus(); Qt.inputMethod.show() }
    }
}
