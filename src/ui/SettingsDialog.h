#pragma once
#include <QDialog>

class QComboBox;
class QLabel;
class QPushButton;
class MainWindow;

// Hangbeállítások. Minden változtatás azonnal érvénybe lép; a szóló
// csatorna kimenete és a hangrendszer lejátszás közben nem váltható.
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(MainWindow *main);

private:
    void reload();

    MainWindow *main_;
    QComboBox *backend_;
    QComboBox *device_[4];
    QLabel *state_[4];
    QPushButton *test_[4];
    QLabel *count_;
    QLabel *remoteState_ = nullptr;
};
