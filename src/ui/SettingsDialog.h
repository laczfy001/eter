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
    void addExternalPage(class QVBoxLayout *root);   // Külső playlistek (SettingsExternal.cpp)

    MainWindow *main_;
    QComboBox *backend_;
    static constexpr int kRows = 5;       // A, B, C lejátszó, előhallgatás, cartwall
    static int channelOf(int row);
    QComboBox *device_[kRows];
    QLabel *state_[kRows];
    QPushButton *test_[kRows];
    QLabel *count_;
    QLabel *remoteState_ = nullptr;
};
