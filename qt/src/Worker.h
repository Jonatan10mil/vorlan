#pragma once

#include "ITransport.h"

#include <QMutex>
#include <QWaitCondition>
#include <atomic>

class QTcpServer;
class QTcpSocket;

// Implementación TCP de ITransport. Vive en el hilo de red (worker) y hace toda
// la E/S TCP **bloqueante**:
//  - Receptor: QTcpServer en el puerto de transferencia; por cada conexión
//    ejecuta el handshake y recibe el archivo, bloqueando SOLO este hilo.
//  - Emisor: conecta a un peer y envía un archivo.
// Reporta progreso/estado por las señales de ITransport (colas hacia el hilo GUI).
class Worker : public ITransport
{
    Q_OBJECT
public:
    explicit Worker(QString selfId, QString selfName, QObject *parent = nullptr);

    // Compartido con el hilo GUI (thread-safe).
    std::atomic<bool> autoAccept{false};
    std::atomic<bool> cancelRequested{false};   // solicitar abortar la transferencia
    std::atomic<bool> encrypt{false};           // cifrar envíos con TLS si el par lo soporta
    std::atomic<bool> showNames{true};          // ¿mostrar nombres de archivo en la notificación?

    // --- ITransport ---
    void setAutoAccept(bool v) override { autoAccept.store(v); }
    void setShowNames(bool v) override { showNames.store(v); }
    void setEncrypt(bool v) override { encrypt.store(v); }
    void requestCancel() override;
    void respondDecision(bool accept) override;
    void setSelfName(const QString &name) override;
    void setDownloadDir(const QString &dir) override;

public slots:
    void startServer() override;                     // crear/escuchar (en este hilo)
    // Envía una lista de rutas (archivos y/o carpetas) y, opcionalmente, un texto.
    void sendItems(const QString &host, quint16 port,
                   const QStringList &paths, const QString &text) override;

private slots:
    void onNewConnection();

private:
    // Un ítem a enviar (archivo, carpeta o texto).
    struct SendItem {
        QString type;       // "file" | "dir" | "text"
        QString relPath;    // ruta relativa preservada
        QString absPath;    // ruta local (vacío para texto)
        qint64 size = 0;
        QByteArray inlineData;   // datos en memoria (texto)
    };

    void handleIncoming(QTcpSocket *sock);
    bool askUser(QTcpSocket *sock, const QString &senderName, const QString &summary, qint64 size, int items);
    QList<SendItem> buildItems(const QStringList &paths, const QString &text,
                               qint64 &totalBytes, QString &summary) const;
    static QString sanitizeRelPath(const QString &relPath);
    QString baseDir() const;
    static QString uniquePath(const QString &desired);
    // Notificación de Android actualizada DESDE ESTE HILO: si se hiciera desde el
    // hilo de UI, al bloquear la pantalla Qt lo suspende y la barra se congelaba.
    void notifyProgress(bool sending, qint64 done, qint64 total,
                        const QString &name, int curFile, int totFiles);
    int m_lastNotifPct = -1;
    // Nombre de carpeta único bajo `base` ("nombre", si no existe; si no "nombre (1)"…).
    static QString uniqueDirName(const QString &base, const QString &name);

    QString selfNameSafe() const;

    QString m_selfId;
    QString m_selfName;
    QString m_downloadDir;            // vacío = predeterminada
    mutable QMutex m_nameMutex;       // protege m_selfName y m_downloadDir
    QTcpServer *m_server = nullptr;
    QTcpSocket *m_activeSendSock = nullptr;   // socket del envío en curso (para abortar desde otro hilo)
    mutable QMutex m_sendSockMutex;

    // Sincronización del prompt aceptar/rechazar entre GUI y worker.
    QMutex m_decisionMutex;
    QWaitCondition m_decisionCond;
    int m_decision = -1;   // -1 pendiente, 0 rechazar, 1 aceptar
};
