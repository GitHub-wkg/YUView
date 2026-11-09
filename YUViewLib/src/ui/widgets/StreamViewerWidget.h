#pragma once

#include <QWidget>
#include <parser/common/NalUnitModel.h>

class QLabel;
class QTreeView;

class StreamViewerWidget : public QWidget
{
  Q_OBJECT

public:
  explicit StreamViewerWidget(QWidget *parent = nullptr);
  void clear();
  void setStatusText(const QString &text);
  void
  setTree(std::shared_ptr<TreeItem> root, const NalUnitIndex::FrameRanges &ranges, int videoStream);

public slots:
  void setCurrentFrame(int frame);

signals:
  void frameSelected(int frame);

private:
  NalUnitModel model;
  QTreeView   *tree;
  QLabel      *status;
  int          currentFrame{-1};
  bool         treePublished{};
};