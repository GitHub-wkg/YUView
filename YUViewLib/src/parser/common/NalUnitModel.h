#pragma once

#include "NalUnitIndex.h"
#include <QAbstractItemModel>

class NalUnitModel : public QAbstractItemModel
{
public:
  explicit NalUnitModel(QObject *parent = nullptr) : QAbstractItemModel(parent) {}

  void
  setTree(std::shared_ptr<TreeItem> root, const NalUnitIndex::FrameRanges &ranges, int videoStream);
  QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
  QModelIndex parent(const QModelIndex &child) const override;
  int         rowCount(const QModelIndex &parent = {}) const override;
  int         columnCount(const QModelIndex & = {}) const override { return 4; }
  QVariant    data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
  QVariant
  headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
  std::optional<int> frameForIndex(const QModelIndex &index) const;
  QModelIndex        indexForFrame(int frame) const;

private:
  // Retain packet ancestors as well, so syntax parent links remain valid.
  std::shared_ptr<TreeItem> root;
  NalUnitIndex              units;
};