// Stream fül: Icecast / Shoutcast enkóderek. Bal oldalt az enkóderek kártyái (állapot, indítás, leállítás) és a kifelé
// menő sávszél összege a feltöltési kerettel; jobb oldalt a kijelölt enkóder beállításai (szerver, kódolás és
// bitráta, hangforrás – a fő kimenet vagy egy hangkártya-bemenet –, cím és állomásadatok, kapcsolat, eseménynapló).
#pragma once

#include "stream/StreamEncoder.h"

#include <QHash>
#include <QPointer>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QRadioButton;
class QScrollArea;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTimer;
class QToolButton;
class QVBoxLayout;
class StreamManager;
namespace eter { class Engine; }

class StreamPage : public QWidget {
    Q_OBJECT
public:
    StreamPage(StreamManager *mgr, eter::Engine *engine, QWidget *parent = nullptr);
    void selectEncoder(const QString &id);
    void selectFirstProblem();          // a STREAM jelzőről: a hibás (vagy újrapróbáló) enkóder

signals:
    void message(const QString &text);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    struct Card {
        QString id;
        QFrame *frame = nullptr;
        QLabel *name = nullptr, *pill = nullptr, *server = nullptr, *format = nullptr, *rate = nullptr, *audio = nullptr, *stats = nullptr;
        QPushButton *button = nullptr;
    };

    void rebuildCards();
    void updateCards();
    void updateBandwidth();
    void loadEditor();
    void applyEditor();                  // a mezőkből a beállítás, mentés (késleltetve hívva)
    void scheduleApply();
    void updateEditorState();            // a szervertípus és a formátum szerint látható / tiltott mezők
    void updateInfo();
    void fillSources(const QString &current);
    void appendEvent(const QString &encoderId, const QString &line);
    stream::Config editorConfig() const;
    stream::Encoder *selected() const;

    StreamManager *mgr_;
    eter::Engine *engine_;
    QString selected_;
    bool loading_ = false;
    QTimer *applyTimer_ = nullptr, *statusTimer_ = nullptr;

    // bal oldal
    QVBoxLayout *cardsLayout_ = nullptr;
    QVector<Card> cards_;
    QLabel *bwTotal_ = nullptr, *bwHour_ = nullptr, *bwPercent_ = nullptr, *bwWarn_ = nullptr;
    QWidget *bwBar_ = nullptr;
    QFrame *bwFrame_ = nullptr;
    QSpinBox *limit_ = nullptr;

    // jobb oldal
    QStackedWidget *rightStack_ = nullptr;
    QLineEdit *name_ = nullptr;
    QCheckBox *autoStart_ = nullptr;
    QPushButton *test_ = nullptr, *delete_ = nullptr;
    QFrame *restartBar_ = nullptr;
    QButtonGroup *serverGroup_ = nullptr, *formatGroup_ = nullptr, *rateGroup_ = nullptr;
    QLineEdit *host_ = nullptr, *mount_ = nullptr, *user_ = nullptr, *password_ = nullptr;
    QSpinBox *port_ = nullptr, *sid_ = nullptr;
    QLabel *mountLabel_ = nullptr, *sidLabel_ = nullptr, *userLabel_ = nullptr, *serverNote_ = nullptr;
    QCheckBox *tls_ = nullptr;
    QComboBox *mode_ = nullptr, *rateHz_ = nullptr;
    QRadioButton *stereo_ = nullptr, *mono_ = nullptr;
    QLabel *info_ = nullptr, *codecNote_ = nullptr;
    QComboBox *source_ = nullptr;
    QSlider *gain_ = nullptr;
    QLabel *gainLabel_ = nullptr, *reduction_ = nullptr;
    QWidget *meter_ = nullptr;
    QCheckBox *limiter_ = nullptr;
    QCheckBox *sendTitle_ = nullptr, *likeNp_ = nullptr, *public_ = nullptr;
    QLineEdit *titleFormat_ = nullptr, *station_ = nullptr, *desc_ = nullptr, *genre_ = nullptr, *url_ = nullptr;
    QLabel *titleNote_ = nullptr;
    QCheckBox *reconnect_ = nullptr;
    QSpinBox *retry_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
};
