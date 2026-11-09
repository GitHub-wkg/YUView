#include "StreamViewerWidget.h"

#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLoggingCategory>
#include <QSignalBlocker>
#include <QTreeView>
#include <QVBoxLayout>

namespace
{
Q_LOGGING_CATEGORY(streamViewerLog, "yuview.streamviewer", QtInfoMsg)
}

StreamViewerWidget::StreamViewerWidget(QWidget *parent) : QWidget(parent), model(this)
{
  auto layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  tree = new QTreeView(this);
  tree->setModel(&model);
  tree->setTreePosition(1);
  tree->setAlternatingRowColors(true);
  tree->setUniformRowHeights(true);
  tree->setSelectionBehavior(QAbstractItemView::SelectRows);
  tree->setColumnWidth(0, 110);
  tree->setColumnWidth(1, 220);
  tree->setColumnWidth(2, 180);
  tree->header()->setStretchLastSection(false);
  tree->header()->setSectionResizeMode(2, QHeaderView::Stretch);
  tree->setColumnWidth(3, 60);
  layout->addWidget(tree, 1);
  status = new QLabel(tr("Select a compressed video to inspect its NAL units."), this);
  status->setWordWrap(true);
  layout->addWidget(status);
  connect(tree->selectionModel(),
          &QItemSelectionModel::currentChanged,
          this,
          [this](const QModelIndex &index)
          {
            if (const auto frame = model.frameForIndex(index))
            {
              qCInfo(streamViewerLog) << "Selecting NAL display frame" << *frame;
              emit frameSelected(*frame);
            }
          });
}

void StreamViewerWidget::clear()
{
  const QSignalBlocker blocker(tree->selectionModel());
  model.setTree({}, {}, -1);
  treePublished = false;
}

void StreamViewerWidget::setStatusText(const QString &text)
{
  if (treePublished)
    status->setText(model.rowCount() > 0 ? tr("%1  (%2 NAL units)").arg(text).arg(model.rowCount())
                                         : tr("%1  No NAL units found for this stream.").arg(text));
  else
    status->setText(text);
}

void StreamViewerWidget::setTree(std::shared_ptr<TreeItem>        root,
                                 const NalUnitIndex::FrameRanges &ranges,
                                 int                              videoStream)
{
  {
    const QSignalBlocker blocker(tree->selectionModel());
    model.setTree(std::move(root), ranges, videoStream);
  }
  treePublished = true;
  qCInfo(streamViewerLog) << "Published" << model.rowCount() << "parsed NAL units";
  setCurrentFrame(currentFrame);
}

void StreamViewerWidget::setCurrentFrame(int frame)
{
  currentFrame = frame;
  if (model.frameForIndex(tree->currentIndex()) == std::optional<int>(frame))
    return; // Keep a manually selected slice or syntax field within the same picture.
  const QSignalBlocker blocker(tree->selectionModel());
  const auto           index = model.indexForFrame(frame);
  tree->selectionModel()->clearSelection();
  tree->setCurrentIndex(index);
  if (index.isValid() && isVisible())
    tree->scrollTo(index, QAbstractItemView::EnsureVisible);
}