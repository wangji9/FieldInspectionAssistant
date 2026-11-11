#include "ConversationExportService.h"
#include "AiInferenceService.h"
#include <QTextDocument>

namespace {
QString label(const QString &mode) { return mode=="voice" ? QStringLiteral("语音助手") : mode=="archive" ? QStringLiteral("巡检档案") : mode=="vision" ? QStringLiteral("图片问答") : mode=="asr" ? QStringLiteral("语音转写") : QStringLiteral("文字问答"); }
QVariantMap failure(const QString &message) { return {{"ok",false},{"error",message}}; }
bool writeFile(const QString &path,const QByteArray &data,QString *error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data)!=data.size() || !file.commit()) { *error=file.errorString();return false; }
    return true;
}
QString localPath(const QUrl &url) { return url.isLocalFile() ? url.toLocalFile() : url.scheme().isEmpty() ? url.toString() : QString(); }
}

QUrl ConversationExportService::defaultFile(const QString &mode,const QString &format) const {
    auto folder=QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (!QDir(folder).exists()) folder=QDir::homePath();
    return QUrl::fromLocalFile(QDir(folder).filePath(label(mode)+"_"+QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")+"."+format));
}

QVariantMap ConversationExportService::saveAudio(const QUrl &url,const QString &source) {
    auto destination=localPath(url);
    if(destination.isEmpty() || !QFileInfo(source).isFile()) return failure(QStringLiteral("请选择保存位置和有效音频"));
    if(QFileInfo(destination).suffix().isEmpty()) destination+=".wav";
    if(QFileInfo(destination).suffix().toLower()!="wav")return failure(QStringLiteral("语音条请保存为 .wav 文件"));
    QTemporaryDir staging;
    const auto wav=staging.filePath("audio.wav");
    QProcess process;
    process.start("ffmpeg",{"-nostdin","-y","-i",source,"-vn","-c:a","pcm_s16le",wav});
    if(!process.waitForFinished(60000) || process.exitCode()!=0) { process.kill();process.waitForFinished();return failure(QStringLiteral("音频转换失败")); }
    QFile file(wav);file.open(QIODevice::ReadOnly);QString error;
    if(!writeFile(destination,file.readAll(),&error))return failure(error);
    return {{"ok",true},{"path",destination}};
}

QVariantMap ConversationExportService::save(const QUrl &url,const QString &mode,const QVariantList &messages,const QString &format) {
    QString destination=localPath(url),error;
    if(destination.isEmpty() || !QFileInfo(destination).isAbsolute())return failure(QStringLiteral("请选择本地保存位置"));
    if(messages.isEmpty())return failure(QStringLiteral("当前没有可导出的对话"));
    if(!QStringList{"md","txt","html","json","zip"}.contains(format))return failure(QStringLiteral("不支持的导出格式"));
    if(QFileInfo(destination).suffix().isEmpty())destination+="."+format;
    if(QFileInfo(destination).suffix().toLower()!=format)return failure(QStringLiteral("文件扩展名应为 .%1，请重新选择格式或文件名").arg(format));
    QTemporaryDir staging;
    if(!staging.isValid())return failure(QStringLiteral("无法创建导出临时目录"));
    QDir(staging.path()).mkdir("media");
    QVariantList exported;
    QString markdown="# "+label(mode)+"\n\n",plain=label(mode)+"\n\n";
    int mediaIndex=0;
    for(const auto &value:messages) {
        auto row=value.toMap();
        auto role=row.value("role").toString()=="user" ? QStringLiteral("我") : QStringLiteral("巡检助手");
        auto text=row.value("text").toString();
        markdown+="## "+role+"\n\n"+text+"\n\n";
        QTextDocument doc;doc.setMarkdown(text);plain+=role+"\n"+doc.toPlainText()+"\n\n";
        const auto systemSources=row.value("systemEvidence").toMap().value("sources").toList();if(!systemSources.isEmpty()){markdown+="系统依据：\n\n";plain+="系统依据：\n";for(auto v:systemSources){auto source=v.toMap();QString line="["+source["citation"].toString()+"] "+source["title"].toString()+" · "+source["at"].toString();markdown+="- "+line.toHtmlEscaped()+"\n";plain+=line+"\n";}markdown+="\n";plain+="\n";}
        for(const auto &key:{QString("imagePath"),QString("audioPath")}) {
            auto source=row.value(key).toString();if(source.isEmpty())continue;
            if(!QFileInfo(source).isFile())return failure(QStringLiteral("附件不存在：%1").arg(source));
            const bool audio=key=="audioPath";
            const auto relative=QString("media/%1.%2").arg(++mediaIndex,3,10,QChar('0')).arg(audio?"wav":QFileInfo(source).suffix());
            const auto target=staging.filePath(relative);
            if(audio) {
                auto result=saveAudio(QUrl::fromLocalFile(target),source);
                if(!result.value("ok").toBool())return result;
            } else if(!QFile::copy(source,target))return failure(QStringLiteral("无法复制现场图片"));
            row[key]=relative;
            markdown+=(audio ? QStringLiteral("[语音音频](%1)\n\n") : QStringLiteral("![现场图片](%1)\n\n")).arg(relative);
        }
        exported.append(row);
    }
    auto metadata=QJsonObject{{"type",mode},{"exported_at",QDateTime::currentDateTime().toString(Qt::ISODate)},{"messages",QJsonArray::fromVariantList(exported)}};
    const auto json=QJsonDocument(metadata).toJson(QJsonDocument::Indented);
    auto html=ai_->renderMarkdown(markdown,960);
    if(format=="zip") {
        if(!writeFile(staging.filePath("conversation.md"),markdown.toUtf8(),&error)
            || !writeFile(staging.filePath("conversation.txt"),plain.toUtf8(),&error)
            || !writeFile(staging.filePath("conversation.html"),html.toUtf8(),&error)
            || !writeFile(staging.filePath("conversation.json"),json,&error))return failure(error);
        QTemporaryDir archive;
        const auto zip=archive.filePath("conversation.zip");
        QProcess process;
        process.start("/usr/bin/python3",{"-c","import sys,pathlib,zipfile;root=pathlib.Path(sys.argv[1]);z=zipfile.ZipFile(sys.argv[2],'w',zipfile.ZIP_DEFLATED);[(z.write(p,p.relative_to(root))) for p in root.rglob('*') if p.is_file()];z.close()",staging.path(),zip});
        if(!process.waitForFinished(60000) || process.exitCode()!=0) { process.kill();process.waitForFinished();return failure(QStringLiteral("创建对话压缩包失败")); }
        QFile file(zip);file.open(QIODevice::ReadOnly);
        if(!writeFile(destination,file.readAll(),&error))return failure(error);
    } else {
        // Standalone documents keep attachments beside the document, using a
        // unique folder so a later export cannot overwrite earlier media.
        if(mediaIndex) {
            auto folder=QFileInfo(destination).completeBaseName()+"_media_"+QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
            auto parent=QFileInfo(destination).absoluteDir();
            if(!parent.mkdir(folder))return failure(QStringLiteral("无法创建附件保存目录"));
            for(const auto &file:QDir(staging.filePath("media")).entryList(QDir::Files))
                if(!QFile::copy(staging.filePath("media/"+file),parent.filePath(folder+"/"+file)))return failure(QStringLiteral("保存附件失败"));
            markdown.replace("](media/", "]("+folder+"/");
            auto raw=QJsonDocument(metadata).toJson();raw.replace("media/",(folder+"/").toUtf8());
            metadata=QJsonDocument::fromJson(raw).object();html=ai_->renderMarkdown(markdown,960);
        }
        auto bytes=format=="md"?markdown.toUtf8():format=="txt"?plain.toUtf8():format=="html"?html.toUtf8():QJsonDocument(metadata).toJson(QJsonDocument::Indented);
        if(!writeFile(destination,bytes,&error))return failure(error);
    }
    return {{"ok",true},{"path",destination}};
}
