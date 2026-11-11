#include "FaceAccessService.h"
#include "VisionLiveService.h"
#include "../app/SessionManager.h"
#include "../database/DatabaseManager.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUuid>
#include <cmath>
#include <cstring>
#include <numeric>
#include <utility>

namespace {
constexpr auto ModelId = "SFace-2021dec-rgb128";
constexpr double MatchScore = .45;
constexpr double MatchMargin = .06;
constexpr int StableFrames = 3;
constexpr qint64 IdentityHoldMs = 450;

double overlap(const QRect &a, const QRect &b)
{
    const auto common = a.intersected(b);
    const double intersection = double(common.width()) * common.height();
    const double area = double(a.width()) * a.height() + double(b.width()) * b.height() - intersection;
    return area > 0 ? intersection / area : 0;
}

QString stamp(qint64 ms)
{
    return QDateTime::fromMSecsSinceEpoch(ms).toString(Qt::ISODateWithMs);
}
}

FaceAccessService::FaceAccessService(DatabaseManager *db, SessionManager *session, VisionLiveService *live, QObject *parent)
    : QObject(parent), db_(db), session_(session), live_(live), status_(tr("选择摄像头后开始识别"))
{
    heartbeat_.setInterval(9000);
    connect(&heartbeat_, &QTimer::timeout, this, [this] {
        resource("renew", token_, [this](const QString &token, const QString &error) {
            if (token_.isEmpty() || stopping_ || !token.isEmpty())
                return;
            stop();
            status_ = error;
            emit changed();
        });
    });
    connect(live_, &VisionLiveService::faceObservations, this, &FaceAccessService::observe);
    connect(live_, &VisionLiveService::changed, this, [this] {
        if (!ownsWorker_ || live_->running())
            return;
        if (!stopping_)
            status_ = live_->status();
        ownsWorker_ = false;
        release();
    });
    connect(session_, &SessionManager::sessionEnding, this, &FaceAccessService::stop);
    connect(session_, &SessionManager::sessionChanged, this, &FaceAccessService::refresh);
}

bool FaceAccessService::canManage() const { return session_->can("manage_face_ids"); }

bool FaceAccessService::canEnroll() const
{
    return enrollmentView() == 0;
}

int FaceAccessService::enrollmentView() const
{
    const auto age = QDateTime::currentMSecsSinceEpoch() - enrollmentMs_;
    return ownsWorker_ && !stopping_ && enrollment_.size() == StableFrames && age >= 0 && age <= 750
        ? enrollmentView_ : -1;
}

double FaceAccessService::Person::similarity(const Feature &sample) const
{
    double best = -1;
    for (const auto &feature : features)
        best = qMax(best, std::inner_product(sample.begin(), sample.end(), feature.begin(), 0.0));
    return best;
}

void FaceAccessService::resource(const QString &action, const QString &token,
                                 std::function<void(QString, QString)> completed)
{
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:1828/api/vision/reservation")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(action == "acquire" ? 47000 : 8000);
    const auto body = QJsonDocument(QJsonObject{{"action", action}, {"token", token}}).toJson(QJsonDocument::Compact);
    auto *reply = network_.post(request, body);
    connect(reply, &QNetworkReply::finished, this, [reply, completed = std::move(completed)] {
        const auto result = QJsonDocument::fromJson(reply->readAll()).object();
        const auto error = reply->error() == QNetworkReply::NoError ? QString()
            : result["error"].toString(QObject::tr("无法连接 RK1828 巡检推理服务"));
        reply->deleteLater();
        completed(result["token"].toString(), error);
    });
}

bool FaceAccessService::start(const QString &device)
{
    if (running() || !session_->can("inspect") || device.isEmpty() || live_->running())
        return false;
    if (!live_->faceAccessReady()) {
        status_ = tr("人脸分析模型未完整安装");
        emit changed();
        return false;
    }
    acquiring_ = true;
    cancelled_ = stopping_ = false;
    candidateKey_.clear(); lastKey_.clear();
    consecutive_ = 0; lastSeenMs_ = lastEventMs_ = 0;
    status_ = tr("正在分配 RK1828 计算资源");
    emit changed();
    resource("acquire", {}, [this, device](const QString &token, const QString &error) {
        acquiring_ = false;
        token_ = token;
        if (cancelled_ || !session_->can("inspect") || token.isEmpty()) {
            status_ = cancelled_ ? tr("已停止识别") : error;
            release();
            return;
        }
        if (!live_->startFaceAccess(device)) {
            status_ = live_->status();
            release();
            return;
        }
        ownsWorker_ = true;
        status_ = tr("正在加载人脸分析模型");
        heartbeat_.start();
        emit changed();
    });
    return true;
}

void FaceAccessService::stop()
{
    if (!running())
        return;
    cancelled_ = true;
    if (stopping_)
        return;
    stopping_ = true;
    status_ = tr("已停止识别");
    faces_.clear();
    enrollment_.clear();
    emit observationsChanged();
    if (ownsWorker_) {
        live_->stop();
    } else if (!acquiring_) {
        release();
    }
}

void FaceAccessService::release()
{
    heartbeat_.stop();
    faces_.clear(); enrollment_.clear();
    heldId_.clear(); candidateKey_.clear(); previousBox_ = {};
    lastKey_.clear(); consecutive_ = 0; lastSeenMs_ = 0;
    const auto token = std::exchange(token_, {});
    if (!token.isEmpty())
        resource("release", token, [](QString, QString) {});
    emit observationsChanged();
    emit changed();
}

void FaceAccessService::refresh()
{
    templates_.clear(); people_.clear(); history_.clear(); notice_.clear();
    heldId_.clear(); candidateKey_.clear(); consecutive_ = 0;
    if (session_->can("inspect")) {
        const bool manager = canManage();
        QSqlQuery query(db_->connection());
        if (query.exec("SELECT p.id,p.code,p.name,p.active,p.feature,p.updated_at,l.feature,r.feature,fl.feature,fr.feature "
                       "FROM face_people p LEFT JOIN face_pose_templates l ON l.person_id=p.id AND l.pose=1 "
                       "LEFT JOIN face_pose_templates r ON r.person_id=p.id AND r.pose=2 "
                       "LEFT JOIN face_pose_templates fl ON fl.person_id=p.id AND fl.pose=3 "
                       "LEFT JOIN face_pose_templates fr ON fr.person_id=p.id AND fr.pose=4 "
                       "WHERE p.model='SFace-2021dec-rgb128' ORDER BY p.code")) {
            while (query.next()) {
                Person person{query.value(0).toString(), query.value(1).toString(), query.value(2).toString(), query.value(3).toBool(), {}};
                const auto blob = query.value(4).toByteArray();
                Feature feature{};
                if (blob.size() != sizeof(feature))
                    continue;
                std::memcpy(feature.data(), blob.constData(), sizeof(feature));
                person.features.append(feature);
                for (int column = 6; column < 10; ++column) {
                    const auto side = query.value(column).toByteArray();
                    if (side.size() == sizeof(feature)) {
                        std::memcpy(feature.data(), side.constData(), sizeof(feature));
                        person.features.append(feature);
                        ++person.sideCount;
                    }
                }
                if (manager)
                    people_.append(QVariantMap{{"id",person.id},{"code",person.code},{"name",person.name},
                        {"active",person.active},{"updated",query.value(5)},{"sideCount",person.sideCount}});
                templates_.append(person);
            }
        } else {
            notice_ = tr("人员档案读取失败");
        }
        if (query.exec("SELECT name,code,result,similarity,captured_at FROM face_events ORDER BY id DESC LIMIT 100"))
            while (query.next())
                history_.append(QVariantMap{{"name",query.value(0)},{"code",query.value(1)},{"result",query.value(2)},
                    {"score",query.value(3)},{"time",query.value(4)}});
    }
    emit recordsChanged();
}

bool FaceAccessService::report(const QString &text, bool success)
{
    notice_ = text;
    emit recordsChanged();
    return success;
}

QByteArray FaceAccessService::enrollmentFeature(const QString &id)
{
    Feature feature{};
    for (const auto &sample : enrollment_)
        for (size_t i = 0; i < feature.size(); ++i)
            feature[i] += sample[i];
    const auto length = std::sqrt(std::inner_product(feature.begin(), feature.end(), feature.begin(), 0.0));
    if (length < 1.0) {
        report(tr("采集的人脸不稳定，请重试"));
        return {};
    }
    for (auto &v : feature)
        v /= length;
    for (const auto &person : templates_) {
        const double score = person.similarity(feature);
        if (person.id != id && score >= .58) {
            report(tr("此人脸已登记在 %1 的档案中").arg(person.code));
            return {};
        }
        if (enrollmentView_ > 0 && person.id == id && score < .24) {
            report(tr("侧脸与此档案差异较大，请减小转头角度后重试"));
            return {};
        }
    }
    return QByteArray(reinterpret_cast<const char *>(feature.data()), sizeof(feature));
}

bool FaceAccessService::save(const QString &id, const QString &code, const QString &name, bool active, bool recapture)
{
    if (!canManage())
        return false;
    const auto number = code.trimmed(), title = name.trimmed();
    if (number.isEmpty() || number.size() > 40 || title.isEmpty() || title.size() > 80)
        return report(tr("请填写姓名和人员编号"));
    const bool enroll = id.isEmpty() || recapture;
    if (enroll && !canEnroll())
        return report(tr("录入时请由一人面向摄像头，保持画面清晰"));
    const auto time = stamp(QDateTime::currentMSecsSinceEpoch());
    QByteArray blob;
    if (enroll) {
        blob = enrollmentFeature(id);
        if (blob.isEmpty())
            return false;
    }
    QSqlQuery query(db_->connection());
    if (id.isEmpty()) {
        query.prepare("INSERT INTO face_people(id,code,name,active,model,feature,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?)");
        query.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
    } else {
        query.prepare(enroll ? "UPDATE face_people SET code=?,name=?,active=?,model=?,feature=?,updated_at=? WHERE id=?"
                             : "UPDATE face_people SET code=?,name=?,active=?,updated_at=? WHERE id=?");
    }
    query.addBindValue(number); query.addBindValue(title); query.addBindValue(active);
    if (enroll) { query.addBindValue(QString::fromLatin1(ModelId)); query.addBindValue(blob); }
    if (id.isEmpty()) query.addBindValue(time);
    query.addBindValue(time);
    if (!id.isEmpty()) query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return report(query.lastError().nativeErrorCode() == "2067" ? tr("人员编号已存在") : tr("无法保存人员档案"));
    enrollment_.clear();
    emit observationsChanged();
    refresh();
    return report(tr("人员档案已保存"), true);
}

bool FaceAccessService::saveSide(const QString &id, int view)
{
    if (!canManage() || id.isEmpty() || view < 1 || view > 4)
        return false;
    if (enrollmentView() != view)
        return report(tr("请按所选角度转头，保持画面内只有一人"));
    const auto blob = enrollmentFeature(id);
    if (blob.isEmpty())
        return false;
    const auto time = stamp(QDateTime::currentMSecsSinceEpoch());
    auto db = db_->connection();
    if (!db.transaction())
        return report(tr("无法保存侧脸模板"));
    QSqlQuery query(db);
    query.prepare("INSERT INTO face_pose_templates(person_id,pose,feature,updated_at) VALUES(?,?,?,?) "
                  "ON CONFLICT(person_id,pose) DO UPDATE SET feature=excluded.feature,updated_at=excluded.updated_at");
    query.addBindValue(id); query.addBindValue(view); query.addBindValue(blob); query.addBindValue(time);
    const bool inserted = query.exec();
    query.prepare("UPDATE face_people SET updated_at=? WHERE id=? AND model=?");
    query.addBindValue(time); query.addBindValue(id); query.addBindValue(QString::fromLatin1(ModelId));
    if (!inserted || !query.exec() || query.numRowsAffected() != 1 || !db.commit()) {
        db.rollback();
        return report(tr("无法保存侧脸模板"));
    }
    enrollment_.clear();
    emit observationsChanged();
    refresh();
    return report(tr("侧脸模板已保存"), true);
}

bool FaceAccessService::remove(const QString &id)
{
    if (!canManage() || id.isEmpty())
        return false;
    QSqlQuery query(db_->connection());
    query.prepare("DELETE FROM face_people WHERE id=?");
    query.addBindValue(id);
    if (!query.exec() || query.numRowsAffected() != 1)
        return report(tr("无法删除人员档案"));
    refresh();
    return report(tr("人员档案已删除"), true);
}

void FaceAccessService::record(const Person *person, double score, qint64 capturedMs)
{
    const auto name = person ? person->name : tr("未识别");
    const auto code = person ? person->code : QStringLiteral("");
    const auto result = person ? QStringLiteral("match") : QStringLiteral("unknown");
    const auto time = stamp(capturedMs);
    QSqlQuery query(db_->connection());
    query.prepare("INSERT INTO face_events(person_id,code,name,result,similarity,captured_at) VALUES(?,?,?,?,?,?)");
    query.addBindValue(person ? QVariant(person->id) : QVariant());
    query.addBindValue(code); query.addBindValue(name); query.addBindValue(result); query.addBindValue(score); query.addBindValue(time);
    if (!query.exec()) {
        report(tr("识别记录写入失败"));
        return;
    }
    history_.prepend(QVariantMap{{"name",name},{"code",code},{"result",result},{"score",score},{"time",time}});
    if (history_.size() > 100)
        history_.removeLast();
    emit recordsChanged();
}

void FaceAccessService::observe(const QVariantList &rows, qint64 capturedMs)
{
    if (!ownsWorker_ || stopping_)
        return;
    if (status_ != tr("正在识别")) {
        status_ = tr("正在识别");
        emit changed();
    }
    const Person *winner = nullptr;
    double confidence = 0;
    faces_.clear();
    QStringList labels;
    bool usable = false, enrollmentValid = false;
    if (rows.size() != 1) {
        heldId_.clear();
        previousBox_ = {};
    }
    for (const auto &value : rows) {
        auto row = value.toMap();
        const auto blob = row.take("embedding").toByteArray();
        row.remove("points");
        row.remove("headOrigin");
        row.remove("gazeOrigin");
        const auto box = row.take("box").toRect();
        const double yaw = row.take("rawHeadYaw").toDouble();
        const double pitch = row.take("rawHeadPitch").toDouble();
        labels.append(tr("未识别"));
        row["name"] = labels.last();
        const double detectionScore = row["score"].toDouble();
        const bool continuous = rows.size() == 1 && overlap(previousBox_, box) >= .55;
        if (!continuous || capturedMs < heldMs_ || capturedMs - heldMs_ > IdentityHoldMs)
            heldId_.clear();
        const auto display = [&](const Person &person, double score, bool tracking) {
            labels.last() = person.name;
            row["name"] = person.name;
            row["code"] = person.code;
            row["similarity"] = score;
            row["identityHeld"] = tracking;
        };
        if (rows.size() == 1 && capturedMs > 0 && blob.size() == sizeof(Feature) && detectionScore >= .70) {
            Feature feature{};
            std::memcpy(feature.data(), blob.constData(), sizeof(feature));
            double first = -1, second = -1;
            const Person *held = nullptr;
            for (const auto &person : templates_) {
                if (!person.active)
                    continue;
                if (person.id == heldId_)
                    held = &person;
                const auto score = person.similarity(feature);
                if (score > first) { second = first; first = score; winner = &person; }
                else if (score > second) second = score;
            }
            confidence = qMax(0.0, first);
            if (first < MatchScore || first - second < MatchMargin)
                winner = nullptr;
            usable = true;
            int view = -1;
            if (std::abs(pitch) <= 28) {
                const int side = yaw > 0 ? 1 : 2;
                if (row["pointCount"].toInt() >= 400) {
                    if (std::abs(yaw) <= 24)
                        view = 0;
                    else if (std::abs(yaw) >= 29 && std::abs(yaw) <= 55)
                        view = side;
                }
                if (std::abs(yaw) >= 58 && std::abs(yaw) <= 88 && detectionScore >= .80)
                    view = side + 2;
            }
            enrollmentValid = view >= 0;
            if (enrollmentValid) {
                if (enrollment_.isEmpty() || !continuous || view != enrollmentView_ || capturedMs - enrollmentMs_ > 500 ||
                    std::inner_product(feature.begin(), feature.end(), enrollment_.constLast().begin(), 0.0) < .68)
                    enrollment_.clear();
                enrollment_.append(feature);
                if (enrollment_.size() > StableFrames) enrollment_.removeFirst();
                enrollmentMs_ = capturedMs;
                enrollmentView_ = view;
            }
            const auto key = winner ? winner->id : QStringLiteral("unknown");
            consecutive_ = continuous && key == candidateKey_ && capturedMs - lastSeenMs_ < 750
                ? qMin(consecutive_ + 1, StableFrames) : 1;
            candidateKey_ = key;
            if (capturedMs - lastSeenMs_ > 1200)
                lastKey_.clear();
            lastSeenMs_ = capturedMs;
            if (winner && winner != held)
                heldId_.clear();
            const Person *displayed = nullptr;
            double displayedScore = confidence;
            if (winner && (consecutive_ >= StableFrames || winner == held)) {
                heldId_ = winner->id;
                heldFeature_ = feature;
                heldMs_ = capturedMs;
                heldScore_ = confidence;
                displayed = winner;
            } else if (!winner && held &&
                       std::inner_product(feature.begin(), feature.end(), heldFeature_.begin(), 0.0) >= .68) {
                displayedScore = held->similarity(feature);
                if (displayedScore >= .35 && first - displayedScore < .04)
                    displayed = held;
            }
            if (displayed)
                display(*displayed, displayedScore, displayed != winner);
            if (consecutive_ >= StableFrames && (winner || !displayed) &&
                key != lastKey_ && capturedMs - lastEventMs_ >= 1500) {
                record(winner, confidence, capturedMs);
                lastKey_ = key;
                lastEventMs_ = capturedMs;
            }
        } else if (rows.size() == 1) {
            if (!heldId_.isEmpty() && detectionScore >= .75 && capturedMs - heldMs_ <= 180) {
                for (const auto &person : templates_)
                    if (person.id == heldId_ && person.active) {
                        display(person, heldScore_, true);
                        break;
                    }
            } else {
                heldId_.clear();
            }
        }
        if (rows.size() == 1)
            previousBox_ = box;
        row["time"] = stamp(capturedMs);
        faces_.append(row);
    }
    if (!usable) consecutive_ = 0;
    if (!enrollmentValid) enrollment_.clear();
    live_->setFaceLabels(labels);
    emit observationsChanged();
}
