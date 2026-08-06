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
    const auto lcd = juce::Colour(0xff102c2d);
    const auto lcdRaised = juce::Colour(0xff1d3b3b);
    const auto mint = juce::Colour(0xff73cdbd);
    const auto paleText = juce::Colour(0xffdbe8df);
    const auto shell = juce::Colour(0xffb8c1a8);

    hardwareLookAndFeel.setColour(
        juce::TextButton::buttonColourId, juce::Colour(0xff435c52));
    hardwareLookAndFeel.setColour(
        juce::TextButton::buttonOnColourId, mint.darker(0.2f));
    hardwareLookAndFeel.setColour(
        juce::TextButton::textColourOffId, paleText);
    hardwareLookAndFeel.setColour(
        juce::ComboBox::backgroundColourId, lcdRaised);
    hardwareLookAndFeel.setColour(
        juce::ComboBox::outlineColourId, juce::Colour(0xff6f8b7d));
    hardwareLookAndFeel.setColour(
        juce::ComboBox::textColourId, paleText);
    hardwareLookAndFeel.setColour(
        juce::ComboBox::arrowColourId, mint);
    hardwareLookAndFeel.setColour(
        juce::Label::textColourId, juce::Colour(0xff283b34));
    hardwareLookAndFeel.setColour(
        juce::Slider::trackColourId, juce::Colour(0xff40594f));
    hardwareLookAndFeel.setColour(
        juce::Slider::thumbColourId, mint);
    hardwareLookAndFeel.setColour(
        juce::Slider::textBoxBackgroundColourId, lcdRaised);
    hardwareLookAndFeel.setColour(
        juce::Slider::textBoxTextColourId, paleText);
    hardwareLookAndFeel.setColour(
        juce::Slider::textBoxOutlineColourId, juce::Colour(0xff6f8b7d));
    hardwareLookAndFeel.setColour(
        juce::TableListBox::backgroundColourId, lcd);
    hardwareLookAndFeel.setColour(
        juce::TableHeaderComponent::backgroundColourId, lcd);
    hardwareLookAndFeel.setColour(
        juce::TableHeaderComponent::textColourId, mint);
    hardwareLookAndFeel.setColour(
        juce::PopupMenu::backgroundColourId, juce::Colour(0xff203b39));
    hardwareLookAndFeel.setColour(
        juce::PopupMenu::textColourId, paleText);
    hardwareLookAndFeel.setColour(
        juce::PopupMenu::highlightedBackgroundColourId, mint.darker(0.45f));
    setLookAndFeel(&hardwareLookAndFeel);

    setColour(juce::ResizableWindow::backgroundColourId, shell);
    addAndMakeVisible(openCsvButton);
    addAndMakeVisible(seriesGraph);
    addAndMakeVisible(seriesSelector);
    addAndMakeVisible(selectedCellLabel);
    addAndMakeVisible(transformNoneButton);
    addAndMakeVisible(transformLogButton);
    addAndMakeVisible(transformDifferenceButton);
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
    addAndMakeVisible(pitchBoundsButton);
    addAndMakeVisible(pitchCentreSpreadButton);
    addAndMakeVisible(centreNoteLabel);
    addAndMakeVisible(centreNoteSlider);
    addAndMakeVisible(pitchSpreadLabel);
    addAndMakeVisible(pitchSpreadSlider);
    addAndMakeVisible(outputNoteButton);
    addAndMakeVisible(outputControllerButton);
    addAndMakeVisible(controllerNumberLabel);
    addAndMakeVisible(controllerNumberSlider);
    addAndMakeVisible(controllerMinimumLabel);
    addAndMakeVisible(controllerMinimumSlider);
    addAndMakeVisible(controllerMaximumLabel);
    addAndMakeVisible(controllerMaximumSlider);
    addAndMakeVisible(crtOverlay);

    seriesSelector.setTextWhenNothingSelected("Select series");
    seriesSelector.setColour(juce::ComboBox::backgroundColourId, lcd);
    seriesSelector.setColour(juce::ComboBox::outlineColourId, mint);
    seriesSelector.setColour(juce::ComboBox::textColourId, mint);
    seriesSelector.setColour(juce::ComboBox::arrowColourId, mint);

    openCsvButton.setColour(juce::TextButton::buttonColourId, lcd);
    openCsvButton.setColour(juce::TextButton::textColourOffId, mint);

    for (auto* button : { &transformNoneButton,
                          &transformLogButton,
                          &transformDifferenceButton })
    {
        button->setColour(juce::TextButton::buttonColourId, lcd);
        button->setColour(juce::TextButton::buttonOnColourId, mint);
        button->setColour(juce::TextButton::textColourOffId, mint);
        button->setColour(juce::TextButton::textColourOnId, lcd);
    }

    selectedCellLabel.setText("Selected cell: none", juce::dontSendNotification);
    selectedCellLabel.setJustificationType(juce::Justification::topLeft);
    selectedCellLabel.setColour(
        juce::Label::textColourId,
        mint);

    transformSelector.addItem("None", 1);
    transformSelector.addItem("ln", 2);
    transformSelector.addItem("Difference", 3);

    const auto selectTransform = [this](int transformId)
    {
        audioProcessor.setSelectedTransform(transformId);
        refreshFromProcessor();

        int validValueCount = 0;
        for (const auto value : processedSeries)
            if (std::isfinite(value))
                ++validValueCount;

        selectedCellLabel.setText(
            "Processed values: " + juce::String(validValueCount),
            juce::dontSendNotification);
    };

    transformNoneButton.onClick = [selectTransform]() { selectTransform(1); };
    transformLogButton.onClick = [selectTransform]() { selectTransform(2); };
    transformDifferenceButton.onClick = [selectTransform]() { selectTransform(3); };

    outputModeLabel.setText("Output", juce::dontSendNotification);
    outputModeSelector.addItemList({ "Note", "Controller" }, 1);
    outputModeAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(),
            "outputMode",
            outputModeSelector);

    const auto selectOutputMode = [this](float normalisedValue)
    {
        if (auto* parameter = audioProcessor.getParameterState()
                .getParameter("outputMode"))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(normalisedValue);
            parameter->endChangeGesture();
        }
    };

    outputNoteButton.onClick = [selectOutputMode]()
    {
        selectOutputMode(0.0f);
    };
    outputControllerButton.onClick = [selectOutputMode]()
    {
        selectOutputMode(1.0f);
    };

    csvTable.setModel(this);
    csvTable.getHeader().addMouseListener(this, false);
    csvTable.getHeader().setStretchToFitActive(false);
    csvTable.setColour(
        juce::TableListBox::outlineColourId,
        mint.withAlpha(0.5f));
    csvTable.setOutlineThickness(1);

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

    lowestNoteLabel.setText("Low", juce::dontSendNotification);
    configureNoteSlider(lowestNoteSlider);
    lowestNoteAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "lowestNote",
            lowestNoteSlider);

    highestNoteLabel.setText("High", juce::dontSendNotification);
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

    const auto selectPitchMappingMode = [this](float normalisedValue)
    {
        if (auto* parameter = audioProcessor.getParameterState()
                .getParameter("pitchMappingMode"))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(normalisedValue);
            parameter->endChangeGesture();
        }
    };

    pitchBoundsButton.onClick = [selectPitchMappingMode]()
    {
        selectPitchMappingMode(0.0f);
    };
    pitchCentreSpreadButton.onClick = [selectPitchMappingMode]()
    {
        selectPitchMappingMode(1.0f);
    };

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

    controllerMinimumLabel.setText("Min", juce::dontSendNotification);
    configureControllerSlider(controllerMinimumSlider);
    controllerMinimumAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "controllerMinimum",
            controllerMinimumSlider);

    controllerMaximumLabel.setText("Max", juce::dontSendNotification);
    configureControllerSlider(controllerMaximumSlider);
    controllerMaximumAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "controllerMaximum",
            controllerMaximumSlider);

    setSize(1280, 620);
    refreshFromProcessor();
    startTimerHz(30);
}

DataSonifierAudioProcessorEditor::~DataSonifierAudioProcessorEditor()
{
    stopTimer();
    csvTable.getHeader().removeMouseListener(this);
    setLookAndFeel(nullptr);
}

//==============================================================================
void DataSonifierAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto shell = juce::Colour(0xffb8c1a8);
    const auto shellDark = juce::Colour(0xff7f907f);
    const auto panel = juce::Colour(0xffd2d7c4);
    const auto lcd = juce::Colour(0xff102c2d);
    const auto ink = juce::Colour(0xff263a33);
    const auto mint = juce::Colour(0xff73cdbd);

    g.fillAll(shell);

    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xff40584e), 0.0f, 0.0f,
        juce::Colour(0xff2d433b), 0.0f, 58.0f, false));
    g.fillRect(0, 0, getWidth(), 58);

    g.setColour(juce::Colour(0xffe7eee4));
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("DATA SONIFIER", 24, 8, 260, 28,
        juce::Justification::centredLeft);
    g.setColour(mint);
    g.setFont(juce::FontOptions(11.0f, juce::Font::plain));
    g.drawText("SPREADSHEET TO MIDI", 26, 34, 240, 16,
        juce::Justification::centredLeft);

    const auto drawPanel = [&](juce::Rectangle<int> area,
                               const juce::String& title,
                               bool screen)
    {
        g.setColour(screen ? lcd : panel);
        g.fillRoundedRectangle(area.toFloat(), 7.0f);
        g.setColour(screen ? mint.withAlpha(0.65f) : shellDark);
        g.drawRoundedRectangle(area.toFloat(), 7.0f, 1.5f);
        g.setColour(screen ? mint : ink);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(title, area.getX() + 12, area.getY() + 7,
            area.getWidth() - 24, 18, juce::Justification::centredLeft);
    };

    drawPanel({ 24, 76, 642, 520 }, "DATA DISPLAY", true);
    drawPanel({ 682, 76, 330, 520 }, "MIDI", false);
    drawPanel({ 1028, 76, 228, 520 }, "MIDI CC TARGET", false);

    g.setColour(ink.withAlpha(0.7f));
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.setColour(mint);
    g.drawText("FILE", 40, 102, 100, 14, juce::Justification::left);
    g.drawText("SERIES", 150, 102, 280, 14, juce::Justification::left);
    g.setColour(juce::Colour(0xff315c52));
    g.drawText("NOTE / CONTROLLER", 698, 102, 180, 14,
        juce::Justification::left);
    g.setColour(juce::Colour(0xff1f302a).withAlpha(0.88f));
    g.setFont(juce::FontOptions(10.0f, juce::Font::plain));
    g.drawFittedText(
        "Used in Controller mode. Match this CC number to the parameter in the receiving synth.",
        1044, 108, 196, 58,
        juce::Justification::topLeft, 4);
}

void DataSonifierAudioProcessorEditor::resized()
{
    openCsvButton.setBounds(40, 120, 100, 34);
    seriesSelector.setBounds(150, 120, 280, 34);

    csvTable.setBounds(40, 168, 494, 166);
    selectedCellLabel.setBounds(40, 342, 494, 26);
    seriesGraph.setBounds(40, 376, 494, 198);
    transformNoneButton.setBounds(550, 190, 96, 34);
    transformLogButton.setBounds(550, 246, 96, 34);
    transformDifferenceButton.setBounds(550, 302, 96, 34);
    crtOverlay.setBounds(24, 76, 642, 520);
    crtOverlay.toFront(false);

    outputNoteButton.setBounds(698, 122, 145, 32);
    outputControllerButton.setBounds(851, 122, 145, 32);
    timingModeLabel.setBounds(698, 172, 55, 28);
    timingModeSelector.setBounds(756, 172, 90, 30);
    subdivisionLabel.setBounds(856, 172, 60, 28);
    subdivisionSelector.setBounds(916, 172, 80, 30);
    playbackSpeedLabel.setBounds(698, 212, 55, 28);
    playbackSpeedSlider.setBounds(756, 212, 240, 30);

    pitchMappingModeLabel.setBounds(698, 274, 78, 28);
    pitchBoundsButton.setBounds(778, 272, 104, 32);
    pitchCentreSpreadButton.setBounds(890, 272, 106, 32);
    lowestNoteLabel.setBounds(698, 324, 48, 28);
    lowestNoteSlider.setBounds(746, 324, 250, 30);
    highestNoteLabel.setBounds(698, 370, 48, 28);
    highestNoteSlider.setBounds(746, 370, 250, 30);
    centreNoteLabel.setBounds(698, 416, 48, 28);
    centreNoteSlider.setBounds(746, 416, 250, 30);
    pitchSpreadLabel.setBounds(698, 462, 48, 28);
    pitchSpreadSlider.setBounds(746, 462, 250, 30);

    controllerNumberLabel.setBounds(1044, 172, 196, 24);
    controllerNumberSlider.setBounds(1044, 198, 196, 32);

    // Controller range reuses the same two rows as the note bounds.
    // Separate components preserve independent Note and Controller settings.
    controllerMinimumLabel.setBounds(698, 324, 48, 28);
    controllerMinimumSlider.setBounds(746, 324, 250, 30);
    controllerMaximumLabel.setBounds(698, 370, 48, 28);
    controllerMaximumSlider.setBounds(746, 370, 250, 30);
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

    const auto screenBackground = juce::Colour(0xff0b2425);
    const auto screenForeground = juce::Colour(0xff73cdbd);

    if (isSelectedColumn)
        g.fillAll(screenForeground.withAlpha(0.12f));

    if (isSelectedCell)
        g.fillAll(screenForeground);

    int col = columnId - 1;

    if (rowNumber >= 0 && rowNumber < csvData.size())
    {
        auto& row = csvData[rowNumber];

        if (col >= 0 && col < row.size())
        {
            g.setColour(isSelectedCell
                ? screenBackground
                : screenForeground);
            g.drawText(
                row[col],
                4, 2,
                width - 8,
                height - 4,
                juce::Justification::centredLeft,
                1);
        }
    }

    g.setColour(screenForeground.withAlpha(0.22f));
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
    transformNoneButton.setToggleState(
        state.selectedTransform == 1, juce::dontSendNotification);
    transformLogButton.setToggleState(
        state.selectedTransform == 2, juce::dontSendNotification);
    transformDifferenceButton.setToggleState(
        state.selectedTransform == 3, juce::dontSendNotification);

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

    pitchBoundsButton.setEnabled(usingNotes);
    pitchCentreSpreadButton.setEnabled(usingNotes);
    pitchBoundsButton.setToggleState(
        usingBounds, juce::dontSendNotification);
    pitchCentreSpreadButton.setToggleState(
        usingCentreAndSpread, juce::dontSendNotification);
    lowestNoteSlider.setEnabled(usingBounds);
    highestNoteSlider.setEnabled(usingBounds);
    centreNoteSlider.setEnabled(usingCentreAndSpread);
    pitchSpreadSlider.setEnabled(usingCentreAndSpread);

    const bool usingController = outputMode == 1;
    outputNoteButton.setToggleState(
        usingNotes, juce::dontSendNotification);
    outputControllerButton.setToggleState(
        usingController, juce::dontSendNotification);
    controllerNumberSlider.setEnabled(usingController);

    pitchMappingModeLabel.setVisible(usingNotes);
    pitchBoundsButton.setVisible(usingNotes);
    pitchCentreSpreadButton.setVisible(usingNotes);
    lowestNoteLabel.setVisible(usingNotes);
    lowestNoteSlider.setVisible(usingNotes);
    highestNoteLabel.setVisible(usingNotes);
    highestNoteSlider.setVisible(usingNotes);
    centreNoteLabel.setVisible(usingNotes);
    centreNoteSlider.setVisible(usingNotes);
    pitchSpreadLabel.setVisible(usingNotes);
    pitchSpreadSlider.setVisible(usingNotes);

    controllerMinimumLabel.setVisible(usingController);
    controllerMinimumSlider.setVisible(usingController);
    controllerMaximumLabel.setVisible(usingController);
    controllerMaximumSlider.setVisible(usingController);
    controllerMinimumSlider.setEnabled(usingController);
    controllerMaximumSlider.setEnabled(usingController);
}
