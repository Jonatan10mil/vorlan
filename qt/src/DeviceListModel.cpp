#include "DeviceListModel.h"
#include <QHostAddress>

static QString cleanAddress(const QHostAddress &addr)
{
    // Muestra la dirección IPv4 pura (sin prefijo ::ffff: que Qt añade a veces).
    bool ok = false;
    const quint32 ipv4 = addr.toIPv4Address(&ok);
    if (ok && ipv4 != 0)
        return QHostAddress(ipv4).toString();
    return addr.toString();
}

DeviceListModel::DeviceListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DeviceListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_peers.size();
}

QVariant DeviceListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_peers.size())
        return {};
    const Peer &p = m_peers.at(index.row());
    switch (role) {
    case IdRole:       return p.id;
    case NameRole:     return p.name;
    case PlatformRole: return p.platform;
    case DeviceTypeRole: return p.dtype;
    case AddressRole:  return cleanAddress(p.address);
    case PortRole:     return p.tcpPort;
    case AvatarRole:   return QString(); // legacy emoji ya no se usa (solo avatarThumb)
    case AvatarThumbRole: return p.avatarThumb;
    default:           return {};
    }
}

QHash<int, QByteArray> DeviceListModel::roleNames() const
{
    return {
        {IdRole,       "peerId"},
        {NameRole,     "name"},
        {PlatformRole, "platform"},
        {DeviceTypeRole, "dtype"},
        {AddressRole,  "address"},
        {PortRole,     "port"},
        {AvatarRole,   "avatar"},
        {AvatarThumbRole, "avatarThumb"},
    };
}

int DeviceListModel::indexOfId(const QString &id) const
{
    for (int i = 0; i < m_peers.size(); ++i)
        if (m_peers.at(i).id == id)
            return i;
    return -1;
}

void DeviceListModel::onPeerFound(const Peer &peer)
{
    if (indexOfId(peer.id) >= 0) {
        onPeerUpdated(peer);
        return;
    }
    beginInsertRows(QModelIndex(), m_peers.size(), m_peers.size());
    m_peers.append(peer);
    endInsertRows();
    emit countChanged();
}

void DeviceListModel::onPeerUpdated(const Peer &peer)
{
    const int row = indexOfId(peer.id);
    if (row < 0) {
        onPeerFound(peer);
        return;
    }
    m_peers[row] = peer;
    const QModelIndex idx = index(row);
    emit dataChanged(idx, idx);
}

void DeviceListModel::onPeerLost(const QString &id)
{
    const int row = indexOfId(id);
    if (row < 0)
        return;
    beginRemoveRows(QModelIndex(), row, row);
    m_peers.removeAt(row);
    endRemoveRows();
    emit countChanged();
}
