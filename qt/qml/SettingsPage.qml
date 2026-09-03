import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs

Flickable {
    id: page
    required property var app
    property string pageTitle: qsTr("Ajustes")
    readonly property bool isSettings: true   // para que la cabecera muestre "Acerca de"
    contentHeight: setCol.implicitHeight + 32
    clip: true

    FolderDialog {
        id: folderDialogDownload; title: qsTr("Elegir dónde guardar")
        currentFolder: transfer.downloadsFolder ? "file://" + (transfer.downloadsFolder.startsWith("/") ? "" : "/") + transfer.downloadsFolder : ""
        onAccepted: transfer.setDownloadDir(selectedFolder)
    }

    Dialog {
        id: colorPickerDialog
        modal: true; anchors.centerIn: Overlay.overlay
        width: Math.min(page.width - 48, 280); padding: 20
        Material.background: app.cardColor
        Material.roundedScale: Material.LargeScale
        Overlay.modal: Rectangle { color: "#99000000" }
        contentItem: ColumnLayout {
            spacing: 14
            Label { text: qsTr("Elegir color"); color: app.textColor
                    font.family: app.titleFont; font.pixelSize: 18; font.bold: true }
            GridLayout {
                Layout.alignment: Qt.AlignHCenter
                columns: 3; columnSpacing: 16; rowSpacing: 16
                readonly property var palette: [
                    "#1976D2", "#388E3C", "#D32F2F",
                    "#F57C00", "#7B1FA2", "#00796B",
                    "#C2185B", "#512DA8", "#0097A7"
                ]
                Repeater {
                    model: parent.palette
                    Rectangle {
                        required property string modelData
                        required property int index
                        Layout.preferredWidth: 44; Layout.preferredHeight: 44
                        radius: 22; color: modelData
                        readonly property bool selected: Qt.colorEqual(app.accent, modelData)
                        border.width: selected ? 3 : (activeFocus ? 2 : 0)
                        border.color: selected ? app.textColor
                                    : activeFocus ? Qt.lighter(modelData, 1.6) : "transparent"
                        activeFocusOnTab: true
                        Keys.onPressed: (e) => {
                            if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter
                                || e.key === Qt.Key_Space || e.key === Qt.Key_Select) {
                                transfer.accentColor = modelData
                                colorPickerDialog.close(); e.accepted = true
                            }
                        }
                        MouseArea {
                            anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: { transfer.accentColor = parent.modelData; colorPickerDialog.close() }
                        }
                        Icon {
                            anchors.centerIn: parent; visible: parent.selected
                            name: "check"; size: 18; color: "#ffffff"
                        }
                    }
                }
            }
        }
    }

    ColumnLayout {
        id: setCol
        x: 18; width: parent.width - 36; y: 14
        spacing: 16

        // Apariencia: tema — combobox en la misma línea de la etiqueta (orden Auto, Claro, Oscuro)
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            Label { text: qsTr("Apariencia"); color: app.textColor; font.pixelSize: 14; Layout.fillWidth: true }
            ThemedComboBox {
                id: themeCombo
                app: page.app
                Layout.preferredWidth: 168
                Layout.preferredHeight: 34; Layout.minimumHeight: 34; Layout.maximumHeight: 34
                textRole: "label"; valueRole: "code"
                model: [
                    { code: "system", label: qsTr("Sistema"), icon: "monitor" },
                    { code: "light",  label: qsTr("Claro"),   icon: "sun" },
                    { code: "dark",   label: qsTr("Oscuro"),  icon: "moon" }
                ]
                Component.onCompleted: currentIndex = indexOfValue(transfer.themeMode)
                onActivated: transfer.themeMode = currentValue
                // Al cambiar de idioma se reconstruye el modelo (por los qsTr) y el
                // ComboBox reinicia el índice → volver a fijar el valor seleccionado.
                onModelChanged: currentIndex = indexOfValue(transfer.themeMode)
                Connections { target: transfer
                    function onThemeChanged() { themeCombo.currentIndex = themeCombo.indexOfValue(transfer.themeMode) } }
            }
        }

        // Idioma — combobox en la misma línea (por defecto: el del sistema)
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            Label { text: qsTr("Idioma"); color: app.textColor; font.pixelSize: 14; Layout.fillWidth: true }
            ThemedComboBox {
                id: langCombo
                app: page.app
                Layout.preferredWidth: 168
                Layout.preferredHeight: 34; Layout.minimumHeight: 34; Layout.maximumHeight: 34
                textRole: "label"; valueRole: "code"
                model: [
                    { code: "",   label: qsTr("Sistema"), icon: "flag_system", tinted: false, ext: "svg" },
                    { code: "es", label: "Español",       icon: "flag_es",     tinted: false, ext: "png" },
                    { code: "en", label: "English",       icon: "flag_en",     tinted: false, ext: "png" },
                    { code: "fr", label: "Français",      icon: "flag_fr",     tinted: false, ext: "png" },
                    { code: "de", label: "Deutsch",       icon: "flag_de",     tinted: false, ext: "png" },
                    { code: "pt", label: "Português",     icon: "flag_pt",     tinted: false, ext: "png" },
                    { code: "it", label: "Italiano",      icon: "flag_it",     tinted: false, ext: "png" },
                    { code: "ru", label: "Русский",       icon: "flag_ru",     tinted: false, ext: "png" },
                    { code: "ja", label: "日本語",         icon: "flag_ja",     tinted: false, ext: "png" },
                    { code: "zh", label: "中文",           icon: "flag_zh",     tinted: false, ext: "png" },
                    { code: "ar", label: "العربية",       icon: "flag_ar",     tinted: false, ext: "png" },
                    { code: "ko", label: "한국어",         icon: "flag_ko",     tinted: false, ext: "png" },
                    { code: "hi", label: "हिन्दी",         icon: "flag_hi",     tinted: false, ext: "png" }
                ]
                Component.onCompleted: currentIndex = indexOfValue(transfer.language)
                onActivated: transfer.language = currentValue
                onModelChanged: currentIndex = indexOfValue(transfer.language)
                Connections { target: transfer
                    function onLanguageChanged() { langCombo.currentIndex = langCombo.indexOfValue(transfer.language) } }
            }
        }

        // Color — swatch que abre el modal con la paleta predefinida.
        RowLayout {
            Layout.fillWidth: true; spacing: 12
            Label { text: qsTr("Color"); color: app.textColor; font.pixelSize: 14
                    Layout.fillWidth: true }
            Rectangle {
                Layout.preferredWidth: 48; Layout.preferredHeight: 30; radius: 8
                color: app.accent
                border.width: 1; border.color: app.divider
                activeFocusOnTab: true
                Keys.onPressed: (e) => {
                    if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter
                        || e.key === Qt.Key_Space || e.key === Qt.Key_Select) {
                        colorPickerDialog.open(); e.accepted = true
                    }
                }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: colorPickerDialog.open()
                }
            }
        }

        // Carpeta (input de solo lectura + botón Cambiar en la misma línea)
        ColumnLayout {
            Layout.fillWidth: true; spacing: 8
            Label { text: qsTr("Guardar archivos en"); color: app.subtextColor; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                // Caja simple (no Material TextField) para controlar el alto exacto.
                Rectangle {
                    id: folderBox
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34; Layout.minimumHeight: 34; Layout.maximumHeight: 34
                    radius: 6
                    color: app.darkMode ? "#26292e" : "#ffffff"
                    border.width: 1; border.color: app.divider
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 10; anchors.rightMargin: 10
                        verticalAlignment: Text.AlignVCenter
                        text: transfer.downloadsFolder
                        color: app.textColor; font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                }
                Button { id: cambiarBtn; Material.roundedScale: Material.ExtraSmallScale;
                    visible: Qt.platform.os !== "ios"
                    activeFocusOnTab: visible
                    Layout.preferredHeight: 34; Layout.minimumHeight: 34; Layout.maximumHeight: 34
                    topPadding: 0; bottomPadding: 0
                    topInset: 0; bottomInset: 0
                    text: qsTr("Cambiar")
                    icon.source: "icons/folder.svg"; icon.width: 16; icon.height: 16
                    icon.color: app.accent
                    onClicked: {
                        if (Qt.platform.os === "android") transfer.pickAndroidFolder()
                        else folderDialogDownload.open()
                    }
                }
            }
        }

        // Todas las opciones en una sola tarjeta, sin espacio entre ellas.
        Rectangle {
            Layout.fillWidth: true
            radius: 12; color: app.cardColor
            Layout.preferredHeight: optCol.implicitHeight
            ColumnLayout {
                id: optCol
                width: parent.width
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 36
                    Layout.leftMargin: 4; Layout.rightMargin: 12; spacing: 8
                    Switch { activeFocusOnTab: true; leftPadding: 0; scale: 0.9; transformOrigin: Item.Left
                             checked: transfer.discoverable; onToggled: transfer.discoverable = checked }
                    ColumnLayout {
                        spacing: 0; Layout.fillWidth: true
                        Label { text: qsTr("Visible para otros dispositivos"); color: app.textColor
                                font.pixelSize: 14; Layout.fillWidth: true }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 36
                    Layout.leftMargin: 4; Layout.rightMargin: 12; spacing: 8
                    Switch { activeFocusOnTab: true; leftPadding: 0; scale: 0.9; transformOrigin: Item.Left
                             checked: transfer.autoAccept; onToggled: transfer.autoAccept = checked }
                    Label { text: qsTr("Aceptar automáticamente"); color: app.textColor
                            font.pixelSize: 14; Layout.fillWidth: true }
                }
                // Cifrado TLS de los envíos (desactivado por defecto). Si el dispositivo
                // no tiene soporte SSL, se muestra deshabilitado con una nota.
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: transfer.sslAvailable ? 36 : 52
                    Layout.leftMargin: 4; Layout.rightMargin: 12; spacing: 8
                    Switch { activeFocusOnTab: transfer.sslAvailable; leftPadding: 0; scale: 0.9; transformOrigin: Item.Left
                             enabled: transfer.sslAvailable
                             checked: transfer.encrypt; onToggled: transfer.encrypt = checked }
                    ColumnLayout {
                        spacing: 0; Layout.fillWidth: true
                        Label { text: qsTr("Cifrar envíos"); color: app.textColor
                                font.pixelSize: 14; Layout.fillWidth: true }
                        Label { visible: !transfer.sslAvailable
                                text: qsTr("No disponible en este dispositivo")
                                color: app.subtextColor; font.pixelSize: 11
                                wrapMode: Text.Wrap; Layout.fillWidth: true }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 36
                    Layout.leftMargin: 4; Layout.rightMargin: 12; spacing: 8
                    Switch { activeFocusOnTab: true; leftPadding: 0; scale: 0.9; transformOrigin: Item.Left
                             checked: transfer.notificationsEnabled; onToggled: transfer.notificationsEnabled = checked }
                    Label { text: qsTr("Mostrar notificaciones"); color: app.textColor
                            font.pixelSize: 14; Layout.fillWidth: true }
                }
                RowLayout {
                    Layout.fillWidth: true; Layout.preferredHeight: 36
                    Layout.leftMargin: 4; Layout.rightMargin: 12; spacing: 8
                    Switch { activeFocusOnTab: true; leftPadding: 0; scale: 0.9; transformOrigin: Item.Left
                             checked: transfer.showFileNames; onToggled: transfer.showFileNames = checked }
                    Label { text: qsTr("Mostrar nombres de archivos"); color: app.textColor
                            font.pixelSize: 14; Layout.fillWidth: true }
                }
                // Segundo plano (Android): las transferencias continúan si minimizas
                // la app (mientras haya una en curso el proceso sigue vivo). Al terminar,
                // o si la app está inactiva, se cierra para arrancar limpia la próxima vez
                // (Qt Quick se cuelga si se deja el proceso vivo e inactivo en segundo
                // plano). Excluir del optimizador de batería protege las transferencias
                // largas para que el sistema no las corte.
                ColumnLayout {
                    visible: Qt.platform.os === "android"
                    Layout.fillWidth: true
                    Layout.leftMargin: 12; Layout.rightMargin: 12
                    Layout.topMargin: 2; Layout.bottomMargin: 4
                    spacing: 6
                    // Aún sin proteger: solo el botón.
                    Button {
                        visible: !transfer.batteryExempt
                        activeFocusOnTab: visible
                        Material.roundedScale: Material.ExtraSmallScale
                        Layout.fillWidth: true; Layout.preferredHeight: 40
                        font.pixelSize: 12
                        highlighted: true
                        text: qsTr("Proteger del ahorro de batería")
                        icon.source: "icons/info.svg"; icon.width: 16; icon.height: 16; icon.color: "white"
                        onClicked: transfer.requestBatteryExemption()
                    }
                    // Ya protegida (botón ya pulsado): confirmación.
                    RowLayout {
                        visible: transfer.batteryExempt
                        Layout.fillWidth: true; spacing: 8
                        Icon { name: "shield"; size: 18; color: app.accent }
                        Label {
                            text: qsTr("Transferencias protegidas del ahorro de batería")
                            color: app.accent; font.pixelSize: 13
                            wrapMode: Text.WordWrap; Layout.fillWidth: true
                        }
                    }
                }
                RowLayout {
                    visible: tray.available
                    Layout.fillWidth: true; Layout.preferredHeight: 36
                    Layout.leftMargin: 4; Layout.rightMargin: 12; spacing: 8
                    Switch { activeFocusOnTab: true; leftPadding: 0; scale: 0.9; transformOrigin: Item.Left
                             checked: transfer.closeToTray; onToggled: transfer.closeToTray = checked }
                    Label { text: qsTr("Minimizar a la bandeja al cerrar"); color: app.textColor
                            font.pixelSize: 14; Layout.fillWidth: true }
                }
            }
        }
    }
}
