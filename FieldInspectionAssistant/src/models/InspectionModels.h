#pragma once

#include <QtCore>

// ---- 巡检核心业务数据结构 ----
struct InspectionRecord {
    QString id;
    QString time;          // created_at
    QString templateName;
    QString result;        // 正常 / 异常 / 已提交 / 草稿
    QString answer;        // 结论说明
    QString imagePath;
    QString taskId;
    QString assetId;
    QString assetName;
    QString taskCode;
    QString status;        // draft / submitted
};

struct InspectionAsset {
    QString id;
    QString code;
    QString name;
    QString location;
    QString model;
    QString createdAt;
};

struct InspectionTask {
    QString id;
    QString code;
    QString name;
    QString status;        // pending / in_progress / completed / overdue
    QString assetId;
    QString plannedAt;
    QString createdAt;
    QString assetName;
    QString progressText;  // 5/8
};

struct InspectionDefect {
    QString id;
    QString code;
    QString recordId;
    QString checkItemKey;
    QString assetId;
    QString title;
    QString description;
    QString status;        // unassigned / pending / in_progress / review / closed
    QString priority;      // high / medium / low
    QString assigneeId;
    QString assigneeName;
    QString dueAt;
    QString createdAt;
    QString closedAt;
};

struct CheckItemDef {
    QString key;
    QString name;
    QString type;          // select / measure / skip
    QString unit;
    QString options;       // 正常|异常
    double minValue = 0;
    double maxValue = 0;
    bool hasThreshold = false;
    QString thresholdText; // "≤ 70 °C"
};

struct CheckItemResult {
    QString key;
    QString name;
    QString type;
    QString unit;
    QString result;        // 正常 / 异常 / 跳过
    QString value;         // 测量值文本
    QString thresholdText;
    bool abnormal = false;
    QString note;
    QString imagePath;
};

struct DefectEvent {
    QString id;
    QString eventType;     // create / assign / start / feedback / review / close / reopen
    QString status;
    QString note;
    QString actorName;
    QString createdAt;
};

struct AttachmentItem {
    QString id;
    QString recordId;
    QString defectId;
    QString path;
    QString sha256;
    QString kind;          // evidence / other
    QString createdAt;
};

struct UserInfo {
    QString id;
    QString name;
    QString role;          // admin / supervisor / inspector
    bool active = true;
    QString createdAt;
};

struct TemplateInfo {
    QString id;
    QString name;
    int version = 1;
    QString definitionJson;
    QString createdAt;
};

struct SyncItem {
    QString id;
    QString entity;
    QString entityId;
    QString operation;
    QString payloadJson;
    QString status;
    int attempts = 0;
    QString updatedAt;
};
