#include "TaskListModel.h"
#include "../services/InspectionService.h"

TaskListModel::TaskListModel(QObject *parent) : QAbstractListModel(parent) {}

QHash<int, QByteArray> TaskListModel::roleNames() const {
    return {{CodeRole, "code"}, {NameRole, "name"}, {StatusRole, "status"},
            {StatusTextRole, "statusText"}, {ProgressTextRole, "progressText"}, {PlannedAtRole, "plannedAt"}};
}

int TaskListModel::rowCount(const QModelIndex &) const { return tasks_.size(); }

QVariant TaskListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= tasks_.size()) return QVariant();
    const InspectionTask &t = tasks_.at(index.row());
    switch (role) {
    case CodeRole: return t.code;
    case NameRole: return t.name;
    case StatusRole: return t.status;
    case StatusTextRole: return InspectionService::statusText(t.status);
    case ProgressTextRole: return t.progressText;
    case PlannedAtRole: return t.plannedAt;
    default: return QVariant();
    }
}

void TaskListModel::setTasks(const QVector<InspectionTask> &tasks) {
    beginResetModel();
    tasks_ = tasks;
    endResetModel();
}
