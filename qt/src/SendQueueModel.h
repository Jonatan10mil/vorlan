#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <QDateTime>

// Cola de envíos salientes (a varios dispositivos, uno a la vez).
// Cada fila describe a quién se envía, qué, y en qué estado va.
class SendQueueModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Status { Queued, Sending, Done, Error, Canceled };
    Q_ENUM(Status)

    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PlatformRole,
        SummaryRole,
        GenericSummaryRole,   // resumen sin nombre de archivo ("1 archivo", "Carpeta"…)
        StatusRole,     // texto: "queued"|"sending"|"done"|"error"|"canceled"
        ProgressRole,
        TimestampRole,
        TotalSizeRole,
    };

    struct Job {
        int id = 0;
        QString host;
        quint16 port = 0;
        QString name;       // nombre del dispositivo destino (para mostrar)
        QString platform;   // para el icono
        QStringList paths;
        QString text;
        QString summary;        // "3 elementos" / "Carpeta «x»" / "Mensaje de texto"
        QString genericSummary; // sin nombre: "1 archivo" / "Carpeta" / "Mensaje de texto"
        Status status = Queued;
        double progress = 0.0;
        QDateTime timestamp;
        qint64 totalSize = 0;
    };

    explicit SendQueueModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int addJob(const Job &j);                 // añade al final, devuelve id
    int firstQueuedId() const;                // primer trabajo en cola (-1 si ninguno)
    const Job *jobById(int id) const;
    void setStatus(int id, Status s);
    void setProgress(int id, double p);
    void removeById(int id);                  // quita una fila concreta
    void clearFinished();                     // quita filas done/error/canceled

private:
    int indexOfId(int id) const;
    static QString statusStr(Status s);

    QList<Job> m_jobs;
    int m_nextId = 1;
};
