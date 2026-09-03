#include "ZipStream.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>

namespace {

// --- CRC32 (polinomio de PKZIP) ---
const quint32 *crcTable()
{
    static quint32 t[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        init = true;
    }
    return t;
}

quint32 crcUpdate(quint32 crc, const char *data, int len)
{
    const quint32 *t = crcTable();
    crc = ~crc;
    for (int i = 0; i < len; ++i)
        crc = t[(crc ^ quint8(data[i])) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

void put16(QByteArray &b, quint16 v) { b.append(char(v & 0xFF)); b.append(char(v >> 8)); }
void put32(QByteArray &b, quint32 v)
{
    b.append(char(v & 0xFF)); b.append(char((v >> 8) & 0xFF));
    b.append(char((v >> 16) & 0xFF)); b.append(char((v >> 24) & 0xFF));
}

// Fecha/hora en formato MS-DOS (el que usa el ZIP).
void dosDateTime(quint16 &date, quint16 &time)
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = now.date();
    const QTime t = now.time();
    date = quint16(((d.year() - 1980) << 9) | (d.month() << 5) | d.day());
    time = quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
}

constexpr quint16 kFlagDescriptor = 0x0008;   // tamaños/CRC van DESPUÉS de los datos

} // namespace

QList<ZipStream::Entry> ZipStream::scanFolder(const QString &folderPath)
{
    QList<Entry> out;
    const QFileInfo root(folderPath);
    if (!root.isDir())
        return out;
    // Rutas relativas al PADRE, para que el zip contenga la carpeta como raíz.
    const QDir parent(root.dir());
    QDirIterator it(root.absoluteFilePath(),
                    QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        Entry e;
        e.rel = parent.relativeFilePath(fi.absoluteFilePath());
        e.abs = fi.absoluteFilePath();
        e.size = fi.size();
        out << e;
    }
    return out;
}

qint64 ZipStream::totalSize(const QList<Entry> &entries)
{
    qint64 total = 0;
    for (const Entry &e : entries) {
        const int n = e.rel.toUtf8().size();
        total += 30 + n;       // encabezado local
        total += e.size;       // datos (sin comprimir)
        total += 16;           // descriptor de datos
        total += 46 + n;       // entrada del directorio central
    }
    total += 22;               // fin del directorio central
    return total;
}

ZipStream::ZipStream(const QList<Entry> &entries) : m_entries(entries) {}

ZipStream::~ZipStream()
{
    if (m_file) { m_file->close(); delete m_file; }
}

QByteArray ZipStream::next(int maxBytes)
{
    QByteArray out;
    while (out.size() < maxBytes && !m_done) {
        switch (m_phase) {
        case LocalHeader: {
            if (m_idx >= m_entries.size()) { buildTail(m_written + out.size()); m_phase = Central; break; }
            const Entry &e = m_entries.at(m_idx);
            const QByteArray name = e.rel.toUtf8();
            m_offsets << m_written + out.size();
            quint16 date, time; dosDateTime(date, time);
            QByteArray h;
            put32(h, 0x04034b50);          // firma
            put16(h, 20);                  // versión necesaria
            put16(h, kFlagDescriptor);     // banderas
            put16(h, 0);                   // método: STORED
            put16(h, time); put16(h, date);
            put32(h, 0); put32(h, 0); put32(h, 0);   // crc/tamaños → en el descriptor
            put16(h, quint16(name.size()));
            put16(h, 0);                   // sin campo extra
            h.append(name);
            out.append(h);
            m_file = new QFile(e.abs);
            if (!m_file->open(QIODevice::ReadOnly)) { delete m_file; m_file = nullptr; }
            m_crc = 0;
            m_phase = FileData;
            break;
        }
        case FileData: {
            if (!m_file || m_file->atEnd()) {
                if (m_file) { m_file->close(); delete m_file; m_file = nullptr; }
                m_phase = Descriptor;
                break;
            }
            const QByteArray chunk = m_file->read(qMin<qint64>(maxBytes - out.size(), 256 * 1024));
            if (chunk.isEmpty()) { m_phase = Descriptor; break; }
            m_crc = crcUpdate(m_crc, chunk.constData(), chunk.size());
            out.append(chunk);
            break;
        }
        case Descriptor: {
            const Entry &e = m_entries.at(m_idx);
            QByteArray d;
            put32(d, 0x08074b50);      // firma del descriptor
            put32(d, m_crc);
            put32(d, quint32(e.size)); // comprimido == original (STORED)
            put32(d, quint32(e.size));
            out.append(d);
            m_crcs << m_crc;
            ++m_idx;
            m_phase = LocalHeader;
            break;
        }
        case Central:
        case Eocd: {
            const int take = qMin(maxBytes - out.size(), m_tail.size() - m_tailPos);
            out.append(m_tail.mid(m_tailPos, take));
            m_tailPos += take;
            if (m_tailPos >= m_tail.size()) { m_phase = Finished; m_done = true; }
            break;
        }
        case Finished:
            m_done = true;
            break;
        }
    }
    m_written += out.size();
    return out;
}

void ZipStream::buildTail(qint64 centralStart)
{
    QByteArray central;
    for (int i = 0; i < m_entries.size(); ++i) {
        const Entry &e = m_entries.at(i);
        const QByteArray name = e.rel.toUtf8();
        quint16 date, time; dosDateTime(date, time);
        put32(central, 0x02014b50);
        put16(central, 20); put16(central, 20);
        put16(central, kFlagDescriptor);
        put16(central, 0);                 // STORED
        put16(central, time); put16(central, date);
        put32(central, m_crcs.value(i));
        put32(central, quint32(e.size));
        put32(central, quint32(e.size));
        put16(central, quint16(name.size()));
        put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0);
        put32(central, 0);
        put32(central, quint32(m_offsets.value(i)));
        central.append(name);
    }
    QByteArray eocd;
    put32(eocd, 0x06054b50);
    put16(eocd, 0); put16(eocd, 0);
    put16(eocd, quint16(m_entries.size()));
    put16(eocd, quint16(m_entries.size()));
    put32(eocd, quint32(central.size()));
    put32(eocd, quint32(centralStart));
    put16(eocd, 0);
    m_tail = central + eocd;
    m_tailPos = 0;
}
