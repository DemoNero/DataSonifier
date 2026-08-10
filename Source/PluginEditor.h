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
    void drawLinearSlider(
        juce::Graphics& g,
        int x, int y, int width, int height,
        float sliderPos,
        float minSliderPos,
        float maxSliderPos,
        juce::Slider::SliderStyle style,
        juce::Slider& slider) override
    {
        juce::ignoreUnused(minSliderPos, maxSliderPos);

        if (style != juce::Slider::LinearHorizontal
            && style != juce::Slider::LinearBar)
        {
            juce::LookAndFeel_V4::drawLinearSlider(
                g, x, y, width, height, sliderPos,
                minSliderPos, maxSliderPos, style, slider);
            return;
        }

        const float alpha = slider.isEnabled() ? 1.0f : 0.42f;
        const float centreY = static_cast<float>(y) + height * 0.5f;
        const auto track = juce::Rectangle<float>(
            static_cast<float>(x) + 2.0f,
            centreY - 3.0f,
            static_cast<float>(width) - 4.0f,
            6.0f);

        // Recessed slot and a fine highlight imitate a rack-unit fader.
        g.setColour(juce::Colour(0xff718173).withAlpha(alpha));
        g.fillRect(track.translated(0.0f, 1.0f));
        g.setColour(juce::Colour(0xff263a33).withAlpha(alpha));
        g.fillRect(track);
        g.setColour(juce::Colour(0xffdbe1d1).withAlpha(0.45f * alpha));
        g.drawHorizontalLine(
            static_cast<int>(track.getY()), track.getX(), track.getRight());

        for (int tick = 0; tick <= 4; ++tick)
        {
            const float tickX = juce::jmap(
                static_cast<float>(tick), 0.0f, 4.0f,
                track.getX() + 2.0f, track.getRight() - 2.0f);
            g.setColour(juce::Colour(0xff53675d).withAlpha(0.7f * alpha));
            g.fillRect(juce::Rectangle<float>(tickX - 0.5f,
                centreY + 5.0f, 1.0f, 3.0f));
        }

        const auto cap = juce::Rectangle<float>(
            sliderPos - 6.0f, centreY - 8.0f, 12.0f, 16.0f);
        g.setColour(slider.findColour(
            juce::Slider::thumbColourId).darker(0.15f).withAlpha(alpha));
        g.fillRoundedRectangle(cap, 1.5f);
        g.setColour(juce::Colour(0xffd8e2d5).withAlpha(0.75f * alpha));
        g.drawRoundedRectangle(cap, 1.5f, 1.0f);
        g.drawHorizontalLine(static_cast<int>(centreY),
            cap.getX() + 2.0f, cap.getRight() - 2.0f);
    }

    void drawComboBox(
        juce::Graphics& g,
        int width, int height,
        bool isButtonDown,
        int buttonX, int buttonY,
        int buttonW, int buttonH,
        juce::ComboBox& box) override
    {
        juce::ignoreUnused(isButtonDown, buttonX, buttonY, buttonW, buttonH);
        const auto bounds = juce::Rectangle<float>(
            0.5f, 0.5f, static_cast<float>(width) - 1.0f,
            static_cast<float>(height) - 1.0f);

        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRect(bounds);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRect(bounds, 1.0f);

        const float centreY = height * 0.5f;
        const float arrowX = static_cast<float>(width - 16);
        juce::Path arrow;
        arrow.addTriangle(
            arrowX - 4.0f, centreY - 2.0f,
            arrowX + 4.0f, centreY - 2.0f,
            arrowX, centreY + 3.0f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.fillPath(arrow);
    }

    juce::Font getComboBoxFont(juce::ComboBox&) override
    {
        return juce::Font(juce::FontOptions(
            "Cascadia Mono", 14.5f, juce::Font::bold));
    }

    juce::Font getLabelFont(juce::Label& label) override
    {
        if (dynamic_cast<juce::Slider*>(label.getParentComponent()) != nullptr)
            return juce::Font(juce::FontOptions(
                "Cascadia Mono", 13.5f, juce::Font::bold));

        return juce::LookAndFeel_V4::getLabelFont(label);
    }

    void drawButtonBackground(
        juce::Graphics& g,
        juce::Button& button,
        const juce::Colour& backgroundColour,
        bool isMouseOverButton,
        bool isButtonDown) override
    {
        auto area = button.getLocalBounds().toFloat().reduced(1.0f);
        const bool lcdButton = static_cast<bool>(
            button.getProperties().getWithDefault("lcdButton", false));
        const bool hardwareSwitch = static_cast<bool>(
            button.getProperties().getWithDefault("hardwareSwitch", false));
        const float radius = lcdButton ? 1.5f : (hardwareSwitch ? 2.0f : 4.0f);

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

    void drawButtonText(
        juce::Graphics& g,
        juce::TextButton& button,
        bool,
        bool) override
    {
        if (static_cast<bool>(button.getProperties()
                .getWithDefault("hardwareSwitch", false)))
            return;

        const bool lcdButton = static_cast<bool>(
            button.getProperties().getWithDefault("lcdButton", false));
        const auto textColour = button.findColour(
            button.getToggleState()
                ? juce::TextButton::textColourOnId
                : juce::TextButton::textColourOffId);

        g.setColour(textColour);
        g.setFont(lcdButton
            ? juce::Font(juce::FontOptions("Cascadia Mono", 14.0f,
                juce::Font::bold))
            : juce::Font(juce::FontOptions(14.0f,
                juce::Font::plain)));
        g.drawFittedText(button.getButtonText(), button.getLocalBounds().reduced(4, 2),
            juce::Justification::centred, 1);
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
    void rebuildDateRangeSelectors(
        const DataSonifierAudioProcessor::DataStateSnapshot& state);

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
    juce::ComboBox dateColumnSelector;
    juce::ComboBox dateStartSelector;
    juce::ComboBox dateEndSelector;

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
    juce::TextButton timingFreeButton{ "FREE" };
    juce::TextButton timingSyncButton{ "SYNC" };
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
    juce::TextButton pitchBoundsButton{ "RANGE" };
    juce::TextButton pitchCentreSpreadButton{ "MIDPOINT / SPREAD" };
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
    int lastPaintedOutputMode = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DataSonifierAudioProcessorEditor)
};
