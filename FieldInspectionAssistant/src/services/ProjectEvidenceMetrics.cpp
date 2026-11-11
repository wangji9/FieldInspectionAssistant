#include "ProjectEvidenceMetrics.h"

#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QtEndian>
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

QVariantMap imageSummary(const QString &path) {
    QImageReader reader(path);
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 50000000)
        return {};
    reader.setScaledSize(size.scaled(192, 192, Qt::KeepAspectRatio));
    const auto image = reader.read().convertToFormat(QImage::Format_Grayscale8);
    if (image.isNull())
        return {};
    qint64 sum = 0, dark = 0, bright = 0;
    for (int y = 0; y < image.height(); ++y) {
        const auto *line = image.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            sum += line[x];
            dark += line[x] < 32;
            bright += line[x] > 223;
        }
    }
    const double count = image.width() * image.height();
    return {{"source","image_pixels"},{"width",size.width()},{"height",size.height()},
            {"mean_luma_255",sum/count},{"dark_fraction",dark/count},{"bright_fraction",bright/count}};
}

QVariantMap wavSummary(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto header = file.read(12);
    if (header.size() != 12 || !header.startsWith("RIFF") || header.mid(8) != "WAVE")
        return {};
    int rate = 0, channels = 0, bits = 0, encoding = 0;
    while (file.pos() + 8 <= file.size()) {
        const auto chunk = file.read(8);
        const auto length = qFromLittleEndian<quint32>(chunk.constData()+4);
        const auto next = file.pos() + length + (length & 1);
        if (file.pos() + length > file.size())
            break;
        if (chunk.startsWith("fmt ") && length >= 16) {
            const auto fmt = file.read(16);
            encoding = qFromLittleEndian<quint16>(fmt.constData());
            channels = qFromLittleEndian<quint16>(fmt.constData()+2);
            rate = qFromLittleEndian<quint32>(fmt.constData()+4);
            bits = qFromLittleEndian<quint16>(fmt.constData()+14);
        } else if (chunk.startsWith("data") && encoding == 1 && channels > 0 && channels <= 32 &&
                   rate >= 1000 && rate <= 384000 && bits == 16 && length >= 2u*channels) {
            const auto limit = qMin<qint64>(length, qMin<qint64>(8*1024*1024, qint64(rate)*channels*2*20));
            double squares = 0, peak = 0;
            const auto samples = file.read(limit - limit % (2*channels));
            const auto count = samples.size()/2;
            if (!count)
                return {};
            for (qsizetype i = 0; i < count; ++i) {
                const double sample = qFromLittleEndian<qint16>(samples.constData()+2*i)/32768.0;
                squares += sample*sample;
                peak = qMax(peak, std::abs(sample));
            }
            return {{"source","wav_pcm16"},{"unit","FS"},{"sample_rate_hz",rate},{"channels",channels},
                    {"duration_s",length/(2.0*channels*rate)},{"window_s",count/(1.0*channels*rate)},
                    {"rms",std::sqrt(squares/count)},{"peak",peak}};
        }
        if (!file.seek(next))
            break;
    }
    return {};
}

QVariantMap signalSummary(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const auto fields = file.readLine(4096).trimmed().split(',');
    if (fields.size() < 2 || (fields[0] != "time_s" && fields[0] != "time_ms"))
        return {};
    const double scale = fields[0] == "time_ms" ? 0.001 : 1.0;
    double first = 0, last = 0, sum = 0, squares = 0, peak = 0;
    int count = 0;
    while (!file.atEnd() && count < 32768) {
        const auto line = file.readLine(4096).trimmed();
        if (line.isEmpty())
            continue;
        const auto values = line.split(',');
        bool validTime = false, validValue = false;
        const double t = values.value(0).toDouble(&validTime)*scale;
        const double value = values.value(1).toDouble(&validValue);
        if (!validTime || !validValue || !std::isfinite(t) || !std::isfinite(value) || std::abs(value)>1e12 ||
            (count && t <= last))
            return {};
        if (!count)
            first = t;
        last = t;
        sum += value;
        squares += value*value;
        peak = qMax(peak, std::abs(value));
        ++count;
    }
    if (count < 2)
        return {};
    return {{"source","timed_csv"},{"channel",QString::fromUtf8(fields[1]).left(80)},
            {"start_s",first},{"window_s",last-first},{"sample_count",count},{"sample_rate_hz",(count-1)/(last-first)},
            {"mean",sum/count},{"rms",std::sqrt(squares/count)},{"peak",peak},{"truncated",!file.atEnd()}};
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
    if (kind=="audio" && originalPath.endsWith(".wav",Qt::CaseInsensitive)) {
        const auto summary = recordingSummary(originalPath, recordingCatalog);
        return summary.isEmpty() ? wavSummary(archivedPath) : summary;
    }
    if (kind=="image" || kind=="thermal")
        return imageSummary(archivedPath);
    if (kind=="signal" && originalPath.endsWith(".csv",Qt::CaseInsensitive))
        return signalSummary(archivedPath);
    return {};
}
