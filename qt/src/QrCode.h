#pragma once
// Generador de códigos QR minimalista y autónomo (sin dependencias de Qt en el
// núcleo, para poder probarlo por separado). Soporta modo BYTE, nivel de
// corrección M y versiones 1..10 (hasta ~216 bytes: de sobra para una URL).
//
// Uso:  auto m = qr::encode("http://192.168.1.5:51890");
//       m[y][x] == true  → módulo negro.

#include <string>
#include <vector>
#include <cstdint>

namespace qr {

// Devuelve la matriz de módulos (cuadrada). Vacía si el texto no cabe.
std::vector<std::vector<bool>> encode(const std::string &text);

// Construye un SVG (string) del QR, con `quiet` módulos de margen. Vacío si falla.
std::string toSvg(const std::string &text, int quiet = 4);

} // namespace qr
