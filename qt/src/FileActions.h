#pragma once

#include <QString>

// Integración con el sistema de archivos del SO: abrir con la app predeterminada,
// abrir carpetas, compartir (Android) y resolver la carpeta de descargas.
// Aísla la lógica dependiente de plataforma (incluido el xdg-open con entorno
// saneado en Linux empaquetado) fuera de TransferManager.
//
// El único estado que necesita es `downloadDir` (ruta local o árbol SAF
// "content://" en Android; vacío = carpeta predeterminada), que se pasa explícito.
namespace FileActions {

// ¿Es una carpeta? (una carpeta no es "compartible" como archivo suelto).
bool isDir(const QString &path);

// Ruta/nombre de la carpeta de descargas resuelta a partir de `downloadDir`.
// Escritorio: ruta absoluta. Android: nombre legible para mostrar.
QString downloadsPath(const QString &downloadDir);

// Abrir la carpeta de descargas en el explorador del sistema.
void openDownloads(const QString &downloadDir);

// Abrir una ruta: archivo → app predeterminada; carpeta → explorador.
void openPath(const QString &path, const QString &downloadDir);

// Abrir la carpeta que contiene `path`.
void openContaining(const QString &path, const QString &downloadDir);

// Android: menú "Compartir" del sistema. No-op en escritorio.
void share(const QString &path);

} // namespace FileActions
