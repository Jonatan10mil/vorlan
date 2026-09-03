#pragma once

#include <QString>

// Notificación de progreso en Android mediante un servicio en primer plano
// (net.vorlan.TransferService). En otras plataformas son no-ops.
namespace AndroidNotify {

void requestPermission();                         // POST_NOTIFICATIONS (Android 13+)
void start(bool sending, const QString &text);    // arranca el servicio + notificación
void startIdle();                                 // servicio persistente "listo para recibir"
void update(bool sending, const QString &text, int percent); // actualiza el progreso
void stop();                                      // detiene el servicio
void result(const QString &title, const QString &text);  // notificación breve de fin

} // namespace AndroidNotify
