// A playlist táblázata: sorok áthúzása, valamint ejtés a hangtárból és a fájlkezelőből.
#pragma once
#include <QTableWidget>

class PlaylistTable : public QTableWidget {
    Q_OBJECT
public:
    explicit PlaylistTable(QWidget *parent = nullptr);

signals:
    void rowMoved(int from, int to);                       // `to`: beszúrási hely a mozgatás előtti számozásban
    void itemsDropped(const QVector<int> &itemIds, int at);
    void pathsDropped(const QStringList &paths, int at);
    void deletePressed();
    void spacePressed();
    void viewGeometryChanged();                            // görgetés, átméretezés: a ráhelyezett elemek igazítása

protected:
    void startDrag(Qt::DropActions) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dragLeaveEvent(QDragLeaveEvent *) override;
    void dropEvent(QDropEvent *) override;
    void paintEvent(QPaintEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void scrollContentsBy(int dx, int dy) override;
    void updateGeometries() override;

private:
    bool accepts(const QMimeData *mime) const;
    int insertionRow(const QPoint &pos) const;
    int dropRow_ = -1;
};
