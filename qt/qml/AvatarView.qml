import QtQuick
import QtQuick.Effects

// Avatar del usuario: círculo con gradiente rotatorio, borde pulsante,
// glow de actividad y anillo de progreso. Se VOLTEA entre la imagen
// del usuario y el icono del SO.
Item {
    id: root
    property int size: 40
    property string imageUrl: ""
    property color accent: "#57a63a"
    property string platform: ""
    property string deviceType: ""
    // Progreso de transferencia (0–1). -1 = sin anillo.
    property real progress: -1
    // Activa el glow de actividad.
    property bool active: false
    width: size
    height: size

    function osIconName() {
        var os = platform !== "" ? platform : Qt.platform.os
        switch (os) {
        case "windows": return "windows";
        case "osx":
        case "macos":   return "apple";
        case "ios":     return "apple";
        case "android": return "android";
        default:        return "linux";
        }
    }

    function typeIconName() {
        switch (deviceType) {
        case "tv":      return "tv";
        case "laptop":  return "laptop";
        case "phone":
        case "tablet":  return "smartphone";
        case "desktop": return "monitor";
        default:        return "";
        }
    }
    readonly property bool hasType: typeIconName() !== ""

    property int backFace: 0
    property bool vertical: false
    readonly property int axX: vertical ? 1 : 0
    readonly property int axY: vertical ? 0 : 1

    readonly property bool hasImage: imageUrl !== ""
    property real flip: (hasImage || hasType) ? 0 : 180
    Behavior on flip {
        enabled: root.hasImage || root.hasType
        NumberAnimation { duration: 750; easing.type: Easing.InOutQuad }
    }
    function nextInterval() { return 2500 + Math.round(Math.random() * 6000) }
    Timer {
        id: flipTimer
        running: (root.hasImage || root.hasType) && root.visible
        repeat: true
        interval: root.nextInterval()
        onTriggered: {
            if (root.flip < 90) {
                if (root.hasType)
                    root.backFace = (root.backFace + 1) % 2
                root.flip = 180
            } else {
                root.flip = 0
            }
            interval = root.nextInterval()
        }
    }

    // ====== GLOW: brillo detrás del avatar ======
    Rectangle {
        anchors.centerIn: parent
        width: root.size + 24; height: width; radius: width / 2
        color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b,
                       0.15 + glowPulse.value * 0.25)
        SequentialAnimation on opacity {
            running: root.active && root.visible
            loops: Animation.Infinite
            NumberAnimation { from: 0.3; to: 1.0; duration: 1200; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.0; to: 0.3; duration: 1200; easing.type: Easing.InOutSine }
        }
    }

    NumberAnimation {
        id: glowPulse
        target: glowPulse; property: "value"
        from: 0; to: 1; duration: 1200; easing.type: Easing.InOutSine
        loops: Animation.Infinite
        running: root.active && root.visible
    }

    // ====== BORDE PULSANTE: anillo animado ======
    Rectangle {
        id: pulseBorder
        anchors.centerIn: parent
        width: root.size + 8; height: width; radius: width / 2
        color: "transparent"
        border.width: 3
        border.color: Qt.rgba(root.accent.r, root.accent.g, root.accent.b,
                              0.4 + borderPulse.value * 0.6)
        SequentialAnimation on scale {
            running: root.visible
            loops: Animation.Infinite
            NumberAnimation { from: 1.0; to: 1.10; duration: 1600; easing.type: Easing.InOutSine }
            NumberAnimation { from: 1.10; to: 1.0; duration: 1600; easing.type: Easing.InOutSine }
        }
    }

    NumberAnimation {
        id: borderPulse
        target: borderPulse; property: "value"
        from: 0; to: 1; duration: 1600; easing.type: Easing.InOutSine
        loops: Animation.Infinite
        running: root.visible
    }

    // ====== GRADIENTE ROTATORIO: segmentos animados vía Canvas ======
    Canvas {
        id: gradCanvas
        anchors.centerIn: parent
        width: root.size + 4; height: width
        property real angle: 0
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var cx = width / 2, cy = height / 2
            var r = width / 2
            var segments = 18
            var step = (Math.PI * 2) / segments
            for (var i = 0; i < segments; i++) {
                var a = angle * Math.PI / 180 + i * step
                var t = i / segments
                // Alternar claros y oscuros con más contraste
                var l = 0.6 + Math.sin(t * Math.PI * 2) * 0.6
                var c = Qt.lighter(root.accent, l)
                ctx.beginPath()
                ctx.moveTo(cx, cy)
                ctx.arc(cx, cy, r, a, a + step + 0.03)
                ctx.closePath()
                ctx.fillStyle = c
                ctx.fill()
            }
        }
        NumberAnimation on angle {
            from: 0; to: 360; duration: 8000; loops: Animation.Infinite
            running: root.visible
        }
        Connections {
            target: gradCanvas
            function onAngleChanged() { gradCanvas.requestPaint() }
        }
    }

    // ====== ANILLO DE PROGRESO ======
    Canvas {
        id: progressArc
        anchors.centerIn: parent
        width: root.size + 8; height: width
        visible: root.progress >= 0
        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()
            var cx = width / 2, cy = height / 2
            var r = (root.size + 4) / 2
            var lw = 3
            // Fondo
            ctx.beginPath()
            ctx.arc(cx, cy, r, 0, Math.PI * 2)
            ctx.strokeStyle = Qt.rgba(root.accent.r, root.accent.g, root.accent.b, 0.15)
            ctx.lineWidth = lw
            ctx.lineCap = "round"
            ctx.stroke()
            // Progreso
            if (root.progress > 0) {
                var startAngle = -Math.PI / 2
                var endAngle = startAngle + (Math.PI * 2 * Math.min(root.progress, 1.0))
                ctx.beginPath()
                ctx.arc(cx, cy, r, startAngle, endAngle)
                ctx.strokeStyle = root.accent
                ctx.lineWidth = lw
                ctx.lineCap = "round"
                ctx.stroke()
            }
        }
        Connections {
            target: root
            function onProgressChanged() { progressArc.requestPaint() }
        }
    }

    // ====== CONTENIDO PRINCIPAL: avatar con flip ======
    Item {
        anchors.centerIn: parent
        width: root.size; height: root.size
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
        }
        transform: Rotation {
            origin.x: root.size / 2; origin.y: root.size / 2
            axis { x: root.axX; y: root.axY; z: 0 }
            angle: root.flip
        }

        // Cara frontal: imagen del usuario
        Item {
            anchors.fill: parent
            visible: root.flip <= 90
            Rectangle {
                anchors.fill: parent
                radius: root.size / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.lighter(root.accent, 1.25) }
                    GradientStop { position: 1; color: Qt.darker(root.accent, 1.35) }
                }
            }
            Image {
                anchors.fill: parent
                visible: root.hasImage
                source: root.imageUrl
                fillMode: Image.PreserveAspectCrop
                cache: false; asynchronous: true
            }
            Icon {
                anchors.centerIn: parent
                visible: !root.hasImage && root.hasType
                name: root.typeIconName(); color: "white"
                size: Math.round(root.size * 0.5)
            }
        }

        // Cara trasera: icono del SO
        Item {
            anchors.fill: parent
            visible: root.flip > 90
            transform: Rotation {
                origin.x: root.size / 2; origin.y: root.size / 2
                axis { x: root.axX; y: root.axY; z: 0 }
                angle: 180
            }
            Rectangle {
                anchors.fill: parent
                radius: root.size / 2
                gradient: Gradient {
                    GradientStop { position: 0; color: Qt.lighter(root.accent, 1.25) }
                    GradientStop { position: 1; color: Qt.darker(root.accent, 1.35) }
                }
            }
            Icon {
                anchors.centerIn: parent
                name: (root.hasImage && root.hasType && root.backFace === 1)
                      ? root.typeIconName() : root.osIconName()
                color: "white"
                size: Math.round(root.size * 0.5)
            }
        }
    }

    // Máscara circular
    Rectangle {
        id: mask
        anchors.centerIn: parent
        width: root.size; height: root.size
        radius: root.size / 2
        visible: false
        layer.enabled: true
        antialiasing: true
    }
}
