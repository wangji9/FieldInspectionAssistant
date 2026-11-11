#pragma once
#include <QtCore>
#include <QQuickItem>
#include <QQuickWindow>

class DictationService;
class InputController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool opened READ opened NOTIFY changed)
    Q_PROPERTY(QQuickWindow *targetWindow READ targetWindow NOTIFY changed)
    Q_PROPERTY(QString initialText READ initialText NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(bool multiline READ multiline NOTIFY changed)
    Q_PROPERTY(int hints READ hints NOTIFY changed)
    Q_PROPERTY(int echoMode READ echoMode NOTIFY changed)
    Q_PROPERTY(int cursorPosition READ cursorPosition NOTIFY changed)
    Q_PROPERTY(int maximumLength READ maximumLength NOTIFY changed)
    Q_PROPERTY(bool voiceRequested READ voiceRequested NOTIFY changed)
public:
    explicit InputController(DictationService *dictation,QObject *parent=nullptr);
    bool opened() const { return opened_; }
    QQuickWindow *targetWindow()const{return target_?target_->window():nullptr;}
    QString initialText() const { return text_; }
    QString title() const { return title_; }
    bool multiline() const { return multiline_; }
    int hints() const { return hints_; }
    int echoMode() const { return echo_; }
    int cursorPosition() const { return cursor_; }
    int maximumLength() const { return maximumLength_; }
    bool voiceRequested() const { return voiceRequested_; }
    Q_INVOKABLE void open(QQuickItem *target,bool voice=false);
    Q_INVOKABLE void apply(const QString &text,int cursor=-1);
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void applied(const QString &text);
private:
    bool editable(QQuickItem *item) const;
    void dismiss();
    DictationService *dictation_;
    QPointer<QQuickItem> target_;
    QMetaObject::Connection destroyed_;
    QString text_,title_;
    bool opened_=false,suppress_=false,multiline_=false;
    bool voiceRequested_=false;
    int hints_=0,echo_=0,cursor_=0,maximumLength_=32767;
};
