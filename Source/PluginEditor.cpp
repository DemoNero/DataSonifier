/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <limits>
//==============================================================================
DataSonifierAudioProcessorEditor::DataSonifierAudioProcessorEditor (DataSonifierAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    addAndMakeVisible(openCsvButton);
    addAndMakeVisible(seriesGraph);
    addAndMakeVisible(seriesSelector);
    addAndMakeVisible(selectedCellLabel);
    addAndMakeVisible(transformSelector);
    addAndMakeVisible(csvTable);
    addAndMakeVisible(playbackSpeedLabel);
    addAndMakeVisible(playbackSpeedSlider);
    addAndMakeVisible(timingModeLabel);
    addAndMakeVisible(timingModeSelector);
    addAndMakeVisible(subdivisionLabel);
    addAndMakeVisible(subdivisionSelector);

    seriesSelector.setTextWhenNothingSelected("Select series");

    selectedCellLabel.setText("Selected cell: none", juce::dontSendNotification);
    selectedCellLabel.setJustificationType(juce::Justification::topLeft);

    transformSelector.addItem("None", 1);
    transformSelector.addItem("ln", 2);
    transformSelector.addItem("Difference", 3);

    csvTable.setModel(this);
    csvTable.getHeader().addMouseListener(this, false);
    csvTable.getHeader().setStretchToFitActive(false);

    openCsvButton.onClick = [this]()
    {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Select a CSV file...", juce::File{}, "*.csv");

        fileChooser->launchAsync(
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectFiles,
            [this](const juce::FileChooser& chooser)
            {
                if (audioProcessor.loadCsvFile(chooser.getResult()))
                {
                    selectedRow = -1;
                    selectedHeaderColumn = -1;
                    selectedCellLabel.setText("", juce::dontSendNotification);
                    refreshFromProcessor();
                }
            });
    };

    transformSelector.onChange = [this]()
    {
        audioProcessor.setSelectedTransform(transformSelector.getSelectedId());
        refreshFromProcessor();

        int validValueCount = 0;
        for (const auto value : processedSeries)
            if (std::isfinite(value))
                ++validValueCount;

        selectedCellLabel.setText(
            "Processed values: " + juce::String(validValueCount),
            juce::dontSendNotification);
    };

    seriesSelector.onChange = [this]()
    {
        selectedRow = -1;
        selectedHeaderColumn = -1;
        selectedCellLabel.setText("", juce::dontSendNotification);

        audioProcessor.setSelectedColumn(seriesSelector.getSelectedId());
        refreshFromProcessor();

        constexpr int columnWidth = 120;
        const int targetX = juce::jmax(0,
            ((selectedColumn - 1) * columnWidth)
                - (csvTable.getWidth() / 2)
                + (columnWidth / 2));

        csvTable.getViewport()->setViewPosition(
            targetX,
            csvTable.getViewport()->getViewPositionY());
    };

    playbackSpeedLabel.setText(
        "Speed",
        juce::dontSendNotification);

    playbackSpeedSlider.setSliderStyle(
        juce::Slider::LinearHorizontal);

    playbackSpeedSlider.setTextBoxStyle(
        juce::Slider::TextBoxRight,
        false,
        80,
        25);

    playbackSpeedSlider.setRange(
        0.25,
        8.0,
        0.25);

    playbackSpeedSlider.setTextValueSuffix(
        " notes/s");

    playbackSpeedAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "speed",
            playbackSpeedSlider);

    timingModeLabel.setText("Timing", juce::dontSendNotification);
    timingModeSelector.addItemList({ "Free", "Sync" }, 1);
    timingModeAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(),
            "timingMode",
            timingModeSelector);

    subdivisionLabel.setText("Division", juce::dontSendNotification);
    subdivisionSelector.addItemList(
        { "1/1", "1/2", "1/4", "1/8", "1/16",
          "1/4T", "1/8T", "1/16T" },
        1);
    subdivisionAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(),
            "subdivision",
            subdivisionSelector);

    setSize(700, 650);
    refreshFromProcessor();
    startTimerHz(30);
}

DataSonifierAudioProcessorEditor::~DataSonifierAudioProcessorEditor()
{
    stopTimer();
    csvTable.getHeader().removeMouseListener(this);
}

//==============================================================================
void DataSonifierAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void DataSonifierAudioProcessorEditor::resized()
{
    openCsvButton.setBounds(20, 20, 150, 30);
    seriesSelector.setBounds(20, 60, 220, 30);
    transformSelector.setBounds(260, 60, 150, 30);

    timingModeLabel.setBounds(20, 100, 50, 30);
    timingModeSelector.setBounds(70, 100, 100, 30);

    subdivisionLabel.setBounds(190, 100, 60, 30);
    subdivisionSelector.setBounds(250, 100, 100, 30);

    playbackSpeedLabel.setBounds(370, 100, 55, 30);
    playbackSpeedSlider.setBounds(425, 100, getWidth() - 445, 30);

    csvTable.setBounds(
        20,
        140,
        getWidth() - 40,
        220);

    selectedCellLabel.setBounds(
        20,
        370,
        getWidth() - 40,
        30);

    seriesGraph.setBounds(
        20,
        410,
        getWidth() - 40,
        getHeight() - 430);
}

int DataSonifierAudioProcessorEditor::getNumRows()
{
    return (int)csvData.size();
}

void DataSonifierAudioProcessorEditor::paintRowBackground(
    juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected)
{
    /*if (rowIsSelected)
        g.fillAll(juce::Colours::darkgrey);*/
}

void DataSonifierAudioProcessorEditor::paintCell(
    juce::Graphics& g,
    int rowNumber,
    int columnId,
    int width,
    int height,
    bool rowIsSelected)
{
    // Clear normal cell background first
    g.fillAll(juce::Colours::transparentBlack);

    bool isSelectedColumn = columnId == selectedColumn;
    bool isSelectedCell = rowNumber == selectedRow && columnId == selectedColumn;

    if (isSelectedColumn)
        g.fillAll(juce::Colours::darkslategrey);

    if (isSelectedCell)
        g.fillAll(juce::Colours::teal);

    int col = columnId - 1;

    if (rowNumber >= 0 && rowNumber < csvData.size())
    {
        auto& row = csvData[rowNumber];

        if (col >= 0 && col < row.size())
        {
            g.setColour(juce::Colours::white);
            g.drawText(
                row[col],
                4, 2,
                width - 8,
                height - 4,
                juce::Justification::centredLeft,
                1);
        }
    }

    g.setColour(juce::Colours::darkgrey);
    g.drawVerticalLine(width - 1, 0.0f, (float)height);
}

void DataSonifierAudioProcessorEditor::cellClicked(
    int rowNumber,
    int columnId,
    const juce::MouseEvent&)
{
    selectedRow = rowNumber;
    selectedHeaderColumn = -1;
    audioProcessor.setSelectedColumn(columnId);
    refreshFromProcessor();

    int col = columnId - 1;

    if (rowNumber >= 0 && rowNumber < csvData.size()
        && col >= 0 && col < csvData[rowNumber].size())
    {
        selectedCellLabel.setText(
            "Selected cell: " + csvData[rowNumber][col],
            juce::dontSendNotification);
    }

    csvTable.repaint();
}

void DataSonifierAudioProcessorEditor::refreshFromProcessor()
{
    const auto state = audioProcessor.getDataStateSnapshot();

    csvData = state.displayData;
    columnHeaders = state.columnHeaders;
    processedSeries = state.processedSeries;
    selectedColumn = state.selectedColumn;

    rebuildTableColumns();
    rebuildSeriesSelector();

    seriesSelector.setSelectedId(selectedColumn, juce::dontSendNotification);
    transformSelector.setSelectedId(state.selectedTransform, juce::dontSendNotification);

    if (selectedColumn > 0 && selectedColumn <= columnHeaders.size())
        seriesGraph.setTitle(columnHeaders[selectedColumn - 1]);
    else
        seriesGraph.setTitle({});

    seriesGraph.setValues(processedSeries);
    csvTable.updateContent();
    csvTable.repaint();
}

void DataSonifierAudioProcessorEditor::rebuildTableColumns()
{
    csvTable.getHeader().removeAllColumns();

    for (int col = 0; col < columnHeaders.size(); ++col)
    {
        csvTable.getHeader().addColumn(
            columnHeaders[col],
            col + 1,
            120,
            120,
            120,
            juce::TableHeaderComponent::notSortable);
    }
}

void DataSonifierAudioProcessorEditor::rebuildSeriesSelector()
{
    seriesSelector.clear(juce::dontSendNotification);

    for (int col = 0; col < columnHeaders.size(); ++col)
        seriesSelector.addItem(columnHeaders[col], col + 1);
}

void DataSonifierAudioProcessorEditor::mouseDown(
    const juce::MouseEvent& event)
{
    if (event.eventComponent != &csvTable.getHeader())
        return;

    const int columnId =
        csvTable.getHeader().getColumnIdAtX(event.x);

    if (columnId <= 0)
        return;

    selectedRow = -1;
    selectedHeaderColumn = columnId;

    audioProcessor.setSelectedColumn(columnId);
    refreshFromProcessor();

    const int col = columnId - 1;

    if (col >= 0 && col < columnHeaders.size())
    {
        selectedCellLabel.setText(
            "Selected column header: " + columnHeaders[col],
            juce::dontSendNotification);
    }

    csvTable.repaint();
}

void DataSonifierAudioProcessorEditor::timerCallback()
{
    seriesGraph.setPlaybackIndex(
        audioProcessor.getCurrentPlaybackIndex());
}
