#include "DeviceListModel.h"

DeviceListModel::DeviceListModel(QObject *parent) : QAbstractListModel(parent) {}

QHash<int, QByteArray> DeviceListModel::roleNames() const {
    return {{CodeRole, "code"}, {NameRole, "name"}, {LocationRole, "location"}, {ModelRole, "model"}};
}

int DeviceListModel::rowCount(const QModelIndex &) const { return assets_.size(); }

QVariant DeviceListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= assets_.size()) return QVariant();
    const InspectionAsset &a = assets_.at(index.row());
    switch (role) {
    case CodeRole: return a.code;
    case NameRole: return a.name;
    case LocationRole: return a.location;
    case ModelRole: return a.model;
    default: return QVariant();
    }
}

void DeviceListModel::setAssets(const QVector<InspectionAsset> &assets) {
    beginResetModel();
    assets_ = assets;
    endResetModel();
}
