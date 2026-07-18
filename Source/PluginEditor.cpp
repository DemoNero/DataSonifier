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
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    addAndMakeVisible(openCsvButton);

    addAndMakeVisible(seriesGraph);

    openCsvButton.onClick = [this]()
        {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Select a CSV file...",
                juce::File{},
                "*.csv");

            addAndMakeVisible(seriesSelector);
            seriesSelector.setTextWhenNothingSelected("Select series");

            addAndMakeVisible(selectedCellLabel);
            selectedCellLabel.setText("Selected cell: none", juce::dontSendNotification);
            selectedCellLabel.setJustificationType(juce::Justification::topLeft);

            addAndMakeVisible(transformSelector);
            transformSelector.addItem("None", 1);
            transformSelector.addItem("ln", 2);
            transformSelector.addItem("Difference", 3);
            transformSelector.setSelectedId(1);

            transformSelector.onChange = [this]()
                {
                    updateProcessedSeries();
                    audioProcessor.setProcessedSeries(processedSeries);
                };

            seriesSelector.onChange = [this]()
                {
                    selectedRow = -1; // clear clicked cell
                    selectedColumn = seriesSelector.getSelectedId();

                    updateProcessedSeries();
                    audioProcessor.setProcessedSeries(processedSeries);

                    int columnWidth = 120;
                    int tableWidth = csvTable.getWidth();

                    int targetX = ((selectedColumn - 1) * columnWidth)
                        - (tableWidth / 2)
                        + (columnWidth / 2);

                    targetX = juce::jmax(0, targetX);

                    csvTable.getViewport()->setViewPosition(
                        targetX,
                        csvTable.getViewport()->getViewPositionY());

                    csvTable.repaint();
                };

            addAndMakeVisible(csvTable);
            csvTable.setModel(this);

            csvTable.getHeader().addMouseListener(this, false);

            csvTable.getHeader().setStretchToFitActive(false);

            fileChooser->launchAsync(
                juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                [this](const juce::FileChooser& chooser)
                {
                    auto file = chooser.getResult();

                    if (file.existsAsFile())
                    {
                        auto csvText = file.loadFileAsString();

                        juce::StringArray lines;
                        lines.addLines(csvText);

                        csvData.clear();

                        for (int i = 0; i < lines.size(); ++i)
                        {
                            juce::StringArray cells;
                            cells.addTokens(lines[i], ",", "\"");
                            cells.trim();

                            csvData.push_back(cells);
                        }

                        seriesSelector.clear();

                        if (!csvData.empty())
                        {
                            int columnCount = csvData[0].size();

                            for (int col = 0; col < columnCount; ++col)
                            {
                                seriesSelector.addItem(
                                    csvData[0][col],
                                    col + 1);
                            }
                        }

                        csvTable.getHeader().removeAllColumns();

                        if (!csvData.empty())
                        {
                            int columnCount = csvData[0].size();

                            for (int col = 0; col < columnCount; ++col)
                            {
                                csvTable.getHeader().addColumn(
                                    csvData[0][col],     // use first row as header
                                    col + 1,
                                    120,120,120, juce::TableHeaderComponent::notSortable);                // maybe make them a bit wider
                            }
                        }
                        columnHeaders = csvData[0];
                        csvData.erase(csvData.begin());

                        originalCsvData = csvData;

                        csvTable.updateContent();
                        csvTable.repaint();
                    }
                });
        };
    addAndMakeVisible(playbackSpeedLabel);
    playbackSpeedLabel.setText(
        "Speed",
        juce::dontSendNotification);

    addAndMakeVisible(playbackSpeedSlider);

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

    playbackSpeedSlider.setValue(
        1.0,
        juce::dontSendNotification);

    playbackSpeedSlider.setTextValueSuffix(
        " notes/s");

    playbackSpeedSlider.onValueChange = [this]()
        {
            audioProcessor.setPlaybackSpeed(
                playbackSpeedSlider.getValue());
        };
    setSize(700, 650);
}

DataSonifierAudioProcessorEditor::~DataSonifierAudioProcessorEditor()
{
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

    playbackSpeedLabel.setBounds(
        430,
        60,
        55,
        30);

    playbackSpeedSlider.setBounds(
        485,
        60,
        getWidth() - 505,
        30);

    csvTable.setBounds(
        20,
        100,
        getWidth() - 40,
        250);

    selectedCellLabel.setBounds(
        20,
        360,
        getWidth() - 40,
        40);

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
    selectedColumn = columnId;

    updateProcessedSeries();
    audioProcessor.setProcessedSeries(processedSeries);

    seriesSelector.setSelectedId(columnId, juce::dontSendNotification);

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

void DataSonifierAudioProcessorEditor::updateProcessedSeries()
{
    csvData = originalCsvData;
    processedSeries.clear();

    if (selectedColumn <= 0 || csvData.empty())
    {
        seriesGraph.setValues({});
        return;
    }

    const int col = selectedColumn - 1;
    const int transform = transformSelector.getSelectedId();

    if (col >= 0 && col < columnHeaders.size())
        seriesGraph.setTitle(columnHeaders[col]);

    const double missingValue =
        std::numeric_limits<double>::quiet_NaN();

    // Attempts to parse the entire cell as a number.
    auto tryParseNumber = [](const juce::String& input, double& result)
        {
            auto text = input.trim();

            if (text.isEmpty() || !text.containsAnyOf("0123456789"))
                return false;

            // Reject text such as "2013 prices".
            auto numericCharacters =
                text.retainCharacters("0123456789+-.eE");

            if (numericCharacters != text)
                return false;

            result = text.getDoubleValue();
            return std::isfinite(result);
        };

    std::vector<double> rawValues;
    rawValues.reserve(originalCsvData.size());

    for (int row = 0; row < static_cast<int>(originalCsvData.size()); ++row)
    {
        if (col >= originalCsvData[row].size())
        {
            rawValues.push_back(missingValue);
            continue;
        }

        double value = 0.0;

        if (tryParseNumber(originalCsvData[row][col], value))
            rawValues.push_back(value);
        else
            rawValues.push_back(missingValue);
    }

    if (transform == 1) // None
    {
        processedSeries = rawValues;

        csvTable.updateContent();
        csvTable.repaint();
        seriesGraph.setValues(processedSeries);
        return;
    }

    if (transform == 2) // ln
    {
        for (double value : rawValues)
        {
            if (std::isfinite(value) && value > 0.0)
                processedSeries.push_back(std::log(value));
            else
                processedSeries.push_back(missingValue);
        }
    }
    else if (transform == 3) // Difference
    {
        processedSeries.resize(rawValues.size(), missingValue);

        for (int row = 1; row < static_cast<int>(rawValues.size()); ++row)
        {
            const double current = rawValues[row];
            const double previous = rawValues[row - 1];

            if (std::isfinite(current) && std::isfinite(previous))
                processedSeries[row] = current - previous;
        }
    }

    // Display transformed values in the selected table column.
    for (int row = 0; row < static_cast<int>(processedSeries.size()); ++row)
    {
        if (row >= static_cast<int>(csvData.size())
            || col >= csvData[row].size())
        {
            continue;
        }

        const double value = processedSeries[row];

        if (std::isfinite(value))
            csvData[row].set(col, juce::String(value, 4));
        else
            csvData[row].set(col, "N/A");
    }

    csvTable.updateContent();
    csvTable.repaint();

    seriesGraph.setValues(processedSeries);



    int validValueCount = 0;

    for (double value : processedSeries)
    {
        if (std::isfinite(value))
            ++validValueCount;
    }

    selectedCellLabel.setText(
        "Processed values: " + juce::String(validValueCount),
        juce::dontSendNotification);
    
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
    selectedColumn = columnId;
    selectedHeaderColumn = columnId;

    // Synchronise the selector without running its onChange callback.
    seriesSelector.setSelectedId(
        columnId,
        juce::dontSendNotification);

    // Recalculate the selected series and update the graph/table.
    updateProcessedSeries();
    audioProcessor.setProcessedSeries(processedSeries);

    const int col = columnId - 1;

    if (col >= 0 && col < columnHeaders.size())
    {
        selectedCellLabel.setText(
            "Selected column header: " + columnHeaders[col],
            juce::dontSendNotification);
    }

    csvTable.repaint();
}
