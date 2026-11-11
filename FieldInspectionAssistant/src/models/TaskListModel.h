#pragma once

#include <QtCore>
#include <QAbstractListModel>
#include "../models/InspectionModels.h"

class TaskListModel final : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { CodeRole = Qt::UserRole + 1, NameRole, StatusRole, StatusTextRole, ProgressTextRole, PlannedAtRole };
    explicit TaskListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setTasks(const QVector<InspectionTask> &tasks);
    QVector<InspectionTask> tasks() const { return tasks_; }

private:
    QVector<InspectionTask> tasks_;
};
