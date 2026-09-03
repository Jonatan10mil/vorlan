import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Flickable {
    required property var app
    property string pageTitle: qsTr("Donar")
    contentHeight: donateCol.implicitHeight + 36
    clip: true

    function openUrl(url) { Qt.openUrlExternally(url) }

    ColumnLayout {
        id: donateCol
        x: 18; width: parent.width - 36; y: 18
        spacing: 0

        // Header
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter; spacing: 6
            Layout.bottomMargin: 8
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 44; height: 44; radius: 12
                color: app.accentA(0.15)
                Icon { anchors.centerIn: parent; name: "heart"; size: 24; color: app.accent }
            }
            Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("Apoya a VorLAN")
                    color: app.textColor; font.family: app.titleFont
                    font.pixelSize: 14; font.bold: true }
            Label { Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Si te gusta VorLAN, considera apoyar el desarrollo.\nCada donación ayuda a mantener el proyecto activo.")
                    color: app.subtextColor; font.pixelSize: 12
                    wrapMode: Text.Wrap; horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true }
        }

        // GIF animado
        AnimatedImage {
            Layout.alignment: Qt.AlignHCenter
            source: Qt.resolvedUrl("money.gif")
            Layout.preferredWidth: 120
            Layout.preferredHeight: 90
            fillMode: Image.PreserveAspectFit
        }

        // Binance card
        Rectangle {
            Layout.fillWidth: true
            radius: 14; color: app.cardColor
            Layout.preferredHeight: 56
            Rectangle {
                width: 40; height: 40; radius: 12
                color: Qt.rgba(app.accent.r, app.accent.g, app.accent.b, 0.15)
                anchors.left: parent.left; anchors.leftMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                Icon { anchors.centerIn: parent; name: "usdt"; size: 22; color: app.accent }
            }
            Column {
                anchors.left: parent.left; anchors.leftMargin: 68
                anchors.right: parent.right; anchors.rightMargin: 14
                anchors.verticalCenter: parent.verticalCenter; spacing: 1
                Label { text: "Binance"; color: app.textColor; font.pixelSize: 14; font.bold: true
                        elide: Text.ElideRight; width: parent.width }
                Label { text: qsTr("USDT (TRC-20) / Binance Pay"); color: app.subtextColor; font.pixelSize: 11
                        elide: Text.ElideRight; width: parent.width }
            }
            Icon { name: "link"; size: 14; color: app.subtextColor
                   anchors.right: parent.right; anchors.rightMargin: 14
                   anchors.verticalCenter: parent.verticalCenter }
            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: openUrl("https://www.binance.com/activity/referral-entry/CPA?ref=CPA_00RDQVSSE2") }
        }

        // TRC-20 address card
        Rectangle {
            Layout.fillWidth: true; radius: 14; color: app.cardColor
            Layout.preferredHeight: cryptoCol.implicitHeight + 20; Layout.topMargin: 12
            ColumnLayout {
                id: cryptoCol; anchors.centerIn: parent
                width: parent.width - 24; spacing: 6
                Label { text: qsTr("Dirección Binance / USDT (TRC-20)"); color: app.subtextColor; font.pixelSize: 12 }
                RowLayout {
                    spacing: 4
                    Label {
                        id: addrLabel
                        text: "THR2Rm7Nv7HFVaLjV8KrFpDzJUubpAVm1K"
                        color: app.textColor; font.pixelSize: 12
                        font.family: "monospace"
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        implicitWidth: 36; implicitHeight: 36
                        onClicked: transfer.setClipboardText(addrLabel.text)
                        contentItem: Icon { name: "clipboard"; size: 16; color: app.accent }
                    }
                }
            }
        }

        // Binance Pay ID card
        Rectangle {
            Layout.fillWidth: true; radius: 14; color: app.cardColor
            Layout.preferredHeight: payCol.implicitHeight + 20; Layout.topMargin: 12
            ColumnLayout {
                id: payCol; anchors.centerIn: parent
                width: parent.width - 24; spacing: 6
                Label { text: qsTr("Binance Pay ID (sin comisiones)"); color: app.subtextColor; font.pixelSize: 12 }
                RowLayout {
                    spacing: 4
                    Label {
                        id: payLabel
                        text: "514440493"
                        color: app.textColor; font.pixelSize: 14
                        font.family: "monospace"; font.bold: true
                        Layout.fillWidth: true
                    }
                    ToolButton {
                        implicitWidth: 36; implicitHeight: 36
                        onClicked: transfer.setClipboardText(payLabel.text)
                        contentItem: Icon { name: "clipboard"; size: 16; color: app.accent }
                    }
                }
            }
        }
    }
}
