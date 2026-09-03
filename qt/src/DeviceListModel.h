#pragma once

#include <QAbstractListModel>
#include <QList>
#include "Discovery.h"

// Modelo de lista de dispositivos descubiertos, para la vista QML.
class DeviceListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCountProp NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PlatformRole,
        DeviceTypeRole,
        AddressRole,
        PortRole,
        AvatarRole,
        AvatarThumbRole,
    };
    Q_ENUM(Roles)

    explicit DeviceListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int rowCountProp() const { return m_peers.size(); }

public slots:
    void onPeerFound(const Peer &peer);
    void onPeerUpdated(const Peer &peer);
    void onPeerLost(const QString &id);

signals:
    void countChanged();

private:
    int indexOfId(const QString &id) const;
    QList<Peer> m_peers;
};
