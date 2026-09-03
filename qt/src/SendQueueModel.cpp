#include "SendQueueModel.h"

SendQueueModel::SendQueueModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int SendQueueModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_jobs.size();
}

QVariant SendQueueModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_jobs.size())
        return {};
    const Job &j = m_jobs.at(index.row());
    switch (role) {
    case IdRole:       return j.id;
    case NameRole:     return j.name;
    case PlatformRole: return j.platform;
    case SummaryRole:  return j.summary;
    case GenericSummaryRole: return j.genericSummary;
    case StatusRole:   return statusStr(j.status);
    case ProgressRole: return j.progress;
    case TimestampRole: return j.timestamp;
    case TotalSizeRole: return j.totalSize;
    }
    return {};
}

QHash<int, QByteArray> SendQueueModel::roleNames() const
{
    return {
        { IdRole, "jobId" },
        { NameRole, "name" },
        { PlatformRole, "platform" },
        { SummaryRole, "summary" },
        { GenericSummaryRole, "genericSummary" },
        { StatusRole, "status" },
        { ProgressRole, "progress" },
        { TimestampRole, "timestamp" },
        { TotalSizeRole, "totalSize" },
    };
}

int SendQueueModel::addJob(const Job &j)
{
    Job copy = j;
    copy.id = m_nextId++;
    if (copy.timestamp.isNull()) copy.timestamp = QDateTime::currentDateTime();
    beginInsertRows({}, m_jobs.size(), m_jobs.size());
    m_jobs.append(copy);
    endInsertRows();
    return copy.id;
}

int SendQueueModel::indexOfId(int id) const
{
    for (int i = 0; i < m_jobs.size(); ++i)
        if (m_jobs.at(i).id == id)
            return i;
    return -1;
}

int SendQueueModel::firstQueuedId() const
{
    for (const Job &j : m_jobs)
        if (j.status == Queued)
            return j.id;
    return -1;
}

const SendQueueModel::Job *SendQueueModel::jobById(int id) const
{
    const int i = indexOfId(id);
    return i < 0 ? nullptr : &m_jobs.at(i);
}

void SendQueueModel::setStatus(int id, Status s)
{
    const int i = indexOfId(id);
    if (i < 0 || m_jobs[i].status == s)
        return;
    m_jobs[i].status = s;
    emit dataChanged(index(i), index(i), { StatusRole });
}

void SendQueueModel::setProgress(int id, double p)
{
    const int i = indexOfId(id);
    if (i < 0)
        return;
    m_jobs[i].progress = p;
    emit dataChanged(index(i), index(i), { ProgressRole });
}

void SendQueueModel::removeById(int id)
{
    const int i = indexOfId(id);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    m_jobs.removeAt(i);
    endRemoveRows();
}

void SendQueueModel::clearFinished()
{
    for (int i = m_jobs.size() - 1; i >= 0; --i) {
        const Status s = m_jobs.at(i).status;
        if (s == Done || s == Error || s == Canceled) {
            beginRemoveRows({}, i, i);
            m_jobs.removeAt(i);
            endRemoveRows();
        }
    }
}

QString SendQueueModel::statusStr(Status s)
{
    switch (s) {
    case Queued:   return QStringLiteral("queued");
    case Sending:  return QStringLiteral("sending");
    case Done:     return QStringLiteral("done");
    case Error:    return QStringLiteral("error");
    case Canceled: return QStringLiteral("canceled");
    }
    return {};
}
