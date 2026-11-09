#include "NalUnitModel.h"

#include <QBrush>
#include <QColor>
#include <QFontDatabase>

void NalUnitModel::setTree(std::shared_ptr<TreeItem>        newRoot,
                           const NalUnitIndex::FrameRanges &ranges,
                           int                              videoStream)
{
  beginResetModel();
  root = std::move(newRoot);
  units.build(root, ranges, videoStream);
  endResetModel();
}

QModelIndex NalUnitModel::index(int row, int column, const QModelIndex &parent) const
{
  if (!hasIndex(row, column, parent))
    return {};
  const auto item =
    parent.isValid()
      ? static_cast<TreeItem *>(parent.internalPointer())->getChild(unsigned(row)).get()
      : units.units()[size_t(row)].item.get();
  return createIndex(row, column, item);
}

QModelIndex NalUnitModel::parent(const QModelIndex &child) const
{
  if (!child.isValid())
    return {};
  auto item = static_cast<TreeItem *>(child.internalPointer());
  if (units.rowForItem(item))
    return {};
  const auto parentItem = item->getParentItem().lock();
  if (!parentItem)
    return {};
  if (const auto row = units.rowForItem(parentItem.get()))
    return createIndex(int(*row), 0, parentItem.get());
  const auto grandparent = parentItem->getParentItem().lock();
  if (grandparent)
    if (const auto row = grandparent->getIndexOfChildItem(parentItem))
      return createIndex(int(*row), 0, parentItem.get());
  return {};
}

int NalUnitModel::rowCount(const QModelIndex &parent) const
{
  if (parent.isValid() && parent.column() != 0)
    return 0;
  if (!parent.isValid())
    return int(units.units().size());
  return int(static_cast<TreeItem *>(parent.internalPointer())->getNrChildItems());
}

QVariant NalUnitModel::headerData(int section, Qt::Orientation orientation, int role) const
{
  if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
    return {};
  switch (section)
  {
  case 0:
    return tr("Offset");
  case 1:
    return tr("Name");
  case 2:
    return tr("Value");
  case 3:
    return tr("Frame");
  default:
    return {};
  }
}

QVariant NalUnitModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid())
    return {};
  const auto  item = static_cast<TreeItem *>(index.internalPointer());
  const auto &info = item->getNalUnitInfo();
  if (role == Qt::ForegroundRole && item->isError())
    return QBrush(QColor(Qt::red));
  if (role == Qt::FontRole && index.column() == 0)
    return QFontDatabase::systemFont(QFontDatabase::FixedFont);
  if (role == Qt::ToolTipRole)
  {
    if (info)
    {
      const auto frame = units.frameForItem(item);
      auto       text  = tr("NAL unit: %1 bytes").arg(qulonglong(info->byteSize));
      text += frame
                ? tr("\nDisplay frame: %1").arg(*frame)
                : tr("\nNo corresponding display frame (parameter data or unavailable mapping)");
      if (info->packetOffset)
        text += tr("\nPacket-relative offset; not an absolute file position.");
      return text;
    }
    return QString::fromStdString(item->getData(0) + "\nValue: " + item->getData(1) +
                                  "\nCoding: " + item->getData(2) + "\nCode: " + item->getData(3) +
                                  "\nMeaning: " + item->getData(4));
  }
  if (role != Qt::DisplayRole)
    return {};
  switch (index.column())
  {
  case 0:
    if (info && info->fileOffset)
      return QStringLiteral("0x%1").arg(qulonglong(*info->fileOffset), 8, 16, QLatin1Char('0'));
    if (info && info->packetOffset)
      return QStringLiteral("pkt+0x%1")
        .arg(qulonglong(*info->packetOffset), 4, 16, QLatin1Char('0'));
    return {};
  case 1:
    return QString::fromStdString(item->getData(0));
  case 2:
  {
    if (info)
      return tr("%1 bytes").arg(qulonglong(info->byteSize));
    auto       value   = QString::fromStdString(item->getData(1));
    const auto meaning = QString::fromStdString(item->getData(4));
    if (!meaning.isEmpty())
      value += QStringLiteral(" (%1)").arg(meaning);
    return value;
  }
  case 3:
    if (info)
      if (const auto frame = units.frameForItem(item))
        return *frame;
    return {};
  default:
    return {};
  }
}

std::optional<int> NalUnitModel::frameForIndex(const QModelIndex &index) const
{
  return index.isValid() ? units.frameForItem(static_cast<TreeItem *>(index.internalPointer()))
                         : std::nullopt;
}

QModelIndex NalUnitModel::indexForFrame(int frame) const
{
  const auto &rows = units.rowsForFrame(frame);
  return rows.empty() ? QModelIndex() : index(int(rows.front()), 0);
}