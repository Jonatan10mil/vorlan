#include "TrayController.h"
#include "TransferManager.h"

#include <QtGlobal>

#if !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QIcon>

TrayController::TrayController(TransferManager *tm, QObject *parent)
    : QObject(parent)
    , m_tm(tm)
{
    m_available = QSystemTrayIcon::isSystemTrayAvailable();
    if (!m_available)
        return;

    m_tray = new QSystemTrayIcon(this);
    m_tray->setToolTip(QStringLiteral("VorLAN"));

    // Icono de bandeja según el tema (claro/oscuro), reactivo a cambios.
    auto applyIcon = [this]() {
        m_tray->setIcon(QIcon(m_tm->effectiveDark() ? QStringLiteral(":/appicon.svg")
                                                    : QStringLiteral(":/appicon-light.svg")));
    };
    applyIcon();
    connect(m_tm, &TransferManager::themeChanged, this, applyIcon);
    connect(m_tm, &TransferManager::systemDarkChanged, this, applyIcon);

    m_menu = new QMenu(); // setContextMenu no toma posesión; se borra con TrayController (QObject parent)
    QAction *showAct = m_menu->addAction(tr("Mostrar VorLAN"));
    connect(showAct, &QAction::triggered, this, &TrayController::showRequested);
    m_menu->addSeparator();
    QAction *quitAct = m_menu->addAction(tr("Salir"));
    connect(quitAct, &QAction::triggered, this, &TrayController::quitRequested);
    m_tray->setContextMenu(m_menu);

    connect(m_tray, &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r) {
                if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick)
                    emit showRequested();
            });
    m_tray->show();

    // --- Notificaciones de transferencia ---
    connect(tm, &TransferManager::transferDone, this,
            [this](bool ok, const QString &dir, const QString &summary) {
                QString title;
                if (dir.startsWith(QLatin1String("received")))
                    title = ok ? tr("Recepción completa")
                               : tr("Recepción fallida");
                else if (ok)
                    title = tr("Envío completado");
                else
                    title = summary.startsWith(QLatin1String("Cancelado"))
                                ? tr("Envío cancelado")
                                : tr("Envío fallido");
                showMessage(title, summary);
            });
    connect(tm, &TransferManager::incomingRequested, this,
            [this](const QString &name, const QString &summary) {
                showMessage(tr("%1 quiere enviarte").arg(name), summary);
            });
    connect(tm, &TransferManager::textReceived, this,
            [this](const QString &sender, const QString &) {
                showMessage(tr("Mensaje recibido"),
                            sender.isEmpty() ? tr("Nuevo mensaje") : sender);
            });
}

void TrayController::showMessage(const QString &title, const QString &body)
{
    if (m_tm && !m_tm->notificationsEnabled())
        return;   // el usuario desactivó las notificaciones
    if (m_tray)
        m_tray->showMessage(title, body, QSystemTrayIcon::Information, 5000);
}

#else  // ---- móvil: stub inerte ----

TrayController::TrayController(TransferManager *, QObject *parent)
    : QObject(parent)
{
}
void TrayController::showMessage(const QString &, const QString &) {}

#endif
