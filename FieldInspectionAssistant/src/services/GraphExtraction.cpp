#include "GraphExtraction.h"
namespace {
QString clean(const QVariant &v){return v.toString().normalized(QString::NormalizationForm_KC).trimmed();}
QString quoteIn(const QString &source,const QString &quote){
    if(quote.isEmpty())return {};if(source.contains(quote))return quote;
    QString flat,needle;QVector<int> pos;for(int i=0;i<source.size();++i)if(!source[i].isSpace()){flat+=source[i];pos<<i;}
    for(auto c:quote)if(!c.isSpace())needle+=c;
    int at=needle.isEmpty()?-1:flat.indexOf(needle);return at<0?QString():source.mid(pos[at],pos[at+needle.size()-1]-pos[at]+1);
}
bool named(const QString &value,int max){return !value.isEmpty()&&value.size()<=max;}
QString sentenceEvidence(const QString &source,const QString &from,const QString &to){
    QString match;const auto sentences=source.split(QRegularExpression("(?<=[。！？!?；;\\n])"),Qt::SkipEmptyParts);
    for(auto sentence:sentences){sentence=sentence.trimmed();if(sentence.size()>700)continue;if(!quoteIn(sentence,from).isEmpty()&&!quoteIn(sentence,to).isEmpty()){if(!match.isEmpty())return {};match=sentence;}}
    return match;
}
}
QVariantMap GraphExtraction::parse(const QString &answer){
    const int start=answer.indexOf('{');if(start<0)return {{"ok",false},{"error","模型没有返回 JSON"}};
    auto data=answer.mid(start).trimmed();auto fence=data.indexOf("```");if(fence>=0)data=data.left(fence).trimmed();
    auto read=[](const QString&s){return QJsonDocument::fromJson(s.toUtf8());};auto parsed=read(data);bool repaired=false;
    if(!parsed.isObject()){
        QVector<QChar> brackets;bool quoted=false,escaped=false,invalid=false;QString fixed;
        for(auto c:data){
            if(quoted){fixed+=c;if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
            if(c=='"'){quoted=true;fixed+=c;continue;}
            if(c=='{'||c=='[')brackets<<c;
            else if(c=='}'||c==']'){
                const auto expected=c=='}'?QChar('{'):QChar('[');
                if(!brackets.contains(expected)){invalid=true;break;}
                while(!brackets.isEmpty()&&brackets.last()!=expected)fixed+=brackets.takeLast()=='{'?'}':']';
                brackets.removeLast();
            }
            fixed+=c;if(brackets.isEmpty()&&!quoted)break;
        }
        if(!invalid&&!quoted){while(!brackets.isEmpty())fixed+=brackets.takeLast()=='{'?'}':']';parsed=read(fixed);repaired=parsed.isObject();}
    }
    if(!parsed.isObject())return {{"ok",false},{"error","模型结果格式不完整，请缩短资料重试"}};
    return {{"ok",true},{"output",parsed.object().toVariantMap()},{"repaired",repaired}};
}
QVariantMap GraphExtraction::extend(const QVariantMap &base,const QVariantList &relations){
    auto o=base;auto types=o["entity_types"].toList(),rules=o["relation_types"].toList();
    for(auto v:relations){auto r=v.toMap();for(auto type:{r["from_type"],r["to_type"]})if(!types.contains(type))types<<type;
        QVariantMap rule{{"name",r["predicate"]},{"from",r["from_type"]},{"to",r["to_type"]}};if(!rules.contains(rule))rules<<rule;}
    o["entity_types"]=types;o["relation_types"]=rules;return o;
}
QVariantMap GraphExtraction::validate(const QVariantMap &obj,const QString &source,const QVariantMap &base,bool evolve){
    QVariantList raw;if(obj.contains("triples")&&obj["triples"].metaType().id()==QMetaType::QVariantList){
        for(auto v:obj["triples"].toList()){auto a=v.toList();if(a.size()!=5&&a.size()!=6){raw<<QVariantMap{};continue;}raw<<QVariantMap{{"from",a[0]},{"from_type",a[1]},{"predicate",a[2]},{"to",a[3]},{"to_type",a[4]},{"evidence",a.size()==6?a[5]:QVariant()}};}
    }else if(obj["relations"].metaType().id()==QMetaType::QVariantList)raw=obj["relations"].toList();
    else return {{"ok",false},{"error","未返回 triples 数组"}};
    QVariantList edges,nodes;int rejected=0;QSet<QString> seen;
    for(auto v:raw){const auto r=v.toMap();const QString inputFrom=r["from"].toString().trimmed(),inputTo=r["to"].toString().trimmed(),ft=clean(r["from_type"]),tt=clean(r["to_type"]),predicate=clean(r["predicate"]),requested=r["evidence"].toString().trimmed();
        const QString quote=requested.isEmpty()?sentenceEvidence(source,inputFrom,inputTo):quoteIn(source,requested);
        const QString from=quoteIn(quote,inputFrom),to=quoteIn(quote,inputTo);
        bool rule=false;for(auto def:base["relation_types"].toList()){auto d=def.toMap();if(d["name"]==predicate&&d["from"]==ft&&d["to"]==tt)rule=true;}
        if(!named(from,100)||!named(to,100)||!named(ft,50)||!named(tt,50)||!named(predicate,50)||quote.size()<4||quote.size()>700||!source.contains(from)||!source.contains(to)||!quote.contains(from)||!quote.contains(to)||(!evolve&&!rule)){++rejected;continue;}
        QVariantMap edge{{"from",from},{"from_type",ft},{"predicate",predicate},{"to",to},{"to_type",tt},{"evidence",quote},{"selected",true}};
        const auto key=QString::fromUtf8(QJsonDocument::fromVariant(edge).toJson(QJsonDocument::Compact));if(seen.contains(key))continue;seen.insert(key);edges<<edge;
        for(auto n:{QVariantMap{{"name",from},{"type",ft}},QVariantMap{{"name",to},{"type",tt}}})if(!nodes.contains(n))nodes<<n;
    }
    auto o=evolve?extend(base,edges):base;
    // Limit individual schema proposals instead of discarding all valid earlier facts.
    while((o["entity_types"].toList().size()>80||o["relation_types"].toList().size()>160)&&!edges.isEmpty()){edges.removeLast();++rejected;o=evolve?extend(base,edges):base;}
    nodes.clear();for(auto v:edges){auto r=v.toMap();for(auto n:{QVariantMap{{"name",r["from"]},{"type",r["from_type"]}},QVariantMap{{"name",r["to"]},{"type",r["to_type"]}}})if(!nodes.contains(n))nodes<<n;}
    return {{"ok",true},{"entities",nodes},{"relations",edges},{"ontology",o},{"rejected",rejected}};
}
