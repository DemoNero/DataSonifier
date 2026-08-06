/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <limits>

class HardwareLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(
        juce::Graphics& g,
        juce::Button& button,
        const juce::Colour& backgroundColour,
        bool isMouseOverButton,
        bool isButtonDown) override
    {
        auto area = button.getLocalBounds().toFloat().reduced(1.0f);
        const float radius = area.getHeight() * 0.5f;

        auto colour = backgroundColour;
        if (isButtonDown)
            colour = colour.brighter(0.18f);
        else if (isMouseOverButton)
            colour = colour.brighter(0.10f);

        g.setColour(colour);
        g.fillRoundedRectangle(area, radius);
        g.setColour(button.findColour(
            button.getToggleState()
                ? juce::TextButton::textColourOnId
                : juce::TextButton::textColourOffId).withAlpha(0.65f));
        g.drawRoundedRectangle(area, radius, 1.0f);
    }
};

class CrtOverlayComponent : public juce::Component
{
public:
    CrtOverlayComponent()
    {
        setInterceptsMouseClicks(false, false);
    }

    void paint(juce::Graphics& g) override
    {
        const auto mint = juce::Colour(0xff73cdbd);

        g.setColour(mint.withAlpha(0.045f));
        for (int y = 0; y < getHeight(); y += 4)
            g.fillRect(0, y, getWidth(), 1);

        juce::ColourGradient vignette(
            juce::Colours::transparentBlack,
            getWidth() * 0.5f,
            getHeight() * 0.5f,
            juce::Colours::black.withAlpha(0.22f),
            0.0f,
            0.0f,
            true);
        g.setGradientFill(vignette);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 4.0f);
    }
};

//==============================================================================
/**
*/

class SeriesGraphComponent : public juce::Component
{
public:
    void setValues(const std::vector<double>& newValues)
    {
        values = newValues;
        repaint();
    }

    void setTitle(const juce::String& newTitle)
    {
        title = newTitle;
        repaint();
    }

    void setPlaybackIndex(int newIndex)
    {
        if (playbackIndex == newIndex)
            return;

        playbackIndex = newIndex;
        repaint();
    }

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(10.0f);


        bounds.removeFromTop(40.0f);
        bounds.removeFromBottom(15.0f);

        g.fillAll(juce::Colour(0xff0b2425));
        
        

        g.setColour(juce::Colour(0xff73cdbd).withAlpha(0.42f));

        // Y axis
        g.drawLine(
            bounds.getX(),
            bounds.getY(),
            bounds.getX(),
            bounds.getBottom(),
            1.0f);

        // X axis
        g.drawLine(
            bounds.getX(),
            bounds.getBottom(),
            bounds.getRight(),
            bounds.getBottom(),
            1.0f);

        if (values.size() < 2)
        {
            g.setColour(juce::Colour(0xff73cdbd));
            g.drawText(
                "Select a numeric series",
                getLocalBounds(),
                juce::Justification::centred);
            return;
        }

        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();

        for (double value : values)
        {
            if (!std::isfinite(value))
                continue;

            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }

        if (!std::isfinite(minimum) || !std::isfinite(maximum))
        {
            g.setColour(juce::Colour(0xff73cdbd));
            g.drawText(
                "No valid numeric values",
                getLocalBounds(),
                juce::Justification::centred);

            return;
        }

        if (minimum == maximum)
        {
            minimum -= 1.0;
            maximum += 1.0;
        }

        double range = maximum - minimum;
        double padding = range * 0.05;

        double dataMinimum = minimum;
        double dataMaximum = maximum;

        minimum -= padding;
        maximum += padding;

        juce::Path graphPath;
        bool currentlyDrawing = false;

        for (int i = 0; i < static_cast<int>(values.size()); ++i)
        {
            const double value = values[i];

            if (!std::isfinite(value))
            {
                currentlyDrawing = false;
                continue;
            }

            float x = juce::jmap(
                static_cast<float>(i),
                0.0f,
                static_cast<float>(values.size() - 1),
                bounds.getX(),
                bounds.getRight());

            float y = juce::jmap(
                static_cast<float>(value),
                static_cast<float>(minimum),
                static_cast<float>(maximum),
                bounds.getBottom(),
                bounds.getY());

            if (!currentlyDrawing)
            {
                graphPath.startNewSubPath(x, y);
                currentlyDrawing = true;
            }
            else
            {
                graphPath.lineTo(x, y);
            }
        }

        g.setColour(juce::Colour(0xff73cdbd));
        g.setFont(16.0f);

        g.drawText(
            title,
            10,
            5,
            bounds.getWidth(),
            25,
            juce::Justification::centred);

        g.setColour(juce::Colour(0xff55d6c2));
        g.strokePath(graphPath, juce::PathStrokeType(2.0f,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

        if (juce::isPositiveAndBelow(
                playbackIndex,
                static_cast<int>(values.size())))
        {
            const double playbackValue = values[playbackIndex];

            if (std::isfinite(playbackValue))
            {
                const float markerX = juce::jmap(
                    static_cast<float>(playbackIndex),
                    0.0f,
                    static_cast<float>(values.size() - 1),
                    bounds.getX(),
                    bounds.getRight());

                const float markerY = juce::jmap(
                    static_cast<float>(playbackValue),
                    static_cast<float>(minimum),
                    static_cast<float>(maximum),
                    bounds.getBottom(),
                    bounds.getY());

                constexpr float markerDiameter = 12.0f;
                g.setColour(juce::Colour(0xff73cdbd));
                g.fillEllipse(
                    markerX - markerDiameter * 0.5f,
                    markerY - markerDiameter * 0.5f,
                    markerDiameter,
                    markerDiameter);
            }
        }

        g.setColour(juce::Colour(0xff73cdbd));

        auto formatValue = [](double value)
            {
                if (std::floor(value) == value)
                    return juce::String(static_cast<long long>(value));

                return juce::String(value, 4)
                    .trimCharactersAtEnd("0")
                    .trimCharactersAtEnd(".");
            };

        g.drawText(
            "Max: " + formatValue(dataMaximum),
            10, 0,
            getWidth() - 70, 70,
            juce::Justification::left);

        g.drawText(
            "Min: " + formatValue(dataMinimum),
            10, getHeight() - 20,
            getWidth() - 15, 15,
            juce::Justification::left);
    }

private:
    std::vector<double> values;
    juce::String title;
    int playbackIndex = -1;
};

class DataSonifierAudioProcessorEditor  : public juce::AudioProcessorEditor, 
                                            public juce::TableListBoxModel,
                                            private juce::Timer
{
public:
    DataSonifierAudioProcessorEditor (DataSonifierAudioProcessor&);
    ~DataSonifierAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;
    
    int getNumRows() override;

    void paintRowBackground(juce::Graphics& g, int rowNumber,
        int width, int height, bool rowIsSelected) override;

    void paintCell(juce::Graphics& g, int rowNumber, int columnId,
        int width, int height, bool rowIsSelected) override;

    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;

    void refreshFromProcessor();
    void rebuildTableColumns();
    void rebuildSeriesSelector();

    void mouseDown(const juce::MouseEvent& event) override;

private:
    void timerCallback() override;

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    DataSonifierAudioProcessor& audioProcessor;

    HardwareLookAndFeel hardwareLookAndFeel;

    juce::TextButton openCsvButton{ "Open CSV" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::vector<juce::StringArray> csvData;

    juce::TableListBox csvTable;
    juce::ComboBox seriesSelector;

    juce::Label selectedCellLabel;
    int selectedRow = -1;
    int selectedColumn = -1;

    juce::ComboBox transformSelector;
    juce::TextButton transformNoneButton{ "RAW" };
    juce::TextButton transformLogButton{ "LN" };
    juce::TextButton transformDifferenceButton{ "DIFF" };

    juce::Slider playbackSpeedSlider;
    juce::Label playbackSpeedLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        playbackSpeedAttachment;

    juce::ComboBox timingModeSelector;
    juce::Label timingModeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        timingModeAttachment;

    juce::ComboBox subdivisionSelector;
    juce::Label subdivisionLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        subdivisionAttachment;

    juce::Slider lowestNoteSlider;
    juce::Label lowestNoteLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        lowestNoteAttachment;

    juce::Slider highestNoteSlider;
    juce::Label highestNoteLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        highestNoteAttachment;

    juce::ComboBox pitchMappingModeSelector;
    juce::Label pitchMappingModeLabel;
    juce::TextButton pitchBoundsButton{ "BOUNDS" };
    juce::TextButton pitchCentreSpreadButton{ "CENTRE / SPREAD" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        pitchMappingModeAttachment;

    juce::Slider centreNoteSlider;
    juce::Label centreNoteLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        centreNoteAttachment;

    juce::Slider pitchSpreadSlider;
    juce::Label pitchSpreadLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        pitchSpreadAttachment;

    juce::ComboBox outputModeSelector;
    juce::Label outputModeLabel;
    juce::TextButton outputNoteButton{ "NOTE" };
    juce::TextButton outputControllerButton{ "CONTROLLER" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        outputModeAttachment;

    juce::Slider controllerNumberSlider;
    juce::Label controllerNumberLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        controllerNumberAttachment;

    juce::Slider controllerMinimumSlider;
    juce::Label controllerMinimumLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        controllerMinimumAttachment;

    juce::Slider controllerMaximumSlider;
    juce::Label controllerMaximumLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        controllerMaximumAttachment;

    std::vector<double> processedSeries;

    juce::StringArray columnHeaders;
    int selectedHeaderColumn = -1;

    SeriesGraphComponent seriesGraph;
    CrtOverlayComponent crtOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DataSonifierAudioProcessorEditor)
};
