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

    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced(10.0f);


        bounds.removeFromTop(40.0f);
        bounds.removeFromBottom(15.0f);

        g.fillAll(juce::Colours::darkgrey.darker());
        
        

        g.setColour(juce::Colour(120, 120, 120));

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
            g.setColour(juce::Colours::lightgrey);
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
            g.setColour(juce::Colours::lightgrey);
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

        g.setColour(juce::Colours::white);
        g.setFont(16.0f);

        g.drawText(
            title,
            10,
            5,
            bounds.getWidth(),
            25,
            juce::Justification::centred);

        g.setColour(juce::Colours::cyan);
        g.strokePath(graphPath, juce::PathStrokeType(2.0f,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

        g.setColour(juce::Colours::lightgrey);

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
};

class DataSonifierAudioProcessorEditor  : public juce::AudioProcessorEditor, 
                                            public juce::TableListBoxModel
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
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    DataSonifierAudioProcessor& audioProcessor;

    juce::TextButton openCsvButton{ "Open CSV" };
    std::unique_ptr<juce::FileChooser> fileChooser;

    std::vector<juce::StringArray> csvData;

    juce::TableListBox csvTable;
    juce::ComboBox seriesSelector;

    juce::Label selectedCellLabel;
    int selectedRow = -1;
    int selectedColumn = -1;

    juce::ComboBox transformSelector;

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

    std::vector<double> processedSeries;

    juce::StringArray columnHeaders;
    int selectedHeaderColumn = -1;

    SeriesGraphComponent seriesGraph;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DataSonifierAudioProcessorEditor)
};
