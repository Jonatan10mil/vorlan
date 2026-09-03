#pragma once

#include <QString>
#include <QStringList>
#include <functional>

// Guarda un archivo ya escrito en la carpeta pública Descargas de Android
// (visible en la app Archivos), vía MediaStore. `relPath` es la ruta relativa
// dentro de Descargas (p.ej. "Vorlan/sub/foto.jpg").
// Devuelve true si se exportó correctamente. En no-Android es un no-op (false).
namespace AndroidStorage {
// Devuelven la URI content:// del archivo guardado (vacío si falla).
QString saveToDownloads(const QString &srcPath, const QString &relPath);

// Abre el selector de carpetas del sistema (SAF). Llama a `cb` con la URI del
// árbol elegido (content://…) o cadena vacía si se cancela. No-op fuera de Android.
void pickFolder(std::function<void(const QString &)> cb);

// Selector de ARCHIVOS del sistema con selección MÚLTIPLE (ACTION_OPEN_DOCUMENT +
// EXTRA_ALLOW_MULTIPLE). Lee las URIs de ClipData, que es donde Android pone las
// múltiples (el diálogo de Qt solo mira getData() y devolvía lista vacía).
void pickFiles(std::function<void(const QStringList &)> cb);

// Guarda srcPath dentro del árbol SAF (treeUri) respetando relPath (subcarpetas).
QString saveToTree(const QString &treeUri, const QString &relPath, const QString &srcPath);

// ENVÍO: copia un content:// (archivo o carpeta SAF) a la caché y devuelve la ruta
// real, ya que el emisor no puede leer content:// como sistema de archivos.
// clearOutgoingCache() vacía la caché de salida (llamar una vez por envío).
QString contentToCache(const QString &uri);
void clearOutgoingCache();
void clearSharedCache(); // borra cache/shared (ShareIntent) — sin dejar rastro
 // Nombre real (display name) de un content:// guardado (MediaStore puede numerarlo).
QString displayNameOf(const QString &uri);

// Abre una URI content:// con la app predeterminada (ACTION_VIEW / "abrir con").
void openContent(const QString &uriString);
void shareContent(const QString &pathOrUri);   // menú "Compartir" del sistema (ACTION_SEND)
void requestBatteryExemption();                // pide excluir la app del optimizador de batería
bool isBatteryExempt();                         // ¿ya está excluida del optimizador? (no-Android: true)

// Abre la carpeta de descargas (carpeta SAF elegida, o la pantalla de Descargas).
void openFolder(const QString &treeUri);

// Apps instaladas (JSON [{label,pkg,apk,size}]) y preparación del APK para enviar.
QString installedApps();
QString stageApk(const QString &apkPath, const QString &label);

// Portapapeles nativo: imagen (guardada a caché, devuelve ruta) o texto.
QString clipboardImage();
QString clipboardText();

// Color de los iconos de la barra de estado según el tema de la app.
void applySystemBars(bool lightBackground);

// Archivos compartidos a la app (menú Compartir); rutas separadas por '\n'.
QString takeSharedFiles();
QString takeSharedText();   // enlace/texto compartido (apps de streaming)
bool takeOpenReceived();    // ¿se abrió tocando la notificación?

// ¿Existe ya un archivo o carpeta con ese nombre en Download/Vorlan/ (via MediaStore)?
bool existsInDownloads(const QString &name);

// ¿Existe una carpeta con ese nombre dentro de un árbol SAF?
bool folderExistsInTree(const QString &treeUri, const QString &folderName);

// Permiso MANAGE_EXTERNAL_STORAGE (Android 11+): Needed para QFile::exists() en Downloads.
bool hasStorageManagerPermission();
void requestStorageManagerPermission();
}
