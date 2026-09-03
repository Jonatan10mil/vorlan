import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// ComboBox con colores del tema (buen contraste en claro/oscuro) y soporte de
// icono opcional por opción (campo `iconRole` en el modelo; p.ej. "moon").
// Si una opción tiene `tintedRole: false` (campo `tinted` por defecto) el
// icono se dibuja con sus colores originales (banderas, logos…); en caso
// contrario se tiñe con `textColor` (o `accent` si está seleccionado).
ComboBox {
    id: control
    required property var app
    property string iconRole: "icon"
    property string tintedRole: "tinted"
    property string extRole: "ext"

    activeFocusOnTab: true
    Material.roundedScale: Material.ExtraSmallScale
    font.pixelSize: 14
    topInset: 0
    bottomInset: 0

    function iconOf(i) {
        if (i < 0 || !control.model || control.model[i] === undefined)
            return ""
        var m = control.model[i]
        return (m && m[control.iconRole] !== undefined) ? m[control.iconRole] : ""
    }
    function iconTintedOf(i) {
        if (i < 0 || !control.model || control.model[i] === undefined)
            return true
        var m = control.model[i]
        if (m && m[control.tintedRole] !== undefined)
            return m[control.tintedRole]
        return true
    }
    function iconExtOf(i) {
        if (i < 0 || !control.model || control.model[i] === undefined)
            return "svg"
        var m = control.model[i]
        if (m && m[control.extRole] !== undefined)
            return m[control.extRole]
        return "svg"
    }
    readonly property string currentIcon: iconOf(currentIndex)
    readonly property bool currentIconTinted: iconTintedOf(currentIndex)
    readonly property string currentIconExt: iconExtOf(currentIndex)

    // Navegación con mando: con el desplegable cerrado, las flechas mueven el foco
    // (no cambian el valor) y OK/Enter lo abre.
    Keys.onPressed: (e) => {
        if (control.popup.visible)
            return
        if (e.key === Qt.Key_Up || e.key === Qt.Key_Down
            || e.key === Qt.Key_Left || e.key === Qt.Key_Right) {
            if (control.app && control.app.handleNavKey)
                control.app.handleNavKey(e)
        } else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter
                   || e.key === Qt.Key_Select || e.key === Qt.Key_Space) {
            control.popup.open()
            e.accepted = true
        }
    }

    // Casilla (selección actual): icono opcional + texto.
    contentItem: RowLayout {
        spacing: 6
        Icon {
            visible: control.currentIcon !== ""
            Layout.leftMargin: 12
            Layout.preferredWidth: visible ? 16 : 0
            name: control.currentIcon
            size: 16
            tinted: control.currentIconTinted
            ext: control.currentIconExt
            color: control.app.textColor
        }
        Text {
            Layout.fillWidth: true
            Layout.leftMargin: control.currentIcon !== "" ? 0 : 12
            Layout.rightMargin: 24
            text: control.displayText
            color: control.app.textColor
            font: control.font
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }

    // Cada opción del desplegable: icono opcional + texto.
    delegate: ItemDelegate {
        id: deleg
        width: ListView.view ? ListView.view.width : control.width
        required property var modelData
        required property int index
        highlighted: control.highlightedIndex === index
        readonly property string dIcon:
            (deleg.modelData && deleg.modelData[control.iconRole] !== undefined)
            ? deleg.modelData[control.iconRole] : ""
        readonly property bool dIconTinted:
            (deleg.modelData && deleg.modelData[control.tintedRole] !== undefined)
            ? deleg.modelData[control.tintedRole] : true
        readonly property string dIconExt:
            (deleg.modelData && deleg.modelData[control.extRole] !== undefined)
            ? deleg.modelData[control.extRole] : "svg"
        contentItem: RowLayout {
            spacing: 6
            Icon {
                visible: deleg.dIcon !== ""
                Layout.preferredWidth: visible ? 16 : 0
                name: deleg.dIcon
                size: 16
                tinted: deleg.dIconTinted
                ext: deleg.dIconExt
                color: control.currentIndex === deleg.index ? control.app.accent
                                                            : control.app.textColor
            }
            Text {
                Layout.fillWidth: true
                text: control.textRole
                      ? (deleg.modelData[control.textRole] !== undefined ? deleg.modelData[control.textRole] : deleg.modelData)
                      : deleg.modelData
                color: control.currentIndex === deleg.index ? control.app.accent
                                                            : control.app.textColor
                font.pixelSize: 14
                font.bold: control.currentIndex === deleg.index
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
        }
        background: Rectangle {
            color: deleg.highlighted ? control.app.accentA(0.14) : "transparent"
        }
    }

    popup: Popup {
        y: control.height + 2
        width: control.width
        padding: 4
        implicitHeight: Math.min(contentItem.implicitHeight + 8, 320)
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator { }
        }
        background: Rectangle {
            color: control.app.cardColor
            border.color: control.app.divider
            border.width: 1
            radius: 8
        }
    }
}
