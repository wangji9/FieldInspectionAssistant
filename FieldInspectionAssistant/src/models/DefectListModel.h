#pragma once

#include <QtCore>
#include <QAbstractListModel>
#include "../models/InspectionModels.h"

class DefectListModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { CodeRole = Qt::UserRole + 1, TitleRole, StatusRole, StatusTextRole,
                 PriorityRole, PriorityTextRole, AssigneeNameRole, DueAtRole, CreatedAtRole };
    explicit DefectListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDefects(const QVector<InspectionDefect> &defects);
    QVector<InspectionDefect> defects() const { return defects_; }

private:
    QVector<InspectionDefect> defects_;
};
