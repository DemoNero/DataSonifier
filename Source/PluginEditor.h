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
#include <functional>

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
    void setIndexClickHandler(std::function<void(int)> handler)
    {
        indexClickHandler = std::move(handler);
    }

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

    void setHighlightRange(int startIndex, int endIndex)
    {
        highlightStart = startIndex;
        highlightEnd = endIndex;
        repaint();
    }

    void setCycleRanges(
        const std::vector<std::pair<int, int>>& newCycleRanges)
    {
        cycleRanges = newCycleRanges;
        repaint();
    }

    void setSelectionAnchor(int index)
    {
        selectionAnchor = index;
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

        // Show every detected cycle as a quiet amber region. The selected
        // cycle receives its stronger overlay and trace below.
        for (const auto& [cycleStart, cycleEnd] : cycleRanges)
        {
            if (cycleStart < 0 || cycleEnd <= cycleStart
                || cycleEnd >= static_cast<int>(values.size())
                || (cycleStart == highlightStart
                    && cycleEnd == highlightEnd))
                continue;

            const float startX = juce::jmap(
                static_cast<float>(cycleStart), 0.0f,
                static_cast<float>(values.size() - 1),
                bounds.getX(), bounds.getRight());
            const float endX = juce::jmap(
                static_cast<float>(cycleEnd), 0.0f,
                static_cast<float>(values.size() - 1),
                bounds.getX(), bounds.getRight());

            g.setColour(juce::Colour(0xffe19a4f).withAlpha(0.085f));
            g.fillRect(juce::Rectangle<float>(
                startX, bounds.getY(), endX - startX, bounds.getHeight()));
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

        // Give every non-selected cycle a subdued amber trace as well as its
        // background band. This keeps all found regions legible while the
        // selected cycle below remains the strongest visual element.
        for (const auto& [cycleStart, cycleEnd] : cycleRanges)
        {
            if (cycleStart < 0 || cycleEnd <= cycleStart
                || cycleEnd >= static_cast<int>(values.size())
                || (cycleStart == highlightStart
                    && cycleEnd == highlightEnd))
                continue;

            juce::Path cyclePath;
            bool drawingCycle = false;

            for (int i = cycleStart; i <= cycleEnd; ++i)
            {
                if (!std::isfinite(values[i]))
                {
                    drawingCycle = false;
                    continue;
                }

                const float x = juce::jmap(
                    static_cast<float>(i), 0.0f,
                    static_cast<float>(values.size() - 1),
                    bounds.getX(), bounds.getRight());
                const float y = juce::jmap(
                    static_cast<float>(values[i]),
                    static_cast<float>(minimum),
                    static_cast<float>(maximum),
                    bounds.getBottom(), bounds.getY());

                if (!drawingCycle)
                {
                    cyclePath.startNewSubPath(x, y);
                    drawingCycle = true;
                }
                else
                    cyclePath.lineTo(x, y);
            }

            g.setColour(juce::Colour(0xffe0b060).withAlpha(0.58f));
            g.strokePath(cyclePath, juce::PathStrokeType(
                2.35f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }

        if (highlightStart >= 0 && highlightEnd > highlightStart
            && highlightEnd < static_cast<int>(values.size()))
        {
            const float startX = juce::jmap(
                static_cast<float>(highlightStart), 0.0f,
                static_cast<float>(values.size() - 1),
                bounds.getX(), bounds.getRight());
            const float endX = juce::jmap(
                static_cast<float>(highlightEnd), 0.0f,
                static_cast<float>(values.size() - 1),
                bounds.getX(), bounds.getRight());

            g.setColour(juce::Colour(0xffd28c45).withAlpha(0.12f));
            g.fillRect(juce::Rectangle<float>(
                startX, bounds.getY(), endX - startX, bounds.getHeight()));

            juce::Path selectedPath;
            bool drawingSelection = false;
            for (int i = highlightStart; i <= highlightEnd; ++i)
            {
                if (!std::isfinite(values[i]))
                {
                    drawingSelection = false;
                    continue;
                }

                const float x = juce::jmap(
                    static_cast<float>(i), 0.0f,
                    static_cast<float>(values.size() - 1),
                    bounds.getX(), bounds.getRight());
                const float y = juce::jmap(
                    static_cast<float>(values[i]),
                    static_cast<float>(minimum),
                    static_cast<float>(maximum),
                    bounds.getBottom(), bounds.getY());

                if (!drawingSelection)
                {
                    selectedPath.startNewSubPath(x, y);
                    drawingSelection = true;
                }
                else
                    selectedPath.lineTo(x, y);
            }

            g.setColour(juce::Colour(0xffe19a4f));
            g.strokePath(selectedPath, juce::PathStrokeType(
                3.5f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }

        if (juce::isPositiveAndBelow(
                selectionAnchor, static_cast<int>(values.size())))
        {
            const float anchorX = juce::jmap(
                static_cast<float>(selectionAnchor), 0.0f,
                static_cast<float>(values.size() - 1),
                bounds.getX(), bounds.getRight());
            g.setColour(juce::Colour(0xffe19a4f));
            g.drawVerticalLine(
                static_cast<int>(std::round(anchorX)),
                bounds.getY(), bounds.getBottom());
        }

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

    void mouseDown(const juce::MouseEvent& event) override
    {
        if (values.size() < 2 || !indexClickHandler)
            return;

        auto plot = getLocalBounds().toFloat().reduced(10.0f);
        plot.removeFromTop(40.0f);
        plot.removeFromBottom(15.0f);
        const float clampedX = juce::jlimit(
            plot.getX(), plot.getRight(), event.position.x);
        const int index = juce::jlimit(0,
            static_cast<int>(values.size()) - 1,
            static_cast<int>(std::round(juce::jmap(
                clampedX, plot.getX(), plot.getRight(), 0.0f,
                static_cast<float>(values.size() - 1)))));
        indexClickHandler(index);
    }

private:
    std::vector<double> values;
    juce::String title;
    int playbackIndex = -1;
    int highlightStart = -1;
    int highlightEnd = -1;
    int selectionAnchor = -1;
    std::vector<std::pair<int, int>> cycleRanges;
    std::function<void(int)> indexClickHandler;
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
    void detectCycles();
    bool rebuildCycleAnalysisSeries();
    void selectCycle(int cycleIndex);
    void updateCycleDisplay();
    void updateEngineVisibility();
    void updateModulationRouteControls();
    void handleManualSelectionPoint(int index);

    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    DataSonifierAudioProcessor& audioProcessor;

    HardwareLookAndFeel hardwareLookAndFeel;

    juce::TextButton openCsvButton{ "Open CSV" };
    juce::ComboBox engineModeSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        engineModeAttachment;
    juce::Slider cycleMinimumLengthSlider;
    juce::Slider cycleSensitivitySlider;
    juce::ComboBox cycleDetectionMethodSelector;
    juce::Slider hpLambdaSlider;
    juce::Slider bkLowPeriodSlider;
    juce::Slider bkHighPeriodSlider;
    juce::Slider bkTruncationSlider;
    juce::Slider cfLowPeriodSlider;
    juce::Slider cfHighPeriodSlider;
    juce::TextButton cfRemoveDriftButton{ "REMOVE DRIFT" };
    juce::Slider hamiltonHorizonSlider;
    juce::Slider hamiltonLagsSlider;
    juce::Label cycleMinimumLengthLabel;
    juce::Label cycleSensitivityLabel;
    juce::Label cycleDetectionMethodLabel;
    juce::Label hpLambdaLabel;
    juce::Label bkLowPeriodLabel;
    juce::Label bkHighPeriodLabel;
    juce::Label bkTruncationLabel;
    juce::Label cfLowPeriodLabel;
    juce::Label cfHighPeriodLabel;
    juce::Label cfDriftLabel;
    juce::Label hamiltonHorizonLabel;
    juce::Label hamiltonLagsLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        cycleDetectionMethodAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        hpLambdaAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        bkLowPeriodAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        bkHighPeriodAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        bkTruncationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        cfLowPeriodAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        cfHighPeriodAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        cfRemoveDriftAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        hamiltonHorizonAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        hamiltonLagsAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        cycleMinimumLengthAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        cycleSensitivityAttachment;
    juce::TextButton findCyclesButton{ "FIND CYCLES" };
    juce::TextButton useFullSeriesButton{ "USE FULL SERIES" };
    juce::TextButton previousCycleButton{ "<" };
    juce::TextButton nextCycleButton{ ">" };
    juce::TextButton auditionCycleButton{ "AUDITION C3" };
    juce::TextButton seriesViewButton{ "SERIES" };
    juce::TextButton wavetableViewButton{ "WAVETABLE" };
    juce::TextButton wavetableNormaliseButton{ "ON" };
    juce::TextButton wavetableNormaliseOffButton{ "OFF" };
    juce::Label wavetableLoopSmoothingLabel;
    juce::Slider wavetableLoopSmoothingSlider;
    juce::Label wavetablePositionLabel;
    juce::Slider wavetablePositionSlider;
    juce::Label wavetableAttackLabel;
    juce::Label wavetableDecayLabel;
    juce::Label wavetableSustainLabel;
    juce::Label wavetableReleaseLabel;
    juce::Label wavetableFilterCutoffLabel;
    juce::Label wavetableFilterResonanceLabel;
    juce::Label wavetableHighPassCutoffLabel;
    juce::Label wavetableHighPassResonanceLabel;
    juce::Slider wavetableAttackSlider;
    juce::Slider wavetableDecaySlider;
    juce::Slider wavetableSustainSlider;
    juce::Slider wavetableReleaseSlider;
    juce::Slider wavetableFilterCutoffSlider;
    juce::Slider wavetableFilterResonanceSlider;
    juce::Slider wavetableHighPassCutoffSlider;
    juce::Slider wavetableHighPassResonanceSlider;
    juce::TextButton shapingViewButton{ "SHAPING" };
    juce::TextButton modulationViewButton{ "MODULATION" };
    juce::TextButton modulationRoute1Button{ "ROUTE 1" };
    juce::TextButton modulationRoute2Button{ "ROUTE 2" };
    juce::Label modulationSourceLabel;
    juce::ComboBox modulationSourceSelector;
    juce::Label modulationTargetLabel;
    juce::ComboBox modulationTargetSelector;
    juce::Label modulationAmountLabel;
    juce::Slider modulationAmountSlider;
    juce::Label modulationTimingLabel;
    juce::ComboBox modulationTimingSelector;
    juce::Label modulationRateLabel;
    juce::Slider modulationRateSlider;
    juce::Label modulationDivisionLabel;
    juce::ComboBox modulationDivisionSelector;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        modulationTargetAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        modulationAmountAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        modulationTimingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        modulationRateAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        modulationDivisionAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        wavetableAttackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        wavetableDecayAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        wavetableSustainAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        wavetableReleaseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        wavetableFilterCutoffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        wavetableFilterResonanceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        wavetableHighPassCutoffAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        wavetableHighPassResonanceAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        wavetableLoopSmoothingAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        wavetablePositionAttachment;
    juce::Label cycleStatusLabel;
    std::vector<std::pair<int, int>> detectedCycles;
    std::vector<double> cycleAnalysisSeries;
    int selectedCycle = -1;
    bool usingFullSeries = false;
    bool showingWavetable = false;
    bool showingModulation = false;
    int selectedModulationRoute = 0;
    int manualSelectionStart = -1;
    int lastEngineMode = -1;
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
    int currentDateColumn = -1;

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
