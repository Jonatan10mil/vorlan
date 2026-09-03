#pragma once

#include <QString>

// Nombre/modelo del dispositivo en Android (vacío en otras plataformas).
namespace AndroidDevice {
QString deviceName();   // nombre puesto por el usuario (Settings.Global "device_name") o el modelo
QString deviceModel();  // fabricante + modelo (p.ej. "Samsung SM-G991B")
bool isTelevision();    // true si el dispositivo es Android TV (UI_MODE_TYPE_TELEVISION)
}
