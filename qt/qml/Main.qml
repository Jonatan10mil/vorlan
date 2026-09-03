import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Effects

ApplicationWindow {
    id: app
    minimumWidth: 350
    minimumHeight: 520
    width: minimumWidth
    height: minimumHeight
    visible: false
    title: "VorLAN"
    font.pixelSize: 12   // controles (botones/inputs) más compactos

    // Botones e inputs más bajos que el default de Material.
    property int ctrlPad: 6

    // Fuente decorativa para encabezados/títulos (cae a la del sistema si falla).
    FontLoader { id: titleFontLoader; source: "fonts/ethnocen.ttf" }
    readonly property string titleFont: titleFontLoader.status === FontLoader.Ready
                                        ? titleFontLoader.name : app.font.family

    // ¿La app está en primer plano y activa? Al minimizar (Inactive/Suspended) hay
    // que PARAR las animaciones en bucle: seguir dibujando tras suspenderse cuelga
    // la app en Android (background_running=true). Ver el radar "Buscando…".
    readonly property bool appActive: Qt.application.state === Qt.ApplicationActive

    // ---- Tema y colores ----
    readonly property bool darkMode: transfer.themeMode === "light" ? false
                                   : transfer.themeMode === "dark" ? true
                                   : transfer.systemDark   // "system": seguir al SO
    readonly property color accent: transfer.accentColor
    readonly property color bgColor: darkMode ? "#15171a" : "#eceef1"
    readonly property color surface: darkMode ? "#1b1e20" : "#ffffff"
    readonly property color cardColor: darkMode ? "#22262a" : "#ffffff"
    readonly property color cardHover: darkMode ? "#2a2e33" : "#e4e7ea"
    readonly property color textColor: darkMode ? "#e6e8ea" : "#1a1c1e"
    readonly property color subtextColor: darkMode ? "#9aa0a6" : "#5f6368"
    readonly property color inputBg: darkMode ? "#15171a" : "#f2f3f5"
    readonly property color divider: darkMode ? "#2f3438" : "#d3d6d9"
    function accentA(a) { return Qt.rgba(accent.r, accent.g, accent.b, a) }

    // ---- Navegación con mando / teclado (Android TV y accesibilidad) ----
    // Las flechas del D-pad (mismos eventos que el teclado) mueven el foco por la
    // cadena de foco; OK/Enter activa el control enfocado (lo hacen los propios
    // controles). El anillo de foco solo se muestra al navegar por teclas
    // (keyboardNav), no con toque/ratón.
    property bool keyboardNav: false
    function handleNavKey(e) {
        if (!transfer.isTv)   // navegación por foco/anillo solo en Android TV
            return
        var forward = (e.key === Qt.Key_Down || e.key === Qt.Key_Right || e.key === Qt.Key_Tab)
        var backward = (e.key === Qt.Key_Up || e.key === Qt.Key_Left || e.key === Qt.Key_Backtab)
        if (!forward && !backward)
            return
        app.keyboardNav = true
        var cur = app.activeFocusItem
        // Si aún no hay un control enfocado, enfocar el primero de la cadena.
        if (!cur || !cur.activeFocusOnTab) {
            var f = app.contentItem.nextItemInFocusChain(true)
            if (f) f.forceActiveFocus(Qt.TabFocusReason)
            e.accepted = true
            return
        }
        var t = cur.nextItemInFocusChain(forward)
        if (t) t.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
        e.accepted = true
    }

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: accent
    Material.primary: accent
    Material.background: bgColor
    color: bgColor

    // Iconos de la barra de estado del sistema según el tema de la app.
    onDarkModeChanged: transfer.applyStatusBar(darkMode)
    Component.onCompleted: {
        transfer.applyStatusBar(darkMode); checkShared()
        maybePromptFolder()
    }
    // El modal de carpeta es para RECIBIR. No lo mostramos si venimos de "Compartir"
    // (eso es enviar y no necesita carpeta), para no bloquear el compartir.
    function maybePromptFolder() {
        if (transfer.shouldPromptFolder() && !app.hasPendingShare)
            folderSetupTimer.start()
    }
    // Pequeño retardo para que la ventana esté lista antes de mostrar el diálogo.
    Timer { id: folderSetupTimer; interval: 600
            onTriggered: if (!app.hasPendingShare) folderSetupDialog.open() }

    // Selector nativo de carpeta (escritorio). En Android se usa el de SAF a
    // través de transfer.pickAndroidFolder().
    FolderDialog {
        id: folderSetupPicker
        title: qsTr("Elegir dónde guardar")
        currentFolder: transfer.downloadsFolder ? "file://" + (transfer.downloadsFolder.startsWith("/") ? "" : "/") + transfer.downloadsFolder : ""
        onAccepted: transfer.setDownloadDir(selectedFolder)
    }

    // Elegir carpeta de recibidos. Obligatorio la primera vez (Android y
    // escritorio): no se puede cerrar ni avanzar hasta seleccionar una carpeta.
    Dialog {
        id: folderSetupDialog
        modal: true; anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 64 : 320, 340)
        padding: 24
        closePolicy: Popup.NoAutoClose               // ni atrás ni tocar fuera cierran
        Material.background: app.cardColor
        Material.roundedScale: Material.LargeScale
        Overlay.modal: Rectangle { color: "#b3000000" }
        onOpened: chooseFolderBtn.forceActiveFocus()
        contentItem: ColumnLayout {
            spacing: 14
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 66; height: 66; radius: 33; color: app.accentA(0.15)
                Icon { anchors.centerIn: parent; name: "folder"; size: 30; color: app.accent }
            }
            // OJO: sin fillWidth + wrap/ajuste, el ancho implícito de este título
            // (la fuente Ethnocentric mide bastante más en Windows que en Linux)
            // pasa a ser el ancho MÍNIMO del ColumnLayout, el layout no puede
            // encoger hasta availableWidth y el botón de abajo (fillWidth) se
            // estira más que la tarjeta. Con fillWidth + HorizontalFit el título
            // se adapta al ancho disponible en vez de imponerlo.
            Label {
                text: qsTr("Elige una carpeta")
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                fontSizeMode: Text.HorizontalFit; minimumPixelSize: 13
                color: app.textColor; font.family: app.titleFont
                font.pixelSize: 19; font.bold: true
            }
            Label {
                text: qsTr("Dónde se guardará lo que recibas.")
                Layout.fillWidth: true; horizontalAlignment: Text.AlignHCenter
                color: app.subtextColor; font.pixelSize: 13; wrapMode: Text.WordWrap
            }
            Button {
                id: chooseFolderBtn
                Layout.fillWidth: true; Layout.topMargin: 8
                Material.roundedScale: Material.SmallScale
                highlighted: true; text: qsTr("Elegir carpeta")
                icon.source: "icons/folder.svg"; icon.width: 18; icon.height: 18; icon.color: "white"
                // Enfocable con mando/teclado (en TV no hay toque) y activable con OK.
                activeFocusOnTab: true
                focus: true
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                onClicked: Qt.platform.os === "android" ? transfer.pickAndroidFolder()
                                                       : folderSetupPicker.open()
            }
        }
        // Al elegir carpeta (downloadDir pasa a content://) → cerrar.
        Connections {
            target: transfer
            function onDownloadDirChanged() {
                if (!transfer.shouldPromptFolder()) folderSetupDialog.close()
            }
        }
    }

    // Enviar por IP: escribe la dirección del equipo destino (conexión TCP directa).
    Dialog {
        id: ipDialog
        parent: Overlay.overlay
        modal: true; anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 64 : 320, 360)
        padding: 22
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Material.background: app.cardColor
        Material.roundedScale: Material.LargeScale
        Overlay.modal: Rectangle { color: "#99000000" }
        onOpened: { ipField.clear(); ipField.forceActiveFocus() }
        function sendByIp() {
            if (!ipField.acceptableInput) return
            var ip = ipField.text.trim()
            ipDialog.close()
            app.openSend(ip, 51889, ip, "")
        }
        contentItem: ColumnLayout {
            spacing: 12
            Label { text: qsTr("Enviar por IP"); color: app.textColor
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    fontSizeMode: Text.HorizontalFit; minimumPixelSize: 13
                    font.family: app.titleFont; font.pixelSize: 18; font.bold: true }
            TextField {
                id: ipField
                Layout.fillWidth: true
                placeholderText: "192.168.1.100"
                inputMethodHints: Qt.ImhPreferNumbers | Qt.ImhNoAutoUppercase
                validator: RegularExpressionValidator {
                    regularExpression: /^((25[0-5]|2[0-4]\d|1?\d?\d)\.){3}(25[0-5]|2[0-4]\d|1?\d?\d)$/
                }
                color: app.textColor
                onAccepted: ipDialog.sendByIp()
            }
        }
        footer: DialogButtonBox {
            Material.background: "transparent"
            Button { flat: true; text: qsTr("Cancelar"); icon.source: Qt.resolvedUrl("icons/close.svg"); icon.color: app.accent; icon.width: 14; icon.height: 14; display: AbstractButton.TextBesideIcon; onClicked: { Qt.inputMethod.hide(); ipDialog.close() } }
            Button { highlighted: true; text: qsTr("Continuar"); icon.source: Qt.resolvedUrl("icons/send.svg"); icon.color: app.accent; icon.width: 14; icon.height: 14; display: AbstractButton.TextBesideIcon
                     enabled: ipField.acceptableInput
                     onClicked: { Qt.inputMethod.hide(); ipDialog.sendByIp() } }
        }
    }

    // Limpiar la lista de recibidos (no borra los archivos guardados en disco).
    Dialog {
        id: clearReceivedDialog
        parent: Overlay.overlay
        modal: true; anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 64 : 320, 360)
        padding: 22
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        Material.background: app.cardColor
        Material.roundedScale: Material.LargeScale
        Overlay.modal: Rectangle { color: "#99000000" }
        contentItem: ColumnLayout {
            spacing: 14
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                width: 66; height: 66; radius: 33; color: app.accentA(0.15)
                Icon { anchors.centerIn: parent; name: "reset"; size: 30; color: app.accent }
            }
            // fillWidth + HorizontalFit: sin esto el ancho implícito del título
            // (mayor en Windows con esta fuente) desborda la tarjeta.
            Label { text: qsTr("Limpiar recibidos"); color: app.textColor
                    Layout.fillWidth: true; wrapMode: Text.WordWrap
                    fontSizeMode: Text.HorizontalFit; minimumPixelSize: 13
                    font.family: app.titleFont; font.pixelSize: 18; font.bold: true
                    horizontalAlignment: Text.AlignHCenter }
        }
        footer: DialogButtonBox {
            Material.background: "transparent"
            Button { flat: true; text: qsTr("Cancelar"); icon.source: Qt.resolvedUrl("icons/close.svg"); icon.color: app.accent; icon.width: 14; icon.height: 14; display: AbstractButton.TextBesideIcon; onClicked: clearReceivedDialog.close() }
            Button { highlighted: true; text: qsTr("Limpiar"); icon.source: Qt.resolvedUrl("icons/reset.svg"); icon.color: app.accent; icon.width: 14; icon.height: 14; display: AbstractButton.TextBesideIcon
                     onClicked: { receivedModel.clear(); clearReceivedDialog.close() } }
        }
    }

    // Solicitud entrante: se muestra como Popup modal sobre todo (Overlay.overlay).
    Dialog {
        id: incomingDialog
        parent: Overlay.overlay
        modal: true; anchors.centerIn: parent
        width: Math.min(parent ? parent.width - 48 : 320, 360)
        padding: 22
        closePolicy: Popup.NoAutoClose
        Material.background: app.cardColor
        Material.roundedScale: Material.LargeScale
        Overlay.modal: Rectangle { color: "#99000000" }
        visible: transfer.incomingActive
        onOpened: Qt.inputMethod.hide()
        contentItem: ColumnLayout {
            spacing: 14
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                Icon { name: "inbox"; size: 30; color: app.accent; Layout.alignment: Qt.AlignTop }
                ColumnLayout {
                    Layout.fillWidth: true; spacing: 2
                    Label {
                        text: qsTr("%1 quiere enviarte").arg(transfer.incomingName)
                        color: app.textColor; font.pixelSize: 15; font.bold: true
                        elide: Text.ElideRight; Layout.fillWidth: true
                    }
                    Label {
                        text: transfer.incomingSummary
                        color: app.textColor; font.pixelSize: 13
                        elide: Text.ElideRight; Layout.fillWidth: true
                    }
                    Label {
                        text: transfer.incomingSizeText
                        color: app.subtextColor; font.pixelSize: 12
                        Layout.fillWidth: true
                    }
                }
            }
        }
        footer: DialogButtonBox {
            Material.background: "transparent"
            Button { flat: true; text: qsTr("Rechazar"); icon.source: Qt.resolvedUrl("icons/close.svg"); icon.color: "#ef4444"; icon.width: 14; icon.height: 14; display: AbstractButton.TextBesideIcon; onClicked: transfer.respond(false) }
            Button { highlighted: true; text: qsTr("Aceptar"); icon.source: Qt.resolvedUrl("icons/check.svg"); icon.color: app.accent; icon.width: 14; icon.height: 14; display: AbstractButton.TextBesideIcon; onClicked: transfer.respond(true) }
        }
    }

    // Archivos compartidos a la app (menú Compartir de Android).
    property var pendingShare: []
    // Enlace/texto compartido (p.ej. YouTube comparte la URL, no un archivo).
    property string pendingShareText: ""
    property bool hasPendingShare: false
    onPendingShareChanged: hasPendingShare = pendingShare.length > 0 || pendingShareText.length > 0
    onPendingShareTextChanged: hasPendingShare = pendingShare.length > 0 || pendingShareText.length > 0
    function checkShared() {
        var s = transfer.takeSharedFiles()
        if (s && s.length > 0) {
            app.pendingShare = s.split("\n").filter(function(p) { return p.length > 0 })
            if (app.pendingShare.length > 0) { goHome(); app.currentTab = 0 }
        }
        // Abierto tocando la notificación de transferencia → pestaña "Recibidos".
        if (transfer.takeOpenReceived()) {
            goHome(); app.currentTab = 1
        }
        var t = transfer.takeSharedText()
        if (t && t.length > 0) {
            app.pendingShareText = t
            goHome(); app.currentTab = 0
        }
    }
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive) {
                checkShared(); transfer.refreshClipboard(); transfer.startBackgroundReceiver()
                // Si volvió del selector sin elegir carpeta, insistir (salvo si viene un compartido).
                if (transfer.shouldPromptFolder() && !folderSetupDialog.visible
                    && !app.hasPendingShare)
                    folderSetupDialog.open()
            }
        }
    }

    // Al cerrar / botón atrás del sistema: navegar dentro de la app antes de salir.
    onClosing: (close) => {
        if (Qt.platform.os === "android") {
            if (Qt.inputMethod.visible) { close.accepted = false; Qt.inputMethod.hide(); return }
            // Diálogos de Main
            if (ipDialog.visible) { close.accepted = false; ipDialog.close(); return }
            if (incomingDialog.visible) { close.accepted = false; transfer.respond(false); return }
            if (clearReceivedDialog.visible) { close.accepted = false; clearReceivedDialog.close(); return }
            if (folderSetupDialog.visible) { close.accepted = false; return } // NoAutoClose: no cerrar con atrás
            // Diálogo PIN del modo web (está dentro del StackView)
            let cur = stack.currentItem
            if (cur && cur.pinDialogItem && cur.pinDialogItem.visible) { close.accepted = false; cur.pinDialogItem.close(); return }
            if (cur && cur.updateDialogItem && cur.updateDialogItem.visible) { close.accepted = false; cur.updateDialogItem.close(); return }
            if (stack.depth > 1) { close.accepted = false; stack.pop(); return }
            if (app.currentTab !== 0) { close.accepted = false; app.currentTab = 0; return }
            // Si el emisor está esperando aprobación, cancelar el envío (no la recepción).
            if (transfer.state === "sending") { close.accepted = false; transfer.cancel(); return }
            return   // en la raíz → salir de la app
        }
        // Escritorio: botón X de la ventana.
        if (transfer.closeToTray && tray.available) {
            close.accepted = false
            app.hide()
        } else {
            Qt.quit()   // quitOnLastWindowClosed está desactivado en escritorio
        }
    }
    Connections {
        target: tray
        function onShowRequested() { app.show(); app.raise(); app.requestActivate() }
        function onQuitRequested() { Qt.quit() }
    }

    // Pestaña actual (Enviar=0, Recibidos=1). Fuente ÚNICA de verdad: ni el
    // reinicio de la barra ni el SwipeView la pisan sin intención del usuario.
    property int currentTab: 0
    // Al cerrar el visor de un texto recibido, cambiar a Recibidos (cuando el
    // SwipeView ya está visible, para no desincronizar su posición visual).
    property bool pendingReceivedTab: false
    property string pendingHost: ""
    property int pendingPort: 0
    property string pendingName: ""
    property string pendingPlatform: ""
    readonly property bool inSubPage: stack.depth > 1

    function platformIconName(p) {
        switch (p) {
        case "android": return "smartphone";
        case "windows": return "windows";
        case "macos":   return "apple";
        case "ios":     return "apple";
        case "linux":   return "laptop";
        default:        return "share";
        }
    }
    function platformName(p) {
        switch (p) {
        case "android": return "Android"; case "windows": return "Windows";
        case "linux": return "Linux"; case "macos": return "macOS";
        case "ios": return "iOS"; default: return p;
        }
    }
    function sendStatusText(s, p) {
        switch (s) {
        case "queued":   return qsTr("En cola");
        case "sending":  return Math.round(p * 100) + "%";
        case "done":     return qsTr("Enviado ✓");
        case "error":    return qsTr("Error");
        case "canceled": return qsTr("Cancelado");
        }
        return "";
    }
    function sendStatusColor(s) {
        switch (s) {
        case "done":  return app.accent;
        case "error": return "#ef4444";
        default:      return app.subtextColor;
        }
    }
    function fmtSize(b) {
        if (b >= 1073741824) return (b / 1073741824).toFixed(1) + " GB"
        if (b >= 1048576)    return (b / 1048576).toFixed(1) + " MB"
        if (b >= 1024)       return (b / 1024).toFixed(0) + " KB"
        return b + " B"
    }
    function fileUrl(path) {
        return ("" + path).startsWith("content://") ? path : "file://" + path
    }
    // Abre un recibido: texto → página de mensaje; archivo/imagen → app predeterminada del sistema.
    function openReceived(kind, content, who) {
        if (kind === "text") showText(content, who)
        else if (kind === "folder") transfer.openDownloadsFolder()   // no es UN archivo
        else transfer.openPath(content)
    }

    ListModel { id: receivedModel }
    Connections {
        target: transfer
        function onTextReceived(sender, text) {
            showText(text, sender)
            receivedModel.insert(0, { "kind": "text", "content": text,
                "sender": sender || "",
                "title": text.length > 50 ? text.substring(0, 50) + "…" : text,
                "preview": "",
                "size": 0,
                "date": Qt.formatDateTime(new Date(), "dd/MM/yyyy HH:mm") })
        }
        function onReceivedFile(summary, path, isFolder, sender, size, timestamp) {
            receivedModel.insert(0, { "kind": isFolder ? "folder" : "file", "content": path,
                "sender": sender || "",
                "title": summary,
                "preview": "",
                "size": size,
                "date": Qt.formatDateTime(timestamp, "dd/MM/yyyy HH:mm") })
        }
    }

    // ---------------- Cabecera ----------------
    header: ToolBar {
        topPadding: SafeArea.margins.top
        Material.background: app.surface
        Keys.onPressed: (e) => app.handleNavKey(e)
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: app.inSubPage ? 4 : 14
            anchors.rightMargin: 6
            spacing: 8
            Item {
                visible: !app.inSubPage
                width: 30; height: 30
                Icon { anchors.centerIn: parent; name: "vortex"; size: 20; color: app.accent }
            }
            ToolButton {
                id: backBtn
                visible: app.inSubPage
                activeFocusOnTab: visible
                implicitWidth: 40; implicitHeight: 40
                onClicked: stack.pop()
                background: Rectangle {
                    anchors.centerIn: parent
                    width: 34; height: 34; radius: 17
                    color: backBtn.down ? app.accentA(0.28)
                         : backBtn.hovered ? app.accentA(0.16) : "transparent"
                }
                contentItem: Icon { name: "back"; size: 20; color: app.textColor }
            }
            Label {
                text: app.inSubPage ? (stack.currentItem && stack.currentItem.pageTitle
                                       ? stack.currentItem.pageTitle : "") : "VorLAN"
                color: app.inSubPage ? app.textColor : app.accent
                font.family: app.titleFont
                font.pixelSize: 20; font.bold: true
                // La fuente decorativa (Ethnocentric) sienta las letras algo altas;
                // centrar verticalmente y compensar para alinearlas con los iconos.
                verticalAlignment: Text.AlignVCenter
                topPadding: 3
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
            }
            ToolButton { Material.roundedScale: Material.ExtraSmallScale;
                // Abrir la carpeta de recibidos (acceso rápido, siempre visible)
                visible: !app.inSubPage
                activeFocusOnTab: visible
                onClicked: transfer.openDownloadsFolder()
                contentItem: Icon { name: "folder"; size: 19; color: app.subtextColor }
            }
            ToolButton { Material.roundedScale: Material.ExtraSmallScale;
                visible: !app.inSubPage
                activeFocusOnTab: visible
                onClicked: stack.push(settingsPage)
                contentItem: Icon { name: "settings"; size: 20; color: app.subtextColor }
            }
            ToolButton { Material.roundedScale: Material.ExtraSmallScale;
                // Acerca de (solo en la pantalla de Ajustes)
                visible: app.inSubPage && stack.currentItem && stack.currentItem.isSettings === true
                activeFocusOnTab: visible
                onClicked: stack.push(aboutPage)
                contentItem: Icon { name: "info"; size: 20; color: app.subtextColor }
            }
        }
    }

    StackView { id: stack; anchors.fill: parent; initialItem: mainPage
        focus: true   // capta la primera tecla del mando aunque no haya control enfocado
        Keys.onPressed: (e) => app.handleNavKey(e)
    }

    // Anillo de foco global: recuadra el control enfocado (para mando/teclado en TV).
    // Un solo indicador que sigue a app.activeFocusItem; solo se muestra para
    // controles enfocables (activeFocusOnTab) y cuando la navegación es por foco.
    Item {
        id: focusLayer
        parent: Overlay.overlay      // capa superior: cubre también cabecera y pie
        anchors.fill: parent
        z: 10000
        Rectangle {
            id: focusRing
            visible: false
            color: "transparent"
            radius: 8
            border.color: app.accent
            border.width: 2
            antialiasing: true
        }
        function refresh() {
            var t = app.keyboardNav ? app.activeFocusItem : null
            // Solo si es un control enfocable Y sigue visible (evita que el recuadro
            // se quede sobre un control que se ocultó, p.ej. el botón "atrás" al
            // volver de una subpágina, que comparte sitio con el logo de la app).
            if (t && t.activeFocusOnTab && t.visible && t.width > 0 && t.height > 0) {
                // Medir dos esquinas opuestas → tamaño VISUAL real (respeta escala,
                // p.ej. los switches van a scale 0.8).
                var a = t.mapToItem(focusLayer, 0, 0)
                var b = t.mapToItem(focusLayer, t.width, t.height)
                var x0 = Math.min(a.x, b.x), y0 = Math.min(a.y, b.y)
                var w = Math.abs(b.x - a.x), h = Math.abs(b.y - a.y)
                var pad = 4
                // Recortar a los límites de la pantalla para que no se vea cortado
                // arriba/abajo (los botones de cabecera/pie están pegados al borde).
                var left = Math.max(0, x0 - pad)
                var top = Math.max(0, y0 - pad)
                var right = Math.min(focusLayer.width, x0 + w + pad)
                var bottom = Math.min(focusLayer.height, y0 + h + pad)
                focusRing.x = left; focusRing.y = top
                focusRing.width = right - left; focusRing.height = bottom - top
                // Esquinas redondeadas consistentes en todas las ubicaciones
                // (no circular): radio suave limitado a 14, como las tarjetas.
                var mn = Math.min(focusRing.width, focusRing.height)
                focusRing.radius = Math.min(mn / 2, 14)
                focusRing.visible = true
            } else {
                focusRing.visible = false
            }
        }
        // Sigue al control enfocado (desplazamientos/animaciones). Solo se ejecuta en
        // Android TV y cuando se está navegando por mando: en móvil/escritorio no hay
        // anillo, así que no gastamos un temporizador a 30 Hz de forma permanente.
        Timer {
            interval: 32; repeat: true
            running: transfer.isTv && app.keyboardNav
            onTriggered: focusLayer.refresh()
        }
    }

    // La barra de pestañas sigue a la fuente única (currentTab).
    Connections {
        target: app
        function onCurrentTabChanged() {
            if (bottomBar.currentIndex !== app.currentTab)
                bottomBar.currentIndex = app.currentTab
        }
    }

    footer: TabBar {
        id: bottomBar
        visible: !app.inSubPage
        // Margen inferior seguro: que no quede bajo los botones/gestos del sistema.
        bottomPadding: SafeArea.margins.bottom
        height: visible ? implicitHeight : 0
        Material.background: app.surface
        Keys.onPressed: (e) => app.handleNavKey(e)
        // Al re-mostrarse (tras una subpágina) restaurar el índice por si la barra
        // se reinició a 0. El seguimiento de la fuente única va por Connections abajo.
        onVisibleChanged: if (visible) currentIndex = app.currentTab
        TabButton { id: tabSend
            activeFocusOnTab: true
            readonly property bool active: app.currentTab === 0
            onClicked: app.currentTab = 0
            background: Rectangle {
                color: tabSend.active ? app.accentA(0.12) : "transparent"
                Behavior on color { ColorAnimation { duration: 150 } }
                Rectangle {   // línea de acento superior en la pestaña activa
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    height: 3; color: app.accent; visible: tabSend.active
                }
            }
            contentItem: ColumnLayout {
                spacing: 3
                Icon { Layout.alignment: Qt.AlignHCenter; name: "send"; size: 18
                       color: tabSend.active ? app.accent : app.subtextColor }
                Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("Enviar"); font.pixelSize: 12
                        font.bold: tabSend.active
                        color: tabSend.active ? app.accent : app.subtextColor }
            }
        }
        TabButton { id: tabRecv
            activeFocusOnTab: true
            readonly property bool active: app.currentTab === 1
            onClicked: app.currentTab = 1
            background: Rectangle {
                color: tabRecv.active ? app.accentA(0.12) : "transparent"
                Behavior on color { ColorAnimation { duration: 150 } }
                Rectangle {
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    height: 3; color: app.accent; visible: tabRecv.active
                }
            }
            contentItem: ColumnLayout {
                spacing: 3
                Icon { Layout.alignment: Qt.AlignHCenter; name: "inbox"; size: 18
                       color: tabRecv.active ? app.accent : app.subtextColor }
                Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("Recibidos"); font.pixelSize: 12
                        font.bold: tabRecv.active
                        color: tabRecv.active ? app.accent : app.subtextColor }
            }
        }
    }

    // ================= Página principal =================
    Component {
        id: mainPage
        Item {
        // Al EMPEZAR a volver de una subpágina (antes de pintar): aplicar el cambio de
        // pestaña pendiente (texto recibido) y reposicionar el SwipeView, para que no
        // se vea "Enviar" un instante antes de mostrar "Recibidos".
        StackView.onActivating: {
            if (app.pendingReceivedTab) { app.pendingReceivedTab = false; app.currentTab = 1 }
            syncSwipe()
        }
        StackView.onActivated: {
            syncSwipe()   // reafirmar tras completar la transición
            // Al volver de una subpágina navegando por mando, reenfocar un control
            // visible (si no, el foco queda en el botón "atrás" ya oculto).
            if (app.keyboardNav)
                Qt.callLater(function() {
                    var f = app.contentItem.nextItemInFocusChain(true)
                    if (f) f.forceActiveFocus(Qt.TabFocusReason)
                })
        }
        // Fuerza el SwipeView a la pestaña actual, incluida su posición VISUAL
        // (contentX). La animación del SwipeView a veces no reposiciona tras un
        // push/pop, dejando el contenido pegado aunque el índice sea correcto.
        function syncSwipe() {
            swipeView.currentIndex = app.currentTab
            // No forzar mientras el usuario arrastra (deslizamiento manual fluido).
            if (swipeView.contentItem && swipeView.width > 0 && !swipeView.contentItem.dragging)
                swipeView.contentItem.contentX = app.currentTab * swipeView.width
        }
        // Deslizar horizontalmente cambia entre Enviar y Recibidos (sincronizado con la barra).
        SwipeView {
            id: swipeView
            anchors.fill: parent
            clip: true
            activeFocusOnTab: false   // no ser parada de la cadena de foco; sí sus controles hijos
            Component.onCompleted: currentIndex = app.currentTab
            onCurrentIndexChanged: app.currentTab = currentIndex
            // ---- Enviar ----
            Item {
                ColumnLayout {
                    anchors.fill: parent; spacing: 0

                    // Modo "Compartir": elige un dispositivo para enviar lo compartido
                    Pane {
                        Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.topMargin: 12
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
                                Label { text: app.pendingShareText.length > 0
                                              ? app.pendingShareText
                                              : qsTr("Elige un dispositivo")
                                        color: app.subtextColor; font.pixelSize: 11
                                        elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                            ToolButton { implicitWidth: 32; implicitHeight: 32
                                         onClicked: { app.pendingShare = []; app.pendingShareText = "" }
                                         contentItem: Icon { name: "close"; size: 15; color: app.subtextColor } }
                        }
                    }

                    Label { text: qsTr("Este dispositivo"); color: app.subtextColor; font.pixelSize: 11
                            Layout.leftMargin: 16; Layout.topMargin: 12; Layout.bottomMargin: 4 }

                    // PANEL "tu zona": tu equipo + enviar por IP. Se distingue de las
                    // tarjetas neutras de "Dispositivos en la red" por su fondo tintado
                    // con el color de acento y por ir agrupados en un solo bloque.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.leftMargin: 12; Layout.rightMargin: 12
                        Layout.preferredHeight: selfCol.implicitHeight
                        radius: 16
                        color: app.accentA(0.10)
                        border.width: 1
                        border.color: app.accentA(0.35)

                        ColumnLayout {
                            id: selfCol
                            width: parent.width
                            spacing: 0

                            // Tu equipo (avatar + nombre + IP) → editar perfil.
                            ItemDelegate {
                                activeFocusOnTab: true
                                Layout.fillWidth: true
                                Layout.preferredHeight: 62
                                onClicked: stack.push(profilePage)
                                background: Rectangle {
                                    radius: 16
                                    color: parent.hovered ? app.accentA(0.14) : "transparent"
                                }
                                contentItem: RowLayout {
                                    spacing: 12
                                    AvatarView { size: 40; imageUrl: transfer.avatarImage; accent: app.accent
                                                 deviceType: transfer.deviceType; active: true
                                                 Layout.leftMargin: 2 }
                                    ColumnLayout {
                                        spacing: 1; Layout.fillWidth: true
                                        Label { text: transfer.deviceName; color: app.textColor
                                                font.pixelSize: 14; font.bold: true
                                                elide: Text.ElideRight; Layout.fillWidth: true }
                                        Label {
                                            text: transfer.hostName
                                            color: app.subtextColor; font.pixelSize: 11
                                            elide: Text.ElideRight; Layout.fillWidth: true
                                        }
                                    }
                                    Rectangle {
                                        radius: 9; color: app.accentA(0.22)
                                        implicitHeight: 20; implicitWidth: tuRow.width + 14
                                        Row {
                                            id: tuRow; anchors.centerIn: parent; spacing: 3
                                            Label { text: qsTr("Tú"); color: app.textColor; font.pixelSize: 11; font.bold: true
                                                    anchors.verticalCenter: parent.verticalCenter }
                                            Icon { name: "edit"; size: 11; color: app.textColor
                                                   anchors.verticalCenter: parent.verticalCenter }
                                        }
                                    }
                                }
                            }

                            Rectangle { Layout.fillWidth: true; Layout.leftMargin: 14; Layout.rightMargin: 14
                                        height: 1; color: app.accentA(0.22) }

                            // Modo web: recibir/enviar desde un navegador, sin instalar VorLAN
                            // en el otro dispositivo (iPhone, PC ajeno, etc.).
                            ItemDelegate {
                                activeFocusOnTab: true
                                Layout.fillWidth: true
                                Layout.preferredHeight: 50
                                onClicked: app.pushPage(webPageComp)
                                background: Rectangle {
                                    radius: 16
                                    color: parent.hovered ? app.accentA(0.14) : "transparent"
                                }
                                contentItem: RowLayout {
                                    spacing: 12
                                    Rectangle {
                                        width: 34; height: 34; radius: 17
                                        color: transfer.webEnabled ? app.accent : app.accentA(0.18)
                                        Layout.leftMargin: 5
                                        Icon { anchors.centerIn: parent; name: "globe"; size: 17
                                               color: transfer.webEnabled ? "white" : app.accent }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 0
                                        Label { text: qsTr("Modo web"); color: app.textColor
                                                font.pixelSize: 14; Layout.fillWidth: true }
                                        // Solo la dirección cuando está activo (sin subtítulo fijo).
                                        Label { visible: transfer.webEnabled
                                                text: transfer.webUrl
                                                color: app.accent
                                                font.pixelSize: 10; elide: Text.ElideRight
                                                Layout.fillWidth: true }
                                    }
                                    Icon { name: "back"; size: 15; color: app.subtextColor; rotation: 180 }
                                }
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16; Layout.rightMargin: 16
                        Layout.topMargin: 8; Layout.bottomMargin: 2
                        Label { text: qsTr("Dispositivos en la red"); color: app.subtextColor; font.pixelSize: 11
                                Layout.fillWidth: true }
                        // Enviar escribiendo la IP (útil si el descubrimiento no encuentra
                        // el equipo: aislamiento de AP, subredes distintas, VPN...).
                        AbstractButton {
                            implicitWidth: 28; implicitHeight: 28
                            onClicked: ipDialog.open()
                            ToolTip.visible: hovered; ToolTip.text: qsTr("Enviar por IP")
                            contentItem: Icon { anchors.centerIn: parent; name: "wifi"; size: 14; color: app.accent }
                        }
                    }

                    Item {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        visible: deviceModel.count === 0
                        ColumnLayout {
                            anchors.centerIn: parent; spacing: 20
                            // Vórtice giratorio (marca de la app, en honor a "Vorlan"),
                            // con un halo sutil que late para dar sensación de "buscando".
                            Item {
                                id: radar
                                Layout.alignment: Qt.AlignHCenter
                                width: 170; height: 170
                                property real t: 0
                                NumberAnimation on t { from: 0; to: 1; duration: 2600
                                    loops: Animation.Infinite; running: app.appActive }
                                Repeater {
                                    model: 2
                                    Rectangle {
                                        property real p: (radar.t + index / 2) % 1
                                        anchors.centerIn: parent
                                        width: radar.width * (0.55 + p * 0.45); height: width
                                        radius: width / 2
                                        color: "transparent"; border.width: 2
                                        border.color: app.accentA((1 - p) * 0.35)
                                    }
                                }
                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 104; height: 104; radius: 52
                                    color: app.accentA(0.12)
                                    Icon {
                                        anchors.centerIn: parent
                                        name: "vortex"; size: 66; color: app.accent
                                        RotationAnimation on rotation {
                                            from: 0; to: 360; duration: 3200
                                            loops: Animation.Infinite; running: app.appActive
                                        }
                                    }
                                }
                            }
                            Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("Buscando dispositivos…")
                                    color: app.textColor; font.family: app.titleFont
                                    font.pixelSize: 16; font.bold: true }
                        }
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        visible: deviceModel.count > 0
                        keyNavigationEnabled: false   // la navegación por foco la lleva el puente D-pad
                        clip: true; model: deviceModel; spacing: 8
                        topMargin: 12; bottomMargin: 12; leftMargin: 12; rightMargin: 12
                        delegate: ItemDelegate {
                            id: devDelegate
                            activeFocusOnTab: true
                            width: ListView.view.width - 24; height: 62
                            property bool dropHover: false
                            onClicked: openSend(address, port, name, platform)
                            background: Rectangle {
                                radius: 14
                                color: devDelegate.dropHover ? app.accentA(0.25)
                                     : (devDelegate.hovered ? app.cardHover : app.cardColor)
                                border.width: devDelegate.dropHover ? 2 : 0
                                border.color: app.accent
                            }
                            // Arrastrar y soltar archivos sobre el dispositivo → enviar
                            DropArea {
                                anchors.fill: parent
                                keys: ["text/uri-list"]
                                onEntered: (drag) => { devDelegate.dropHover = true; drag.accept(Qt.CopyAction) }
                                onExited: devDelegate.dropHover = false
                                onDropped: (drop) => {
                                    devDelegate.dropHover = false
                                    if (drop.hasUrls) {
                                        transfer.enqueueSend(address, port, name, platform, drop.urls)
                                        drop.accept(Qt.CopyAction)
                                    }
                                }
                            }
                            contentItem: RowLayout {
                                spacing: 12
                                // Avatar del peer: voltea entre su foto (si la recibimos)
                                // y el icono de SU sistema operativo.
                                AvatarView {
                                    size: 40
                                    imageUrl: avatarThumb ?? ""
                                    platform: model.platform
                                    deviceType: model.dtype
                                    accent: app.accent
                                    active: true
                                    vertical: true   // giro vertical (arriba→abajo) para la red
                                }
                                ColumnLayout {
                                    spacing: 1; Layout.fillWidth: true
                                    Label { text: name; color: app.textColor; font.pixelSize: 14; font.bold: true
                                            elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: address + " · " + app.platformName(platform)
                                            color: app.subtextColor; font.pixelSize: 11
                                            elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                Button { Material.roundedScale: Material.ExtraSmallScale; text: qsTr("Enviar"); flat: true; icon.name: ""; Material.foreground: app.accent
                                         onClicked: openSend(address, port, name, platform) }
                            }
                        }
                    }
                    // ---- Cola de envíos (a quién y qué se está enviando) — mismo estilo que Recibidos ----
                    Rectangle {
                        Layout.fillWidth: true
                        visible: queueView.count > 0
                        radius: 14; color: app.cardColor
                        Layout.preferredHeight: enviHeader.height + Math.min(queueView.contentHeight, 148) + 16
                        RowLayout {
                            id: enviHeader
                            anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                            anchors.leftMargin: 16; anchors.rightMargin: 8
                            height: 34
                            Label { text: qsTr("Envíos"); color: app.subtextColor; font.pixelSize: 11
                                    Layout.fillWidth: true }
                            ToolButton {
                                activeFocusOnTab: true
                                implicitHeight: 32
                                onClicked: transfer.clearFinishedSends()
                                contentItem: RowLayout {
                                    spacing: 5
                                    Icon { name: "reset"; size: 14; color: app.subtextColor }
                                    Label { text: qsTr("Limpiar"); color: app.subtextColor; font.pixelSize: 12 }
                                }
                            }
                        }
                        ListView {
                            id: queueView
                            anchors.top: enviHeader.bottom; anchors.left: parent.left
                            anchors.right: parent.right; anchors.bottom: parent.bottom
                            anchors.leftMargin: 12; anchors.rightMargin: 12; anchors.topMargin: 4
                            clip: true; spacing: 8; model: transfer.sendQueue
                            delegate: ItemDelegate {
                                activeFocusOnTab: true
                                width: ListView.view.width; height: 54; clip: true
                                background: Rectangle { radius: 14; color: parent.hovered ? app.cardHover : app.cardColor }
                                contentItem: RowLayout {
                                    spacing: 12
                                    Rectangle {
                                        width: 36; height: 36; radius: 18
                                        color: status === "error" ? "#fdecea" : status === "canceled" ? "#f3f4f6" : app.accentA(0.15)
                                        Icon { anchors.centerIn: parent
                                               name: status === "error" ? "close" : status === "canceled" ? "close" : (genericSummary === qsTr("Carpeta") ? "folder" : "file")
                                               size: 17; color: status === "error" ? "#ef4444" : status === "canceled" ? "#a3a3a3" : app.accent }
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true; Layout.preferredWidth: 0; spacing: 1; clip: true
                                        RowLayout {
                                            spacing: 6
                                            Label { text: transfer.showFileNames ? summary : genericSummary; color: app.textColor
                                                    font.pixelSize: 13; font.bold: true
                                                    elide: Text.ElideRight; Layout.fillWidth: true; wrapMode: Text.NoWrap; maximumLineCount: 1 }
                                            Label {
                                                visible: status === "error" || status === "canceled"
                                                text: status === "error" ? qsTr("Rechazado") : qsTr("Cancelado")
                                                color: status === "error" ? "#ef4444" : "#a3a3a3"
                                                font.pixelSize: 11; font.bold: true
                                                Layout.alignment: Qt.AlignRight
                                            }
                                        }
                                        Label {
                                            visible: status !== "sending"
                                            text: {
                                                var parts = []
                                                parts.push(name)
                                                if (totalSize > 0) parts.push("(" + fmtSize(totalSize) + ")")
                                                var ts = Qt.formatDateTime(timestamp, "dd/MM/yyyy HH:mm")
                                                if (ts.length > 0) parts.push(ts)
                                                return parts.join(" · ")
                                            }
                                            color: app.subtextColor; font.pixelSize: 11
                                            elide: Text.ElideRight; Layout.fillWidth: true; wrapMode: Text.NoWrap; maximumLineCount: 1
                                        }
                                        ProgressBar {
                                            visible: status === "sending"
                                            Layout.fillWidth: true; Layout.preferredHeight: 4
                                            from: 0; to: 1; value: progress
                                        }
                                    }
                                    ToolButton { Material.roundedScale: Material.ExtraSmallScale;
                                        visible: status === "queued" || status === "sending"
                                        implicitWidth: 44; implicitHeight: 44
                                        onClicked: transfer.cancelJob(jobId)
                                        contentItem: Icon { name: "close"; size: 18; color: app.subtextColor }
                                    }
                                }
                            }
                        }
                    }

                }
            }
            // ---- Recibidos ----
            Item {
                // Cabecera con botón "Limpiar" (solo si hay elementos).
                RowLayout {
                    id: recvHeader
                    anchors.top: parent.top; anchors.left: parent.left; anchors.right: parent.right
                    anchors.leftMargin: 16; anchors.rightMargin: 8
                    anchors.topMargin: 10
                    height: receivedModel.count > 0 ? 34 : 0
                    visible: receivedModel.count > 0
                    Label { text: qsTr("Recibidos"); color: app.subtextColor; font.pixelSize: 11
                            Layout.fillWidth: true }
                    ToolButton {
                        activeFocusOnTab: true
                        implicitHeight: 32
                        onClicked: clearReceivedDialog.open()
                        contentItem: RowLayout {
                            spacing: 5
                            Icon { name: "reset"; size: 14; color: app.subtextColor }
                            Label { text: qsTr("Limpiar"); color: app.subtextColor; font.pixelSize: 12 }
                        }
                    }
                }
                ColumnLayout {
                    anchors.centerIn: parent; spacing: 12
                    visible: receivedModel.count === 0
                    Icon { Layout.alignment: Qt.AlignHCenter; name: "inbox"; size: 46
                           color: app.subtextColor; opacity: 0.6 }
                    Label { Layout.alignment: Qt.AlignHCenter; text: qsTr("Sin nada recibido")
                            color: app.textColor; font.family: app.titleFont
                            font.pixelSize: 16; font.bold: true }
                }
                ListView {
                    anchors.top: recvHeader.bottom; anchors.left: parent.left
                    anchors.right: parent.right; anchors.bottom: parent.bottom
                    visible: receivedModel.count > 0
                    keyNavigationEnabled: false
                    clip: true; model: receivedModel; spacing: 8
                    topMargin: 12; bottomMargin: 12; leftMargin: 12; rightMargin: 12
                    delegate: ItemDelegate {
                        activeFocusOnTab: true
                        width: ListView.view.width - 24; height: 54; clip: true
                        onClicked: openReceived(kind, content, sender)
                        background: Rectangle { radius: 14; color: parent.hovered ? app.cardHover : app.cardColor }
                        contentItem: RowLayout {
                            spacing: 12
                            Rectangle {
                                width: 36; height: 36; radius: 18; color: app.accentA(0.15)
                                Icon { anchors.centerIn: parent
                                       name: kind === "text" ? "message" : (kind === "folder" ? "folder" : "file")
                                       size: 17; color: app.accent }
                            }
                            Item {
                                Layout.fillWidth: true; Layout.preferredWidth: 0; implicitHeight: 36
                                ColumnLayout {
                                    anchors.fill: parent; spacing: 1; clip: true
                                    Label { text: title; color: app.textColor; font.pixelSize: 13; font.bold: true
                                            elide: Text.ElideRight; Layout.fillWidth: true; wrapMode: Text.NoWrap; maximumLineCount: 1 }
                                    Label {
                                        text: {
                                            var parts = []
                                            if (sender.length > 0) parts.push(sender)
                                            if (kind !== "text" && size > 0) parts.push("(" + fmtSize(size) + ")")
                                            if (date.length > 0) parts.push(date)
                                            return parts.join(" · ")
                                        }
                                        color: app.subtextColor; font.pixelSize: 11
                                        elide: Text.ElideRight; Layout.fillWidth: true; wrapMode: Text.NoWrap; maximumLineCount: 1
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: openReceived(kind, content, sender)
                                }
                            }
                            // Archivo en Android: botón "Compartir" del sistema. En escritorio
                            // no se muestra (para reenviar basta la pestaña Enviar).
                            // Texto: icono de mensaje. (Las carpetas no son compartibles.)
                            ToolButton {
                                visible: Qt.platform.os === "android" && kind === "file" && !transfer.isDir(content)
                                activeFocusOnTab: visible
                                implicitWidth: 44; implicitHeight: 44
                                onClicked: transfer.shareFile(content)
                                contentItem: Icon { name: "share"; size: 18; color: app.subtextColor }
                            }
                            ToolButton {
                                visible: kind !== "text"
                                activeFocusOnTab: visible
                                implicitWidth: 44; implicitHeight: 44
                                onClicked: kind === "folder" ? transfer.openDownloadsFolder() : transfer.openContainingFolder(content)
                                contentItem: Icon { name: "folder"; size: 19; color: app.subtextColor }
                            }
                            Item {
                                visible: kind === "text"
                                implicitWidth: 44; implicitHeight: 44
                                Icon { anchors.centerIn: parent; name: "message"; size: 18; color: app.subtextColor }
                            }
                        }
                    }
                }
            }
        }
        // Cualquier cambio de pestaña (tap, back del sistema, recepción de texto…) mueve
        // el SwipeView. Se difiere un frame: si el cambio viene del botón atrás del sistema
        // (evento onClosing), aplicarlo dentro de ese evento no surtía efecto en el móvil.
        Connections {
            target: app
            function onCurrentTabChanged() { Qt.callLater(syncSwipe) }
        }
        // Banner global de envío/recepción (visible en ambas pestañas).
        Loader {
            anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.margins: 8
            z: 100
            active: transfer.busy || transfer.lastResult !== ""
            sourceComponent: progressBanner
        }
        }
    }

    // ---- Páginas (cada una en su propio archivo; se les pasa la ventana raíz) ----
    // Nota: se usa `appRoot` (no `app`) al asignar, porque dentro del objeto de la
    // página el nombre `app` ya está tomado por su propiedad → se auto-sombrearía.
    readonly property var appRoot: app
    Component { id: settingsPage;   SettingsPage   { app: appRoot } }
    Component { id: aboutPage;      AboutPage      { app: appRoot } }
    Component { id: sendPage;       SendPage       { app: appRoot } }
    Component { id: composePage;    ComposePage    { app: appRoot } }
    Component { id: appsPage;       AppsPage       { app: appRoot } }
    Component { id: viewerPage;     ViewerPage     { app: appRoot } }
    Component { id: webPageComp;    WebPage        { app: appRoot } }
    Component { id: profilePage;    ProfilePage    { app: appRoot } }
    Component { id: progressBanner; ProgressBanner { app: appRoot } }
    readonly property Component donatePageComp: Component { DonatePage { app: appRoot } }

    function openSend(host, port, name, platform) {
        app.pendingHost = host; app.pendingPort = port
        app.pendingName = name || host; app.pendingPlatform = platform || ""
        // Si venimos de "Compartir", enviar directamente lo compartido.
        // Enlace/texto (p.ej. de YouTube) → se manda como mensaje de texto.
        if (app.pendingShareText.length > 0) {
            transfer.enqueueText(host, port, app.pendingName, app.pendingPlatform,
                                 app.pendingShareText)
            app.pendingShareText = ""
            return
        }
        // Archivos/carpetas (por RUTAS: robusto con espacios/paréntesis y carpetas).
        if (app.pendingShare.length > 0) {
            transfer.enqueueSendPaths(host, port, app.pendingName, app.pendingPlatform,
                                      app.pendingShare)
            app.pendingShare = []
            return
        }
        stack.push(sendPage, { targetName: (name || host),
                               targetPlatform: (platform || ""), targetAddress: host })
    }


    // Navegación de la pila (para que las páginas en archivos aparte no dependan
    // del id `stack`, que solo es visible dentro de este archivo).
    function pushPage(comp, props) { return stack.push(comp, props || {}) }
    function popPage() { if (stack.depth > 1) stack.pop() }

    // Vuelve a la página principal (cierra todas las sub-páginas).
    function goHome() { while (stack.depth > 1) stack.pop() }
    // NO cambiamos la pestaña aquí (el SwipeView se ocultaría y su posición visual
    // se desincronizaría del índice). Marcamos para cambiar a Recibidos al volver,
    // cuando el SwipeView vuelva a estar visible (ver StackView.onActivated de mainPage).
    function showText(t, who) { app.pendingReceivedTab = true;
        stack.push(viewerPage, { content: t, sender: who || "" }) }
}
