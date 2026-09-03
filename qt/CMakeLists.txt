cmake_minimum_required(VERSION 3.16)

# Nombre provisional del proyecto (renombrar al elegir el definitivo).
project(vorlan VERSION 1.01 LANGUAGES CXX)

# Versión en runtime (QML la lee vía transfer.appVersion). Centralizada aquí.
add_compile_definitions(VORLAN_VERSION="${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Network QuickControls2 Svg LinguistTools)

qt_standard_project_setup(REQUIRES 6.5)
set(QT_NO_PRIVATE_MODULE_WARNING ON)

qt_add_executable(vorlan
    src/main.cpp
    src/Discovery.h
    src/Discovery.cpp
    src/DeviceListModel.h
    src/DeviceListModel.cpp
    src/SendQueueModel.h
    src/SendQueueModel.cpp
    src/Protocol.h
    src/Protocol.cpp
    src/QrCode.h
    src/QrCode.cpp
    src/WebServer.h
    src/WebServer.cpp
    src/ZipStream.h
    src/ZipStream.cpp
    src/ITransport.h
    src/Worker.h
    src/Worker.cpp
    src/TransferManager.h
    src/TransferManager.cpp
    src/SettingsStore.h
    src/FileActions.h
    src/FileActions.cpp
    src/AndroidMulticast.h
    src/AndroidMulticast.cpp
    src/AndroidStorage.h
    src/AndroidStorage.cpp
    src/AndroidDevice.h
    src/AndroidDevice.cpp
    src/AndroidNotify.h
    src/AndroidNotify.cpp
    src/TrayController.h
    src/TrayController.cpp
)

qt_add_resources(vorlan "appicon" PREFIX "/" FILES appicon.svg appicon-light.svg)
# --- Certificado TLS embebido (para "Cifrar envíos") ---
# NO se versiona (es una clave privada; ver .gitignore): se genera aquí la primera
# vez. Es autofirmado y solo sirve para cifrar el canal frente a escuchas pasivas,
# no para autenticar, así que cada clon puede tener el suyo.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tls/cert.pem"
   OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tls/key.pem")
    find_program(VORLAN_OPENSSL openssl)
    if(NOT VORLAN_OPENSSL)
        message(FATAL_ERROR
            "No se encontró 'openssl' para generar el certificado TLS de qt/tls/.\n"
            "Instálalo (Linux: openssl · Windows: viene con Git for Windows) o copia\n"
            "manualmente cert.pem y key.pem en qt/tls/.")
    endif()
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tls")
    message(STATUS "VorLAN: generando certificado TLS autofirmado en qt/tls/…")
    execute_process(
        COMMAND "${VORLAN_OPENSSL}" req -x509 -newkey rsa:2048 -nodes
                -keyout tls/key.pem -out tls/cert.pem -days 36500 -subj "/CN=VorLAN"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _tls_rc
        OUTPUT_QUIET ERROR_QUIET)
    if(NOT _tls_rc EQUAL 0)
        message(FATAL_ERROR "Falló la generación del certificado TLS (openssl devolvió ${_tls_rc}).")
    endif()
endif()

qt_add_resources(vorlan "tls" PREFIX "/" FILES tls/cert.pem tls/key.pem)

# Directorio de paquete Android (manifest con permisos + MulticastLock).
if(ANDROID)
    set_property(TARGET vorlan PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
        ${CMAKE_CURRENT_SOURCE_DIR}/android)
    # OpenSSL para Android (prebuilt KDAB) → habilita QSslSocket/TLS en el dispositivo.
    # Se empaquetan las .so de la ABI que se esté compilando (arm64-v8a, armeabi-v7a…).
    set(_ossl_dir "${CMAKE_CURRENT_SOURCE_DIR}/android_openssl/${ANDROID_ABI}")
    if(EXISTS "${_ossl_dir}/libssl_3.so")
        set_property(TARGET vorlan PROPERTY QT_ANDROID_EXTRA_LIBS
            "${_ossl_dir}/libcrypto_3.so"
            "${_ossl_dir}/libssl_3.so")
    endif()
endif()

file(GLOB VORLAN_ICONS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} qml/icons/*.svg qml/icons/*.png)

qt_policy(SET QTP0004 NEW)
qt_add_qml_module(vorlan
    URI Vorlan
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/AvatarView.qml
        qml/Icon.qml
        qml/AboutPage.qml
        qml/DonatePage.qml
        qml/SettingsPage.qml
        qml/ProfilePage.qml
        qml/SendPage.qml
        qml/WebPage.qml
        qml/WebSharePage.qml
        qml/ComposePage.qml
        qml/AppsPage.qml
        qml/ViewerPage.qml
        qml/ProgressBanner.qml
        qml/UpdateDialog.qml
        qml/ThemedComboBox.qml
    RESOURCES
        ${VORLAN_ICONS}
        qml/fonts/ethnocen.ttf
        qml/money.gif
)

# --- Traducciones (i18n) ---
# El idioma fuente de los textos (qsTr/tr) es español; se añade una traducción
# por idioma. lrelease compila los .ts a .qm y se embeben en :/i18n/ .
# Actualizar los .ts tras cambiar textos:  cmake --build <build> --target update_translations
qt_add_translations(vorlan
    TS_FILES
        i18n/vorlan_en.ts
        i18n/vorlan_fr.ts
        i18n/vorlan_de.ts
        i18n/vorlan_pt.ts
        i18n/vorlan_it.ts
        i18n/vorlan_ru.ts
        i18n/vorlan_ja.ts
        i18n/vorlan_zh.ts
        i18n/vorlan_ar.ts
        i18n/vorlan_ko.ts
        i18n/vorlan_hi.ts
    RESOURCE_PREFIX "/i18n"
)

target_link_libraries(vorlan PRIVATE
    Qt6::Quick
    Qt6::Network
    Qt6::QuickControls2
)

# API privada para el selector de carpetas del sistema en Android (SAF).
if(ANDROID)
    find_package(Qt6 REQUIRED COMPONENTS CorePrivate)
    target_link_libraries(vorlan PRIVATE Qt6::CorePrivate)
endif()

# QtWidgets solo en escritorio (para QSystemTrayIcon: bandeja + notificaciones).
if(NOT ANDROID AND NOT IOS)
    find_package(Qt6 REQUIRED COMPONENTS Widgets)
    target_link_libraries(vorlan PRIVATE Qt6::Widgets)
endif()

# Windows: app de escritorio (sin ventana de consola) + icono incrustado en el .exe.
if(WIN32)
    set_target_properties(vorlan PROPERTIES WIN32_EXECUTABLE TRUE)
    # Fuerza recompilación del .rc cuando el .ico cambia (windres no genera deps automáticos).
    set_source_files_properties(appicon.rc PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/appicon.ico")
    target_sources(vorlan PRIVATE appicon.rc)
    # advapi32: SID del usuario (OpenProcessToken/ConvertSidToStringSid) para
    # localizar la foto de la cuenta de Windows. Ver findSystemUserPhoto().
    target_link_libraries(vorlan PRIVATE advapi32)
endif()

# macOS: bundle .app con icono (el .icns lo genera build-macos.sh con iconutil).
if(APPLE AND NOT IOS)
    set_target_properties(vorlan PROPERTIES
        MACOSX_BUNDLE TRUE
        OUTPUT_NAME "VorLAN"                       # -> VorLAN.app (nombre visible en Finder/Launchpad)
        MACOSX_BUNDLE_BUNDLE_NAME "VorLAN"
        MACOSX_BUNDLE_GUI_IDENTIFIER "net.vorlan.app"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "appicon.icns")
    target_sources(vorlan PRIVATE appicon.icns)
    set_source_files_properties(appicon.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
endif()

# --- Despliegue de escritorio (para el .deb autocontenido) ---
# Usa la API oficial de Qt que auto-detecta los módulos QML importados.
if(NOT ANDROID AND NOT IOS)
    install(TARGETS vorlan
        RUNTIME DESTINATION bin
        BUNDLE DESTINATION .
        LIBRARY DESTINATION lib)
    qt_generate_deploy_qml_app_script(
        TARGET vorlan
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
        DEPLOY_USER_QML_MODULES_ON_UNSUPPORTED_PLATFORM
    )
    install(SCRIPT ${deploy_script})
endif()
