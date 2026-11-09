#pragma once

#include "TreeItem.h"

#include <algorithm>
#include <functional>
#include <limits>
#include <unordered_map>
#include <utility>

// Built on the GUI thread only after the parser has stopped writing its tree.
class NalUnitIndex
{
public:
  using FrameRanges = std::vector<std::optional<std::pair<uint64_t, uint64_t>>>;
  struct Unit
  {
    std::shared_ptr<TreeItem> item;
    std::optional<int>        frame;
  };

  void build(const std::shared_ptr<TreeItem> &root, const FrameRanges &ranges, int videoStream)
  {
    this->nalUnits.clear();
    this->rowsByItem.clear();
    this->rowsByFrame.clear();
    if (!root)
      return;

    struct Interval
    {
      uint64_t start;
      uint64_t end;
      int      frame;
    };
    std::vector<Interval> intervals;
    for (size_t frame = 0; frame < ranges.size(); ++frame)
      if (ranges[frame])
        intervals.push_back({ranges[frame]->first, ranges[frame]->second, int(frame)});
    std::sort(intervals.begin(),
              intervals.end(),
              [](const auto &a, const auto &b) { return a.start < b.start; });

    std::function<bool(const std::shared_ptr<TreeItem> &)> hasSlice;
    hasSlice = [&hasSlice](const std::shared_ptr<TreeItem> &item)
    {
      if (const auto &info = item->getNalUnitInfo())
        return info->isSlice;
      for (size_t i = 0; i < item->getNrChildItems(); ++i)
        if (hasSlice(item->getChild(unsigned(i))))
          return true;
      return false;
    };

    // A packet's coding index is not its display index when B frames are present.
    std::vector<std::shared_ptr<TreeItem>> packets;
    bool                                   timestampsKnown = true;
    if (videoStream >= 0)
      for (size_t i = 0; i < root->getNrChildItems(); ++i)
      {
        auto packet = root->getChild(unsigned(i));
        if (packet->getStreamIndex() != videoStream || !hasSlice(packet))
          continue;
        const auto pts = packet->getPresentationTimestamp();
        if (!pts || *pts == std::numeric_limits<int64_t>::min())
          timestampsKnown = false;
        packets.push_back(packet);
      }
    std::unordered_map<const TreeItem *, int> packetFrames;
    if (timestampsKnown)
    {
      std::stable_sort(packets.begin(),
                       packets.end(),
                       [](const auto &a, const auto &b)
                       { return a->getPresentationTimestamp() < b->getPresentationTimestamp(); });
      for (size_t i = 0; i < packets.size(); ++i)
      {
        const auto pts = packets[i]->getPresentationTimestamp();
        // Duplicate timestamps are ambiguous: keep the NAL visible without guessing a picture.
        if ((i > 0 && packets[i - 1]->getPresentationTimestamp() == pts) ||
            (i + 1 < packets.size() && packets[i + 1]->getPresentationTimestamp() == pts))
          continue;
        packetFrames.emplace(packets[i].get(), int(i));
      }
    }

    std::function<void(const std::shared_ptr<TreeItem> &, std::optional<int>)> collect;
    collect = [&](const std::shared_ptr<TreeItem> &item, std::optional<int> packetFrame)
    {
      if (const auto found = packetFrames.find(item.get()); found != packetFrames.end())
        packetFrame = found->second;
      if (const auto &info = item->getNalUnitInfo())
      {
        const auto stream = item->getStreamIndex();
        if (videoStream >= 0 && stream >= 0 && stream != videoStream)
          return;
        std::optional<int> frame;
        if (info->isSlice)
        {
          if (videoStream >= 0)
            frame = packetFrame;
          else if (info->fileOffset)
          {
            auto interval = std::upper_bound(intervals.begin(),
                                             intervals.end(),
                                             *info->fileOffset,
                                             [](uint64_t offset, const auto &value)
                                             { return offset < value.start; });
            if (interval != intervals.begin())
            {
              --interval;
              if (*info->fileOffset <= interval->end)
                frame = interval->frame;
            }
          }
        }
        const auto row = this->nalUnits.size();
        this->rowsByItem.emplace(item.get(), row);
        this->nalUnits.push_back({item, frame});
        if (frame)
          this->rowsByFrame[*frame].push_back(row);
        return;
      }
      for (size_t i = 0; i < item->getNrChildItems(); ++i)
        collect(item->getChild(unsigned(i)), packetFrame);
    };
    collect(root, {});
  }

  const std::vector<Unit> &units() const { return this->nalUnits; }
  std::optional<size_t>    rowForItem(const TreeItem *item) const
  {
    if (const auto found = this->rowsByItem.find(item); found != this->rowsByItem.end())
      return found->second;
    return {};
  }
  std::optional<int> frameForItem(const TreeItem *item) const
  {
    while (item)
    {
      if (const auto row = this->rowForItem(item))
        return this->nalUnits[*row].frame;
      const auto parent = item->getParentItem().lock();
      item              = parent.get();
    }
    return {};
  }
  const std::vector<size_t> &rowsForFrame(int frame) const
  {
    static const std::vector<size_t> empty;
    const auto                       found = this->rowsByFrame.find(frame);
    return found == this->rowsByFrame.end() ? empty : found->second;
  }

private:
  std::vector<Unit>                            nalUnits;
  std::unordered_map<const TreeItem *, size_t> rowsByItem;
  std::unordered_map<int, std::vector<size_t>> rowsByFrame;
};