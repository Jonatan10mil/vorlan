#pragma once

#include <QObject>
#include <QString>

class TransferManager;
class QSystemTrayIcon;
class QMenu;

// Icono de bandeja del sistema + notificaciones nativas (solo escritorio).
// En Android/iOS es un stub inerte (available=false).
class TrayController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
public:
    explicit TrayController(TransferManager *tm, QObject *parent = nullptr);

    bool available() const { return m_available; }
    Q_INVOKABLE void showMessage(const QString &title, const QString &body);

signals:
    void showRequested();   // el usuario pidió mostrar la ventana
    void quitRequested();   // el usuario pidió salir de verdad

private:
    bool m_available = false;
    TransferManager *m_tm = nullptr;
#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_menu = nullptr;
#endif
};
