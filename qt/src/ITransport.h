#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// Contrato del motor de transferencia (DIP): TransferManager depende de esta
// abstracción, no de una implementación concreta. Hoy la implementa Worker
// (TCP bloqueante en un hilo dedicado); mañana podría haber otra (BLE, relay…)
// sin tocar TransferManager.
//
// Vive en el hilo de red; los métodos-slot se invocan por conexiones en cola
// desde el hilo GUI, y reporta progreso/estado por señales (también en cola).
class ITransport : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~ITransport() override = default;

    // Control (thread-safe en las implementaciones).
    virtual void setAutoAccept(bool v) = 0;      // aceptar entrantes sin preguntar
    virtual void setEncrypt(bool v) = 0;         // cifrar los envíos (TLS) si el par lo soporta
    virtual void setShowNames(bool v) = 0;       // ¿nombres de archivo en la notificación?
    virtual void requestCancel() = 0;            // abortar la transferencia en curso
    virtual void respondDecision(bool accept) = 0;   // respuesta al prompt entrante
    virtual void setSelfName(const QString &name) = 0;
    virtual void setDownloadDir(const QString &dir) = 0;

public slots:
    virtual void startServer() = 0;              // crear/escuchar (en el hilo de red)
    // Envía rutas (archivos/carpetas) y, opcionalmente, un texto.
    virtual void sendItems(const QString &host, quint16 port,
                           const QStringList &paths, const QString &text) = 0;

signals:
    void serverReady(quint16 port);
    void statusChanged(const QString &state);        // sending|receiving|idle
    void progress(qint64 done, qint64 total, const QString &name, int curFile, int totFiles);
    void savingFile(const QString &name, int curFile, int totFiles);   // exportando a la carpeta final (Android)
    void receivedFolder();                 // lo recibido contiene una carpeta
    void senderIdentified(const QString &senderName);   // quién nos envía (del HELLO)
    void incomingRequest(const QString &senderName, const QString &summary, qint64 size, int items);
    void incomingResolved();                          // cerrar diálogo (aceptado/timeout)
    void finished(bool ok, const QString &direction, const QString &summary);
    void textReceived(const QString &sender, const QString &text);
    void fileReceivedAt(const QString &path);         // ruta del archivo recibido
};
