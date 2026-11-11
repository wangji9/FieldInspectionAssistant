#include "WaveCsvExport.h"
#include <QtEndian>
#include <cmath>
#include <cstring>
namespace {
QVariantMap failure(const QString&s){return {{"ok",false},{"error",s}};}
quint16 u16(const QByteArray&b,int p){return qFromLittleEndian<quint16>(b.constData()+p);}
quint32 u32(const QByteArray&b,int p){return qFromLittleEndian<quint32>(b.constData()+p);}
double sample(const char*p,int bits,int encoding){
    if(encoding==3){if(bits==32){quint32 raw=qFromLittleEndian<quint32>(p);float v;std::memcpy(&v,&raw,4);return v;}quint64 raw=qFromLittleEndian<quint64>(p);double v;std::memcpy(&v,&raw,8);return v;}
    if(bits==8)return (quint8(*p)-128)/128.;
    if(bits==16)return qFromLittleEndian<qint16>(p)/32768.;
    if(bits==24){qint32 v=quint8(p[0])|(quint32(quint8(p[1]))<<8)|(quint32(quint8(p[2]))<<16);if(v&0x800000)v-=0x1000000;return v/8388608.;}
    return qFromLittleEndian<qint32>(p)/2147483648.;
}
}
QVariantMap exportWaveCsv(const QString&source,const QString&target,const QString&scope,double seconds,qint64 pinned,const QVariantMap&origin,const std::function<bool()>&cancelled){
    if(!std::isfinite(seconds)||seconds<=0)return failure("窗口长度无效");
    if(scope!="full"&&scope!="window")return failure("请选择整段录音或当前窗口");
    if(!QFileInfo(target).isAbsolute()||QFileInfo(source).absoluteFilePath()==QFileInfo(target).absoluteFilePath())return failure("保存路径无效，不能覆盖原始录音");
    QFile input(source);if(!input.open(QIODevice::ReadOnly))return failure("无法读取完整原始录音："+input.errorString());
    const auto header=input.read(12);if(header.size()!=12||header.left(4)!="RIFF"||header.mid(8,4)!="WAVE")return failure("只支持标准小端 WAV，未使用缓冲数据替代");
    int encoding=0,channels=0,rate=0,bits=0,align=0;qint64 offset=-1,declared=-1;
    while(input.pos()+8<=input.size()){
        auto chunk=input.read(8);auto size=u32(chunk,4);qint64 start=input.pos();
        if(chunk.left(4)=="fmt "){
            if(size<16||size>4096||start+size>input.size())return failure("WAV 格式头损坏");auto fmt=input.read(size);
            encoding=u16(fmt,0);channels=u16(fmt,2);rate=u32(fmt,4);align=u16(fmt,12);bits=u16(fmt,14);
            if(encoding==0xfffe){if(fmt.size()<40)return failure("WAV 扩展头不完整");encoding=u16(fmt,24);}
        }else if(chunk.left(4)=="data"){offset=start;declared=size;break;}
        if(!input.seek(start+size+(size&1)))return failure("WAV 数据块损坏");
    }
    if(offset<0||channels<1||channels>64||rate<4000||rate>384000||align!=channels*(bits/8)||!((encoding==1&&(bits==8||bits==16||bits==24||bits==32))||(encoding==3&&(bits==32||bits==64))))return failure("WAV 编码或通道信息不支持");
    qint64 available=(input.size()-offset)/align;
    qint64 total=pinned>=0?pinned:declared/align;
    if(total<=0)return failure("尚无完整采样数据");
    if(total>available||(pinned<0&&declared%align))return failure("原始录音尚未完整落盘或已损坏，请稍后重试");
    qint64 first=scope=="full"?0:qMax<qint64>(0,total-qMax<qint64>(1,qRound64(seconds*rate)));
    if(!input.seek(offset+first*align))return failure("无法定位导出范围");
    QSaveFile output(target);if(!output.open(QIODevice::WriteOnly))return failure(output.errorString());
    QTextStream text(&output);text.setLocale(QLocale::c());text.setEncoding(QStringConverter::Utf8);text.setRealNumberPrecision(12);text<<QChar(0xFEFF)<<"time_s";for(int c=0;c<channels;c++)text<<",channel_"<<c+1<<"_FS";text<<'\n';
    const qint64 blockFrames=qMax(1,65536/align);
    for(qint64 frame=first;frame<total;){
        if(cancelled()){output.cancelWriting();return failure("导出已取消");}
        qint64 count=qMin(blockFrames,total-frame);auto block=input.read(count*align);if(block.size()!=count*align){output.cancelWriting();return failure("原始录音读取不完整，已取消导出");}
        for(qint64 i=0;i<count;i++){text<<double(frame+i)/rate;for(int c=0;c<channels;c++){double v=sample(block.constData()+i*align+c*(bits/8),bits,encoding);if(!std::isfinite(v)){output.cancelWriting();return failure("录音中存在非有限采样值");}text<<','<<v;}text<<'\n';}
        frame+=count;if(text.status()!=QTextStream::Ok){output.cancelWriting();return failure("CSV 写入失败，请检查磁盘空间");}
    }
    text.flush();if(cancelled()){output.cancelWriting();return failure("导出已取消");}
    if(text.status()!=QTextStream::Ok||!output.commit())return failure("CSV 保存失败："+output.errorString());
    QVariantMap result=origin;result["format"]="fieldops-raw-csv/1";result["ok"]=true;result["path"]=target;result["scope"]=scope;result["source_path"]=source;result["sample_rate"]=rate;result["channels"]=channels;result["unit"]="FS";result["calibration_applied"]=false;result["start_frame"]=first;result["end_frame_exclusive"]=total;result["rows"]=total-first;result["start_seconds"]=double(first)/rate;result["end_seconds_exclusive"]=double(total)/rate;result["source_total_frames"]=total;result["created_at"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSaveFile metadata(target+".meta.json");auto bytes=QJsonDocument::fromVariant(result).toJson();bool metadataOk=metadata.open(QIODevice::WriteOnly)&&metadata.write(bytes)==bytes.size()&&metadata.commit();result["metadata_written"]=metadataOk;if(!metadataOk)result["warning"]="CSV 已保存，范围说明文件写入失败";
    return result;
}
