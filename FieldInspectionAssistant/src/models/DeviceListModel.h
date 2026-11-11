#pragma once

#include <QtCore>
#include <QAbstractListModel>
#include "../models/InspectionModels.h"

class DeviceListModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { CodeRole = Qt::UserRole + 1, NameRole, LocationRole, ModelRole };
    explicit DeviceListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setAssets(const QVector<InspectionAsset> &assets);
    QVector<InspectionAsset> assets() const { return assets_; }

private:
    QVector<InspectionAsset> assets_;
};
