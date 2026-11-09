#pragma once

#include <QWidget>

class QLabel;
class playlistItem;

class FrameBrowserWidget : public QWidget
{
  Q_OBJECT

public:
  explicit FrameBrowserWidget(QWidget *parent = nullptr);

  void setItem(playlistItem *item, int frame);
  void setCurrentFrame(int frame);
  void itemAboutToBeDeleted(playlistItem *item);

signals:
  void frameSelected(int frame);

private:
  playlistItem *item{};
  QWidget      *strip{};
  QLabel       *details{};
};