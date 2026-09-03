#pragma once

#include <QString>
#include <QList>
#include <QByteArray>

class QFile;

// Genera un ZIP "al vuelo" para servirlo por HTTP sin crear un archivo temporal
// ni cargarlo en memoria. Usa el método STORED (sin comprimir) y descriptores de
// datos, así el tamaño total se conoce ANTES de empezar (hace falta para la
// cabecera Content-Length) y el CRC se calcula mientras se envía.
class ZipStream
{
public:
    struct Entry {
        QString rel;    // ruta dentro del zip ("Carpeta/sub/a.txt")
        QString abs;    // ruta real en disco
        qint64 size = 0;
    };

    // Recorre una carpeta y devuelve sus archivos (rutas relativas al padre,
    // de forma que el zip contenga la carpeta como raíz).
    static QList<Entry> scanFolder(const QString &folderPath);

    // Bytes exactos que ocupará el zip resultante.
    static qint64 totalSize(const QList<Entry> &entries);

    explicit ZipStream(const QList<Entry> &entries);
    ~ZipStream();

    bool atEnd() const { return m_done; }
    // Devuelve el siguiente trozo (vacío si terminó).
    QByteArray next(int maxBytes = 256 * 1024);

private:
    enum Phase { LocalHeader, FileData, Descriptor, Central, Eocd, Finished };

    QList<Entry> m_entries;
    int m_idx = 0;
    Phase m_phase = LocalHeader;
    QFile *m_file = nullptr;
    quint32 m_crc = 0;
    qint64 m_written = 0;          // offset absoluto dentro del zip
    QList<quint32> m_crcs;         // CRC de cada entrada (para el directorio)
    QList<qint64> m_offsets;       // offset del encabezado local de cada entrada
    QByteArray m_tail;             // directorio central + fin, ya construido
    int m_tailPos = 0;
    bool m_done = false;

    void buildTail(qint64 centralStart);
};
