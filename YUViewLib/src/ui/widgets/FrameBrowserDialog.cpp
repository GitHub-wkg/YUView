#include "FrameBrowserDialog.h"

#include <QAbstractScrollArea>
#include <QCache>
#include <QDialog>
#include <QFutureWatcher>
#include <QLabel>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrentRun>

#include <cmath>

#include <playlistitem/playlistItemWithVideo.h>

namespace
{
Q_LOGGING_CATEGORY(frameBrowserLog, "yuview.framebrowser", QtInfoMsg)

constexpr int   cellWidth = 144;
constexpr int   thumbnailCacheLimit = 96;
const QSize     thumbnailSize(128, 72);
const QSize     previewSize(720, 400);

struct FramePreview
{
  QSize  size;
  QImage thumbnail;
  QImage image;
};

class FrameStrip : public QAbstractScrollArea
{
public:
  FrameStrip(playlistItemWithVideo &item, QLabel &preview, QLabel &details)
      : item(item), preview(preview), details(details), properties(item.properties()),
        selectedFrame(properties.startEndRange.first)
  {
    setFixedHeight(118);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    setLayoutDirection(Qt::LeftToRight);
    setAccessibleName(tr("Video frames"));
    horizontalScrollBar()->setSingleStep(1);

    refresh.setSingleShot(true);
    refresh.setInterval(35);
    connect(&refresh, &QTimer::timeout, this, &FrameStrip::loadNextFrame);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this]
    {
      viewport()->update();
      refresh.start();
    });
    connect(&loader, &QFutureWatcher<FramePreview>::finished, this, &FrameStrip::frameLoaded);
    showSelection();
    refresh.start();
  }

  ~FrameStrip() override
  {
    // The background request borrows the playlist item for the duration of the dialog.
    if (loader.isRunning())
    {
      try
      {
        loader.waitForFinished();
      }
      catch (...)
      {
        qCWarning(frameBrowserLog) << "Frame loading failed while closing the browser";
      }
    }
  }

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().base());
    const auto first = firstVisibleFrame();
    const auto last  = lastVisibleFrame();
    for (int frame = first; frame <= last; ++frame)
    {
      QRect cell((frame - first) * cellWidth, 0, cellWidth, viewport()->height());
      const auto selected = frame == selectedFrame;
      if (selected)
        painter.fillRect(cell, palette().highlight());

      QRect thumbnailRect(cell.x() + (cellWidth - thumbnailSize.width()) / 2,
                          5, thumbnailSize.width(), thumbnailSize.height());
      const auto thumbnail = thumbnails.object(frame);
      if (thumbnail && !thumbnail->isNull())
      {
        auto imageRect = QRect(QPoint(), thumbnail->size());
        imageRect.moveCenter(thumbnailRect.center());
        painter.drawImage(imageRect, *thumbnail);
      }
      else
      {
        painter.fillRect(thumbnailRect, palette().alternateBase());
        painter.setPen(palette().text().color());
        painter.drawText(thumbnailRect, Qt::AlignCenter, thumbnail ? tr("Unavailable") : tr("…"));
      }

      painter.setPen(selected ? palette().highlightedText().color() : palette().text().color());
      painter.drawText(cell.adjusted(0, thumbnailSize.height() + 5, 0, 0),
                       Qt::AlignCenter, tr("Frame %1").arg(frame));
    }
  }

  void resizeEvent(QResizeEvent *event) override
  {
    QAbstractScrollArea::resizeEvent(event);
    horizontalScrollBar()->setPageStep(qMax(1, viewport()->width() / cellWidth));
    thumbnails.setMaxCost(qMax(thumbnailCacheLimit, horizontalScrollBar()->pageStep() + 2));
    horizontalScrollBar()->setRange(
      0, qMax(0, properties.startEndRange.second - properties.startEndRange.first
                   - horizontalScrollBar()->pageStep() + 1));
    refresh.start();
  }

  void wheelEvent(QWheelEvent *event) override
  {
    const auto scroll = event->angleDelta();
    const auto delta = scroll.x() != 0 ? scroll.x() : scroll.y();
    if (delta == 0)
    {
      QAbstractScrollArea::wheelEvent(event);
      return;
    }

    const auto steps = qMax(1, qAbs(delta) / 120);
    horizontalScrollBar()->setValue(horizontalScrollBar()->value() + (delta > 0 ? -steps : steps));
    event->accept();
  }

  void mousePressEvent(QMouseEvent *event) override
  {
    if (event->button() != Qt::LeftButton)
      return;
    const auto frame = firstVisibleFrame() + int(event->position().x()) / cellWidth;
    if (frame > lastVisibleFrame() || frame == selectedFrame)
      return;

    selectedFrame = frame;
    selectedPreview = {};
    selectionLoaded = false;
    showSelection();
    viewport()->update();
    refresh.start();
  }

private:
  int firstVisibleFrame() const
  {
    return properties.startEndRange.first + horizontalScrollBar()->value();
  }

  int lastVisibleFrame() const
  {
    return qMin(properties.startEndRange.second,
                firstVisibleFrame() + qMax(0, viewport()->width() - 1) / cellWidth);
  }

  void loadNextFrame()
  {
    if (loadingFrame >= 0 || !isVisible())
      return;

    int frame = -1;
    const bool loadSelection = !selectionLoaded;
    if (loadSelection)
      frame = selectedFrame;
    else
      for (int visibleFrame = firstVisibleFrame(); visibleFrame <= lastVisibleFrame(); ++visibleFrame)
      {
        if (!thumbnails.contains(visibleFrame))
        {
          frame = visibleFrame;
          break;
        }
      }

    if (frame < 0)
      return;

    loadingFrame = frame;
    qCDebug(frameBrowserLog) << "Requesting frame" << frame << "preview" << loadSelection;
    loader.setFuture(QtConcurrent::run([source = &item, frame, loadSelection]
    {
      const auto image = source->loadFramePreview(frame);
      if (image.isNull())
        return FramePreview{};

      FramePreview result;
      result.size = image.size();
      result.thumbnail = image.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      if (loadSelection)
        result.image = image.scaled(previewSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
      return result;
    }));
  }

  void frameLoaded()
  {
    FramePreview result;
    try
    {
      result = loader.result();
    }
    catch (...)
    {
      qCWarning(frameBrowserLog) << "Unable to load frame" << loadingFrame;
    }

    thumbnails.insert(loadingFrame, new QImage(result.thumbnail));
    if (loadingFrame == selectedFrame && (!result.image.isNull() || result.thumbnail.isNull()))
    {
      selectedPreview = result;
      selectionLoaded = true;
      showSelection();
    }

    loadingFrame = -1;
    viewport()->update();
    if (!refresh.isActive())
      loadNextFrame();
  }

  void showSelection()
  {
    if (selectedPreview.image.isNull())
      preview.setText(selectionLoaded ? tr("Frame unavailable") : tr("Loading frame…"));
    else
      preview.setPixmap(QPixmap::fromImage(selectedPreview.image));

    const auto size = selectedPreview.size;
    const auto resolution = size.isValid()
      ? tr("%1 × %2").arg(size.width()).arg(size.height())
      : tr("Unavailable");
    const auto rate = properties.frameRate;
    const auto timestamp = std::isfinite(rate) && rate > 0
      ? tr("%1 s (from frame rate)").arg(selectedFrame / rate, 0, 'f', 3)
      : tr("Frame rate not set");
    details.setText(tr("Frame %1   |   Time %2   |   Resolution %3")
                      .arg(selectedFrame).arg(timestamp, resolution));
  }

  playlistItemWithVideo          &item;
  QLabel                        &preview;
  QLabel                        &details;
  const playlistItem::Properties properties;
  QCache<int, QImage>             thumbnails{thumbnailCacheLimit};
  QTimer                          refresh;
  QFutureWatcher<FramePreview>    loader;
  FramePreview                    selectedPreview;
  int                             selectedFrame;
  int                             loadingFrame{-1};
  bool                            selectionLoaded{};
};
}

void showFrameBrowser(playlistItemWithVideo &item, QWidget *parent)
{
  QDialog dialog(parent);
  dialog.setWindowTitle(QObject::tr("Frame Browser – %1").arg(item.properties().name));
  auto layout = new QVBoxLayout(&dialog);
  auto preview = new QLabel(&dialog);
  preview->setAlignment(Qt::AlignCenter);
  preview->setMinimumSize(previewSize);
  auto details = new QLabel(&dialog);
  layout->addWidget(preview, 1);
  layout->addWidget(details);
  layout->addWidget(new FrameStrip(item, *preview, *details));
  dialog.resize(920, 600);
  dialog.exec();
}