import QtQuick
import QtQuick.Effects

// Icono vectorial teñido con `color` (por defecto). Si `tinted` es false
// se muestra el SVG con sus colores originales (útil para banderas, logos…).
// Sustituye a los emoji para que se vean siempre, en cualquier plataforma.
Item {
    id: root
    property string name
    property color color: "#ffffff"
    property int size: 20
    property bool tinted: true
    property string ext: "svg"

    implicitWidth: size
    implicitHeight: size

    Image {
        id: img
        anchors.fill: parent
        source: root.name ? Qt.resolvedUrl("icons/" + root.name + "." + root.ext) : ""
        sourceSize.width: root.size * 2
        sourceSize.height: root.size * 2
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: !root.tinted
    }
    MultiEffect {
        anchors.fill: parent
        source: img
        colorization: root.tinted ? 1.0 : 0.0
        colorizationColor: root.color
        visible: root.tinted
    }
}
