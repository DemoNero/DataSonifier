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
        juce::ComboBox::textColourId, juce::Colour(0xffbfe4d9));
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
        juce::Slider::textBoxTextColourId, juce::Colour(0xffbfe4d9));
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
    addAndMakeVisible(timingFreeButton);
    addAndMakeVisible(timingSyncButton);
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
    openCsvButton.getProperties().set("lcdButton", true);

    for (auto* button : { &transformNoneButton,
                          &transformLogButton,
                          &transformDifferenceButton })
    {
        button->getProperties().set("lcdButton", true);
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

    for (auto* button : { &outputNoteButton, &outputControllerButton,
                          &timingFreeButton, &timingSyncButton,
                          &pitchBoundsButton, &pitchCentreSpreadButton })
        button->getProperties().set("hardwareSwitch", true);

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
        "SPEED",
        juce::dontSendNotification);

    playbackSpeedSlider.setSliderStyle(
        juce::Slider::LinearHorizontal);

    playbackSpeedSlider.setTextBoxStyle(
        juce::Slider::TextBoxRight,
        false,
        96,
        25);

    playbackSpeedSlider.setRange(
        0.25,
        8.0,
        0.25);

    playbackSpeedSlider.setTextValueSuffix(
        " NOTE/S");

    playbackSpeedAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "speed",
            playbackSpeedSlider);

    timingModeLabel.setText("TIMING MODE", juce::dontSendNotification);
    timingModeSelector.addItemList({ "FREE", "SYNC" }, 1);
    timingModeAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(),
            "timingMode",
            timingModeSelector);

    const auto selectTimingMode = [this](float normalisedValue)
    {
        if (auto* parameter = audioProcessor.getParameterState()
                .getParameter("timingMode"))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(normalisedValue);
            parameter->endChangeGesture();
        }
    };

    timingFreeButton.onClick = [selectTimingMode]()
    {
        selectTimingMode(0.0f);
    };
    timingSyncButton.onClick = [selectTimingMode]()
    {
        selectTimingMode(1.0f);
    };

    subdivisionLabel.setText("DIVISION", juce::dontSendNotification);
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

    lowestNoteLabel.setText("LOW", juce::dontSendNotification);
    configureNoteSlider(lowestNoteSlider);
    lowestNoteAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "lowestNote",
            lowestNoteSlider);

    highestNoteLabel.setText("HIGH", juce::dontSendNotification);
    configureNoteSlider(highestNoteSlider);
    highestNoteAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "highestNote",
            highestNoteSlider);

    pitchMappingModeLabel.setText("PITCH MODE", juce::dontSendNotification);
    pitchMappingModeLabel.setFont(
        juce::FontOptions(10.0f, juce::Font::bold));
    pitchMappingModeLabel.setColour(
        juce::Label::textColourId, juce::Colour(0xff315c52));
    pitchMappingModeSelector.addItemList(
        { "Range", "Midpoint + Spread" },
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

    centreNoteLabel.setText("MIDPOINT", juce::dontSendNotification);
    configureNoteSlider(centreNoteSlider);
    centreNoteAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "centreNote",
            centreNoteSlider);

    pitchSpreadLabel.setText("SPREAD", juce::dontSendNotification);
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

    controllerNumberLabel.setText("CC NUMBER", juce::dontSendNotification);
    configureControllerSlider(controllerNumberSlider);
    controllerNumberAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "controllerNumber",
            controllerNumberSlider);

    controllerMinimumLabel.setText("MIN", juce::dontSendNotification);
    configureControllerSlider(controllerMinimumSlider);
    controllerMinimumAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "controllerMinimum",
            controllerMinimumSlider);

    controllerMaximumLabel.setText("MAX", juce::dontSendNotification);
    configureControllerSlider(controllerMaximumSlider);
    controllerMaximumAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            "controllerMaximum",
            controllerMaximumSlider);

    const auto hardwareLegendColour = juce::Colour(0xff315c52);
    for (auto* label : { &timingModeLabel, &subdivisionLabel,
                         &playbackSpeedLabel, &lowestNoteLabel,
                         &highestNoteLabel, &centreNoteLabel,
                         &pitchSpreadLabel, &controllerNumberLabel,
                         &controllerMinimumLabel, &controllerMaximumLabel })
    {
        label->setFont(juce::FontOptions(11.5f, juce::Font::bold));
        label->setColour(juce::Label::textColourId, hardwareLegendColour);
    }

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

    // A narrow name strip leaves the interface feeling like rack hardware
    // instead of a software header.
    g.setGradientFill(juce::ColourGradient(
        juce::Colour(0xffd8ddcc), 0.0f, 0.0f,
        juce::Colour(0xffaeb9a3), 0.0f, 24.0f, false));
    g.fillRect(0, 0, getWidth(), 24);
    g.setColour(juce::Colour(0xff7b2f2b));
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("DATA SONIFIER", 24, 3, 180, 18,
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

    drawPanel({ 24, 34, 642, 562 }, "DATA DISPLAY", true);
    drawPanel({ 682, 34, 330, 562 }, "MIDI", false);
    drawPanel({ 1028, 34, 228, 562 }, "MIDI CC TARGET", false);

    g.setColour(ink.withAlpha(0.7f));
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.setColour(mint);
    g.drawText("FILE", 40, 60, 100, 14, juce::Justification::left);
    g.drawText("SERIES", 150, 60, 280, 14, juce::Justification::left);
    g.setColour(juce::Colour(0xff315c52));
    g.drawText("OUTPUT MODE", 698, 60, 180, 14,
        juce::Justification::left);

    const auto hardwareRed = juce::Colour(0xff7b2f2b);
    g.setColour(hardwareRed);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("NOTE", 720, 78, 78, 14, juce::Justification::centred);
    g.drawText("CONTROLLER", 862, 78, 100, 14, juce::Justification::centred);
    g.drawText("FREE", 720, 148, 78, 14, juce::Justification::centred);
    g.drawText("SYNC", 873, 148, 78, 14, juce::Justification::centred);
    const int outputMode = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("outputMode")->load()));
    if (outputMode == 0)
    {
        g.drawText("RANGE", 718, 306, 82, 14, juce::Justification::centred);
        g.drawText("MIDPOINT / SPREAD", 857, 306, 110, 14,
            juce::Justification::centred);
    }

    g.setColour(juce::Colour(0xff1f302a).withAlpha(0.88f));
    g.setFont(juce::FontOptions(10.0f, juce::Font::plain));
    g.drawFittedText(
        "Match this CC to the receiving synth control.",
        1044, 154, 196, 34,
        juce::Justification::topLeft, 2);
}

void DataSonifierAudioProcessorEditor::resized()
{
    openCsvButton.setBounds(40, 78, 100, 32);
    seriesSelector.setBounds(150, 78, 280, 32);

    csvTable.setBounds(40, 126, 494, 166);
    selectedCellLabel.setBounds(40, 300, 494, 26);
    seriesGraph.setBounds(40, 334, 494, 240);
    transformNoneButton.setBounds(550, 148, 96, 30);
    transformLogButton.setBounds(550, 204, 96, 30);
    transformDifferenceButton.setBounds(550, 260, 96, 30);
    crtOverlay.setBounds(24, 34, 642, 562);
    crtOverlay.toFront(false);

    outputNoteButton.setBounds(746, 94, 26, 18);
    outputControllerButton.setBounds(899, 94, 26, 18);
    timingModeLabel.setBounds(693, 128, 130, 18);
    timingFreeButton.setBounds(746, 164, 26, 18);
    timingSyncButton.setBounds(899, 164, 26, 18);
    subdivisionLabel.setBounds(693, 196, 74, 28);
    subdivisionSelector.setBounds(778, 196, 100, 30);
    playbackSpeedLabel.setBounds(693, 236, 62, 28);
    playbackSpeedSlider.setBounds(756, 236, 240, 30);

    pitchMappingModeLabel.setBounds(693, 286, 130, 18);
    pitchBoundsButton.setBounds(746, 322, 26, 18);
    pitchCentreSpreadButton.setBounds(899, 322, 26, 18);
    lowestNoteLabel.setBounds(693, 360, 65, 28);
    lowestNoteSlider.setBounds(758, 360, 238, 30);
    highestNoteLabel.setBounds(693, 406, 65, 28);
    highestNoteSlider.setBounds(758, 406, 238, 30);
    centreNoteLabel.setBounds(693, 452, 65, 28);
    centreNoteSlider.setBounds(758, 452, 238, 30);
    pitchSpreadLabel.setBounds(693, 498, 65, 28);
    pitchSpreadSlider.setBounds(758, 498, 238, 30);

    controllerNumberLabel.setBounds(1039, 90, 201, 24);
    controllerNumberSlider.setBounds(1044, 116, 196, 32);

    // Controller range reuses the same two rows as the note bounds.
    // Separate components preserve independent Note and Controller settings.
    controllerMinimumLabel.setBounds(693, 360, 65, 28);
    controllerMinimumSlider.setBounds(758, 360, 238, 30);
    controllerMaximumLabel.setBounds(693, 406, 65, 28);
    controllerMaximumSlider.setBounds(758, 406, 238, 30);
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

    if (lastPaintedOutputMode != outputMode)
    {
        lastPaintedOutputMode = outputMode;
        repaint();
    }

    const int timingMode = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("timingMode")->load()));

    const bool usingFreeTiming = timingMode == 0;
    timingFreeButton.setToggleState(
        usingFreeTiming, juce::dontSendNotification);
    timingSyncButton.setToggleState(
        !usingFreeTiming, juce::dontSendNotification);
    playbackSpeedSlider.setEnabled(usingFreeTiming);
    subdivisionSelector.setEnabled(!usingFreeTiming);

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
