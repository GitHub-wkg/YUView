#include "FrameBrowserWidget.h"

#include <QAbstractScrollArea>
#include <QCache>
#include <QFutureWatcher>
#include <QLabel>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QShowEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrentRun>

#include <cmath>
#include <functional>
#include <utility>

#include <playlistitem/playlistItemWithVideo.h>

namespace
{
Q_LOGGING_CATEGORY(frameBrowserLog, "yuview.framebrowser", QtInfoMsg)

constexpr int   cellWidth = 144;
constexpr int   thumbnailCacheLimit = 96;
const QSize     thumbnailSize(128, 72);

struct FramePreview
{
  QSize  size;
  QImage thumbnail;
};

class FrameStrip : public QAbstractScrollArea
{
public:
  FrameStrip(playlistItemWithVideo &item, QLabel &details, int frame,
             std::function<void(int)> selectFrame)
      : item(item), details(details), properties(item.properties()),
        selectFrame(std::move(selectFrame)), selectedFrame(frame)
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

  bool hasProperties(const playlistItem::Properties &current) const
  {
    return properties.startEndRange == current.startEndRange &&
           properties.frameRate == current.frameRate;
  }

  void setCurrentFrame(int frame)
  {
    if (frame < properties.startEndRange.first || frame > properties.startEndRange.second)
      return;
    selectedFrame = frame;
    if (frame < firstVisibleFrame())
      horizontalScrollBar()->setValue(frame - properties.startEndRange.first);
    else if (frame > lastVisibleFrame())
      horizontalScrollBar()->setValue(frame - properties.startEndRange.first
                                     - horizontalScrollBar()->pageStep() + 1);
    showSelection();
    viewport()->update();
    if (!refresh.isActive())
      refresh.start();
  }

  ~FrameStrip() override
  {
    // The background request borrows the playlist item until the strip has stopped loading.
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
  void showEvent(QShowEvent *event) override
  {
    QAbstractScrollArea::showEvent(event);
    setCurrentFrame(selectedFrame);
    refresh.start();
  }

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
      if (thumbnail && !thumbnail->thumbnail.isNull())
      {
        auto imageRect = QRect(QPoint(), thumbnail->thumbnail.size());
        imageRect.moveCenter(thumbnailRect.center());
        painter.drawImage(imageRect, thumbnail->thumbnail);
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
    if (frame > lastVisibleFrame())
      return;

    qCDebug(frameBrowserLog) << "Selecting playback frame" << frame;
    selectFrame(frame);
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
    const bool loadSelection = !thumbnails.contains(selectedFrame);
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
    loader.setFuture(QtConcurrent::run([source = &item, frame]
    {
      const auto image = source->loadFramePreview(frame);
      if (image.isNull())
        return FramePreview{};

      FramePreview result;
      result.size = image.size();
      result.thumbnail = image.scaled(thumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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

    thumbnails.insert(loadingFrame, new FramePreview(result));
    if (loadingFrame == selectedFrame)
      showSelection();

    loadingFrame = -1;
    viewport()->update();
    if (!refresh.isActive())
      loadNextFrame();
  }

  void showSelection()
  {
    const auto selected = thumbnails.object(selectedFrame);
    const auto size = selected ? selected->size : QSize();
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
  QLabel                        &details;
  const playlistItem::Properties properties;
  std::function<void(int)>        selectFrame;
  QCache<int, FramePreview>       thumbnails{thumbnailCacheLimit};
  QTimer                          refresh;
  QFutureWatcher<FramePreview>    loader;
  int                             selectedFrame;
  int                             loadingFrame{-1};
};
}

FrameBrowserWidget::FrameBrowserWidget(QWidget *parent) : QWidget(parent)
{
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  details = new QLabel(this);
  layout->addWidget(details);
  hide();
}

void FrameBrowserWidget::setItem(playlistItem *source, int frame)
{
  auto video = dynamic_cast<playlistItemWithVideo *>(source);
  if (video && source == item && strip && video->isCachable() &&
      static_cast<FrameStrip *>(strip)->hasProperties(video->properties()))
  {
    setCurrentFrame(frame);
    return;
  }

  // Stop the old request before the playlist can release or change its source.
  delete strip;
  strip = nullptr;
  item = nullptr;
  hide();
  if (!video)
    return;

  const auto properties = video->properties();
  if (!properties.isFileSource || !properties.isIndexedByFrame() || !video->isCachable() ||
      properties.startEndRange.first < 0 ||
      properties.startEndRange.second < properties.startEndRange.first)
    return;

  item = source;
  qCDebug(frameBrowserLog) << "Browsing playback item" << properties.name;
  strip = new FrameStrip(*video, *details,
                        qBound(properties.startEndRange.first, frame, properties.startEndRange.second),
                        [this](int selectedFrame) { emit frameSelected(selectedFrame); });
  static_cast<QVBoxLayout *>(layout())->insertWidget(0, strip);
  show();
}

void FrameBrowserWidget::setCurrentFrame(int frame)
{
  if (strip)
    static_cast<FrameStrip *>(strip)->setCurrentFrame(frame);
}

void FrameBrowserWidget::itemAboutToBeDeleted(playlistItem *source)
{
  if (source == item)
    setItem(nullptr, -1);
}