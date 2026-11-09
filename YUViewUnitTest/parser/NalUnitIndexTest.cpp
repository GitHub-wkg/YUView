#include <gtest/gtest.h>
#include <limits>
#include <parser/common/NalUnitIndex.h>
#include <parser/common/NalUnitRanges.h>

namespace
{
auto nal(const std::shared_ptr<TreeItem> &parent, uint64_t offset, bool slice = true)
{
  auto item = parent->createChildItem("NAL");
  item->setNalUnitInfo({offset, {}, 10, slice});
  return item;
}
} // namespace

TEST(NalUnitIndex, MapsReorderedAccessUnitsAndMultipleSlices)
{
  auto         root         = std::make_shared<TreeItem>();
  auto         first        = nal(root, 0);
  auto         third        = nal(root, 100);
  auto         second       = nal(root, 200);
  auto         anotherSlice = nal(root, 220);
  auto         field        = anotherSlice->createChildItem("slice_type", 1);
  NalUnitIndex index;
  index.build(root, {{{0, 99}}, {{200, 299}}, {{100, 199}}}, -1);
  EXPECT_EQ(index.frameForItem(first.get()), 0);
  EXPECT_EQ(index.frameForItem(third.get()), 2);
  EXPECT_EQ(index.frameForItem(second.get()), 1);
  EXPECT_EQ(index.frameForItem(field.get()), 1);
  EXPECT_EQ(index.rowsForFrame(1).size(), 2u);
}

TEST(NalUnitIndex, ParameterSetsHaveNoPictureAndLargeOffsetsArePreserved)
{
  auto           root       = std::make_shared<TreeItem>();
  const uint64_t offset     = uint64_t{1} << 34;
  auto           parameters = nal(root, offset, false);
  auto           slice      = nal(root, offset + 16);
  NalUnitIndex   index;
  index.build(root, {{{offset, offset + 100}}}, -1);
  EXPECT_FALSE(index.frameForItem(parameters.get()));
  EXPECT_EQ(index.frameForItem(slice.get()), 0);
  EXPECT_EQ(index.units().front().item->getNalUnitInfo()->fileOffset, offset);
}

TEST(NalUnitIndex, ContainerUsesPresentationOrderAndIgnoresAudio)
{
  auto root   = std::make_shared<TreeItem>();
  auto packet = [&root](int stream, int64_t pts)
  {
    auto item = root->createChildItem("packet");
    item->setStreamIndex(stream);
    item->setPresentationTimestamp(pts);
    return nal(item, 0);
  };
  auto first = packet(0, 0);
  packet(1, 50);
  auto         third  = packet(0, 20);
  auto         second = packet(0, 10);
  NalUnitIndex index;
  index.build(root, {}, 0);
  EXPECT_EQ(index.units().size(), 3u);
  EXPECT_EQ(index.frameForItem(first.get()), 0);
  EXPECT_EQ(index.frameForItem(third.get()), 2);
  EXPECT_EQ(index.frameForItem(second.get()), 1);
}

TEST(NalUnitIndex, UnknownTimestampDoesNotInventFrameOrderAndResetClearsData)
{
  auto root   = std::make_shared<TreeItem>();
  auto packet = root->createChildItem("packet");
  packet->setStreamIndex(0);
  packet->setPresentationTimestamp(std::numeric_limits<int64_t>::min());
  auto         slice = nal(packet, 0);
  NalUnitIndex index;
  index.build(root, {}, 0);
  EXPECT_EQ(index.units().size(), 1u);
  EXPECT_FALSE(index.frameForItem(slice.get()));
  index.build({}, {}, -1);
  EXPECT_TRUE(index.units().empty());
  EXPECT_TRUE(index.rowsForFrame(0).empty());
}

TEST(NalUnitRanges, KeepsFirstNalAndMixedStartCodes)
{
  const std::vector<uint8_t> bytes{0, 0, 0, 1, 0x67, 1, 0, 0, 1, 0x68, 2};
  const auto                 units = splitNalUnits(bytes, true);
  EXPECT_EQ(units.size(), 2u);
  EXPECT_EQ(units[0].offset, 0u);
  EXPECT_EQ(units[0].payloadOffset, 4u);
  EXPECT_EQ(units[0].end, 6u);
  EXPECT_EQ(units[1].offset, 6u);
  EXPECT_TRUE(units[0].complete);
}

TEST(NalUnitRanges, RejectsTruncatedAndEmptyPayloadsWithoutInvalidIterators)
{
  EXPECT_FALSE(splitNalUnits(std::vector<uint8_t>{0, 1}, false)[0].complete);
  EXPECT_FALSE(splitNalUnits(std::vector<uint8_t>{0, 0, 0, 5, 0x65}, false)[0].complete);
  EXPECT_FALSE(splitNalUnits(std::vector<uint8_t>{0, 0, 1}, true)[0].complete);
  EXPECT_TRUE(splitNalUnits(std::vector<uint8_t>{}, true).empty());
  const auto shortLength = splitNalUnits(std::vector<uint8_t>{1, 0x65, 1, 0x41}, false, 1);
  EXPECT_EQ(shortLength.size(), 2u);
  EXPECT_EQ(shortLength[1].payloadOffset, 3u);
}

TEST(NalUnitIndex, ParameterOnlyPacketsDoNotConsumeDisplayFrameNumbers)
{
  auto root       = std::make_shared<TreeItem>();
  auto parameters = root->createChildItem("parameter packet");
  parameters->setStreamIndex(0);
  parameters->setPresentationTimestamp(std::numeric_limits<int64_t>::min());
  auto sps     = nal(parameters, 0, false);
  auto picture = root->createChildItem("first picture packet");
  picture->setStreamIndex(0);
  picture->setPresentationTimestamp(10);
  auto         slice = nal(picture, 0);
  NalUnitIndex index;
  index.build(root, {}, 0);
  EXPECT_EQ(index.units().size(), 2u);
  EXPECT_FALSE(index.frameForItem(sps.get()));
  EXPECT_EQ(index.frameForItem(slice.get()), 0);
}