#include "ProjectEvidenceMetrics.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
QVariantMap thermalFrame(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size()>128*1024)
        return {};
    double minimum=std::numeric_limits<double>::infinity();
    double maximum=-minimum, total=0;
    const int columns=80, rows=60;
    for (int y=0;y<rows;++y) {
        const auto values=file.readLine().trimmed().split(',');
        if (values.size()!=columns)
            return {};
        for (const auto &field:values) {
            bool ok=false;
            const double value=field.toDouble(&ok);
            if (!ok || !std::isfinite(value))
                return {};
            minimum=std::min(minimum,value);
            maximum=std::max(maximum,value);
            total+=value;
        }
    }
    if (!file.readAll().trimmed().isEmpty())
        return {};
    return {{"source","temperature_matrix"},{"unit","°C"},{"minimum",minimum},{"maximum",maximum},
            {"mean",total/(columns*rows)},{"sample_count",columns*rows}};
}

QVariantMap recordingSummary(const QString &path,const QString &catalog) {
    if (!QFileInfo::exists(catalog))
        return {};
    const auto name=QUuid::createUuid().toString();
    QVariantMap result;
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE",name);
        db.setDatabaseName(catalog);
        db.setConnectOptions("QSQLITE_OPEN_READONLY");
        if (db.open()) {
            QSqlQuery recording(db);
            recording.prepare("SELECT id,fs,channels,duration FROM recordings WHERE path=? AND complete=1 AND deleted_at='' LIMIT 1");
            recording.addBindValue(path);
            if (recording.exec() && recording.next()) {
                result={{"source","recording_catalog"},{"unit","FS"},{"sample_rate_hz",recording.value(1)},
                        {"channels",recording.value(2)},{"duration_s",recording.value(3)}};
                QSqlQuery window(db);
                window.prepare("SELECT channel,time,metrics FROM measurements WHERE recording_id=? ORDER BY time DESC,id DESC LIMIT ?");
                window.addBindValue(recording.value(0));
                window.addBindValue(recording.value(2));
                QVariantList levels;
                if (window.exec()) {
                    while (window.next()) {
                        const auto metrics=QJsonDocument::fromJson(window.value(2).toByteArray()).toVariant().toMap();
                        if (metrics.contains("rms") && metrics.contains("peak"))
                            levels.append(QVariantMap{{"channel",window.value(0).toInt()+1},{"time_s",window.value(1)},
                                                       {"rms",metrics.value("rms")},{"peak",metrics.value("peak")}});
                    }
                }
                if (!levels.isEmpty())
                    result["latest_window_levels"]=levels;
            }
        }
    }
    QSqlDatabase::removeDatabase(name);
    return result;
}
}

QVariantMap projectEvidenceMetrics(const QString &kind, const QString &archivedPath,
                                   const QString &originalPath, const QString &recordingCatalog) {
    if (kind=="thermal" && originalPath.endsWith("-celsius.csv",Qt::CaseInsensitive))
        return thermalFrame(archivedPath);
    if (kind=="audio" && originalPath.endsWith(".wav",Qt::CaseInsensitive))
        return recordingSummary(originalPath,recordingCatalog);
    return {};
}
