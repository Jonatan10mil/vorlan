import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Ver un mensaje de texto recibido.
Item {
    required property var app
    property string content: ""
    property string pageTitle: qsTr("Texto")
    // Quién lo envió (se muestra bajo el título).
    property string sender: ""
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 16; spacing: 12

        // De quién viene el texto.
        RowLayout {
            Layout.fillWidth: true; spacing: 8
            visible: sender.length > 0
            Rectangle {
                width: 26; height: 26; radius: 13; color: app.accentA(0.16)
                Icon { anchors.centerIn: parent; name: "message"; size: 13; color: app.accent }
            }
            Label {
                text: qsTr("De %1").arg(sender)
                color: app.textColor; font.pixelSize: 13; font.bold: true
                elide: Text.ElideRight; Layout.fillWidth: true
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 10; color: app.inputBg; border.width: 1; border.color: app.divider
            Flickable {
                anchors.fill: parent; anchors.margins: 10; clip: true
                contentWidth: width; contentHeight: Math.max(height, viewerArea.implicitHeight)
                TextArea {
                    id: viewerArea; width: parent.width
                    readOnly: true; wrapMode: TextArea.Wrap; background: null
                    text: content
                    color: app.textColor; font.pixelSize: 15
                }
            }
        }
        Button { Material.roundedScale: Material.ExtraSmallScale
                 Layout.alignment: Qt.AlignRight; text: qsTr("Copiar"); flat: true
                 icon.source: "icons/clipboard.svg"; icon.width: 16; icon.height: 16; icon.color: app.accent
                 onClicked: { viewerArea.selectAll(); viewerArea.copy(); viewerArea.deselect() } }
    }
}
