/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <limits>
#include <numeric>

namespace
{
std::vector<double> applyHodrickPrescottFilter(
    const std::vector<double>& input,
    double lambda)
{
    const size_t count = input.size();
    if (count < 3 || lambda <= 0.0)
        return {};

    std::vector<double> observations(input);
    std::vector<size_t> validIndices;
    validIndices.reserve(count);
    for (size_t index = 0; index < count; ++index)
        if (std::isfinite(observations[index]))
            validIndices.push_back(index);

    if (validIndices.size() < 3)
        return {};

    // HP requires a complete, regularly-spaced series. Fill internal gaps
    // linearly and extend the nearest valid observation at either edge.
    const size_t firstValid = validIndices.front();
    const size_t lastValid = validIndices.back();
    std::fill(observations.begin(), observations.begin() + firstValid,
        observations[firstValid]);
    std::fill(observations.begin() + lastValid + 1, observations.end(),
        observations[lastValid]);

    for (size_t pair = 1; pair < validIndices.size(); ++pair)
    {
        const size_t left = validIndices[pair - 1];
        const size_t right = validIndices[pair];
        const double span = static_cast<double>(right - left);
        for (size_t index = left + 1; index < right; ++index)
        {
            const double fraction = static_cast<double>(index - left) / span;
            observations[index] = juce::jmap(
                fraction, observations[left], observations[right]);
        }
    }

    // Solve (I + lambda * K'K) trend = observations using a banded
    // Cholesky factorisation. K is the second-difference matrix, so the
    // system has only two non-zero off-diagonals.
    std::vector<double> diagonal(count, 1.0);
    std::vector<double> offOne(count, 0.0);
    std::vector<double> offTwo(count, 0.0);
    for (size_t row = 0; row + 2 < count; ++row)
    {
        diagonal[row] += lambda;
        diagonal[row + 1] += 4.0 * lambda;
        diagonal[row + 2] += lambda;
        offOne[row + 1] -= 2.0 * lambda;
        offOne[row + 2] -= 2.0 * lambda;
        offTwo[row + 2] += lambda;
    }

    std::vector<double> factorDiagonal(count, 0.0);
    std::vector<double> factorOne(count, 0.0);
    std::vector<double> factorTwo(count, 0.0);
    for (size_t row = 0; row < count; ++row)
    {
        if (row >= 2)
            factorTwo[row] = offTwo[row] / factorDiagonal[row - 2];
        if (row >= 1)
        {
            const double sharedTerm = row >= 2
                ? factorTwo[row] * factorOne[row - 1]
                : 0.0;
            factorOne[row] = (offOne[row] - sharedTerm)
                / factorDiagonal[row - 1];
        }

        const double remainder = diagonal[row]
            - factorOne[row] * factorOne[row]
            - factorTwo[row] * factorTwo[row];
        if (!(remainder > 0.0))
            return {};
        factorDiagonal[row] = std::sqrt(remainder);
    }

    std::vector<double> forward(count, 0.0);
    for (size_t row = 0; row < count; ++row)
    {
        double value = observations[row];
        if (row >= 1)
            value -= factorOne[row] * forward[row - 1];
        if (row >= 2)
            value -= factorTwo[row] * forward[row - 2];
        forward[row] = value / factorDiagonal[row];
    }

    std::vector<double> trend(count, 0.0);
    for (size_t reverse = count; reverse-- > 0;)
    {
        double value = forward[reverse];
        if (reverse + 1 < count)
            value -= factorOne[reverse + 1] * trend[reverse + 1];
        if (reverse + 2 < count)
            value -= factorTwo[reverse + 2] * trend[reverse + 2];
        trend[reverse] = value / factorDiagonal[reverse];
    }

    std::vector<double> cycle(count);
    for (size_t index = 0; index < count; ++index)
        cycle[index] = observations[index] - trend[index];
    return cycle;
}

std::vector<double> applyBaxterKingFilter(
    const std::vector<double>& input,
    double lowPeriod,
    double highPeriod,
    int truncation)
{
    const size_t count = input.size();
    const double missing = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> cycle(count, missing);

    truncation = juce::jmax(1, truncation);
    lowPeriod = juce::jmax(2.0, lowPeriod);
    highPeriod = juce::jmax(lowPeriod + 1.0, highPeriod);
    if (count <= static_cast<size_t>(2 * truncation))
        return cycle;

    std::vector<double> observations(input);
    std::vector<size_t> validIndices;
    validIndices.reserve(count);
    for (size_t index = 0; index < count; ++index)
        if (std::isfinite(observations[index]))
            validIndices.push_back(index);

    if (validIndices.size() < static_cast<size_t>(2 * truncation + 1))
        return cycle;

    // BK assumes an evenly-spaced complete series. Interpolate internal gaps
    // and extend the nearest valid observation at either edge.
    const size_t firstValid = validIndices.front();
    const size_t lastValid = validIndices.back();
    std::fill(observations.begin(), observations.begin() + firstValid,
        observations[firstValid]);
    std::fill(observations.begin() + lastValid + 1, observations.end(),
        observations[lastValid]);
    for (size_t pair = 1; pair < validIndices.size(); ++pair)
    {
        const size_t left = validIndices[pair - 1];
        const size_t right = validIndices[pair];
        const double span = static_cast<double>(right - left);
        for (size_t index = left + 1; index < right; ++index)
        {
            const double fraction = static_cast<double>(index - left) / span;
            observations[index] = juce::jmap(
                fraction, observations[left], observations[right]);
        }
    }

    const double omegaLow = juce::MathConstants<double>::twoPi / highPeriod;
    const double omegaHigh = juce::MathConstants<double>::twoPi / lowPeriod;
    std::vector<double> weights(static_cast<size_t>(2 * truncation + 1));
    weights[static_cast<size_t>(truncation)] =
        (omegaHigh - omegaLow) / juce::MathConstants<double>::pi;
    for (int lag = 1; lag <= truncation; ++lag)
    {
        const double weight = (std::sin(omegaHigh * lag)
            - std::sin(omegaLow * lag))
            / (juce::MathConstants<double>::pi * lag);
        weights[static_cast<size_t>(truncation - lag)] = weight;
        weights[static_cast<size_t>(truncation + lag)] = weight;
    }

    const double correction = -std::accumulate(
        weights.begin(), weights.end(), 0.0) / weights.size();
    for (double& weight : weights)
        weight += correction;

    // Keep the output aligned with the table. A symmetric BK filter cannot
    // calculate the first or last K observations, so they remain NaN.
    for (int centre = truncation;
         centre < static_cast<int>(count) - truncation; ++centre)
    {
        double filtered = 0.0;
        for (int lag = -truncation; lag <= truncation; ++lag)
            filtered += weights[static_cast<size_t>(lag + truncation)]
                * observations[static_cast<size_t>(centre + lag)];
        cycle[static_cast<size_t>(centre)] = filtered;
    }

    return cycle;
}

std::vector<double> applyChristianoFitzgeraldFilter(
    const std::vector<double>& input,
    double lowPeriod,
    double highPeriod,
    bool removeDrift)
{
    const size_t count = input.size();
    if (count < 3)
        return {};

    lowPeriod = juce::jmax(2.0, lowPeriod);
    highPeriod = juce::jmax(lowPeriod + 1.0, highPeriod);

    std::vector<double> observations(input);
    std::vector<size_t> validIndices;
    validIndices.reserve(count);
    for (size_t index = 0; index < count; ++index)
        if (std::isfinite(observations[index]))
            validIndices.push_back(index);

    if (validIndices.size() < 3)
        return {};

    // CF also requires a complete regularly-spaced series.
    const size_t firstValid = validIndices.front();
    const size_t lastValid = validIndices.back();
    std::fill(observations.begin(), observations.begin() + firstValid,
        observations[firstValid]);
    std::fill(observations.begin() + lastValid + 1, observations.end(),
        observations[lastValid]);
    for (size_t pair = 1; pair < validIndices.size(); ++pair)
    {
        const size_t left = validIndices[pair - 1];
        const size_t right = validIndices[pair];
        const double span = static_cast<double>(right - left);
        for (size_t index = left + 1; index < right; ++index)
        {
            const double fraction = static_cast<double>(index - left) / span;
            observations[index] = juce::jmap(
                fraction, observations[left], observations[right]);
        }
    }

    if (removeDrift)
    {
        const double driftPerObservation =
            (observations.back() - observations.front())
            / static_cast<double>(count - 1);
        for (size_t index = 0; index < count; ++index)
            observations[index] -= static_cast<double>(index)
                * driftPerObservation;
    }

    const double omegaLow = juce::MathConstants<double>::twoPi / highPeriod;
    const double omegaHigh = juce::MathConstants<double>::twoPi / lowPeriod;
    std::vector<double> coefficients(count + 1, 0.0);
    coefficients[0] = (omegaHigh - omegaLow)
        / juce::MathConstants<double>::pi;
    for (size_t lag = 1; lag <= count; ++lag)
        coefficients[lag] = (std::sin(omegaHigh * lag)
            - std::sin(omegaLow * lag))
            / (juce::MathConstants<double>::pi * lag);

    // Asymmetric random-walk CF approximation. Endpoint weights change for
    // every observation, allowing the entire sample to remain available.
    std::vector<double> cycle(count, 0.0);
    for (size_t index = 0; index < count; ++index)
    {
        const size_t futureCount = index + 2 < count
            ? count - index - 2 : 0;
        double futureWeightSum = 0.0;
        for (size_t lag = 1; lag <= futureCount; ++lag)
            futureWeightSum += coefficients[lag];

        double pastWeightSum = 0.0;
        for (size_t lag = 1; lag < index; ++lag)
            pastWeightSum += coefficients[lag];

        const double endWeight = -0.5 * coefficients[0]
            - futureWeightSum;
        const double startWeight = -coefficients[0]
            - futureWeightSum - pastWeightSum - endWeight;

        double filtered = coefficients[0] * observations[index]
            + endWeight * observations.back()
            + startWeight * observations.front();
        for (size_t lag = 1; lag <= futureCount; ++lag)
            filtered += coefficients[lag] * observations[index + lag];
        for (size_t lag = 1; lag < index; ++lag)
            filtered += coefficients[lag] * observations[index - lag];
        cycle[index] = filtered;
    }

    return cycle;
}

std::vector<double> applyHamiltonRegressionFilter(
    const std::vector<double>& input,
    int horizon,
    int lagCount)
{
    const size_t count = input.size();
    const double missing = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> cycle(count, missing);

    horizon = juce::jmax(1, horizon);
    lagCount = juce::jmax(1, lagCount);
    const int firstTarget = horizon + lagCount - 1;
    if (static_cast<int>(count) <= firstTarget + lagCount)
        return cycle;

    std::vector<double> observations(input);
    std::vector<size_t> validIndices;
    for (size_t index = 0; index < count; ++index)
        if (std::isfinite(observations[index]))
            validIndices.push_back(index);
    if (validIndices.size() < static_cast<size_t>(firstTarget + lagCount + 1))
        return cycle;

    // Hamilton regression requires a complete, evenly spaced series.
    const size_t firstValid = validIndices.front();
    const size_t lastValid = validIndices.back();
    std::fill(observations.begin(), observations.begin() + firstValid,
        observations[firstValid]);
    std::fill(observations.begin() + lastValid + 1, observations.end(),
        observations[lastValid]);
    for (size_t pair = 1; pair < validIndices.size(); ++pair)
    {
        const size_t left = validIndices[pair - 1];
        const size_t right = validIndices[pair];
        const double span = static_cast<double>(right - left);
        for (size_t index = left + 1; index < right; ++index)
        {
            const double fraction = static_cast<double>(index - left) / span;
            observations[index] = juce::jmap(
                fraction, observations[left], observations[right]);
        }
    }

    const int coefficientCount = lagCount + 1; // constant plus p lags
    std::vector<std::vector<double>> normalMatrix(
        static_cast<size_t>(coefficientCount),
        std::vector<double>(static_cast<size_t>(coefficientCount), 0.0));
    std::vector<double> normalVector(
        static_cast<size_t>(coefficientCount), 0.0);

    // Predict y[t + h] from 1, y[t], y[t-1], ..., y[t-p+1].
    for (int origin = lagCount - 1;
         origin + horizon < static_cast<int>(count); ++origin)
    {
        std::vector<double> row(static_cast<size_t>(coefficientCount), 1.0);
        for (int lag = 0; lag < lagCount; ++lag)
            row[static_cast<size_t>(lag + 1)] =
                observations[static_cast<size_t>(origin - lag)];
        const double target =
            observations[static_cast<size_t>(origin + horizon)];
        for (int i = 0; i < coefficientCount; ++i)
        {
            normalVector[static_cast<size_t>(i)] +=
                row[static_cast<size_t>(i)] * target;
            for (int j = 0; j < coefficientCount; ++j)
                normalMatrix[static_cast<size_t>(i)][static_cast<size_t>(j)] +=
                    row[static_cast<size_t>(i)] * row[static_cast<size_t>(j)];
        }
    }

    // Solve the small OLS normal system with partial-pivot elimination.
    for (int pivot = 0; pivot < coefficientCount; ++pivot)
    {
        int bestRow = pivot;
        for (int row = pivot + 1; row < coefficientCount; ++row)
            if (std::abs(normalMatrix[static_cast<size_t>(row)]
                    [static_cast<size_t>(pivot)])
                > std::abs(normalMatrix[static_cast<size_t>(bestRow)]
                    [static_cast<size_t>(pivot)]))
                bestRow = row;
        if (std::abs(normalMatrix[static_cast<size_t>(bestRow)]
                [static_cast<size_t>(pivot)]) < 1.0e-12)
            return cycle;
        std::swap(normalMatrix[static_cast<size_t>(pivot)],
            normalMatrix[static_cast<size_t>(bestRow)]);
        std::swap(normalVector[static_cast<size_t>(pivot)],
            normalVector[static_cast<size_t>(bestRow)]);
        for (int row = pivot + 1; row < coefficientCount; ++row)
        {
            const double factor = normalMatrix[static_cast<size_t>(row)]
                [static_cast<size_t>(pivot)]
                / normalMatrix[static_cast<size_t>(pivot)]
                    [static_cast<size_t>(pivot)];
            for (int column = pivot; column < coefficientCount; ++column)
                normalMatrix[static_cast<size_t>(row)]
                    [static_cast<size_t>(column)] -= factor
                    * normalMatrix[static_cast<size_t>(pivot)]
                        [static_cast<size_t>(column)];
            normalVector[static_cast<size_t>(row)] -=
                factor * normalVector[static_cast<size_t>(pivot)];
        }
    }

    std::vector<double> coefficients(
        static_cast<size_t>(coefficientCount), 0.0);
    for (int row = coefficientCount - 1; row >= 0; --row)
    {
        double value = normalVector[static_cast<size_t>(row)];
        for (int column = row + 1; column < coefficientCount; ++column)
            value -= normalMatrix[static_cast<size_t>(row)]
                [static_cast<size_t>(column)]
                * coefficients[static_cast<size_t>(column)];
        coefficients[static_cast<size_t>(row)] = value
            / normalMatrix[static_cast<size_t>(row)][static_cast<size_t>(row)];
    }

    for (int origin = lagCount - 1;
         origin + horizon < static_cast<int>(count); ++origin)
    {
        double prediction = coefficients[0];
        for (int lag = 0; lag < lagCount; ++lag)
            prediction += coefficients[static_cast<size_t>(lag + 1)]
                * observations[static_cast<size_t>(origin - lag)];
        const int targetIndex = origin + horizon;
        cycle[static_cast<size_t>(targetIndex)] =
            observations[static_cast<size_t>(targetIndex)] - prediction;
    }
    return cycle;
}
}
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
    addAndMakeVisible(engineModeSelector);
    addAndMakeVisible(cycleMinimumLengthSlider);
    addAndMakeVisible(cycleSensitivitySlider);
    addAndMakeVisible(cycleDetectionMethodSelector);
    addAndMakeVisible(hpLambdaSlider);
    addAndMakeVisible(bkLowPeriodSlider);
    addAndMakeVisible(bkHighPeriodSlider);
    addAndMakeVisible(bkTruncationSlider);
    addAndMakeVisible(cfLowPeriodSlider);
    addAndMakeVisible(cfHighPeriodSlider);
    addAndMakeVisible(cfRemoveDriftButton);
    addAndMakeVisible(hamiltonHorizonSlider);
    addAndMakeVisible(hamiltonLagsSlider);
    addAndMakeVisible(cycleMinimumLengthLabel);
    addAndMakeVisible(cycleSensitivityLabel);
    addAndMakeVisible(cycleDetectionMethodLabel);
    addAndMakeVisible(hpLambdaLabel);
    addAndMakeVisible(bkLowPeriodLabel);
    addAndMakeVisible(bkHighPeriodLabel);
    addAndMakeVisible(bkTruncationLabel);
    addAndMakeVisible(cfLowPeriodLabel);
    addAndMakeVisible(cfHighPeriodLabel);
    addAndMakeVisible(cfDriftLabel);
    addAndMakeVisible(hamiltonHorizonLabel);
    addAndMakeVisible(hamiltonLagsLabel);
    addAndMakeVisible(findCyclesButton);
    addAndMakeVisible(useFullSeriesButton);
    addAndMakeVisible(previousCycleButton);
    addAndMakeVisible(nextCycleButton);
    addAndMakeVisible(auditionCycleButton);
    addAndMakeVisible(seriesViewButton);
    addAndMakeVisible(wavetableViewButton);
    addAndMakeVisible(wavetableNormaliseButton);
    addAndMakeVisible(wavetableNormaliseOffButton);
    addAndMakeVisible(wavetableLoopSmoothingLabel);
    addAndMakeVisible(wavetableLoopSmoothingSlider);
    addAndMakeVisible(wavetablePositionLabel);
    addAndMakeVisible(wavetablePositionSlider);
    addAndMakeVisible(wavetableAttackLabel);
    addAndMakeVisible(wavetableDecayLabel);
    addAndMakeVisible(wavetableSustainLabel);
    addAndMakeVisible(wavetableReleaseLabel);
    addAndMakeVisible(wavetableFilterCutoffLabel);
    addAndMakeVisible(wavetableFilterResonanceLabel);
    addAndMakeVisible(wavetableHighPassCutoffLabel);
    addAndMakeVisible(wavetableHighPassResonanceLabel);
    addAndMakeVisible(wavetableAttackSlider);
    addAndMakeVisible(wavetableDecaySlider);
    addAndMakeVisible(wavetableSustainSlider);
    addAndMakeVisible(wavetableReleaseSlider);
    addAndMakeVisible(wavetableFilterCutoffSlider);
    addAndMakeVisible(wavetableFilterResonanceSlider);
    addAndMakeVisible(wavetableHighPassCutoffSlider);
    addAndMakeVisible(wavetableHighPassResonanceSlider);
    addAndMakeVisible(shapingViewButton);
    addAndMakeVisible(modulationViewButton);
    addAndMakeVisible(modulationRoute1Button);
    addAndMakeVisible(modulationRoute2Button);
    addAndMakeVisible(modulationSourceLabel);
    addAndMakeVisible(modulationSourceSelector);
    addAndMakeVisible(modulationTargetLabel);
    addAndMakeVisible(modulationTargetSelector);
    addAndMakeVisible(modulationAmountLabel);
    addAndMakeVisible(modulationAmountSlider);
    addAndMakeVisible(modulationTimingLabel);
    addAndMakeVisible(modulationTimingSelector);
    addAndMakeVisible(modulationRateLabel);
    addAndMakeVisible(modulationRateSlider);
    addAndMakeVisible(modulationDivisionLabel);
    addAndMakeVisible(modulationDivisionSelector);
    addAndMakeVisible(cycleStatusLabel);
    addAndMakeVisible(seriesGraph);
    addAndMakeVisible(seriesSelector);
    addAndMakeVisible(dateColumnSelector);
    addAndMakeVisible(dateStartSelector);
    addAndMakeVisible(dateEndSelector);
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

    for (auto* selector : { &dateColumnSelector,
                            &dateStartSelector,
                            &dateEndSelector })
    {
        selector->setColour(juce::ComboBox::backgroundColourId, lcd);
        selector->setColour(juce::ComboBox::outlineColourId, mint);
        selector->setColour(juce::ComboBox::textColourId, mint);
        selector->setColour(juce::ComboBox::arrowColourId, mint);
    }

    dateColumnSelector.setTextWhenNothingSelected("Select date column");
    dateStartSelector.setTextWhenNothingSelected("From");
    dateEndSelector.setTextWhenNothingSelected("To");

    openCsvButton.setColour(juce::TextButton::buttonColourId, lcd);
    openCsvButton.setColour(juce::TextButton::textColourOffId, mint);
    openCsvButton.getProperties().set("lcdButton", true);

    engineModeSelector.addItemList({ "MIDI", "WAVETABLE" }, 1);
    engineModeAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(),
            "engineMode",
            engineModeSelector);

    for (auto* button : { &seriesViewButton, &wavetableViewButton,
                          &shapingViewButton, &modulationViewButton,
                          &modulationRoute1Button, &modulationRoute2Button })
        button->getProperties().set("hardwareSwitch", true);
    shapingViewButton.onClick = [this]()
    {
        showingModulation = false;
        updateEngineVisibility();
        repaint();
    };
    modulationViewButton.onClick = [this]()
    {
        showingModulation = true;
        updateEngineVisibility();
        repaint();
    };
    modulationRoute1Button.onClick = [this]()
    {
        selectedModulationRoute = 0;
        updateModulationRouteControls();
    };
    modulationRoute2Button.onClick = [this]()
    {
        selectedModulationRoute = 1;
        updateModulationRouteControls();
    };

    modulationSourceLabel.setText("SOURCE SERIES", juce::dontSendNotification);
    modulationTargetLabel.setText("TARGET", juce::dontSendNotification);
    modulationAmountLabel.setText("AMOUNT", juce::dontSendNotification);
    modulationTimingLabel.setText("TIMING", juce::dontSendNotification);
    modulationRateLabel.setText("RATE", juce::dontSendNotification);
    modulationDivisionLabel.setText("DIVISION", juce::dontSendNotification);
    modulationSourceSelector.setTextWhenNothingSelected("Select series");
    modulationTargetSelector.addItemList(
        { "OFF", "AMPLITUDE", "LP CUTOFF", "HP CUTOFF" }, 1);
    modulationAmountSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    modulationAmountSlider.setTextBoxStyle(
        juce::Slider::TextBoxRight, false, 70, 25);
    modulationTargetAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(), "modulationTarget",
            modulationTargetSelector);
    modulationAmountAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "modulationAmount",
            modulationAmountSlider);
    modulationTimingSelector.addItemList({ "FREE", "SYNC" }, 1);
    modulationTimingAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(), "modulationTimingMode",
            modulationTimingSelector);
    modulationTimingSelector.onChange = [this]()
    {
        const bool freeTiming = modulationTimingSelector.getSelectedId() == 1;
        modulationRateSlider.setEnabled(freeTiming);
        modulationDivisionSelector.setEnabled(!freeTiming);
    };
    modulationRateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    modulationRateSlider.setTextBoxStyle(
        juce::Slider::TextBoxRight, false, 78, 25);
    modulationRateSlider.setTextValueSuffix(" VALUE/S");
    modulationRateAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "modulationRate",
            modulationRateSlider);
    modulationDivisionSelector.addItemList(
        { "1/1", "1/2", "1/4", "1/8", "1/16",
          "1/4T", "1/8T", "1/16T" }, 1);
    modulationDivisionAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(), "modulationSubdivision",
            modulationDivisionSelector);
    modulationSourceSelector.onChange = [this]()
    {
        if (selectedModulationRoute == 0)
            audioProcessor.setModulationColumn(
                modulationSourceSelector.getSelectedId());
        else
            audioProcessor.setModulationColumn2(
                modulationSourceSelector.getSelectedId());
    };

    const auto configureEnvelopeControl = [](
        juce::Label& label, juce::Slider& slider,
        const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 25);
    };

    configureEnvelopeControl(
        wavetableAttackLabel, wavetableAttackSlider, "ATTACK");
    configureEnvelopeControl(
        wavetableDecayLabel, wavetableDecaySlider, "DECAY");
    configureEnvelopeControl(
        wavetableSustainLabel, wavetableSustainSlider, "SUSTAIN");
    configureEnvelopeControl(
        wavetableReleaseLabel, wavetableReleaseSlider, "RELEASE");
    configureEnvelopeControl(
        wavetableFilterCutoffLabel, wavetableFilterCutoffSlider, "LP CUTOFF");
    configureEnvelopeControl(
        wavetableFilterResonanceLabel, wavetableFilterResonanceSlider, "LP RESO");
    configureEnvelopeControl(
        wavetableHighPassCutoffLabel, wavetableHighPassCutoffSlider, "HP CUTOFF");
    configureEnvelopeControl(
        wavetableHighPassResonanceLabel, wavetableHighPassResonanceSlider,
        "HP RESO");
    wavetableNormaliseButton.getProperties().set("hardwareSwitch", true);
    wavetableNormaliseOffButton.getProperties().set("hardwareSwitch", true);
    wavetableLoopSmoothingLabel.setText(
        "LOOP SMOOTH", juce::dontSendNotification);
    wavetableLoopSmoothingSlider.setSliderStyle(
        juce::Slider::LinearHorizontal);
    wavetableLoopSmoothingSlider.setTextBoxStyle(
        juce::Slider::TextBoxRight, false, 62, 25);
    wavetableAttackAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "wavetableAttack",
            wavetableAttackSlider);
    wavetableDecayAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "wavetableDecay",
            wavetableDecaySlider);
    wavetableSustainAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "wavetableSustain",
            wavetableSustainSlider);
    wavetableReleaseAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "wavetableRelease",
            wavetableReleaseSlider);
    wavetableFilterCutoffAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "wavetableFilterCutoff",
            wavetableFilterCutoffSlider);
    wavetableFilterResonanceAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "wavetableFilterResonance",
            wavetableFilterResonanceSlider);
    wavetableHighPassCutoffAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "wavetableHighPassCutoff",
            wavetableHighPassCutoffSlider);
    wavetableHighPassResonanceAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "wavetableHighPassResonance",
            wavetableHighPassResonanceSlider);
    wavetableLoopSmoothingAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "wavetableLoopSmoothing",
            wavetableLoopSmoothingSlider);
    wavetablePositionLabel.setText(
        "WT POSITION", juce::dontSendNotification);
    wavetablePositionSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    wavetablePositionSlider.setTextBoxStyle(
        juce::Slider::TextBoxRight, false, 62, 25);
    wavetablePositionAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "wavetablePosition",
            wavetablePositionSlider);

    auto* editor = this;
    const auto refreshWavetableDisplay = [editor]()
    {
        const juce::Component::SafePointer<
            DataSonifierAudioProcessorEditor> safeEditor(editor);
        juce::Timer::callAfterDelay(60,
            [safeEditor]()
            {
                if (safeEditor != nullptr && safeEditor->showingWavetable)
                    safeEditor->updateCycleDisplay();
            });
    };
    const auto setNormalise = [this, refreshWavetableDisplay](bool enabled)
    {
        if (auto* parameter = audioProcessor.getParameterState()
                .getParameter("wavetableNormalize"))
        {
            parameter->beginChangeGesture();
            parameter->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
            parameter->endChangeGesture();
        }
        refreshWavetableDisplay();
    };
    wavetableNormaliseButton.onClick = [setNormalise]()
    {
        setNormalise(true);
    };
    wavetableNormaliseOffButton.onClick = [setNormalise]()
    {
        setNormalise(false);
    };
    wavetableLoopSmoothingSlider.onValueChange = refreshWavetableDisplay;
    wavetablePositionSlider.onValueChange = [this, refreshWavetableDisplay]()
    {
        if (!detectedCycles.empty())
        {
            selectedCycle = juce::jlimit(
                0, static_cast<int>(detectedCycles.size()) - 1,
                static_cast<int>(std::round(
                    wavetablePositionSlider.getValue()
                    * static_cast<double>(detectedCycles.size() - 1))));
            updateCycleDisplay();
            csvTable.repaint();
        }
        refreshWavetableDisplay();
    };

    const auto configureCycleSlider = [](juce::Slider& slider)
    {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 62, 25);
    };

    cycleMinimumLengthLabel.setText("MIN LENGTH", juce::dontSendNotification);
    cycleSensitivityLabel.setText("SENSITIVITY", juce::dontSendNotification);
    cycleDetectionMethodLabel.setText("METHOD", juce::dontSendNotification);
    hpLambdaLabel.setText("HP LAMBDA", juce::dontSendNotification);
    bkLowPeriodLabel.setText("SHORTEST PERIOD", juce::dontSendNotification);
    bkHighPeriodLabel.setText("LONGEST PERIOD", juce::dontSendNotification);
    bkTruncationLabel.setText("LEAD / LAG", juce::dontSendNotification);
    cfLowPeriodLabel.setText("SHORTEST PERIOD", juce::dontSendNotification);
    cfHighPeriodLabel.setText("LONGEST PERIOD", juce::dontSendNotification);
    cfDriftLabel.setText("LINEAR DRIFT", juce::dontSendNotification);
    hamiltonHorizonLabel.setText("FORECAST HORIZON", juce::dontSendNotification);
    hamiltonLagsLabel.setText("LAG COUNT", juce::dontSendNotification);
    configureCycleSlider(cycleMinimumLengthSlider);
    configureCycleSlider(cycleSensitivitySlider);
    configureCycleSlider(hpLambdaSlider);
    configureCycleSlider(bkLowPeriodSlider);
    configureCycleSlider(bkHighPeriodSlider);
    configureCycleSlider(bkTruncationSlider);
    configureCycleSlider(cfLowPeriodSlider);
    configureCycleSlider(cfHighPeriodSlider);
    configureCycleSlider(hamiltonHorizonSlider);
    configureCycleSlider(hamiltonLagsSlider);
    cfRemoveDriftButton.getProperties().set("lcdButton", true);
    cfRemoveDriftButton.setClickingTogglesState(true);
    cycleMinimumLengthSlider.setRange(2.0, 128.0, 1.0);
    cycleSensitivitySlider.setRange(0.0, 100.0, 1.0);
    cycleSensitivitySlider.setTextValueSuffix(" %");

    cycleDetectionMethodSelector.addItemList(
        { "MEAN CROSSING", "HP FILTER", "BAXTER-KING",
          "CHRISTIANO-FITZGERALD", "HAMILTON REGRESSION",
          "MANUAL SELECTION" }, 1);
    cycleDetectionMethodAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(), "cycleMethod",
            cycleDetectionMethodSelector);
    hpLambdaAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "hpLambda", hpLambdaSlider);
    bkLowPeriodAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "bkLowPeriod",
            bkLowPeriodSlider);
    bkHighPeriodAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "bkHighPeriod",
            bkHighPeriodSlider);
    bkTruncationAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "bkTruncation",
            bkTruncationSlider);
    cfLowPeriodAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "cfLowPeriod",
            cfLowPeriodSlider);
    cfHighPeriodAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "cfHighPeriod",
            cfHighPeriodSlider);
    cfRemoveDriftAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ButtonAttachment>(
            audioProcessor.getParameterState(), "cfRemoveDrift",
            cfRemoveDriftButton);
    hamiltonHorizonAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "hamiltonHorizon",
            hamiltonHorizonSlider);
    hamiltonLagsAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "hamiltonLags",
            hamiltonLagsSlider);
    cycleDetectionMethodSelector.onChange = [this]() { detectCycles(); };
    seriesGraph.setIndexClickHandler([this](int index)
    {
        handleManualSelectionPoint(index);
    });

    cycleMinimumLengthAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "cycleMinLength",
            cycleMinimumLengthSlider);
    cycleSensitivityAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(), "cycleSensitivity",
            cycleSensitivitySlider);

    for (auto* button : { &findCyclesButton, &useFullSeriesButton,
                          &previousCycleButton,
                          &nextCycleButton, &auditionCycleButton })
        button->getProperties().set("lcdButton", true);

    findCyclesButton.onClick = [this]() { detectCycles(); };
    useFullSeriesButton.onClick = [this]()
    {
        if (!rebuildCycleAnalysisSeries()
            || cycleAnalysisSeries.size() < 2)
            return;

        usingFullSeries = true;
        manualSelectionStart = -1;
        seriesGraph.setSelectionAnchor(-1);
        detectedCycles.clear();
        detectedCycles.emplace_back(
            0, static_cast<int>(cycleAnalysisSeries.size()) - 1);
        selectCycle(0);
    };
    previousCycleButton.onClick = [this]()
    {
        if (!detectedCycles.empty())
            selectCycle((selectedCycle - 1
                + static_cast<int>(detectedCycles.size()))
                % static_cast<int>(detectedCycles.size()));
    };
    nextCycleButton.onClick = [this]()
    {
        if (!detectedCycles.empty())
            selectCycle((selectedCycle + 1)
                % static_cast<int>(detectedCycles.size()));
    };
    auditionCycleButton.onClick = [this]()
    {
        audioProcessor.auditionWavetable();
    };
    seriesViewButton.onClick = [this]()
    {
        showingWavetable = false;
        updateCycleDisplay();
    };
    wavetableViewButton.onClick = [this]()
    {
        showingWavetable = true;
        updateCycleDisplay();
    };

    cycleStatusLabel.setJustificationType(juce::Justification::centred);
    cycleStatusLabel.setColour(juce::Label::textColourId, juce::Colour(0xff315c52));

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

    dateColumnSelector.onChange = [this]()
    {
        audioProcessor.setDateColumn(dateColumnSelector.getSelectedId());
        refreshFromProcessor();
    };

    const auto updateDateRange = [this]()
    {
        const int startRow = dateStartSelector.getSelectedId() - 1;
        const int endRow = dateEndSelector.getSelectedId() - 1;

        if (startRow >= 0 && endRow >= 0)
        {
            audioProcessor.setDateRange(startRow, endRow);
            refreshFromProcessor();
        }
    };

    dateStartSelector.onChange = updateDateRange;
    dateEndSelector.onChange = updateDateRange;

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

    // Keep the Wavetable engine's legends visually identical to the MIDI
    // engine: compact, uppercase hardware lettering in the same dark ink.
    for (auto* label : {
             &wavetableLoopSmoothingLabel,
             &wavetablePositionLabel,
             &wavetableAttackLabel, &wavetableDecayLabel,
             &wavetableSustainLabel, &wavetableReleaseLabel,
             &wavetableFilterCutoffLabel,
             &wavetableFilterResonanceLabel,
             &wavetableHighPassCutoffLabel,
             &wavetableHighPassResonanceLabel,
             &cycleDetectionMethodLabel, &cycleMinimumLengthLabel,
             &cycleSensitivityLabel, &hpLambdaLabel,
             &bkLowPeriodLabel, &bkHighPeriodLabel,
             &bkTruncationLabel, &cfLowPeriodLabel,
             &cfHighPeriodLabel, &cfDriftLabel,
             &hamiltonHorizonLabel, &hamiltonLagsLabel,
             &modulationSourceLabel, &modulationTargetLabel,
             &modulationAmountLabel, &modulationTimingLabel,
             &modulationRateLabel, &modulationDivisionLabel })
    {
        label->setFont(juce::FontOptions(11.5f, juce::Font::bold));
        label->setColour(juce::Label::textColourId, hardwareLegendColour);
        label->setJustificationType(juce::Justification::centredLeft);
    }

    setSize(1280, 620);
    refreshFromProcessor();
    updateEngineVisibility();
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
    g.drawText("ENGINE", 476, 3, 70, 18,
        juce::Justification::centredRight);
    g.drawText("VERSION 4.7", getWidth() - 204, 3, 180, 18,
        juce::Justification::centredRight);

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
    const int engineMode = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("engineMode")->load()));
    const bool wavetableEngine = engineMode == 1;
    drawPanel({ 682, 34, 330, 562 },
        wavetableEngine ? "WAVETABLE" : "MIDI", false);
    drawPanel({ 1028, 34, 228, 562 },
        wavetableEngine ? "SOUND SHAPING" : "MIDI CC TARGET", false);

    g.setColour(ink.withAlpha(0.7f));
    g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
    g.setColour(mint);
    g.drawText("FILE", 40, 60, 100, 14, juce::Justification::left);
    g.drawText("SERIES", 150, 60, 280, 14, juce::Justification::left);
    g.drawText("DATE COLUMN", 40, 116, 190, 14, juce::Justification::left);
    g.drawText("FROM", 240, 116, 135, 14, juce::Justification::left);
    g.drawText("TO", 385, 116, 135, 14, juce::Justification::left);
    g.setColour(juce::Colour(0xff315c52));
    if (!wavetableEngine)
    {
        g.drawText("OUTPUT MODE", 698, 60, 180, 14,
            juce::Justification::left);
        g.drawText("CC NUMBER", 1044, 60, 180, 14,
            juce::Justification::left);
    }
    else
    {
        g.setColour(juce::Colour(0xff7b2f2b));
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText("SERIES", 698, 62, 144, 12,
            juce::Justification::centred);
        g.drawText("WAVETABLE", 852, 62, 144, 12,
            juce::Justification::centred);
        g.drawText("SHAPING", 1044, 62, 94, 12,
            juce::Justification::centred);
        g.drawText("MODULATION", 1146, 62, 94, 12,
            juce::Justification::centred);

        g.setColour(juce::Colour(0xff315c52));
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText("NORMALIZE", 698, 116, 144, 14,
            juce::Justification::left);
        g.drawText("CYCLE DETECTION", 698, 354, 298, 14,
            juce::Justification::left);
        if (!showingModulation)
        {
            g.drawText("ENVELOPE", 1044, 102, 196, 14,
                juce::Justification::left);
            g.drawText("FILTERS", 1044, 360, 196, 14,
                juce::Justification::left);
        }
    }

    const auto hardwareRed = juce::Colour(0xff7b2f2b);
    g.setColour(hardwareRed);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    if (!wavetableEngine)
    {
        g.drawText("NOTE", 720, 78, 78, 14, juce::Justification::centred);
        g.drawText("CONTROLLER", 862, 78, 100, 14, juce::Justification::centred);
        g.drawText("FREE", 720, 148, 78, 14, juce::Justification::centred);
        g.drawText("SYNC", 873, 148, 78, 14, juce::Justification::centred);
    }
    else
    {
        g.drawText("ON", 698, 132, 144, 12, juce::Justification::centred);
        g.drawText("OFF", 852, 132, 144, 12, juce::Justification::centred);
        if (showingModulation)
        {
            g.drawText("ROUTE 1", 1044, 108, 94, 12,
                juce::Justification::centred);
            g.drawText("ROUTE 2", 1146, 108, 94, 12,
                juce::Justification::centred);
        }
    }
    const int outputMode = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("outputMode")->load()));
    if (!wavetableEngine && outputMode == 0)
    {
        g.drawText("RANGE", 718, 306, 82, 14, juce::Justification::centred);
        g.drawText("MIDPOINT / SPREAD", 857, 306, 110, 14,
            juce::Justification::centred);
    }

    g.setColour(juce::Colour(0xff1f302a).withAlpha(0.88f));
    g.setFont(juce::FontOptions(10.0f, juce::Font::plain));
    if (!wavetableEngine)
        g.drawFittedText(
            "Match this CC to the receiving synth control.",
            1044, 120, 196, 34,
            juce::Justification::topLeft, 2);
}

void DataSonifierAudioProcessorEditor::resized()
{
    engineModeSelector.setBounds(554, 2, 132, 22);
    seriesViewButton.setBounds(757, 78, 26, 18);
    wavetableViewButton.setBounds(911, 78, 26, 18);
    wavetableNormaliseButton.setBounds(757, 146, 26, 18);
    wavetableNormaliseOffButton.setBounds(911, 146, 26, 18);
    constexpr int wavetableSliderLabelX = 698;
    constexpr int wavetableSliderLabelWidth = 110;
    constexpr int wavetableSliderX = 810;
    constexpr int wavetableSliderWidth = 186;
    constexpr int wavetableSliderHeight = 30;

    wavetableLoopSmoothingLabel.setBounds(
        wavetableSliderLabelX, 188, wavetableSliderLabelWidth, 18);
    wavetableLoopSmoothingSlider.setBounds(
        wavetableSliderX, 182, wavetableSliderWidth, wavetableSliderHeight);
    wavetablePositionLabel.setBounds(
        wavetableSliderLabelX, 230, wavetableSliderLabelWidth, 18);
    wavetablePositionSlider.setBounds(
        wavetableSliderX, 224, wavetableSliderWidth, wavetableSliderHeight);
    previousCycleButton.setBounds(698, 266, 42, 32);
    cycleStatusLabel.setBounds(746, 266, 202, 32);
    nextCycleButton.setBounds(954, 266, 42, 32);
    auditionCycleButton.setBounds(698, 308, 298, 34);
    shapingViewButton.setBounds(1078, 78, 26, 18);
    modulationViewButton.setBounds(1180, 78, 26, 18);
    modulationRoute1Button.setBounds(1078, 124, 26, 18);
    modulationRoute2Button.setBounds(1180, 124, 26, 18);
    wavetableAttackLabel.setBounds(1044, 120, 196, 18);
    wavetableAttackSlider.setBounds(1044, 138, 196, 28);
    wavetableDecayLabel.setBounds(1044, 178, 196, 18);
    wavetableDecaySlider.setBounds(1044, 196, 196, 28);
    wavetableSustainLabel.setBounds(1044, 236, 196, 18);
    wavetableSustainSlider.setBounds(1044, 254, 196, 28);
    wavetableReleaseLabel.setBounds(1044, 294, 196, 18);
    wavetableReleaseSlider.setBounds(1044, 312, 196, 28);
    wavetableFilterCutoffLabel.setBounds(1044, 378, 196, 18);
    wavetableFilterCutoffSlider.setBounds(1044, 396, 196, 28);
    wavetableFilterResonanceLabel.setBounds(1044, 436, 196, 18);
    wavetableFilterResonanceSlider.setBounds(1044, 454, 196, 28);
    wavetableHighPassCutoffLabel.setBounds(1044, 494, 196, 18);
    wavetableHighPassCutoffSlider.setBounds(1044, 512, 196, 28);
    wavetableHighPassResonanceLabel.setBounds(1044, 546, 196, 18);
    wavetableHighPassResonanceSlider.setBounds(1044, 564, 196, 28);
    modulationSourceLabel.setBounds(1044, 156, 196, 18);
    modulationSourceSelector.setBounds(1044, 176, 196, 30);
    modulationTargetLabel.setBounds(1044, 220, 196, 18);
    modulationTargetSelector.setBounds(1044, 240, 196, 30);
    modulationAmountLabel.setBounds(1044, 284, 196, 18);
    modulationAmountSlider.setBounds(1044, 304, 196, 30);
    modulationTimingLabel.setBounds(1044, 348, 196, 18);
    modulationTimingSelector.setBounds(1044, 368, 196, 30);
    modulationRateLabel.setBounds(1044, 412, 196, 18);
    modulationRateSlider.setBounds(1044, 432, 196, 30);
    modulationDivisionLabel.setBounds(1044, 476, 196, 18);
    modulationDivisionSelector.setBounds(1044, 496, 196, 30);
    cycleDetectionMethodLabel.setBounds(698, 376, 72, 28);
    cycleDetectionMethodSelector.setBounds(778, 376, 218, 28);
    openCsvButton.setBounds(40, 78, 100, 32);
    seriesSelector.setBounds(150, 78, 280, 32);

    dateColumnSelector.setBounds(40, 134, 190, 30);
    dateStartSelector.setBounds(240, 134, 135, 30);
    dateEndSelector.setBounds(385, 134, 135, 30);

    transformNoneButton.setBounds(442, 78, 62, 32);
    transformLogButton.setBounds(510, 78, 62, 32);
    transformDifferenceButton.setBounds(578, 78, 68, 32);
    csvTable.setBounds(40, 178, 606, 154);
    selectedCellLabel.setBounds(40, 338, 606, 22);
    seriesGraph.setBounds(40, 366, 606, 208);
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

    controllerNumberSlider.setBounds(1044, 82, 196, 32);

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
    bool isSelectedColumn = columnId == selectedColumn;
    bool isSelectedCell = rowNumber == selectedRow && columnId == selectedColumn;
    const bool isSelectedRow = rowNumber == selectedRow;
    const bool wavetableEngine = lastEngineMode == 1;
    const bool hasSelectedCycle = wavetableEngine
        && juce::isPositiveAndBelow(
            selectedCycle, static_cast<int>(detectedCycles.size()));
    const int cycleStart = hasSelectedCycle
        ? detectedCycles[selectedCycle].first : -1;
    const int cycleEnd = hasSelectedCycle
        ? detectedCycles[selectedCycle].second : -1;
    const bool isCycleRow = rowNumber >= cycleStart && rowNumber <= cycleEnd;
    bool isOtherCycleRow = false;

    if (wavetableEngine && !isCycleRow)
    {
        for (int cycle = 0;
             cycle < static_cast<int>(detectedCycles.size());
             ++cycle)
        {
            if (cycle == selectedCycle)
                continue;

            const auto [start, end] = detectedCycles[cycle];
            if (rowNumber >= start && rowNumber <= end)
            {
                isOtherCycleRow = true;
                break;
            }
        }
    }

    const auto screenBackground = juce::Colour(0xff0b2425);
    const auto screenForeground = juce::Colour(0xff73cdbd);

    // Translucent layers deliberately blend at intersections. A selected
    // row/column remains cyan, a cycle remains amber, and their overlap
    // becomes a distinct mixed shade without losing the cell text.
    if (isOtherCycleRow)
        g.fillAll(juce::Colour(0xffe19a4f).withAlpha(0.085f));

    if (isCycleRow)
        g.fillAll(juce::Colour(0xffe19a4f).withAlpha(0.16f));

    if (isSelectedRow)
        g.fillAll(screenForeground.withAlpha(0.13f));

    if (isSelectedColumn)
        g.fillAll(screenForeground.withAlpha(0.12f));

    if (isSelectedCell)
        g.fillAll(screenForeground.withAlpha(0.62f));

    int col = columnId - 1;

    if (rowNumber >= 0 && rowNumber < csvData.size())
    {
        auto& row = csvData[rowNumber];

        if (col >= 0 && col < row.size())
        {
            g.setColour(isSelectedCell ? screenBackground : screenForeground);
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

    // The first and last cycle rows receive joined horizontal boundaries,
    // visually enclosing the complete block across every table column.
    if (hasSelectedCycle && rowNumber == cycleStart)
    {
        g.setColour(juce::Colour(0xffe19a4f));
        g.drawHorizontalLine(0, 0.0f, static_cast<float>(width));
        g.drawHorizontalLine(1, 0.0f, static_cast<float>(width));
    }

    if (hasSelectedCycle && rowNumber == cycleEnd)
    {
        g.setColour(juce::Colour(0xffe19a4f));
        g.drawHorizontalLine(height - 2, 0.0f, static_cast<float>(width));
        g.drawHorizontalLine(height - 1, 0.0f, static_cast<float>(width));
    }
}

void DataSonifierAudioProcessorEditor::cellClicked(
    int rowNumber,
    int columnId,
    const juce::MouseEvent&)
{
    const bool selectedSeriesChanged = columnId != selectedColumn;

    selectedRow = rowNumber;
    selectedHeaderColumn = -1;

    // Selecting another cell in the current series must not rerun cycle
    // detection, because detection starts by selecting its first result.
    if (selectedSeriesChanged)
    {
        audioProcessor.setSelectedColumn(columnId);
        refreshFromProcessor();
    }

    const auto* engineModeParameter =
        audioProcessor.getParameterState().getRawParameterValue("engineMode");

    const bool wavetableEngine = engineModeParameter != nullptr
        && static_cast<int>(std::round(engineModeParameter->load())) == 1;

    // In Wavetable mode, clicking a row inside a detected cycle makes that
    // cycle current everywhere. Rows outside all cycles leave the current
    // cycle unchanged.
    if (wavetableEngine)
    {
        const int method = static_cast<int>(std::round(
            audioProcessor.getParameterState()
                .getRawParameterValue("cycleMethod")->load()));
        if (method == 5)
        {
            handleManualSelectionPoint(rowNumber);
        }
        else
        {
        for (int cycle = 0;
             cycle < static_cast<int>(detectedCycles.size());
             ++cycle)
        {
            const auto [start, end] = detectedCycles[cycle];

            if (rowNumber >= start && rowNumber <= end)
            {
                selectCycle(cycle);
                break;
            }
        }
        }
    }

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
    currentDateColumn = state.dateColumn;

    modulationSourceSelector.clear(juce::dontSendNotification);
    for (int column = 0; column < columnHeaders.size(); ++column)
        modulationSourceSelector.addItem(
            columnHeaders[column], column + 1);
    modulationSourceSelector.setSelectedId(
        selectedModulationRoute == 0
            ? state.modulationColumn : state.modulationColumn2,
        juce::dontSendNotification);

    rebuildTableColumns();
    rebuildSeriesSelector();
    rebuildDateRangeSelectors(state);

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
    detectCycles();
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

void DataSonifierAudioProcessorEditor::detectCycles()
{
    detectedCycles.clear();
    selectedCycle = -1;
    usingFullSeries = false;

    if (!rebuildCycleAnalysisSeries()
        || cycleAnalysisSeries.size() < 2)
    {
        audioProcessor.setWavetableCycleRange(-1, -1);
        updateCycleDisplay();
        csvTable.repaint();
        return;
    }

    const int method = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("cycleMethod")->load()));
    if (method == 5)
    {
        manualSelectionStart = -1;
        seriesGraph.setSelectionAnchor(-1);
        audioProcessor.setWavetableCycleRange(-1, -1);
        updateCycleDisplay();
        csvTable.repaint();
        return;
    }

    double sum = 0.0;
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    int validCount = 0;

    for (const double value : cycleAnalysisSeries)
    {
        if (!std::isfinite(value))
            continue;
        sum += value;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
        ++validCount;
    }

    if (validCount < 2 || !(maximum > minimum))
    {
        audioProcessor.setWavetableCycleRange(-1, -1);
        updateCycleDisplay();
        csvTable.repaint();
        return;
    }

    const double mean = sum / validCount;
    const int minimumLength = juce::jmax(
        2, static_cast<int>(std::round(cycleMinimumLengthSlider.getValue())));
    const double requiredRange = (maximum - minimum)
        * cycleSensitivitySlider.getValue() / 100.0;

    std::vector<int> upwardCrossings;
    for (int i = 1; i < static_cast<int>(cycleAnalysisSeries.size()); ++i)
    {
        const double previous = cycleAnalysisSeries[i - 1];
        const double current = cycleAnalysisSeries[i];
        if (std::isfinite(previous) && std::isfinite(current)
            && previous <= mean && current > mean)
            upwardCrossings.push_back(i);
    }

    for (int crossing = 1;
         crossing < static_cast<int>(upwardCrossings.size()); ++crossing)
    {
        const int start = upwardCrossings[crossing - 1];
        const int end = upwardCrossings[crossing];
        if (end - start < minimumLength)
            continue;

        double cycleMin = std::numeric_limits<double>::infinity();
        double cycleMax = -std::numeric_limits<double>::infinity();
        for (int i = start; i <= end; ++i)
        {
            if (!std::isfinite(cycleAnalysisSeries[i]))
                continue;
            cycleMin = std::min(cycleMin, cycleAnalysisSeries[i]);
            cycleMax = std::max(cycleMax, cycleAnalysisSeries[i]);
        }

        if (cycleMax - cycleMin >= requiredRange)
            detectedCycles.emplace_back(start, end);
    }

    if (!detectedCycles.empty())
        selectCycle(0);
    else
    {
        audioProcessor.setWavetableCycleRange(-1, -1);
        updateCycleDisplay();
        csvTable.repaint();
    }
}

void DataSonifierAudioProcessorEditor::handleManualSelectionPoint(int index)
{
    const int engineMode = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("engineMode")->load()));
    const int method = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("cycleMethod")->load()));
    if (engineMode != 1 || method != 5 || showingWavetable
        || !juce::isPositiveAndBelow(
            index, static_cast<int>(cycleAnalysisSeries.size())))
        return;

    if (manualSelectionStart < 0)
    {
        manualSelectionStart = index;
        detectedCycles.clear();
        selectedCycle = -1;
        updateCycleDisplay();
        seriesGraph.setSelectionAnchor(index);
        csvTable.repaint();
        return;
    }

    int start = manualSelectionStart;
    int end = index;
    if (start > end)
        std::swap(start, end);
    if (start == end)
        return;

    manualSelectionStart = -1;
    seriesGraph.setSelectionAnchor(-1);
    detectedCycles.clear();
    detectedCycles.emplace_back(start, end);
    usingFullSeries = false;
    selectCycle(0);
}

bool DataSonifierAudioProcessorEditor::rebuildCycleAnalysisSeries()
{
    cycleAnalysisSeries = processedSeries;

    const auto* methodParameter = audioProcessor.getParameterState()
        .getRawParameterValue("cycleMethod");
    const int method = methodParameter == nullptr
        ? 0
        : static_cast<int>(std::round(methodParameter->load()));

    if (method == 1)
    {
        const auto* lambdaParameter = audioProcessor.getParameterState()
            .getRawParameterValue("hpLambda");
        const double lambda = lambdaParameter == nullptr
            ? 1600.0
            : static_cast<double>(lambdaParameter->load());
        cycleAnalysisSeries = applyHodrickPrescottFilter(
            processedSeries, lambda);
    }
    else if (method == 2)
    {
        const auto& parameters = audioProcessor.getParameterState();
        const double lowPeriod = parameters
            .getRawParameterValue("bkLowPeriod")->load();
        const double highPeriod = parameters
            .getRawParameterValue("bkHighPeriod")->load();
        const int truncation = static_cast<int>(std::round(parameters
            .getRawParameterValue("bkTruncation")->load()));
        cycleAnalysisSeries = applyBaxterKingFilter(
            processedSeries, lowPeriod, highPeriod, truncation);
    }
    else if (method == 3)
    {
        const auto& parameters = audioProcessor.getParameterState();
        const double lowPeriod = parameters
            .getRawParameterValue("cfLowPeriod")->load();
        const double highPeriod = parameters
            .getRawParameterValue("cfHighPeriod")->load();
        const bool removeDrift = parameters
            .getRawParameterValue("cfRemoveDrift")->load() >= 0.5f;
        cycleAnalysisSeries = applyChristianoFitzgeraldFilter(
            processedSeries, lowPeriod, highPeriod, removeDrift);
    }
    else if (method == 4)
    {
        const auto& parameters = audioProcessor.getParameterState();
        const int horizon = static_cast<int>(std::round(parameters
            .getRawParameterValue("hamiltonHorizon")->load()));
        const int lagCount = static_cast<int>(std::round(parameters
            .getRawParameterValue("hamiltonLags")->load()));
        cycleAnalysisSeries = applyHamiltonRegressionFilter(
            processedSeries, horizon, lagCount);
    }

    return cycleAnalysisSeries.size() == processedSeries.size()
        && cycleAnalysisSeries.size() >= 2;
}

void DataSonifierAudioProcessorEditor::selectCycle(int cycleIndex)
{
    if (!juce::isPositiveAndBelow(
            cycleIndex, static_cast<int>(detectedCycles.size())))
        return;

    selectedCycle = cycleIndex;
    std::vector<std::vector<double>> frames;
    frames.reserve(detectedCycles.size());
    for (const auto [start, end] : detectedCycles)
    {
        if (start >= 0 && end >= start
            && end < static_cast<int>(cycleAnalysisSeries.size()))
            frames.emplace_back(
                cycleAnalysisSeries.begin() + start,
                cycleAnalysisSeries.begin() + end + 1);
    }
    audioProcessor.setWavetableFrames(frames);

    if (auto* parameter = audioProcessor.getParameterState()
            .getParameter("wavetablePosition"))
    {
        const float position = detectedCycles.size() > 1
            ? static_cast<float>(selectedCycle)
                / static_cast<float>(detectedCycles.size() - 1)
            : 0.0f;
        parameter->beginChangeGesture();
        parameter->setValueNotifyingHost(position);
        parameter->endChangeGesture();
    }
    updateCycleDisplay();
    csvTable.repaint();
}

void DataSonifierAudioProcessorEditor::updateCycleDisplay()
{
    const bool wavetableEngine = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("engineMode")->load())) == 1;
    const bool hasCycle = juce::isPositiveAndBelow(
        selectedCycle, static_cast<int>(detectedCycles.size()));

    const int cycleMethod = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("cycleMethod")->load()));
    const bool manualMode = wavetableEngine && cycleMethod == 5;

    cycleStatusLabel.setText(
        manualMode && !hasCycle
            ? (manualSelectionStart < 0 ? "CLICK START" : "CLICK END")
            : hasCycle
            ? (usingFullSeries
                ? juce::String("FULL SERIES")
                : manualMode
                    ? ([this]()
                    {
                        const auto [start, end] =
                            detectedCycles[selectedCycle];
                        const int dateIndex = currentDateColumn - 1;
                        const bool hasDates = dateIndex >= 0
                            && juce::isPositiveAndBelow(
                                start, static_cast<int>(csvData.size()))
                            && juce::isPositiveAndBelow(
                                end, static_cast<int>(csvData.size()))
                            && dateIndex < csvData[start].size()
                            && dateIndex < csvData[end].size();
                        return hasDates
                            ? "MANUAL " + csvData[start][dateIndex]
                                + " - " + csvData[end][dateIndex]
                            : "MANUAL " + juce::String(start + 1)
                                + " - " + juce::String(end + 1);
                    })()
                : "CYCLE " + juce::String(selectedCycle + 1)
                    + " / " + juce::String(detectedCycles.size()))
            : "NO CYCLES",
        juce::dontSendNotification);

    previousCycleButton.setEnabled(detectedCycles.size() > 1);
    nextCycleButton.setEnabled(detectedCycles.size() > 1);
    auditionCycleButton.setEnabled(hasCycle);
    useFullSeriesButton.setEnabled(processedSeries.size() >= 2);
    seriesViewButton.setToggleState(!showingWavetable,
        juce::dontSendNotification);
    wavetableViewButton.setToggleState(showingWavetable,
        juce::dontSendNotification);

    if (!wavetableEngine || !hasCycle)
    {
        seriesGraph.setValues(cycleAnalysisSeries.empty()
            ? processedSeries : cycleAnalysisSeries);
        seriesGraph.setCycleRanges({});
        seriesGraph.setHighlightRange(-1, -1);
        return;
    }

    const auto [start, end] = detectedCycles[selectedCycle];
    if (showingWavetable)
    {
        const auto interpolatedWavetable =
            audioProcessor.getWavetableSnapshot();
        seriesGraph.setValues(interpolatedWavetable);
        seriesGraph.setTitle("INTERPOLATED WAVETABLE - "
            + juce::String(interpolatedWavetable.size())
            + " SAMPLES");
        seriesGraph.setCycleRanges({});
        seriesGraph.setHighlightRange(-1, -1);
    }
    else
    {
        seriesGraph.setValues(cycleAnalysisSeries);
        const int method = cycleMethod;
        if (method == 1)
            seriesGraph.setTitle("HP CYCLE COMPONENT");
        else if (method == 2)
            seriesGraph.setTitle("BAXTER-KING CYCLE COMPONENT");
        else if (method == 3)
            seriesGraph.setTitle("CHRISTIANO-FITZGERALD CYCLE COMPONENT");
        else if (method == 4)
            seriesGraph.setTitle("HAMILTON REGRESSION CYCLE COMPONENT");
        else if (method == 5)
            seriesGraph.setTitle("MANUAL CYCLE SELECTION");
        else if (selectedColumn > 0 && selectedColumn <= columnHeaders.size())
            seriesGraph.setTitle(columnHeaders[selectedColumn - 1]);
        seriesGraph.setCycleRanges(detectedCycles);
        seriesGraph.setHighlightRange(start, end);
    }
}

void DataSonifierAudioProcessorEditor::updateModulationRouteControls()
{
    modulationTargetAttachment.reset();
    modulationAmountAttachment.reset();

    const bool routeTwo = selectedModulationRoute == 1;
    modulationTargetAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
            audioProcessor.getParameterState(),
            routeTwo ? "modulationTarget2" : "modulationTarget",
            modulationTargetSelector);
    modulationAmountAttachment = std::make_unique<
        juce::AudioProcessorValueTreeState::SliderAttachment>(
            audioProcessor.getParameterState(),
            routeTwo ? "modulationAmount2" : "modulationAmount",
            modulationAmountSlider);

    const auto state = audioProcessor.getDataStateSnapshot();
    modulationSourceSelector.setSelectedId(
        routeTwo ? state.modulationColumn2 : state.modulationColumn,
        juce::dontSendNotification);
    updateEngineVisibility();
    repaint();
}

void DataSonifierAudioProcessorEditor::updateEngineVisibility()
{
    const int engineMode = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("engineMode")->load()));
    const bool wavetableEngine = engineMode == 1;

    const std::initializer_list<juce::Component*> midiComponents {
          &outputNoteButton,
          &outputControllerButton, &timingModeLabel, &timingFreeButton,
          &timingSyncButton, &subdivisionLabel, &subdivisionSelector,
          &playbackSpeedLabel, &playbackSpeedSlider, &pitchMappingModeLabel,
          &pitchBoundsButton, &pitchCentreSpreadButton, &lowestNoteLabel,
          &lowestNoteSlider, &highestNoteLabel, &highestNoteSlider,
          &centreNoteLabel, &centreNoteSlider, &pitchSpreadLabel,
          &pitchSpreadSlider, &controllerNumberSlider,
          &controllerMinimumLabel, &controllerMinimumSlider,
          &controllerMaximumLabel, &controllerMaximumSlider };

    for (auto* component : midiComponents)
        component->setVisible(!wavetableEngine);

    const std::initializer_list<juce::Component*> wavetableComponents {
          &seriesViewButton,
          &wavetableViewButton, &previousCycleButton, &cycleStatusLabel,
          &nextCycleButton, &auditionCycleButton,
          &wavetableNormaliseButton, &wavetableNormaliseOffButton,
          &wavetableLoopSmoothingLabel,
          &wavetableLoopSmoothingSlider, &wavetablePositionLabel,
          &wavetablePositionSlider, &cycleMinimumLengthLabel,
          &wavetableAttackLabel, &wavetableAttackSlider,
          &wavetableDecayLabel, &wavetableDecaySlider,
          &wavetableSustainLabel, &wavetableSustainSlider,
          &wavetableReleaseLabel, &wavetableReleaseSlider,
          &wavetableFilterCutoffLabel, &wavetableFilterCutoffSlider,
          &wavetableFilterResonanceLabel, &wavetableFilterResonanceSlider,
          &wavetableHighPassCutoffLabel, &wavetableHighPassCutoffSlider,
          &wavetableHighPassResonanceLabel,
          &wavetableHighPassResonanceSlider,
          &shapingViewButton, &modulationViewButton,
          &modulationRoute1Button, &modulationRoute2Button,
          &modulationSourceLabel, &modulationSourceSelector,
          &modulationTargetLabel, &modulationTargetSelector,
          &modulationAmountLabel, &modulationAmountSlider,
          &modulationTimingLabel, &modulationTimingSelector,
          &modulationRateLabel, &modulationRateSlider,
          &modulationDivisionLabel, &modulationDivisionSelector,
          &cycleMinimumLengthSlider, &cycleSensitivityLabel,
          &cycleSensitivitySlider, &findCyclesButton,
          &useFullSeriesButton, &cycleDetectionMethodSelector,
          &cycleDetectionMethodLabel };

    for (auto* component : wavetableComponents)
        component->setVisible(wavetableEngine);

    shapingViewButton.setToggleState(
        wavetableEngine && !showingModulation,
        juce::dontSendNotification);
    modulationViewButton.setToggleState(
        wavetableEngine && showingModulation,
        juce::dontSendNotification);
    modulationRoute1Button.setToggleState(
        wavetableEngine && showingModulation
            && selectedModulationRoute == 0,
        juce::dontSendNotification);
    modulationRoute2Button.setToggleState(
        wavetableEngine && showingModulation
            && selectedModulationRoute == 1,
        juce::dontSendNotification);

    for (auto* component : {
             static_cast<juce::Component*>(&wavetableAttackLabel),
             static_cast<juce::Component*>(&wavetableAttackSlider),
             static_cast<juce::Component*>(&wavetableDecayLabel),
             static_cast<juce::Component*>(&wavetableDecaySlider),
             static_cast<juce::Component*>(&wavetableSustainLabel),
             static_cast<juce::Component*>(&wavetableSustainSlider),
             static_cast<juce::Component*>(&wavetableReleaseLabel),
             static_cast<juce::Component*>(&wavetableReleaseSlider),
             static_cast<juce::Component*>(&wavetableFilterCutoffLabel),
             static_cast<juce::Component*>(&wavetableFilterCutoffSlider),
             static_cast<juce::Component*>(&wavetableFilterResonanceLabel),
             static_cast<juce::Component*>(&wavetableFilterResonanceSlider),
             static_cast<juce::Component*>(&wavetableHighPassCutoffLabel),
             static_cast<juce::Component*>(&wavetableHighPassCutoffSlider),
             static_cast<juce::Component*>(&wavetableHighPassResonanceLabel),
             static_cast<juce::Component*>(&wavetableHighPassResonanceSlider) })
        component->setVisible(wavetableEngine && !showingModulation);

    for (auto* component : {
             static_cast<juce::Component*>(&modulationSourceLabel),
             static_cast<juce::Component*>(&modulationRoute1Button),
             static_cast<juce::Component*>(&modulationRoute2Button),
             static_cast<juce::Component*>(&modulationSourceSelector),
             static_cast<juce::Component*>(&modulationTargetLabel),
             static_cast<juce::Component*>(&modulationTargetSelector),
             static_cast<juce::Component*>(&modulationAmountLabel),
             static_cast<juce::Component*>(&modulationAmountSlider),
             static_cast<juce::Component*>(&modulationTimingLabel),
             static_cast<juce::Component*>(&modulationTimingSelector),
             static_cast<juce::Component*>(&modulationRateLabel),
             static_cast<juce::Component*>(&modulationRateSlider),
             static_cast<juce::Component*>(&modulationDivisionLabel),
             static_cast<juce::Component*>(&modulationDivisionSelector) })
        component->setVisible(wavetableEngine && showingModulation);

    const bool freeModulationTiming = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("modulationTimingMode")->load())) == 0;
    modulationRateSlider.setEnabled(freeModulationTiming);
    modulationDivisionSelector.setEnabled(!freeModulationTiming);

    const int cycleMethod = static_cast<int>(std::round(
        audioProcessor.getParameterState()
            .getRawParameterValue("cycleMethod")->load()));
    hpLambdaLabel.setVisible(wavetableEngine && cycleMethod == 1);
    hpLambdaSlider.setVisible(wavetableEngine && cycleMethod == 1);
    const bool usingBaxterKing = wavetableEngine && cycleMethod == 2;
    bkLowPeriodLabel.setVisible(usingBaxterKing);
    bkLowPeriodSlider.setVisible(usingBaxterKing);
    bkHighPeriodLabel.setVisible(usingBaxterKing);
    bkHighPeriodSlider.setVisible(usingBaxterKing);
    bkTruncationLabel.setVisible(usingBaxterKing);
    bkTruncationSlider.setVisible(usingBaxterKing);

    const bool usingChristianoFitzgerald =
        wavetableEngine && cycleMethod == 3;
    cfLowPeriodLabel.setVisible(usingChristianoFitzgerald);
    cfLowPeriodSlider.setVisible(usingChristianoFitzgerald);
    cfHighPeriodLabel.setVisible(usingChristianoFitzgerald);
    cfHighPeriodSlider.setVisible(usingChristianoFitzgerald);
    cfDriftLabel.setVisible(usingChristianoFitzgerald);
    cfRemoveDriftButton.setVisible(usingChristianoFitzgerald);

    const bool usingHamiltonRegression =
        wavetableEngine && cycleMethod == 4;
    hamiltonHorizonLabel.setVisible(usingHamiltonRegression);
    hamiltonHorizonSlider.setVisible(usingHamiltonRegression);
    hamiltonLagsLabel.setVisible(usingHamiltonRegression);
    hamiltonLagsSlider.setVisible(usingHamiltonRegression);

    const bool usingManualSelection =
        wavetableEngine && cycleMethod == 5;
    cycleMinimumLengthLabel.setVisible(
        wavetableEngine && !usingManualSelection);
    cycleMinimumLengthSlider.setVisible(
        wavetableEngine && !usingManualSelection);
    cycleSensitivityLabel.setVisible(
        wavetableEngine && !usingManualSelection);
    cycleSensitivitySlider.setVisible(
        wavetableEngine && !usingManualSelection);
    findCyclesButton.setButtonText(
        usingManualSelection ? "RESET SELECTION" : "FIND CYCLES");

    constexpr int methodRowsY = 414;
    const int methodRowCount = cycleMethod == 2 || cycleMethod == 3
        ? 3 : (cycleMethod == 4 ? 2 : (cycleMethod == 1 ? 1 : 0));
    const int methodRowSpacing = methodRowCount == 1 ? 44 : 32;
    const auto positionCycleRow = [=](juce::Label& label,
                                     juce::Component& control,
                                     int row)
    {
        const int y = methodRowsY + row * methodRowSpacing;
        label.setBounds(698, y, 110, 18);
        control.setBounds(810, y - 6, 186, 30);
    };

    positionCycleRow(hpLambdaLabel, hpLambdaSlider, 0);
    positionCycleRow(bkLowPeriodLabel, bkLowPeriodSlider, 0);
    positionCycleRow(bkHighPeriodLabel, bkHighPeriodSlider, 1);
    positionCycleRow(bkTruncationLabel, bkTruncationSlider, 2);
    positionCycleRow(cfLowPeriodLabel, cfLowPeriodSlider, 0);
    positionCycleRow(cfHighPeriodLabel, cfHighPeriodSlider, 1);
    positionCycleRow(cfDriftLabel, cfRemoveDriftButton, 2);
    positionCycleRow(hamiltonHorizonLabel, hamiltonHorizonSlider, 0);
    positionCycleRow(hamiltonLagsLabel, hamiltonLagsSlider, 1);

    const int firstCommonY = methodRowCount == 0 ? 438
        : (methodRowCount == 1 ? 458
        : (methodRowCount == 2 ? 478 : 510));
    const int commonRowSpacing = methodRowCount == 0 ? 58
        : (methodRowCount == 1 ? 44 : 32);
    const int actionButtonsY = methodRowCount == 3 ? 568 : 554;
    cycleMinimumLengthLabel.setBounds(698, firstCommonY, 110, 18);
    cycleMinimumLengthSlider.setBounds(810, firstCommonY - 6, 186, 30);
    cycleSensitivityLabel.setBounds(
        698, firstCommonY + commonRowSpacing, 110, 18);
    cycleSensitivitySlider.setBounds(
        810, firstCommonY + commonRowSpacing - 6, 186, 30);
    findCyclesButton.setBounds(698, actionButtonsY, 144, 28);
    useFullSeriesButton.setBounds(852, actionButtonsY, 144, 28);
    if (usingManualSelection)
    {
        findCyclesButton.setBounds(698, 470, 144, 32);
        useFullSeriesButton.setBounds(852, 470, 144, 32);
    }

    if (lastEngineMode != engineMode)
    {
        lastEngineMode = engineMode;
        updateCycleDisplay();
        csvTable.repaint();
        repaint();
    }
}

void DataSonifierAudioProcessorEditor::rebuildDateRangeSelectors(
    const DataSonifierAudioProcessor::DataStateSnapshot& state)
{
    dateColumnSelector.clear(juce::dontSendNotification);
    for (int col = 0; col < columnHeaders.size(); ++col)
        dateColumnSelector.addItem(columnHeaders[col], col + 1);

    dateColumnSelector.setSelectedId(
        state.dateColumn, juce::dontSendNotification);

    dateStartSelector.clear(juce::dontSendNotification);
    dateEndSelector.clear(juce::dontSendNotification);

    int firstValidRow = -1;
    int lastValidRow = -1;

    for (int row = 0; row < state.dateValues.size(); ++row)
    {
        auto label = state.dateValues[row].trim();
        if (label.isEmpty())
            continue;

        if (firstValidRow < 0)
            firstValidRow = row;
        lastValidRow = row;

        // ComboBox IDs must be positive, so row zero is represented by ID 1.
        dateStartSelector.addItem(label, row + 1);
        dateEndSelector.addItem(label, row + 1);
    }

    const bool hasDates = firstValidRow >= 0;
    dateStartSelector.setEnabled(hasDates);
    dateEndSelector.setEnabled(hasDates);

    if (hasDates)
    {
        const int selectedStart = juce::jlimit(
            firstValidRow, lastValidRow, state.dateRangeStartRow);
        const int selectedEnd = juce::jlimit(
            selectedStart, lastValidRow, state.dateRangeEndRow);

        dateStartSelector.setSelectedId(
            selectedStart + 1, juce::dontSendNotification);
        dateEndSelector.setSelectedId(
            selectedEnd + 1, juce::dontSendNotification);
    }
}

void DataSonifierAudioProcessorEditor::timerCallback()
{
    updateEngineVisibility();
    seriesGraph.setPlaybackIndex(
        audioProcessor.getCurrentPlaybackIndex());

    const bool normaliseWavetable =
        audioProcessor.getParameterState()
            .getRawParameterValue("wavetableNormalize")->load() >= 0.5f;
    wavetableNormaliseButton.setToggleState(
        normaliseWavetable, juce::dontSendNotification);
    wavetableNormaliseOffButton.setToggleState(
        !normaliseWavetable, juce::dontSendNotification);

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

    const bool midiEngine = lastEngineMode == 0;
    pitchMappingModeLabel.setVisible(midiEngine && usingNotes);
    pitchBoundsButton.setVisible(midiEngine && usingNotes);
    pitchCentreSpreadButton.setVisible(midiEngine && usingNotes);
    lowestNoteLabel.setVisible(midiEngine && usingNotes);
    lowestNoteSlider.setVisible(midiEngine && usingNotes);
    highestNoteLabel.setVisible(midiEngine && usingNotes);
    highestNoteSlider.setVisible(midiEngine && usingNotes);
    centreNoteLabel.setVisible(midiEngine && usingNotes);
    centreNoteSlider.setVisible(midiEngine && usingNotes);
    pitchSpreadLabel.setVisible(midiEngine && usingNotes);
    pitchSpreadSlider.setVisible(midiEngine && usingNotes);

    controllerMinimumLabel.setVisible(midiEngine && usingController);
    controllerMinimumSlider.setVisible(midiEngine && usingController);
    controllerMaximumLabel.setVisible(midiEngine && usingController);
    controllerMaximumSlider.setVisible(midiEngine && usingController);
    controllerMinimumSlider.setEnabled(usingController);
    controllerMaximumSlider.setEnabled(usingController);
}
