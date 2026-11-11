#include "InputController.h"
#include "../services/DictationService.h"
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>

InputController::InputController(DictationService *dictation,QObject *parent):QObject(parent),dictation_(dictation) {
    connect(qGuiApp,&QGuiApplication::focusObjectChanged,this,[this](QObject *object){
        auto *item=qobject_cast<QQuickItem*>(object);
        if(suppress_||opened_||!editable(item))return;
        QPointer<QQuickItem> candidate=item;
        QTimer::singleShot(0,this,[this,candidate]{if(!suppress_&&!opened_&&candidate&&qGuiApp->focusObject()==candidate)open(candidate);});
    });
}
bool InputController::editable(QQuickItem *item) const {
    if(!item||!item->isVisible()||!item->isEnabled()||item->property("readOnly").toBool()||item->property("inputAssistantInternal").toBool())return false;
    return item->inherits("QQuickTextInput")||item->inherits("QQuickTextEdit");
}
void InputController::open(QQuickItem *target,bool voice) {
    if(opened_||suppress_||!editable(target))return;
    target_=target;text_=target->property("text").toString();cursor_=target->property("cursorPosition").toInt();
    voiceRequested_=voice;
    multiline_=target->inherits("QQuickTextEdit");hints_=target->property("inputMethodHints").toInt();echo_=target->property("echoMode").toInt();
    maximumLength_=target->property("maximumLength").isValid()?target->property("maximumLength").toInt():1000000;
    title_=target->property("placeholderText").toString();
    if(title_.isEmpty()&&target->parentItem())title_=target->parentItem()->property("placeholder").toString();
    if(title_.isEmpty())title_=QStringLiteral("文字输入");
    QObject::disconnect(destroyed_);
    destroyed_=connect(target,&QObject::destroyed,this,[this]{cancel();});
    opened_=true;emit changed();
}
void InputController::dismiss() {
    dictation_->cancel();suppress_=true;opened_=false;emit changed();
    QGuiApplication::inputMethod()->hide();
}
void InputController::apply(const QString &text,int cursor) {
    if(!opened_||!target_)return;
    const auto target=target_;dismiss();
    QTimer::singleShot(0,this,[this,target,text,cursor]{
        if(target&&editable(target)) {
            // Input events preserve QML text bindings and emit real textEdited signals.
            target->forceActiveFocus();QMetaObject::invokeMethod(target,"selectAll");
            QInputMethodEvent commit;commit.setCommitString(text);QCoreApplication::sendEvent(target,&commit);
            target->setProperty("cursorPosition",cursor<0?text.size():qBound(0,cursor,int(text.size())));
            target->setFocus(false);emit applied(target->property("text").toString());
        }
        QGuiApplication::inputMethod()->hide();target_.clear();suppress_=false;
    });
}
void InputController::cancel() {
    if(!opened_)return;
    auto target=target_;dismiss();
    QTimer::singleShot(0,this,[this,target]{if(target)target->setFocus(false);target_.clear();QGuiApplication::inputMethod()->hide();suppress_=false;});
}
