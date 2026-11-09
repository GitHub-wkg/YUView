#include <gtest/gtest.h>
#include <parser/common/NalUnitModel.h>
#include <ui/widgets/StreamViewerWidget.h>

#include <QAbstractItemModelTester>
#include <QApplication>
#include <QSignalSpy>
#include <QTreeView>

namespace
{
struct NalTree
{
  std::shared_ptr<TreeItem> root = std::make_shared<TreeItem>();
  std::shared_ptr<TreeItem> first;
  std::shared_ptr<TreeItem> second;
  std::shared_ptr<TreeItem> field;
  NalUnitIndex::FrameRanges ranges{{{0, 99}}, {{100, 199}}};

  NalTree()
  {
    first = root->createChildItem("NAL 0: IDR Slice");
    first->setNalUnitInfo({0, {}, 100, true});
    auto header = first->createChildItem("slice_header");
    field       = header->createChildItem("entropy_coding_mode_flag", 1, "u(1)", "1", "CABAC");
    second      = root->createChildItem("NAL 1: P Slice");
    second->setNalUnitInfo({100, {}, 100, true});
  }
};
} // namespace

TEST(NalUnitModel, NestedSyntaxHasValidParentsAndCombinedValues)
{
  NalTree                  input;
  NalUnitModel             model;
  QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
  model.setTree(input.root, input.ranges, -1);
  const auto nal    = model.index(0, 0);
  const auto header = model.index(0, 0, nal);
  const auto field  = model.index(0, 2, header);
  EXPECT_EQ(model.parent(header), nal);
  EXPECT_EQ(model.parent(field), header);
  EXPECT_EQ(model.data(field).toString().toStdString(), "1 (CABAC)");
  EXPECT_EQ(model.frameForIndex(field), 0);
  EXPECT_EQ(model.data(nal).toString().toStdString(), "0x00000000");
  model.setTree({}, {}, -1);
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(NalUnitModel, FlattensContainerNalsWithoutExposingPacketParents)
{
  auto root   = std::make_shared<TreeItem>();
  auto packet = root->createChildItem("AVPacket 0");
  packet->setStreamIndex(0);
  packet->setPresentationTimestamp(0);
  auto nal = packet->createChildItem("NAL 0");
  nal->setNalUnitInfo({{}, 4, 20, true});
  nal->createChildItem("field", 1);
  NalUnitModel             model;
  QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
  model.setTree(root, {}, 0);
  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_FALSE(model.parent(model.index(0, 0)).isValid());
  EXPECT_EQ(model.data(model.index(0, 0)).toString().toStdString(), "pkt+0x0004");
  EXPECT_EQ(model.frameForIndex(model.index(0, 0)), 0);
}

TEST(StreamViewerWidget, BidirectionalSelectionDoesNotFeedBackIntoPlayback)
{
  NalTree            input;
  StreamViewerWidget viewer;
  viewer.resize(800, 500);
  viewer.show();
  QSignalSpy selection(&viewer, &StreamViewerWidget::frameSelected);
  viewer.setTree(input.root, input.ranges, -1);
  auto tree = viewer.findChild<QTreeView *>();
  ASSERT_NE(tree, nullptr);
  auto model = tree->model();
  tree->setCurrentIndex(model->index(1, 1));
  ASSERT_EQ(selection.size(), 1);
  EXPECT_EQ(selection[0][0].toInt(), 1);
  viewer.setCurrentFrame(0);
  EXPECT_EQ(selection.size(), 1);
  EXPECT_EQ(tree->currentIndex().row(), 0);
  const auto nal = model->index(0, 0);
  tree->expand(nal);
  const auto header = model->index(0, 0, nal);
  tree->expand(header);
  const auto field = model->index(0, 1, header);
  tree->setCurrentIndex(field);
  const auto count = selection.size();
  viewer.setCurrentFrame(0);
  EXPECT_EQ(tree->currentIndex(), field);
  EXPECT_EQ(selection.size(), count);
  QApplication::processEvents();
  EXPECT_TRUE(tree->visualRect(field).isValid());
  viewer.setCurrentFrame(10);
  EXPECT_FALSE(tree->currentIndex().isValid());
  viewer.clear();
  EXPECT_EQ(model->rowCount(), 0);
  EXPECT_EQ(selection.size(), count);
}