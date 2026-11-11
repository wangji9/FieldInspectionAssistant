#include "ConversationContext.h"

QVariantList recentConversationTurns(const QVariantList &messages,int budget){
    QList<QVariantList> turns;QVariantList turn;bool answered=false,invalid=false;
    auto finish=[&]{if(!turn.isEmpty()&&answered&&!invalid)turns<<turn;turn.clear();answered=false;invalid=false;};
    for(auto v:messages){
        const auto row=v.toMap();const auto kind=row.value("kind","text").toString();
        const auto role=row.contains("outgoing")?(row.value("outgoing").toBool()?QStringLiteral("user"):QStringLiteral("assistant")):row.value("role").toString();
        if(role=="assistant"&&(kind=="error"||row.value("streaming").toBool())){invalid=true;continue;}
        if(kind!="text"||row.value("streaming").toBool()||row.value("body").toString().trimmed().isEmpty())continue;
        if(role=="user"){finish();turn<<QVariantMap{{"role","user"},{"content",row["body"]}};}
        else if(role=="assistant"&&!turn.isEmpty()){turn<<QVariantMap{{"role","assistant"},{"content",row["body"]}};answered=true;}
    }
    finish();QVariantList result;int used=0;
    for(int i=turns.size()-1;i>=0;--i){const int size=QString::fromUtf8(QJsonDocument::fromVariant(turns[i]).toJson(QJsonDocument::Compact)).size();if(used+size>budget)break;result=turns[i]+result;used+=size;}
    return result;
}
