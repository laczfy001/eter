#include "i18n.h"
#include "SettingsDialog.h"
#include "MainWindow.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(MainWindow *main) : QDialog(main), main_(main)
{
    setWindowTitle(TR("Beállítások"));
    setMinimumWidth(720);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 16, 18, 14);
    root->setSpacing(10);

    // Nyelv: a program indulásakor dől el, ezért a váltás az újraindítás után érvényes.
    auto *langCaption = new QLabel(TR("NYELV"));
    langCaption->setObjectName("caption");
    root->addWidget(langCaption);
    auto *langRow = new QHBoxLayout;
    auto *language = new QComboBox;
    language->setObjectName("languageCombo");
    language->setAccessibleName(TR("A felület nyelve"));
    language->addItem(QStringLiteral("English"), QStringLiteral("en"));
    language->addItem(QStringLiteral("Magyar"), QStringLiteral("hu"));
    language->setMinimumWidth(220);
    const QString saved = QSettings().value("ui/language", QStringLiteral("en")).toString();
    language->setCurrentIndex(std::max(0, language->findData(saved)));
    auto *langNote = new QLabel;
    langNote->setObjectName("small");
    auto showNote = [langNote, language] {
        // A megjegyzés mindkét nyelven látszik, hogy annak is érthető legyen, aki épp átváltott.
        const bool pending = language->currentData().toString() != i18n::language();
        langNote->setText(pending ? QStringLiteral("Restart the program to apply · A váltáshoz indítsd újra a programot")
                                  : QString());
    };
    langRow->addWidget(new QLabel(QStringLiteral("Language · Nyelv")));
    langRow->addWidget(language);
    langRow->addSpacing(16);
    langRow->addWidget(langNote);
    langRow->addStretch(1);
    root->addLayout(langRow);
    root->addSpacing(8);
    connect(language, &QComboBox::activated, this, [language, showNote] {
        QSettings().setValue("ui/language", language->currentData().toString());
        showNote();
    });
    showNote();

    auto *sysCaption = new QLabel(TR("HANGRENDSZER"));
    sysCaption->setObjectName("caption");
    root->addWidget(sysCaption);

    auto *sysRow = new QHBoxLayout;
    auto *backendLabel = new QLabel(TR("Háttérrendszer"));
    backend_ = new QComboBox;
    backend_->addItem(TR("Automatikus"), QString());
#ifdef Q_OS_WIN
    backend_->addItem(QStringLiteral("WASAPI"), QStringLiteral("wasapi"));
    backend_->addItem(QStringLiteral("DirectSound"), QStringLiteral("dsound"));
#else
    backend_->addItem(TR("PulseAudio / PipeWire"), QStringLiteral("pulseaudio"));
    backend_->addItem(TR("ALSA (közvetlen)"), QStringLiteral("alsa"));
    backend_->addItem(TR("JACK"), QStringLiteral("jack"));
#endif
    backend_->setMinimumWidth(220);
    backendLabel->setBuddy(backend_);
    auto *rate = new QLabel(TR("Belső mintavétel: 48 000 Hz, sztereó"));
    rate->setObjectName("small");
    sysRow->addWidget(backendLabel);
    sysRow->addWidget(backend_);
    sysRow->addSpacing(16);
    sysRow->addWidget(rate);
    sysRow->addStretch(1);
    root->addLayout(sysRow);
    root->addSpacing(8);

    auto *outRow = new QHBoxLayout;
    auto *outCaption = new QLabel(TR("KIMENETEK"));
    outCaption->setObjectName("caption");
    auto *refresh = new QPushButton(TR("Eszközlista frissítése"));
    outRow->addWidget(outCaption);
    outRow->addStretch(1);
    outRow->addWidget(refresh);
    root->addLayout(outRow);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(10);
    grid->setVerticalSpacing(8);
    grid->setColumnStretch(2, 1);
    const QString names[4] = {TR("Lejátszó 1"), TR("Lejátszó 2"),
                              TR("Előhallgatás (PFL)"), TR("Cartwall")};
    const char *badges[4] = {"badge1", "badge2", "badgeP", "badgeP"};
    const QString letters[4] = {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("P"), QStringLiteral("C")};
    for (int ch = 0; ch < 4; ++ch) {
        auto *badge = new QLabel(letters[ch]);
        badge->setObjectName(badges[ch]);
        badge->setAlignment(Qt::AlignCenter);
        auto *name = new QLabel(names[ch]);
        device_[ch] = new QComboBox;
        device_[ch]->setAccessibleName(names[ch] + TR(" kimenete"));
        name->setBuddy(device_[ch]);
        test_[ch] = new QPushButton(TR("Teszthang"));
        test_[ch]->setToolTip(TR("Rövid sípoló hang ezen a kimeneten"));
        state_[ch] = new QLabel;
        state_[ch]->setObjectName("small");
        state_[ch]->setMinimumWidth(120);
        grid->addWidget(badge, ch, 0);
        grid->addWidget(name, ch, 1);
        grid->addWidget(device_[ch], ch, 2);
        grid->addWidget(test_[ch], ch, 3);
        grid->addWidget(state_[ch], ch, 4);

        connect(device_[ch], &QComboBox::activated, this, [this, ch](int index) {
            main_->changeRoute(ch, device_[ch]->itemData(index).toString());
        });
        connect(test_[ch], &QPushButton::clicked, this, [this, ch] {
            main_->engine()->testTone(ch == MainWindow::kCartRoute ? eter::Engine::Cart1 : static_cast<eter::Engine::Channel>(ch));
        });
    }
    root->addLayout(grid);

    auto *note = new QLabel(TR("Minden csatorna kimenete szabadon választható. Ha több csatorna ugyanarra a kimenetre "
        "van állítva, a program összekeveri őket, és együtt szólnak azon. A cartwall alapból "
        "az 1-es lejátszó kimenetét követi."));
    note->setObjectName("small");
    note->setWordWrap(true);
    root->addWidget(note);
    root->addStretch(1);

    auto *bottom = new QHBoxLayout;
    count_ = new QLabel;
    count_->setObjectName("small");
    auto *close = new QPushButton(TR("Bezárás"));
    close->setProperty("accent", true);
    close->setMinimumWidth(96);
    close->setDefault(true);
    bottom->addWidget(count_);
    bottom->addStretch(1);
    bottom->addWidget(close);
    root->addLayout(bottom);

    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(refresh, &QPushButton::clicked, main_, &MainWindow::refreshDeviceLists);
    connect(main_, &MainWindow::devicesChanged, this, &SettingsDialog::reload);
    connect(backend_, &QComboBox::activated, this, [this](int index) {
        main_->changeBackend(backend_->itemData(index).toString());
        reload();
    });

    // A „szól – zárolva" jelzés frissítése
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this] {
        const bool any = main_->anythingPlaying();
        backend_->setEnabled(!any);
        for (int ch = 0; ch < 4; ++ch) {
            const bool busy = main_->channelBusy(ch);
            device_[ch]->setEnabled(!busy);
            state_[ch]->setText(busy ? TR("szól – zárolva") : QString());
        }
    });
    timer->start(250);

    reload();
}

void SettingsDialog::reload()
{
    backend_->setCurrentIndex(std::max(0, backend_->findData(main_->backendSetting())));
    const QStringList names = main_->deviceNames();
    for (int ch = 0; ch < 4; ++ch) {
        device_[ch]->blockSignals(true);
        device_[ch]->clear();
        if (ch == MainWindow::kCartRoute)
            device_[ch]->addItem(TR("Az 1-es lejátszó kimenetén (adás)"), QStringLiteral("@player1"));
        device_[ch]->addItem(TR("Alapértelmezett kimenet"), QString());
        for (const QString &n : names) device_[ch]->addItem(n, n);
        device_[ch]->setCurrentIndex(std::max(0, device_[ch]->findData(main_->routeOf(ch))));
        device_[ch]->blockSignals(false);
    }
    count_->setText(TR("%1 kimenet észlelve · %2")
                        .arg(names.size())
                        .arg(QString::fromStdString(main_->engine()->backendName())));
}
