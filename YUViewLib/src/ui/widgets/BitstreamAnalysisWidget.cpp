/*  This file is part of YUView - The YUV player with advanced analytics toolset
 *   <https://github.com/IENT/YUView>
 *   Copyright (C) 2015  Institut f�r Nachrichtentechnik, RWTH Aachen University, GERMANY
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   In addition, as a special exception, the copyright holders give
 *   permission to link the code of portions of this program with the
 *   OpenSSL library under certain conditions as described in each
 *   individual source file, and distribute linked combinations including
 *   the two.
 *
 *   You must obey the GNU General Public License in all respects for all
 *   of the code used other than OpenSSL. If you modify file(s) with this
 *   exception, you may extend this exception to your version of the
 *   file(s), but you are not obligated to do so. If you do not wish to do
 *   so, delete this exception statement from your version. If you delete
 *   this exception statement from all source files in the program, then
 *   also delete it here.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "BitstreamAnalysisWidget.h"

#include "parser/AVC/ParserAnnexBAVC.h"
#include "parser/AVFormat/ParserAVFormat.h"
#include "parser/HEVC/ParserAnnexBHEVC.h"
#include "parser/Mpeg2/ParserAnnexBMpeg2.h"
#include "parser/VVC/ParserAnnexBVVC.h"

#include <QLoggingCategory>

namespace
{
Q_LOGGING_CATEGORY(bitstreamLog, "yuview.bitstream", QtInfoMsg)
}

#define BITSTREAM_ANALYSIS_WIDGET_DEBUG_OUTPUT 0
#if BITSTREAM_ANALYSIS_WIDGET_DEBUG_OUTPUT
#include <QDebug>
#define DEBUG_ANALYSIS(msg) qDebug() << msg
#else
#define DEBUG_ANALYSIS(msg) ((void)0)
#endif

BitstreamAnalysisWidget::BitstreamAnalysisWidget(QWidget *parent) : QWidget(parent)
{
  this->ui.setupUi(this);
  this->ui.parseEntireFileCheckBox->setChecked(true);
  this->ui.streamInfoTreeWidget->setColumnWidth(0, 300);
  this->updateParsingStatusText(-1);

  this->connect(this->ui.showStreamComboBox,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                &BitstreamAnalysisWidget::showOnlyStreamComboBoxIndexChanged);
  this->connect(this->ui.colorCodeStreamsCheckBox,
                &QCheckBox::toggled,
                this,
                &BitstreamAnalysisWidget::colorCodeStreamsCheckBoxToggled);
  this->connect(this->ui.parseEntireFileCheckBox,
                &QCheckBox::toggled,
                this,
                &BitstreamAnalysisWidget::parseEntireBitstreamCheckBoxToggled);
  this->connect(this->ui.bitratePlotOrderComboBox,
                QOverload<int>::of(&QComboBox::currentIndexChanged),
                this,
                &BitstreamAnalysisWidget::bitratePlotOrderComboBoxIndexChanged);

  this->currentSelectedItemsChanged(nullptr, nullptr, false);
}

MoveAndZoomableView *BitstreamAnalysisWidget::getCurrentActiveView()
{
  const auto idx = this->ui.analysisTab->currentIndex();
  if (idx == 2)
    return this->ui.plotViewWidget;
  if (idx == 3)
    return this->ui.hrdPlotWidget;
  return {};
}

void BitstreamAnalysisWidget::updateParserItemModel()
{
  if (this->parser)
  {
    if (this->backgroundParserFuture.isFinished())
      this->parser->updateNumberModelItems();
    this->updateParsingStatusText(this->parser->getParsingProgressPercent());
  }
}

void BitstreamAnalysisWidget::updateStreamInfo()
{
  this->ui.streamInfoTreeWidget->clear();
  for (auto item : this->parser->getStreamInfo())
    this->ui.streamInfoTreeWidget->addTopLevelItem(item);
  this->ui.streamInfoTreeWidget->expandAll();

  DEBUG_ANALYSIS("BitstreamAnalysisWidget::updateStreamInfo comboBox entries "
                 << this->ui.showStreamComboBox->count() << " parser->getNrStreams "
                 << this->parser->getNrStreams());
  auto nrSelections = this->parser->getNrStreams();
  if (this->parser->getNrStreams() > 1)
    nrSelections += 1;
  if (this->ui.showStreamComboBox->count() != int(nrSelections))
  {
    this->ui.showStreamComboBox->clear();
    if (nrSelections == 1)
    {
      this->ui.showStreamComboBox->addItem("Show stream 0");
      this->ui.showStreamComboBox->setEnabled(false);
    }
    else
    {
      this->ui.showStreamComboBox->setEnabled(true);
      this->ui.showStreamComboBox->addItem("Show all streams");
      for (unsigned i = 0; i < this->parser->getNrStreams(); i++)
      {
        const auto info = this->parser->getShortStreamDescription(i);
        this->ui.showStreamComboBox->addItem(QString("Stream %1 - ").arg(i) +
                                             QString::fromStdString(info));
      }
    }
  }
}

void BitstreamAnalysisWidget::backgroundParsingDone(QString error)
{
  if (!this->parser)
    return;
  // A completed tree is shared read-only with the Stream Viewer.
  this->backgroundParserFuture.waitForFinished();
  this->parser->updateNumberModelItems();
  this->updateStreamInfo();
  this->updateParsingStatusText(100);
  if (this->streamViewer)
  {
    this->streamViewer->setTree(this->parser->getParsedTree(),
                                this->parser->getFrameByteRanges(),
                                this->parser->getVideoStreamIndex());
  }
  if (!error.isEmpty())
  {
    qCWarning(bitstreamLog) << "Bitstream parsing failed:" << error;
    this->ui.parsingStatusText->setText(tr("Error parsing the file: %1").arg(error));
  }
  if (this->streamViewer)
    this->streamViewer->setStatusText(this->ui.parsingStatusText->text());
}

void BitstreamAnalysisWidget::showOnlyStreamComboBoxIndexChanged(int index)
{
  if (this->parser && this->showOnlyStream != index - 1)
  {
    this->showOnlyStream = index - 1;
    this->parser->setFilterStreamIndex(this->showOnlyStream);
  }
}

void BitstreamAnalysisWidget::bitratePlotOrderComboBoxIndexChanged(int index)
{
  if (this->parser)
    this->parser->setBitrateSortingIndex(index);
}

void BitstreamAnalysisWidget::updateParsingStatusText(int progressValue)
{
  if (progressValue <= -1)
    this->ui.parsingStatusText->setText(
        "No bitstream file selected - Select a bitstream file to start bitstream analysis.");
  else if (progressValue < 100)
    this->ui.parsingStatusText->setText(QString("Parsing file (%1%)").arg(progressValue));
  else
  {
    const auto parsingLimitSet = !this->ui.parseEntireFileCheckBox->isChecked();
    this->ui.parsingStatusText->setText(
        parsingLimitSet ? "Partial parsing done. Enable full parsing if needed." : "Parsing done.");
  }
  if (this->streamViewer)
    this->streamViewer->setStatusText(this->ui.parsingStatusText->text());
}

void BitstreamAnalysisWidget::stopAndDeleteParserBlocking()
{
  ++this->parserGeneration; // Reject already queued updates from an earlier source.
  if (!this->parser)
    return;
  disconnect(this->parser.get(), nullptr, this, nullptr);
  if (this->backgroundParserFuture.isRunning())
  {
    qCInfo(bitstreamLog) << "Stopping background bitstream parsing";
    this->parser->setAbortParsing();
    this->backgroundParserFuture.waitForFinished();
  }
  if (this->streamViewer)
    this->streamViewer->clear();
  this->ui.dataTreeView->setModel(nullptr);
  this->ui.plotViewWidget->setModel(nullptr);
  this->ui.hrdPlotWidget->setModel(nullptr);
  this->parser.reset();
}

void BitstreamAnalysisWidget::backgroundParsingFunction(parser::Parser *activeParser,
                                                        const std::filesystem::path &fileName)
{
  try
  {
    if (!activeParser->runParsingOfFile(fileName))
      emit activeParser->backgroundParsingDone(tr("Unable to finish parsing the bitstream."));
  }
  catch (const std::exception &error)
  {
    emit activeParser->backgroundParsingDone(QString::fromUtf8(error.what()));
  }
  catch (...)
  {
    emit activeParser->backgroundParsingDone(tr("Unknown bitstream parsing error."));
  }
}

void BitstreamAnalysisWidget::setStreamViewerVisible(bool visible)
{
  this->streamViewerVisible = visible;
  if (visible && !this->parser)
    this->restartParsingOfCurrentItem();
  else if (!visible && !this->isVisible())
    this->stopAndDeleteParserBlocking();
}

void BitstreamAnalysisWidget::itemAboutToBeDeleted(playlistItem *item)
{
  if (this->currentCompressedVideo == item)
    this->currentSelectedItemsChanged(nullptr, nullptr, false);
}

void BitstreamAnalysisWidget::currentSelectedItemsChanged(playlistItem *item1, playlistItem *, bool)
{
  auto source = dynamic_cast<playlistItemCompressedVideo *>(item1);
  if (this->currentCompressedVideo == source && this->parser)
    return;
  this->stopAndDeleteParserBlocking();
  disconnect(this->sourceReloadStartedConnection);
  disconnect(this->sourceReloadedConnection);
  this->currentCompressedVideo = source;
  if (source)
  {
    this->sourceReloadStartedConnection = connect(source,
        &playlistItemCompressedVideo::sourceReloadStarted, this,
        &BitstreamAnalysisWidget::stopAndDeleteParserBlocking);
    this->sourceReloadedConnection = connect(source,
        &playlistItemCompressedVideo::sourceReloaded, this,
        &BitstreamAnalysisWidget::restartParsingOfCurrentItem);
  }
  this->ui.streamInfoTreeWidget->clear();

  const bool isBitstream = !this->currentCompressedVideo.isNull();
  this->ui.tabStreamInfo->setEnabled(isBitstream);
  this->ui.tabPacketAnalysis->setEnabled(isBitstream);
  this->ui.tabBitrateGraphicsView->setEnabled(isBitstream);

  this->restartParsingOfCurrentItem();
}

void BitstreamAnalysisWidget::updateSettings()
{
  this->ui.plotViewWidget->updateSettings();
  this->ui.hrdPlotWidget->updateSettings();
}

void BitstreamAnalysisWidget::restartParsingOfCurrentItem()
{
  this->stopAndDeleteParserBlocking();
  if (!this->isVisible() && !this->streamViewerVisible)
    return;

  if (this->currentCompressedVideo.isNull())
  {
    DEBUG_ANALYSIS(
        "BitstreamAnalysisWidget::restartParsingOfCurrentItem no compressed video - abort");
    this->updateParsingStatusText(-1);
    this->ui.streamInfoTreeWidget->clear();
    this->ui.dataTreeView->setModel(nullptr);
    this->ui.plotViewWidget->setModel(nullptr);
    this->ui.hrdPlotWidget->setModel(nullptr);
    return;
  }

  this->createAndConnectNewParser(this->currentCompressedVideo->getInputFormat());
  if (!this->parser)
  {
    const auto message = tr("Bitstream analysis is not supported for this input format.");
    this->ui.parsingStatusText->setText(message);
    if (this->streamViewer)
      this->streamViewer->setStatusText(message);
    return;
  }

  this->ui.dataTreeView->setModel(this->parser->getPacketItemModel());
  this->ui.dataTreeView->setColumnWidth(0, 600);
  this->ui.dataTreeView->setColumnWidth(1, 100);
  this->ui.dataTreeView->setColumnWidth(2, 120);
  this->ui.plotViewWidget->setModel(this->parser->getBitratePlotModel());
  this->ui.hrdPlotWidget->setModel(this->parser->getHRDPlotModel());

  this->updateStreamInfo();

  this->updateParsingStatusText(0);
  const auto fileName = std::filesystem::u8path(this->currentCompressedVideo->properties().name.toUtf8().constData());
  qCInfo(bitstreamLog) << "Starting NAL parsing for" << this->currentCompressedVideo->properties().name;
  auto activeParser = this->parser.get();
  this->backgroundParserFuture = QtConcurrent::run([this, activeParser, fileName]
  {
    this->backgroundParsingFunction(activeParser, fileName);
  });
  DEBUG_ANALYSIS(
      "BitstreamAnalysisWidget::restartParsingOfCurrentItem new parser created and started");
}

void BitstreamAnalysisWidget::createAndConnectNewParser(InputFormat inputFormat)
{
  Q_ASSERT_X(
      !this->parser, Q_FUNC_INFO, "Error reinitlaizing parser. The current parser is not null.");
  if (inputFormat == InputFormat::AnnexBHEVC)
    this->parser.reset(new parser::ParserAnnexBHEVC(this));
  else if (inputFormat == InputFormat::AnnexBVVC)
    this->parser.reset(new parser::ParserAnnexBVVC(this));
  else if (inputFormat == InputFormat::AnnexBAVC)
    this->parser.reset(new parser::ParserAnnexBAVC(this));
  else if (inputFormat == InputFormat::Libav)
    this->parser.reset(new parser::ParserAVFormat(this));
  if (!this->parser)
    return;
  this->parser->enableModel();
  const bool parsingLimitSet = !this->ui.parseEntireFileCheckBox->isChecked();
  this->parser->setParsingLimitEnabled(parsingLimitSet);

  const auto generation = this->parserGeneration;
  connect(this->parser.get(), &parser::Parser::modelDataUpdated, this, [this, generation]
  {
    if (generation == this->parserGeneration)
      this->updateParserItemModel();
  });
  connect(this->parser.get(), &parser::Parser::streamInfoUpdated, this, [this, generation]
  {
    if (generation == this->parserGeneration && this->backgroundParserFuture.isFinished())
      this->updateStreamInfo();
  });
  connect(this->parser.get(), &parser::Parser::backgroundParsingDone, this,
          [this, generation](const QString &error)
  {
    if (generation == this->parserGeneration)
      this->backgroundParsingDone(error);
  });
}

void BitstreamAnalysisWidget::hideEvent(QHideEvent *event)
{
  DEBUG_ANALYSIS("BitstreamAnalysisWidget::hideEvent");
  if (!this->streamViewerVisible)
    this->stopAndDeleteParserBlocking();
  QWidget::hideEvent(event);
}

void BitstreamAnalysisWidget::showEvent(QShowEvent *event)
{
  DEBUG_ANALYSIS("BitstreamAnalysisWidget::showEvent");
  if (!this->parser)
    this->restartParsingOfCurrentItem();
  QWidget::showEvent(event);
}