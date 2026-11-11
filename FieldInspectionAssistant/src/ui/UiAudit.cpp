#include "UiAudit.h"
#include <QQmlApplicationEngine>
#include <QQmlExpression>
#include <QQmlContext>
#include <QQuickWindow>
#include <QGuiApplication>
#include <QQuickItem>
#include <QMouseEvent>
#include <QtCore>
#include <memory>

// Opt-in on-board layout audit. Run against an isolated XDG_DATA_HOME database.
void runUiAudit(QQmlApplicationEngine &engine) {
    const auto script=qEnvironmentVariable("FIELDOPS_QA_SCRIPT");
    if(script.isEmpty()) return;
    QFile input(script); if(!input.open(QIODevice::ReadOnly)) return;
    auto steps=QJsonDocument::fromJson(input.readAll()).array();
    auto output=qEnvironmentVariable("FIELDOPS_QA_DIR","/tmp/fieldops-ui-audit"); QDir().mkpath(output);
    auto index=std::make_shared<int>(0);
    auto report=std::make_shared<QJsonArray>();
    auto next=std::make_shared<std::function<void()>>();
    auto *window=qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    engine.rootContext()->setContextProperty("auditWindow",window);
    *next=[&engine,steps,output,index,report,next,window] {
        if(*index>=steps.size()) {
            QFile file(output+"/report.json"); file.open(QIODevice::WriteOnly); file.write(QJsonDocument(*report).toJson());
            qInfo()<<"UI_AUDIT_COMPLETE"<<output; QCoreApplication::quit(); return;
        }
        auto step=steps[(*index)++].toObject();
        QQmlExpression expression(engine.rootContext(),window,step["expression"].toString()); auto value=expression.evaluate();
        QJsonObject entry{{"name",step["name"]},{"result",QJsonValue::fromVariant(value)}};
        if(expression.hasError()) entry["error"]=expression.error().toString();
        if(step.contains("tapObject")) {
            QQuickItem *target=nullptr;
            std::function<void(QQuickItem*)> find=[&](QQuickItem *item){
                if(!target&&item->objectName()==step["tapObject"].toString()&&item->isVisible()&&item->isEnabled())target=item;
                for(auto *child:item->childItems())if(!target)find(child);
            };find(window->contentItem());
            if(!target)entry["error"]="Tap target not found or disabled";
            else{
                auto point=target->mapToScene(QPointF(target->width()/2,target->height()/2));QPointF global=window->mapToGlobal(point.toPoint());
                QMouseEvent press(QEvent::MouseButtonPress,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&press);
                QMouseEvent release(QEvent::MouseButtonRelease,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QCoreApplication::sendEvent(window,&release);
            }
        }
        // Exercise the real embedded keyboard's hit testing and input method.
        if(step.contains("tapKey")||step.contains("tapText")) {
            QQuickItem *keyboard=nullptr,*key=nullptr;
            std::function<void(QQuickItem*)> findKeyboard=[&](QQuickItem *item){
                if(item->objectName()=="onScreenKeyboard")keyboard=item;
                for(auto *child:item->childItems())findKeyboard(child);
            };findKeyboard(window->contentItem());
            std::function<void(QQuickItem*)> findKey=[&](QQuickItem *item){
                if(!item->isVisible()||!item->isEnabled()||key)return;
                const bool match=step.contains("tapKey") ? item->property("key").isValid()&&item->property("key").toInt()==step["tapKey"].toInt()
                    : item->property("text").toString()==step["tapText"].toString();
                if(match&&item->width()>0&&item->height()>0)key=item;
                for(auto *child:item->childItems())findKey(child);
            };if(keyboard)findKey(keyboard);
            if(!key)entry["error"]="Keyboard key/candidate not found";
            else {
                const auto point=key->mapToScene(QPointF(key->width()/2,key->height()/2));
                const QPointF global=window->mapToGlobal(point.toPoint());
                QMouseEvent press(QEvent::MouseButtonPress,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QCoreApplication::sendEvent(window,&press);
                QMouseEvent release(QEvent::MouseButtonRelease,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QCoreApplication::sendEvent(window,&release);
            }
        }
        QQuickWindow *captureWindow=window;if(step.contains("captureWindow")){for(auto *candidate:QGuiApplication::allWindows())if(candidate->objectName()==step["captureWindow"].toString())if(auto *quick=qobject_cast<QQuickWindow*>(candidate))captureWindow=quick;}
        auto finish=[entry,report,next,window=captureWindow,output](const QString &waitError=QString()) mutable {
            if(!waitError.isEmpty())entry["error"]=waitError;
            window->grabWindow().save(output+"/"+entry["name"].toString()+".png");
            entry["window"] = QJsonObject{{"width",window->width()},{"height",window->height()},
                {"visibility",int(window->visibility())},
                {"minimize_button",window->flags().testFlag(Qt::WindowMinimizeButtonHint)},
                {"maximize_button",window->flags().testFlag(Qt::WindowMaximizeButtonHint)},
                {"close_button",window->flags().testFlag(Qt::WindowCloseButtonHint)}};
            QJsonArray warnings;
            std::function<void(QQuickItem*,bool)> scan=[&](QQuickItem *item,bool clipped) {
                if(!item->isVisible() || item->opacity()==0) return;
                const auto text=item->property("text");
                if(text.isValid() && item->width()>0 && item->height()>0) {
                    const auto rect=item->mapRectToScene(QRectF(0,0,item->width(),item->height()));
                    auto label=text.toString().left(90);
                    if(!clipped && (rect.left() < -1 || rect.right()>window->width()+1 || rect.top() < -1 || rect.bottom()>window->height()+1))
                        warnings.append(QJsonObject{{"text",label},{"problem","outside screen"},{"x",rect.x()},{"y",rect.y()},{"width",rect.width()},{"height",rect.height()}});
                    if(!item->property("readOnly").isValid() && item->property("contentWidth").toDouble()>item->width()+2 && item->property("elide").toInt()==0 && !clipped)
                        warnings.append(QJsonObject{{"text",label},{"problem","text wider than item"}});
                }
                for(auto *child:item->childItems()) scan(child,clipped||item->clip());
            };
            scan(window->contentItem(),false); entry["warnings"]=warnings; report->append(entry); (*next)();
        };
        if(step.contains("until")){
            auto *timer=new QTimer(window);timer->setInterval(250);auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
            QObject::connect(timer,&QTimer::timeout,window,[&engine,window,timer,elapsed,step,finish]()mutable{
                QQmlExpression predicate(engine.rootContext(),window,step["until"].toString());auto result=predicate.evaluate();
                if(predicate.hasError()||result.toBool()||elapsed->elapsed()>step["timeout"].toInt(90000)){
                    timer->stop();timer->deleteLater();finish(predicate.hasError()?predicate.error().toString():result.toBool()?QString():QStringLiteral("Wait condition timed out"));
                }
            });timer->start();
        }else QTimer::singleShot(step["delay"].toInt(450),window,[finish]()mutable{finish();});
    };
    QTimer::singleShot(1000,window,*next);
}
