#include "DefectListModel.h"
#include "../services/DefectService.h"

DefectListModel::DefectListModel(QObject *parent) : QAbstractListModel(parent) {}

QHash<int, QByteArray> DefectListModel::roleNames() const {
    return {{CodeRole, "code"}, {TitleRole, "title"}, {StatusRole, "status"}, {StatusTextRole, "statusText"},
            {PriorityRole, "priority"}, {PriorityTextRole, "priorityText"},
            {AssigneeNameRole, "assigneeName"}, {DueAtRole, "dueAt"}, {CreatedAtRole, "createdAt"}};
}

int DefectListModel::rowCount(const QModelIndex &) const { return defects_.size(); }

QVariant DefectListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= defects_.size()) return QVariant();
    const InspectionDefect &d = defects_.at(index.row());
    switch (role) {
    case CodeRole: return d.code;
    case TitleRole: return d.title;
    case StatusRole: return d.status;
    case StatusTextRole: return DefectService::statusText(d.status);
    case PriorityRole: return d.priority;
    case PriorityTextRole: return DefectService::priorityText(d.priority);
    case AssigneeNameRole: return d.assigneeName;
    case DueAtRole: return d.dueAt;
    case CreatedAtRole: return d.createdAt;
    default: return QVariant();
    }
}

void DefectListModel::setDefects(const QVector<InspectionDefect> &defects) {
    beginResetModel();
    defects_ = defects;
    endResetModel();
}
