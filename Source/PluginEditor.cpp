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
    addAndMakeVisible(lowestNoteLabel);
    addAndMakeVisible(lowestNoteSlider);
    addAndMakeVisible(highestNoteLabel);
    addAndMakeVisible(highestNoteSlider);
    addAndMakeVisible(pitchMappingModeLabel);
    addAndMakeVisible(pitchMappingModeSelector);
    addAndMakeVisible(centreNoteLabel);
    addAndMakeVisible(centreNoteSlider);
    addAndMakeVisible(pitchSpreadLabel);
    addAndMakeVisible(pitchSpreadSlider);
    addAndMakeVisible(outputModeLabel);
    addAndMakeVisible(outputModeSelector);
    addAndMakeVisible(controllerNumberLabel);
    addAndMakeVisible(controllerNumberSlider);
    addAndMakeVisible(controllerMinimumLabel);
    addAndMakeVisible(controllerMinimumSlider);
    addAndMakeVisible(controllerMaximumLabel);
    addAndMakeVisible(controllerMaximumSlider);

    seriesSelector.setTextWhenNothingSelected("Select series");

    selectedCellLabel.setText("Selected cell: none", juce::dontSendNotification);
    selectedCellLabel.setJustificationType(juce::Justification::topLeft);

    transformSelector.addItem("None", 1);
    transformSelector.addItem("ln", 2);
    transformSelector.addItem("Difference", 3);

    outputModeLabel.setText("Output", juce::dontSendNotification);
    outputModeSelector.addItemList({ "Note", "Controller" }, 1);
    outputModeAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(),
            "outputMode",
            outputModeSelector);

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

    const auto configureNoteSlider = [](juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(
            juce::Slider::TextBoxRight,
            false,
            60,
            25);
        slider.setRange(0.0, 127.0, 1.0);
    };

    lowestNoteLabel.setText("Low Note", juce::dontSendNotification);
    configureNoteSlider(lowestNoteSlider);
    lowestNoteAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "lowestNote",
            lowestNoteSlider);

    highestNoteLabel.setText("High Note", juce::dontSendNotification);
    configureNoteSlider(highestNoteSlider);
    highestNoteAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "highestNote",
            highestNoteSlider);

    pitchMappingModeLabel.setText("Pitch Mode", juce::dontSendNotification);
    pitchMappingModeSelector.addItemList(
        { "Bounds", "Centre + Spread" },
        1);
    pitchMappingModeAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(),
            "pitchMappingMode",
            pitchMappingModeSelector);

    centreNoteLabel.setText("Centre", juce::dontSendNotification);
    configureNoteSlider(centreNoteSlider);
    centreNoteAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "centreNote",
            centreNoteSlider);

    pitchSpreadLabel.setText("Spread", juce::dontSendNotification);
    pitchSpreadSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    pitchSpreadSlider.setTextBoxStyle(
        juce::Slider::TextBoxRight,
        false,
        70,
        25);
    pitchSpreadSlider.setRange(0.0, 64.0, 1.0);
    pitchSpreadSlider.setTextValueSuffix(" st");
    pitchSpreadAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "pitchSpread",
            pitchSpreadSlider);

    const auto configureControllerSlider = [](juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(
            juce::Slider::TextBoxRight,
            false,
            65,
            25);
        slider.setRange(0.0, 127.0, 1.0);
    };

    controllerNumberLabel.setText("CC Number", juce::dontSendNotification);
    configureControllerSlider(controllerNumberSlider);
    controllerNumberAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "controllerNumber",
            controllerNumberSlider);

    controllerMinimumLabel.setText("CC Min", juce::dontSendNotification);
    configureControllerSlider(controllerMinimumSlider);
    controllerMinimumAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "controllerMinimum",
            controllerMinimumSlider);

    controllerMaximumLabel.setText("CC Max", juce::dontSendNotification);
    configureControllerSlider(controllerMaximumSlider);
    controllerMaximumAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "controllerMaximum",
            controllerMaximumSlider);

    setSize(700, 780);
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
    outputModeLabel.setBounds(430, 60, 50, 30);
    outputModeSelector.setBounds(480, 60, getWidth() - 500, 30);

    timingModeLabel.setBounds(20, 100, 50, 30);
    timingModeSelector.setBounds(70, 100, 100, 30);

    subdivisionLabel.setBounds(190, 100, 60, 30);
    subdivisionSelector.setBounds(250, 100, 100, 30);

    playbackSpeedLabel.setBounds(370, 100, 55, 30);
    playbackSpeedSlider.setBounds(425, 100, getWidth() - 445, 30);

    pitchMappingModeLabel.setBounds(20, 140, 75, 30);
    pitchMappingModeSelector.setBounds(95, 140, 140, 30);

    lowestNoteLabel.setBounds(255, 140, 80, 30);
    lowestNoteSlider.setBounds(335, 140, 120, 30);

    highestNoteLabel.setBounds(480, 140, 80, 30);
    highestNoteSlider.setBounds(560, 140, 120, 30);

    centreNoteLabel.setBounds(20, 180, 50, 30);
    centreNoteSlider.setBounds(70, 180, 255, 30);

    pitchSpreadLabel.setBounds(350, 180, 50, 30);
    pitchSpreadSlider.setBounds(400, 180, getWidth() - 420, 30);

    controllerNumberLabel.setBounds(20, 220, 75, 30);
    controllerNumberSlider.setBounds(95, 220, 140, 30);

    controllerMinimumLabel.setBounds(255, 220, 55, 30);
    controllerMinimumSlider.setBounds(310, 220, 165, 30);

    controllerMaximumLabel.setBounds(490, 220, 55, 30);
    controllerMaximumSlider.setBounds(545, 220, getWidth() - 565, 30);

    csvTable.setBounds(
        20,
        260,
        getWidth() - 40,
        220);

    selectedCellLabel.setBounds(
        20,
        490,
        getWidth() - 40,
        30);

    seriesGraph.setBounds(
        20,
        530,
        getWidth() - 40,
        getHeight() - 550);
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

    const int pitchMode = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("pitchMappingMode")->load()));

    const int outputMode = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("outputMode")->load()));

    const bool usingNotes = outputMode == 0;
    const bool usingBounds = usingNotes && pitchMode == 0;
    const bool usingCentreAndSpread = usingNotes && pitchMode == 1;

    pitchMappingModeSelector.setEnabled(usingNotes);
    lowestNoteSlider.setEnabled(usingBounds);
    highestNoteSlider.setEnabled(usingBounds);
    centreNoteSlider.setEnabled(usingCentreAndSpread);
    pitchSpreadSlider.setEnabled(usingCentreAndSpread);

    const bool usingController = outputMode == 1;
    controllerNumberSlider.setEnabled(usingController);
    controllerMinimumSlider.setEnabled(usingController);
    controllerMaximumSlider.setEnabled(usingController);
}
