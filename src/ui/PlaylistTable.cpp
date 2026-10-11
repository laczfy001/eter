#include "PlaylistTable.h"
#include "Library.h"
#include "Theme.h"

#include <QDrag>
#include <QDragEnterEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>

static const char *kRowMime = "application/x-eter-row";

PlaylistTable::PlaylistTable(QWidget *parent) : QTableWidget(parent)
{
    setDragEnabled(true);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDropIndicatorShown(false);
    setDefaultDropAction(Qt::MoveAction);
}

bool PlaylistTable::accepts(const QMimeData *mime) const
{
    return mime->hasFormat(kRowMime) || mime->hasFormat(kItemsMime) || mime->hasUrls();
}

int PlaylistTable::insertionRow(const QPoint &pos) const
{
    const int row = rowAt(pos.y());
    if (row < 0) return rowCount();
    const QRect r = visualRect(model()->index(row, 0));
    return pos.y() > r.center().y() ? row + 1 : row;
}

void PlaylistTable::startDrag(Qt::DropActions)
{
    const int row = currentRow();
    if (row < 0) return;
    auto *mime = new QMimeData;
    mime->setData(kRowMime, QByteArray::number(row));
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::MoveAction);
}

void PlaylistTable::dragEnterEvent(QDragEnterEvent *e)
{
    if (accepts(e->mimeData())) e->acceptProposedAction();
}

void PlaylistTable::dragMoveEvent(QDragMoveEvent *e)
{
    if (!accepts(e->mimeData())) return;
    e->acceptProposedAction();
    const int row = insertionRow(e->position().toPoint());
    if (row != dropRow_) {
        dropRow_ = row;
        viewport()->update();
    }
}

void PlaylistTable::dragLeaveEvent(QDragLeaveEvent *)
{
    dropRow_ = -1;
    viewport()->update();
}

void PlaylistTable::dropEvent(QDropEvent *e)
{
    const int at = insertionRow(e->position().toPoint());
    dropRow_ = -1;
    viewport()->update();
    const QMimeData *mime = e->mimeData();
    if (mime->hasFormat(kRowMime)) {
        e->acceptProposedAction();
        emit rowMoved(mime->data(kRowMime).toInt(), at);
    } else if (mime->hasFormat(kItemsMime)) {
        e->acceptProposedAction();
        emit itemsDropped(decodeIds(mime->data(kItemsMime)), at);
    } else if (mime->hasUrls()) {
        QStringList paths;
        for (const QUrl &u : mime->urls())
            if (u.isLocalFile()) paths << u.toLocalFile();
        if (!paths.isEmpty()) {
            e->acceptProposedAction();
            emit pathsDropped(paths, at);
        }
    }
}

void PlaylistTable::paintEvent(QPaintEvent *e)
{
    QTableWidget::paintEvent(e);
    if (dropRow_ < 0) return;
    int y;
    if (rowCount() == 0) y = 1;
    else if (dropRow_ >= rowCount()) y = visualRect(model()->index(rowCount() - 1, 0)).bottom() + 1;
    else y = visualRect(model()->index(dropRow_, 0)).top();
    QPainter p(viewport());
    p.fillRect(QRect(0, std::max(0, y - 1), viewport()->width(), 2), theme::amber);
}

void PlaylistTable::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Delete) emit deletePressed();
    else if (e->key() == Qt::Key_Space) emit spacePressed();   // a táblázat ne nyelje el az adásgombot
    else QTableWidget::keyPressEvent(e);
}
