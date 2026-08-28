/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace
{
bool tryParseNumber(const juce::String& input, double& result)
{
    const auto text = input.trim();

    if (text.isEmpty() || !text.containsAnyOf("0123456789"))
        return false;

    if (text.retainCharacters("0123456789+-.eE") != text)
        return false;

    result = text.getDoubleValue();
    return std::isfinite(result);
}
}

//==============================================================================
DataSonifierAudioProcessor::DataSonifierAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
     , parameterState(*this, nullptr, "PARAMETERS", createParameterLayout())
#else
     : parameterState(*this, nullptr, "PARAMETERS", createParameterLayout())
#endif
{
    parameterState.addParameterListener("wavetableNormalize", this);
    parameterState.addParameterListener("wavetableLoopSmoothing", this);
}

DataSonifierAudioProcessor::~DataSonifierAudioProcessor()
{
    cancelPendingUpdate();
    parameterState.removeParameterListener("wavetableNormalize", this);
    parameterState.removeParameterListener("wavetableLoopSmoothing", this);
}

//==============================================================================
const juce::String DataSonifierAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool DataSonifierAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool DataSonifierAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool DataSonifierAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double DataSonifierAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int DataSonifierAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing programs.
}

int DataSonifierAudioProcessor::getCurrentProgram()
{
    return 0;
}

void DataSonifierAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String DataSonifierAudioProcessor::getProgramName (int index)
{
    return {};
}

void DataSonifierAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

//==============================================================================
void DataSonifierAudioProcessor::prepareToPlay(
    double sampleRate,
    int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    currentSampleRate = sampleRate;

    samplesUntilNextNote = 0;
    samplesUntilNoteOff = -1;
    currentlyPlayingNote = -1;
    currentSeriesIndex = 0;
    currentPlaybackIndex.store(-1);
    wasTransportPlaying = false;
    wavetablePhase = 0.0;
    modulationPhase = 0.0;
    activeWavetable.reset();
    previousWavetable.reset();
    wavetableCrossfadeSamplesRemaining = 0;
    wavetableCrossfadeLengthSamples = juce::jmax(
        1, static_cast<int>(sampleRate * 0.02));
    wavetableNote = 48;
    wavetableVelocity = 0.0f;
    wavetableKeyDown = false;
    auditionEnvelopeActive = false;
    wavetableEnvelope.setSampleRate(sampleRate);
    wavetableEnvelope.reset();
    wavetableLowPassFilter.setType(
        juce::dsp::StateVariableTPTFilterType::lowpass);
    wavetableLowPassFilter.prepare({
        sampleRate,
        static_cast<juce::uint32>(samplesPerBlock),
        static_cast<juce::uint32>(juce::jmax(1, getTotalNumOutputChannels())) });
    wavetableLowPassFilter.reset();
    wavetableHighPassFilter.setType(
        juce::dsp::StateVariableTPTFilterType::highpass);
    wavetableHighPassFilter.prepare({
        sampleRate,
        static_cast<juce::uint32>(samplesPerBlock),
        static_cast<juce::uint32>(juce::jmax(1, getTotalNumOutputChannels())) });
    wavetableHighPassFilter.reset();
}

void DataSonifierAudioProcessor::releaseResources()
{
    // When playback stops, you can use this as an opportunity to free up any
    // spare memory, etc.
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool DataSonifierAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    // This is the place where you check if the layout is supported.
    // In this template code we only support mono or stereo.
    // Some plugin hosts, such as certain GarageBand versions, will only
    // load plugins that support stereo bus layouts.
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // This checks if the input layout matches the output layout
   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void DataSonifierAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // In case we have more outputs than inputs, this code clears any output
    // channels that didn't contain input data, (because these aren't
    // guaranteed to be empty - they may contain garbage).
    // This is here to avoid people getting screaming feedback
    // when they first compile a plugin, but obviously you don't need to keep
    // this code if your algorithm always overwrites all the output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // This is the place where you'd normally do the guts of your plugin's
    // audio processing...
    // Make sure to reset the state if your inner loop is processing
    // the samples and the outer loop is handling the channels.
    // Alternatively, you can process the samples with the channels
    // interleaved by keeping the same state.
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        auto* channelData = buffer.getWritePointer (channel);

        // ..do something to the data...
    }

    const int engineMode = static_cast<int>(std::round(
        parameterState.getRawParameterValue("engineMode")->load()));

    if (engineMode == 1)
    {
        processWavetableSynth(buffer, midiMessages);
        return;
    }

    bool transportIsPlaying = false;
    double hostBpm = 120.0;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            transportIsPlaying = position->getIsPlaying();

            if (const auto bpm = position->getBpm())
                hostBpm = *bpm;
        }
    }

    // Ableton has just been stopped.
    if (!transportIsPlaying)
    {
        // Prevent any incoming MIDI events from passing through.
        midiMessages.clear();

        // Ensure no note can remain held when playback stops.
        if (wasTransportPlaying)
            midiMessages.addEvent(juce::MidiMessage::allNotesOff(1), 0);

        // Reset playback so the next Play starts from the first CSV value.
        currentSeriesIndex = 0;
        samplesUntilNextNote = 0;
        samplesUntilNoteOff = -1;
        currentlyPlayingNote = -1;
        currentPlaybackIndex.store(-1);
        wasTransportPlaying = false;

        return;
    }

    wasTransportPlaying = true;

    auto normalisedValues = std::atomic_load(&normalisedMidiSeries);

    const int blockSize = buffer.getNumSamples();

    for (int sample = 0; sample < blockSize; ++sample)
    {
        // Send the note-off when its duration has finished.
        if (samplesUntilNoteOff == 0 && currentlyPlayingNote >= 0)
        {
            midiMessages.addEvent(
                juce::MidiMessage::noteOff(
                    1,
                    currentlyPlayingNote),
                sample);

            currentlyPlayingNote = -1;
            currentPlaybackIndex.store(-1);
            samplesUntilNoteOff = -1;
        }

        // Move to the next value at the selected Free or Sync rate.
        if (samplesUntilNextNote == 0)
        {
            constexpr int midiChannel = 1;
            constexpr float velocity = 0.8f;

            const int timingMode = static_cast<int>(
                parameterState.getRawParameterValue("timingMode")->load());

            double intervalSeconds = 1.0;

            if (timingMode == 0) // Free: rate is measured in Hz.
            {
                const double notesPerSecond =
                    parameterState.getRawParameterValue("speed")->load();

                intervalSeconds = 1.0 / notesPerSecond;
            }
            else // Sync: rate follows Ableton's tempo and note division.
            {
                const int subdivision = juce::jlimit(
                    0,
                    7,
                    static_cast<int>(parameterState
                        .getRawParameterValue("subdivision")->load()));

                constexpr double quarterNoteLengths[] = {
                    4.0,       // 1/1
                    2.0,       // 1/2
                    1.0,       // 1/4
                    0.5,       // 1/8
                    0.25,      // 1/16
                    2.0 / 3.0, // 1/4 triplet
                    1.0 / 3.0, // 1/8 triplet
                    1.0 / 6.0  // 1/16 triplet
                };

                const double secondsPerQuarterNote =
                    60.0 / juce::jmax(1.0, hostBpm);

                intervalSeconds = secondsPerQuarterNote
                    * quarterNoteLengths[subdivision];
            }

            samplesUntilNextNote = juce::jmax(
                1,
                static_cast<int>(currentSampleRate * intervalSeconds));

            // Safety: finish the previous note before starting another one.
            if (currentlyPlayingNote >= 0)
            {
                midiMessages.addEvent(
                    juce::MidiMessage::noteOff(
                        midiChannel,
                        currentlyPlayingNote),
                    sample);

                currentlyPlayingNote = -1;
                currentPlaybackIndex.store(-1);
                samplesUntilNoteOff = -1;
            }

            if (normalisedValues != nullptr && !normalisedValues->empty())
            {
                // Keep the index valid if a new CSV series has a different size.
                currentSeriesIndex %= normalisedValues->size();

                const float normalisedValue =
                    (*normalisedValues)[currentSeriesIndex];

                currentPlaybackIndex.store(-1);

                // -1 represents an invalid/missing value, so play a rest.
                if (normalisedValue >= 0.0f)
                {
                    const int outputMode = static_cast<int>(std::round(
                        parameterState
                            .getRawParameterValue("outputMode")->load()));

                    if (outputMode == 0) // Note output
                    {
                    const int pitchMappingMode = static_cast<int>(std::round(
                        parameterState
                            .getRawParameterValue("pitchMappingMode")->load()));

                    int lowestNote = 0;
                    int highestNote = 127;

                    if (pitchMappingMode == 0)
                    {
                        lowestNote = static_cast<int>(std::round(
                            parameterState
                                .getRawParameterValue("lowestNote")->load()));

                        highestNote = static_cast<int>(std::round(
                            parameterState
                                .getRawParameterValue("highestNote")->load()));

                        if (lowestNote > highestNote)
                            std::swap(lowestNote, highestNote);
                    }
                    else
                    {
                        const int centreNote = static_cast<int>(std::round(
                            parameterState
                                .getRawParameterValue("centreNote")->load()));

                        const int spread = static_cast<int>(std::round(
                            parameterState
                                .getRawParameterValue("pitchSpread")->load()));

                        lowestNote = juce::jmax(0, centreNote - spread);
                        highestNote = juce::jmin(127, centreNote + spread);
                    }

                    const int noteNumber = juce::jlimit(
                        0,
                        127,
                        static_cast<int>(std::round(
                            lowestNote
                            + normalisedValue * (highestNote - lowestNote))));

                    midiMessages.addEvent(
                        juce::MidiMessage::noteOn(
                            midiChannel,
                            noteNumber,
                            velocity),
                        sample);

                    currentlyPlayingNote = noteNumber;
                    currentPlaybackIndex.store(
                        static_cast<int>(currentSeriesIndex));

                    // Keep notes shorter than their interval so high rates
                    // cannot create overlapping or permanently held notes.
                    const int maximumNoteLength =
                        static_cast<int>(currentSampleRate * 0.2);

                    samplesUntilNoteOff = juce::jmax(
                        1,
                        juce::jmin(
                            maximumNoteLength,
                            static_cast<int>(samplesUntilNextNote * 0.8)));
                    }
                    else // MIDI controller output
                    {
                        const int controllerNumber = juce::jlimit(
                            0,
                            127,
                            static_cast<int>(std::round(parameterState
                                .getRawParameterValue("controllerNumber")->load())));

                        int controllerMinimum = static_cast<int>(std::round(
                            parameterState
                                .getRawParameterValue("controllerMinimum")->load()));

                        int controllerMaximum = static_cast<int>(std::round(
                            parameterState
                                .getRawParameterValue("controllerMaximum")->load()));

                        if (controllerMinimum > controllerMaximum)
                            std::swap(controllerMinimum, controllerMaximum);

                        const int controllerValue = juce::jlimit(
                            0,
                            127,
                            static_cast<int>(std::round(
                                controllerMinimum
                                + normalisedValue
                                    * (controllerMaximum - controllerMinimum))));

                        midiMessages.addEvent(
                            juce::MidiMessage::controllerEvent(
                                midiChannel,
                                controllerNumber,
                                controllerValue),
                            sample);

                        currentPlaybackIndex.store(
                            static_cast<int>(currentSeriesIndex));
                    }
                }

                ++currentSeriesIndex;

                if (currentSeriesIndex >= normalisedValues->size())
                    currentSeriesIndex = 0;
            }

        }

        if (samplesUntilNextNote > 0)
            --samplesUntilNextNote;

        if (samplesUntilNoteOff > 0)
            --samplesUntilNoteOff;
    }
}

//==============================================================================
bool DataSonifierAudioProcessor::hasEditor() const
{
    return true; // (change this to false if you choose to not supply an editor)
}

juce::AudioProcessorEditor* DataSonifierAudioProcessor::createEditor()
{
    return new DataSonifierAudioProcessorEditor (*this);
}

//==============================================================================
void DataSonifierAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameterState.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void DataSonifierAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        if (xml->hasTagName(parameterState.state.getType()))
            parameterState.replaceState(juce::ValueTree::fromXml(*xml));
    }
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DataSonifierAudioProcessor();
}

bool DataSonifierAudioProcessor::loadCsvFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    juce::StringArray lines;
    // A terminal CR/LF can be returned by StringArray::addLines as one more
    // logical line. Remove only trailing whitespace before splitting so the
    // final newline cannot become a phantom observation.
    const auto csvText = file.loadFileAsString().trimEnd();
    lines.addLines(csvText);

    std::vector<juce::StringArray> parsedRows;
    parsedRows.reserve(static_cast<size_t>(lines.size()));

    for (const auto& line : lines)
    {
        juce::StringArray cells;
        cells.addTokens(line, ",", "\"");
        cells.trim();
        parsedRows.push_back(std::move(cells));
    }

    if (parsedRows.empty() || parsedRows.front().isEmpty())
        return false;

    std::vector<double> seriesForMidi;

    {
        const juce::ScopedLock lock(dataStateLock);

        columnHeaders = parsedRows.front();
        parsedRows.erase(parsedRows.begin());
        originalCsvData = std::move(parsedRows);
        loadedFilePath = file.getFullPathName();
        selectedColumn = columnHeaders.isEmpty() ? -1 : 1;
        selectedTransform = 1;
        modulationColumn = -1;
        modulationColumn2 = -1;
        parameterState.state.setProperty(
            "modulationColumn", modulationColumn, nullptr);
        parameterState.state.setProperty(
            "modulationColumn2", modulationColumn2, nullptr);

        dateColumn = -1;
        for (int column = 0; column < columnHeaders.size(); ++column)
        {
            const auto header = columnHeaders[column].toLowerCase();
            if (header.contains("date") || header.contains("year")
                || header.contains("time") || header.contains("period"))
            {
                dateColumn = column + 1;
                break;
            }
        }

        // Most economic datasets put the observation date in column one.
        // The user can correct this from the editor if the guess is wrong.
        if (dateColumn < 1 && !columnHeaders.isEmpty())
            dateColumn = 1;

        dateRangeStartRow = originalCsvData.empty() ? -1 : 0;
        dateRangeEndRow = originalCsvData.empty()
            ? -1
            : static_cast<int>(originalCsvData.size()) - 1;

        rebuildProcessedSeries();
        rebuildModulationSeries();
        seriesForMidi = processedSeries;
    }

    rebuildNormalisedMidiSeries(seriesForMidi);
    return true;
}

void DataSonifierAudioProcessor::setSelectedColumn(int columnId)
{
    std::vector<double> seriesForMidi;

    {
        const juce::ScopedLock lock(dataStateLock);
        selectedColumn = juce::isPositiveAndBelow(columnId - 1, columnHeaders.size())
            ? columnId
            : -1;
        rebuildProcessedSeries();
        rebuildModulationSeries();
        seriesForMidi = processedSeries;
    }

    rebuildNormalisedMidiSeries(seriesForMidi);
}

void DataSonifierAudioProcessor::setSelectedTransform(int transformId)
{
    std::vector<double> seriesForMidi;

    {
        const juce::ScopedLock lock(dataStateLock);
        selectedTransform = juce::jlimit(1, 3, transformId);
        rebuildProcessedSeries();
        rebuildModulationSeries();
        seriesForMidi = processedSeries;
    }

    rebuildNormalisedMidiSeries(seriesForMidi);
}

void DataSonifierAudioProcessor::setDateColumn(int columnId)
{
    std::vector<double> seriesForMidi;

    {
        const juce::ScopedLock lock(dataStateLock);
        dateColumn = juce::isPositiveAndBelow(columnId - 1, columnHeaders.size())
            ? columnId
            : -1;
        dateRangeStartRow = originalCsvData.empty() ? -1 : 0;
        dateRangeEndRow = originalCsvData.empty()
            ? -1
            : static_cast<int>(originalCsvData.size()) - 1;
        rebuildProcessedSeries();
        rebuildModulationSeries();
        seriesForMidi = processedSeries;
    }

    rebuildNormalisedMidiSeries(seriesForMidi);
}

void DataSonifierAudioProcessor::setDateRange(int startRow, int endRow)
{
    std::vector<double> seriesForMidi;

    {
        const juce::ScopedLock lock(dataStateLock);
        if (originalCsvData.empty())
        {
            dateRangeStartRow = -1;
            dateRangeEndRow = -1;
        }
        else
        {
            const int finalRow = static_cast<int>(originalCsvData.size()) - 1;
            startRow = juce::jlimit(0, finalRow, startRow);
            endRow = juce::jlimit(0, finalRow, endRow);
            if (startRow > endRow)
                std::swap(startRow, endRow);
            dateRangeStartRow = startRow;
            dateRangeEndRow = endRow;
        }

        rebuildProcessedSeries();
        rebuildModulationSeries();
        seriesForMidi = processedSeries;
    }

    rebuildNormalisedMidiSeries(seriesForMidi);
}

void DataSonifierAudioProcessor::setModulationColumn(int columnId)
{
    const juce::ScopedLock lock(dataStateLock);
    modulationColumn = juce::isPositiveAndBelow(
        columnId - 1, columnHeaders.size()) ? columnId : -1;
    parameterState.state.setProperty(
        "modulationColumn", modulationColumn, nullptr);
    rebuildModulationSeries();
}

void DataSonifierAudioProcessor::setModulationColumn2(int columnId)
{
    const juce::ScopedLock lock(dataStateLock);
    modulationColumn2 = juce::isPositiveAndBelow(
        columnId - 1, columnHeaders.size()) ? columnId : -1;
    parameterState.state.setProperty(
        "modulationColumn2", modulationColumn2, nullptr);
    rebuildModulationSeries();
}

void DataSonifierAudioProcessor::setWavetableCycleRange(
    int startIndex,
    int endIndex)
{
    if (startIndex < 0 || endIndex < startIndex)
    {
        modulationCycleStart.store(0);
        modulationCycleEnd.store(-1);
        rebuildWavetable({});
        auditionSamplesRemaining.store(0);
        return;
    }

    std::vector<double> cycle;

    {
        const juce::ScopedLock lock(dataStateLock);
        if (processedSeries.empty())
        {
            rebuildWavetable({});
            return;
        }

        const int last = static_cast<int>(processedSeries.size()) - 1;
        startIndex = juce::jlimit(0, last, startIndex);
        endIndex = juce::jlimit(startIndex, last, endIndex);
        modulationCycleStart.store(startIndex);
        modulationCycleEnd.store(endIndex);
        cycle.assign(processedSeries.begin() + startIndex,
                     processedSeries.begin() + endIndex + 1);
    }

    rebuildWavetable(cycle);
}

void DataSonifierAudioProcessor::setWavetableSource(
    const std::vector<double>& source)
{
    rebuildWavetable(source);
}

void DataSonifierAudioProcessor::auditionWavetable()
{
    auditionSamplesRemaining.store(
        juce::jmax(1, static_cast<int>(currentSampleRate)));
}

DataSonifierAudioProcessor::DataStateSnapshot
DataSonifierAudioProcessor::getDataStateSnapshot() const
{
    const juce::ScopedLock lock(dataStateLock);

    juce::StringArray dateValues;
    const int dateCol = dateColumn - 1;
    if (dateCol >= 0)
    {
        for (const auto& row : originalCsvData)
            dateValues.add(dateCol < row.size() ? row[dateCol] : juce::String{});
    }

    return {
        displayCsvData,
        columnHeaders,
        processedSeries,
        loadedFilePath,
        selectedColumn,
        selectedTransform,
        dateColumn,
        dateRangeStartRow,
        dateRangeEndRow,
        dateValues,
        modulationColumn,
        modulationColumn2
    };
}

std::vector<double>
DataSonifierAudioProcessor::getWavetableSnapshot() const
{
    const auto table = std::atomic_load(&wavetable);
    if (table == nullptr)
        return {};

    return std::vector<double>(table->begin(), table->end());
}

void DataSonifierAudioProcessor::rebuildProcessedSeries()
{
    displayCsvData.clear();
    processedSeries.clear();

    if (originalCsvData.empty())
        return;

    const int finalRow = static_cast<int>(originalCsvData.size()) - 1;
    const int firstIncludedRow = dateRangeStartRow < 0
        ? 0
        : juce::jlimit(0, finalRow, dateRangeStartRow);
    const int lastIncludedRow = dateRangeEndRow < 0
        ? finalRow
        : juce::jlimit(firstIncludedRow, finalRow, dateRangeEndRow);

    displayCsvData.assign(
        originalCsvData.begin() + firstIncludedRow,
        originalCsvData.begin() + lastIncludedRow + 1);

    if (selectedColumn <= 0)
        return;

    const int col = selectedColumn - 1;
    const double missingValue = std::numeric_limits<double>::quiet_NaN();

    std::vector<double> rawValues;
    rawValues.reserve(displayCsvData.size());

    for (const auto& row : displayCsvData)
    {
        if (col >= row.size())
        {
            rawValues.push_back(missingValue);
            continue;
        }

        double value = 0.0;
        rawValues.push_back(tryParseNumber(row[col], value) ? value : missingValue);
    }

    if (selectedTransform == 1)
    {
        processedSeries = rawValues;
        return;
    }

    if (selectedTransform == 2)
    {
        processedSeries.reserve(rawValues.size());

        for (const double value : rawValues)
            processedSeries.push_back(std::isfinite(value) && value > 0.0
                ? std::log(value)
                : missingValue);
    }
    else
    {
        processedSeries.resize(rawValues.size(), missingValue);

        for (size_t row = 1; row < rawValues.size(); ++row)
        {
            if (std::isfinite(rawValues[row]) && std::isfinite(rawValues[row - 1]))
                processedSeries[row] = rawValues[row] - rawValues[row - 1];
        }
    }

    for (size_t row = 0; row < processedSeries.size(); ++row)
    {
        if (row >= displayCsvData.size() || col >= displayCsvData[row].size())
            continue;

        const auto value = processedSeries[row];
        displayCsvData[row].set(col,
            std::isfinite(value) ? juce::String(value, 4) : juce::String("N/A"));
    }
}

void DataSonifierAudioProcessor::rebuildModulationSeries()
{
    const auto buildValues = [this](int columnId)
    {
        auto values = std::make_shared<std::vector<float>>();
        if (columnId <= 0 || displayCsvData.empty())
            return values;

        const int column = columnId - 1;
        std::vector<double> rawValues;
        rawValues.reserve(displayCsvData.size());
        double minimum = std::numeric_limits<double>::infinity();
        double maximum = -std::numeric_limits<double>::infinity();

        for (const auto& row : displayCsvData)
        {
            double value = std::numeric_limits<double>::quiet_NaN();
            if (column < row.size())
                tryParseNumber(row[column], value);
            rawValues.push_back(value);
            if (std::isfinite(value))
            {
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }

        const bool validRange = std::isfinite(minimum)
            && std::isfinite(maximum);
        values->reserve(rawValues.size());
        for (const double value : rawValues)
        {
            if (!validRange || !std::isfinite(value))
            {
                values->push_back(0.5f);
                continue;
            }
            const float normalised = minimum == maximum
                ? 0.5f
                : static_cast<float>((value - minimum) / (maximum - minimum));
            values->push_back(juce::jlimit(0.0f, 1.0f, normalised));
        }
        return values;
    };

    auto values = buildValues(modulationColumn);
    auto values2 = buildValues(modulationColumn2);
    std::atomic_store(&normalisedModulationSeries,
        std::shared_ptr<const std::vector<float>>(values));
    std::atomic_store(&normalisedModulationSeries2,
        std::shared_ptr<const std::vector<float>>(values2));
    modulationCycleStart.store(0);
    modulationCycleEnd.store(
        displayCsvData.empty() ? -1
                               : static_cast<int>(displayCsvData.size()) - 1);
}

void DataSonifierAudioProcessor::rebuildNormalisedMidiSeries(
    const std::vector<double>& series)
{
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();

    // Find the minimum and maximum valid values.
    for (double value : series)
    {
        if (!std::isfinite(value))
            continue;

        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }

    auto values = std::make_shared<std::vector<float>>();
    values->reserve(series.size());

    const bool hasValidValues =
        std::isfinite(minimum) && std::isfinite(maximum);

    for (double value : series)
    {
        // Missing values become rests.
        if (!hasValidValues || !std::isfinite(value))
        {
            values->push_back(-1.0f);
            continue;
        }

        float normalisedValue = 0.5f;

        if (minimum != maximum)
            normalisedValue = static_cast<float>(
                (value - minimum) / (maximum - minimum));

        values->push_back(juce::jlimit(0.0f, 1.0f, normalisedValue));
    }

    std::atomic_store(
        &normalisedMidiSeries,
        std::shared_ptr<const std::vector<float>>(values));

    rebuildWavetable(series);

    DBG("Normalised "
        << values->size()
        << " values for MIDI mapping");
}

void DataSonifierAudioProcessor::rebuildWavetable(
    const std::vector<double>& series)
{
    const juce::ScopedLock buildLock(wavetableBuildLock);

    {
        const juce::ScopedLock sourceLock(wavetableSourceLock);
        currentWavetableSource = series;
    }

    constexpr size_t wavetableSize = 2048;
    constexpr size_t seamLength = 128;
    const bool shouldNormalise = parameterState
        .getRawParameterValue("wavetableNormalize")->load() >= 0.5f;
    const float smoothingAmount = juce::jlimit(
        0.0f, 1.0f,
        parameterState
            .getRawParameterValue("wavetableLoopSmoothing")->load());

    std::vector<float> source;
    source.reserve(series.size());

    double sum = 0.0;
    for (const double value : series)
    {
        if (!std::isfinite(value))
            continue;

        source.push_back(static_cast<float>(value));
        sum += value;
    }

    auto table = std::make_shared<std::vector<float>>();

    if (source.size() >= 2)
    {
        const float sourceMean = static_cast<float>(sum / source.size());
        for (auto& value : source)
            value -= sourceMean;

        table->resize(wavetableSize);

        // Every detected cycle becomes the same-sized waveform. This keeps
        // oscillator pitch independent of the number of spreadsheet rows in
        // the source cycle.
        const double sourceScale = static_cast<double>(source.size() - 1)
            / static_cast<double>(wavetableSize - 1);

        for (size_t index = 0; index < wavetableSize; ++index)
        {
            const double sourcePosition = static_cast<double>(index)
                * sourceScale;
            const size_t indexA = static_cast<size_t>(sourcePosition);
            const size_t indexB = std::min(indexA + 1, source.size() - 1);
            const float fraction = static_cast<float>(
                sourcePosition - static_cast<double>(indexA));

            (*table)[index] = juce::jmap(
                fraction, source[indexA], source[indexB]);
        }

        // Bring both sides of the loop smoothly towards the same value. The
        // smoothstep curve confines the change to a short region around the
        // wrap point and avoids a click when the oscillator loops.
        const float seamValue = 0.5f
            * (table->front() + table->back());

        for (size_t offset = 0; offset < seamLength; ++offset)
        {
            const float position = static_cast<float>(offset)
                / static_cast<float>(seamLength - 1);
            const float blend = position * position
                * (3.0f - 2.0f * position);
            const size_t endIndex = wavetableSize - 1 - offset;

            const float startValue = (*table)[offset];
            const float endValue = (*table)[endIndex];
            const float smoothedStart = juce::jmap(
                blend, seamValue, startValue);
            const float smoothedEnd = juce::jmap(
                blend, seamValue, endValue);

            (*table)[offset] = juce::jmap(
                smoothingAmount, startValue, smoothedStart);
            (*table)[endIndex] = juce::jmap(
                smoothingAmount, endValue, smoothedEnd);
        }

        // Seam processing can introduce a small DC offset, so centre and
        // normalise the finished table once more.
        double tableSum = 0.0;
        for (const float value : *table)
            tableSum += value;

        const float tableMean = static_cast<float>(
            tableSum / static_cast<double>(table->size()));
        float peak = 0.0f;

        for (auto& value : *table)
        {
            value -= tableMean;
            peak = std::max(peak, std::abs(value));
        }

        if (shouldNormalise && peak > 0.0f)
            for (auto& value : *table)
                value /= peak;
    }
    std::atomic_store(
        &wavetable,
        std::shared_ptr<const std::vector<float>>(table));
}

void DataSonifierAudioProcessor::parameterChanged(
    const juce::String& parameterID, float newValue)
{
    juce::ignoreUnused(newValue);

    if (parameterID == "wavetableNormalize"
        || parameterID == "wavetableLoopSmoothing")
        triggerAsyncUpdate();
}

void DataSonifierAudioProcessor::handleAsyncUpdate()
{
    std::vector<double> source;

    {
        const juce::ScopedLock sourceLock(wavetableSourceLock);
        source = currentWavetableSource;
    }

    rebuildWavetable(source);
}

void DataSonifierAudioProcessor::processWavetableSynth(
    juce::AudioBuffer<float>& buffer,
    juce::MidiBuffer& midiMessages)
{
    buffer.clear();

    juce::ADSR::Parameters envelopeParameters;
    envelopeParameters.attack = parameterState
        .getRawParameterValue("wavetableAttack")->load();
    envelopeParameters.decay = parameterState
        .getRawParameterValue("wavetableDecay")->load();
    envelopeParameters.sustain = parameterState
        .getRawParameterValue("wavetableSustain")->load();
    envelopeParameters.release = parameterState
        .getRawParameterValue("wavetableRelease")->load();
    wavetableEnvelope.setParameters(envelopeParameters);

    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();

        if (message.isNoteOn())
        {
            wavetableNote = message.getNoteNumber();
            wavetableVelocity = message.getFloatVelocity();
            wavetableKeyDown = true;
            auditionEnvelopeActive = false;
            auditionSamplesRemaining.store(0);
            wavetableEnvelope.noteOn();
        }
        else if ((message.isNoteOff()
                    && message.getNoteNumber() == wavetableNote)
                 || message.isAllNotesOff()
                 || message.isAllSoundOff())
        {
            wavetableKeyDown = false;
            wavetableEnvelope.noteOff();
        }
    }

    midiMessages.clear();

    const auto latestWavetable = std::atomic_load(&wavetable);

    if (latestWavetable != activeWavetable)
    {
        previousWavetable = activeWavetable;
        activeWavetable = latestWavetable;

        const bool canCrossfade = previousWavetable != nullptr
            && previousWavetable->size() >= 2
            && activeWavetable != nullptr
            && activeWavetable->size() >= 2;
        wavetableCrossfadeSamplesRemaining = canCrossfade
            ? wavetableCrossfadeLengthSamples : 0;
    }

    const auto& table = activeWavetable;
    const int auditionRemaining = auditionSamplesRemaining.load();
    const bool auditioning = auditionRemaining > 0;

    if (auditioning && !wavetableKeyDown && !auditionEnvelopeActive)
    {
        wavetableNote = 48;
        wavetableVelocity = 0.8f;
        auditionEnvelopeActive = true;
        wavetableEnvelope.noteOn();
    }

    if (table == nullptr || table->size() < 2
        || (!wavetableEnvelope.isActive() && !auditioning))
        return;

    const auto modulationValues = std::atomic_load(
        &normalisedModulationSeries);
    const auto modulationValues2 = std::atomic_load(
        &normalisedModulationSeries2);
    const int modulationTarget = static_cast<int>(std::round(
        parameterState.getRawParameterValue("modulationTarget")->load()));
    const float modulationAmount = parameterState
        .getRawParameterValue("modulationAmount")->load();
    const int modulationTarget2 = static_cast<int>(std::round(
        parameterState.getRawParameterValue("modulationTarget2")->load()));
    const float modulationAmount2 = parameterState
        .getRawParameterValue("modulationAmount2")->load();

    double hostBpm = 120.0;
    if (auto* playHead = getPlayHead())
        if (auto position = playHead->getPosition())
            if (const auto bpm = position->getBpm())
                hostBpm = *bpm;

    const int modulationTimingMode = static_cast<int>(std::round(
        parameterState.getRawParameterValue("modulationTimingMode")->load()));
    double modulationValuesPerSecond = parameterState
        .getRawParameterValue("modulationRate")->load();

    if (modulationTimingMode == 1)
    {
        const int subdivision = juce::jlimit(
            0, 7, static_cast<int>(std::round(parameterState
                .getRawParameterValue("modulationSubdivision")->load())));
        constexpr double quarterNoteLengths[] = {
            4.0, 2.0, 1.0, 0.5, 0.25,
            2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0
        };
        const double secondsPerStep = (60.0 / juce::jmax(1.0, hostBpm))
            * quarterNoteLengths[subdivision];
        modulationValuesPerSecond = 1.0 / secondsPerStep;
    }

    int modulationPointCount = 1;
    const auto modulationSize = juce::jmax(
        modulationValues == nullptr ? 0 : static_cast<int>(modulationValues->size()),
        modulationValues2 == nullptr ? 0 : static_cast<int>(modulationValues2->size()));
    if (modulationSize > 0)
    {
        const int finalIndex = modulationSize - 1;
        const int start = juce::jlimit(
            0, finalIndex, modulationCycleStart.load());
        const int requestedEnd = modulationCycleEnd.load();
        const int end = requestedEnd < start
            ? finalIndex
            : juce::jlimit(start, finalIndex, requestedEnd);
        modulationPointCount = juce::jmax(1, end - start + 1);
    }
    const double modulationPhaseIncrement = modulationValuesPerSecond
        / (static_cast<double>(modulationPointCount) * currentSampleRate);
    const auto readModulation = [this](
        const std::shared_ptr<const std::vector<float>>& values,
        double phase)
    {
        if (values == nullptr || values->empty())
            return 0.5f;

        const int finalIndex = static_cast<int>(values->size()) - 1;
        const int start = juce::jlimit(
            0, finalIndex, modulationCycleStart.load());
        const int requestedEnd = modulationCycleEnd.load();
        const int end = requestedEnd < start
            ? finalIndex
            : juce::jlimit(start, finalIndex, requestedEnd);

        if (start == end)
            return (*values)[static_cast<size_t>(start)];

        const double position = start + phase * (end - start);
        const int indexA = juce::jlimit(
            start, end, static_cast<int>(std::floor(position)));
        const int indexB = juce::jmin(end, indexA + 1);
        const float fraction = static_cast<float>(position - indexA);
        return juce::jmap(
            fraction,
            (*values)[static_cast<size_t>(indexA)],
            (*values)[static_cast<size_t>(indexB)]);
    };

    const double frequency = juce::MidiMessage::getMidiNoteInHertz(
        wavetableNote);
    const double phaseIncrement = frequency / currentSampleRate;
    const float gain = 0.18f * wavetableVelocity;

    const auto readWavetable = [this](
        const std::shared_ptr<const std::vector<float>>& source)
    {
        const int sourceSize = static_cast<int>(source->size());
        const double tablePosition = wavetablePhase * sourceSize;
        const int indexA = static_cast<int>(tablePosition) % sourceSize;
        const int indexB = (indexA + 1) % sourceSize;
        const float fraction = static_cast<float>(
            tablePosition - std::floor(tablePosition));

        return juce::jmap(
            fraction, (*source)[indexA], (*source)[indexB]);
    };

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const float envelopeValue = wavetableEnvelope.getNextSample();
        const float currentValue = readWavetable(table);
        float oscillatorValue = currentValue;

        if (wavetableCrossfadeSamplesRemaining > 0
            && previousWavetable != nullptr
            && previousWavetable->size() >= 2)
        {
            const float fadeProgress = 1.0f
                - static_cast<float>(wavetableCrossfadeSamplesRemaining)
                    / static_cast<float>(wavetableCrossfadeLengthSamples);
            oscillatorValue = juce::jmap(
                fadeProgress,
                readWavetable(previousWavetable),
                currentValue);
            --wavetableCrossfadeSamplesRemaining;
        }

        oscillatorValue = juce::jlimit(-1.0f, 1.0f, oscillatorValue);
        const auto amplitudeForRoute = [&](
            int target, float amount,
            const std::shared_ptr<const std::vector<float>>& values)
        {
            return target == 1 && values != nullptr && !values->empty()
                ? juce::jmap(amount, 1.0f,
                    readModulation(values, modulationPhase))
                : 1.0f;
        };
        const float amplitudeModulation =
            amplitudeForRoute(
                modulationTarget, modulationAmount, modulationValues)
            * amplitudeForRoute(
                modulationTarget2, modulationAmount2, modulationValues2);
        const float value = oscillatorValue * gain * envelopeValue
            * amplitudeModulation;

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(channel, sample, value);

        wavetablePhase += phaseIncrement;
        wavetablePhase -= std::floor(wavetablePhase);
        modulationPhase += modulationPhaseIncrement;
        modulationPhase -= std::floor(modulationPhase);

        if (auditionEnvelopeActive && !wavetableKeyDown)
        {
            const int remaining = auditionSamplesRemaining.load();

            if (remaining > 1)
                auditionSamplesRemaining.fetch_sub(1);
            else if (remaining == 1)
            {
                auditionSamplesRemaining.store(0);
                auditionEnvelopeActive = false;
                wavetableEnvelope.noteOff();
            }
        }
    }

    const float cutoff = parameterState
        .getRawParameterValue("wavetableFilterCutoff")->load();
    const float resonance = parameterState
        .getRawParameterValue("wavetableFilterResonance")->load();
    const auto cutoffFactorForRoute = [&](
        int target, int desiredTarget, float amount,
        const std::shared_ptr<const std::vector<float>>& values)
    {
        if (target != desiredTarget || values == nullptr || values->empty())
            return 1.0f;
        const float current = readModulation(values, modulationPhase);
        return std::pow(2.0f, (current - 0.5f) * 8.0f * amount);
    };
    const float lpModulatedCutoff = cutoff
        * cutoffFactorForRoute(
            modulationTarget, 2, modulationAmount, modulationValues)
        * cutoffFactorForRoute(
            modulationTarget2, 2, modulationAmount2, modulationValues2);
    const float safeCutoff = juce::jlimit(
        20.0f,
        static_cast<float>(currentSampleRate * 0.45),
        lpModulatedCutoff);

    wavetableLowPassFilter.setCutoffFrequency(safeCutoff);
    wavetableLowPassFilter.setResonance(resonance);

    auto audioBlock = juce::dsp::AudioBlock<float>(buffer);
    auto filterContext = juce::dsp::ProcessContextReplacing<float>(audioBlock);
    wavetableLowPassFilter.process(filterContext);

    const float highPassCutoff = parameterState
        .getRawParameterValue("wavetableHighPassCutoff")->load();
    const float highPassResonance = parameterState
        .getRawParameterValue("wavetableHighPassResonance")->load();
    const float hpModulatedCutoff = highPassCutoff
        * cutoffFactorForRoute(
            modulationTarget, 3, modulationAmount, modulationValues)
        * cutoffFactorForRoute(
            modulationTarget2, 3, modulationAmount2, modulationValues2);
    const float safeHighPassCutoff = juce::jlimit(
        20.0f,
        static_cast<float>(currentSampleRate * 0.45),
        hpModulatedCutoff);

    wavetableHighPassFilter.setCutoffFrequency(safeHighPassCutoff);
    wavetableHighPassFilter.setResonance(highPassResonance);
    wavetableHighPassFilter.process(filterContext);
}

juce::AudioProcessorValueTreeState&
DataSonifierAudioProcessor::getParameterState() noexcept
{
    return parameterState;
}

int DataSonifierAudioProcessor::getCurrentPlaybackIndex() const noexcept
{
    return currentPlaybackIndex.load();
}

juce::AudioProcessorValueTreeState::ParameterLayout
DataSonifierAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // Register parameters in the same order users encounter them in the UI.
    // Ableton uses registration order for its generic parameter panel.
    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "engineMode", 1 },
        "Engine",
        juce::StringArray{ "MIDI", "Wavetable" },
        0));

    const auto envelopeTimeAttributes =
        juce::AudioParameterFloatAttributes{}
            .withLabel("s")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(value, value < 1.0f ? 3 : 2);
            });

    auto envelopeTimeRange = juce::NormalisableRange<float>{
        0.001f, 5.0f, 0.001f };
    envelopeTimeRange.setSkewForCentre(0.25f);

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "wavetableAttack", 1 },
        "Wavetable Attack",
        envelopeTimeRange,
        0.01f,
        envelopeTimeAttributes));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "wavetableDecay", 1 },
        "Wavetable Decay",
        envelopeTimeRange,
        0.15f,
        envelopeTimeAttributes));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "wavetableSustain", 1 },
        "Wavetable Sustain",
        juce::NormalisableRange<float>{ 0.0f, 1.0f, 0.01f },
        0.8f,
        juce::AudioParameterFloatAttributes{}
            .withLabel("%")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(std::round(value * 100.0f));
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "wavetableRelease", 1 },
        "Wavetable Release",
        envelopeTimeRange,
        0.25f,
        envelopeTimeAttributes));

    auto cutoffRange = juce::NormalisableRange<float>{
        20.0f, 20000.0f, 1.0f };
    cutoffRange.setSkewForCentre(1000.0f);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "wavetableFilterCutoff", 1 },
        "Wavetable LP Cutoff",
        cutoffRange,
        20000.0f,
        juce::AudioParameterFloatAttributes{}
            .withLabel("Hz")
            .withStringFromValueFunction([](float value, int)
            {
                if (value >= 1000.0f)
                    return juce::String(value / 1000.0f, 2) + " kHz";

                return juce::String(std::round(value)) + " Hz";
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "wavetableFilterResonance", 1 },
        "Wavetable LP Resonance",
        juce::NormalisableRange<float>{ 0.1f, 10.0f, 0.01f },
        0.707f,
        juce::AudioParameterFloatAttributes{}
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(value, 2);
            })));

    auto highPassRange = juce::NormalisableRange<float>{
        20.0f, 20000.0f, 1.0f };
    highPassRange.setSkewForCentre(500.0f);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "wavetableHighPassCutoff", 1 },
        "Wavetable HP Cutoff",
        highPassRange,
        20.0f,
        juce::AudioParameterFloatAttributes{}
            .withLabel("Hz")
            .withStringFromValueFunction([](float value, int)
            {
                if (value >= 1000.0f)
                    return juce::String(value / 1000.0f, 2) + " kHz";

                return juce::String(std::round(value)) + " Hz";
            })));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "wavetableHighPassResonance", 1 },
        "Wavetable HP Resonance",
        juce::NormalisableRange<float>{ 0.1f, 10.0f, 0.01f },
        0.707f,
        juce::AudioParameterFloatAttributes{}
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(value, 2);
            })));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "wavetableNormalize", 1 },
        "Wavetable Normalize",
        true));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "wavetableLoopSmoothing", 1 },
        "Wavetable Loop Smoothing",
        juce::NormalisableRange<float>{ 0.0f, 1.0f, 0.01f },
        1.0f,
        juce::AudioParameterFloatAttributes{}
            .withLabel("%")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(std::round(value * 100.0f));
            })));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "modulationTarget", 1 },
        "Data Modulation Target",
        juce::StringArray{
            "Off", "Amplitude", "LP Cutoff", "HP Cutoff" },
        0));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "modulationAmount", 1 },
        "Data Modulation Amount",
        juce::NormalisableRange<float>{ 0.0f, 1.0f, 0.01f },
        0.5f,
        juce::AudioParameterFloatAttributes{}
            .withLabel("%")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(std::round(value * 100.0f));
            })));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "modulationTarget2", 1 },
        "Data Modulation 2 Target",
        juce::StringArray{
            "Off", "Amplitude", "LP Cutoff", "HP Cutoff" },
        0));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "modulationAmount2", 1 },
        "Data Modulation 2 Amount",
        juce::NormalisableRange<float>{ 0.0f, 1.0f, 0.01f },
        0.5f,
        juce::AudioParameterFloatAttributes{}
            .withLabel("%")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(std::round(value * 100.0f));
            })));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "modulationTimingMode", 1 },
        "Data Modulation Timing",
        juce::StringArray{ "Free", "Sync" },
        0));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "modulationRate", 1 },
        "Data Modulation Rate",
        juce::NormalisableRange<float>{ 0.25f, 16.0f, 0.25f },
        2.0f,
        juce::AudioParameterFloatAttributes{}
            .withLabel("value/s")
            .withStringFromValueFunction([](float value, int)
            {
                return juce::String(value, 2);
            })));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "modulationSubdivision", 1 },
        "Data Modulation Division",
        juce::StringArray{
            "1/1", "1/2", "1/4", "1/8", "1/16",
            "1/4T", "1/8T", "1/16T" },
        2));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "cycleMethod", 1 },
        "Cycle Detection Method",
        juce::StringArray{
            "Mean Crossing", "HP Filter", "Baxter-King",
            "Christiano-Fitzgerald", "Hamilton Regression",
            "Manual Selection" },
        0));

    auto hpLambdaRange = juce::NormalisableRange<float>{
        1.0f, 129600.0f, 1.0f };
    hpLambdaRange.setSkewForCentre(1600.0f);
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "hpLambda", 1 },
        "HP Lambda",
        hpLambdaRange,
        1600.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "bkLowPeriod", 1 },
        "BK Shortest Period",
        juce::NormalisableRange<float>{ 2.0f, 32.0f, 1.0f },
        6.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "bkHighPeriod", 1 },
        "BK Longest Period",
        juce::NormalisableRange<float>{ 3.0f, 128.0f, 1.0f },
        32.0f));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "bkTruncation", 1 },
        "BK Lead Lag Length",
        1, 48, 12));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "cfLowPeriod", 1 },
        "CF Shortest Period",
        juce::NormalisableRange<float>{ 2.0f, 32.0f, 1.0f },
        6.0f));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "cfHighPeriod", 1 },
        "CF Longest Period",
        juce::NormalisableRange<float>{ 3.0f, 128.0f, 1.0f },
        32.0f));

    layout.add(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{ "cfRemoveDrift", 1 },
        "CF Remove Drift",
        true));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "hamiltonHorizon", 1 },
        "Hamilton Forecast Horizon",
        1, 32, 8));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "hamiltonLags", 1 },
        "Hamilton Lag Count",
        1, 16, 4));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "cycleMinLength", 1 },
        "Cycle Minimum Length",
        2, 128, 8));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "cycleSensitivity", 1 },
        "Cycle Sensitivity",
        juce::NormalisableRange<float>{ 0.0f, 100.0f, 1.0f },
        10.0f,
        juce::AudioParameterFloatAttributes{}.withLabel("%")));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "outputMode", 1 },
        "Output Mode",
        juce::StringArray{ "Note", "Controller" },
        0));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "timingMode", 1 },
        "Timing Mode",
        juce::StringArray{ "Free", "Sync" },
        0));

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "subdivision", 1 },
        "Subdivision",
        juce::StringArray{
            "1/1", "1/2", "1/4", "1/8", "1/16",
            "1/4T", "1/8T", "1/16T"
        },
        2));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "speed", 1 },
        "Speed",
        juce::NormalisableRange<float>{ 0.25f, 8.0f, 0.25f },
        1.0f,
        juce::AudioParameterFloatAttributes{}
        .withLabel("note/s")
        .withStringFromValueFunction([](float value, int)
            {
                return juce::String(value, 2);
            })));

    const auto midiNoteToText = [](int note, int)
    {
        return juce::MidiMessage::getMidiNoteName(
            note,
            true,
            true,
            4);
    };

    const auto midiNoteFromText = [](const juce::String& input)
    {
        auto text = input.trim().removeCharacters(" ").toUpperCase();

        if (text.containsOnly("0123456789"))
            return juce::jlimit(0, 127, text.getIntValue());

        if (text.length() < 2 || !juce::String("ABCDEFG").containsChar(text[0]))
            return 60;

        int semitone = 0;

        switch (text[0])
        {
            case 'C': semitone = 0;  break;
            case 'D': semitone = 2;  break;
            case 'E': semitone = 4;  break;
            case 'F': semitone = 5;  break;
            case 'G': semitone = 7;  break;
            case 'A': semitone = 9;  break;
            case 'B': semitone = 11; break;
            default: return 60;
        }

        int octaveStart = 1;

        if (text.length() > 1 && text[1] == '#')
        {
            ++semitone;
            ++octaveStart;
        }
        else if (text.length() > 1 && text[1] == 'B')
        {
            --semitone;
            ++octaveStart;
        }

        const auto octaveText = text.substring(octaveStart);
        const bool validOctave = octaveText.isNotEmpty()
            && (octaveText.containsOnly("0123456789")
                || (octaveText.startsWithChar('-')
                    && octaveText.substring(1).containsOnly("0123456789")));

        if (!validOctave)
            return 60;

        const int midiNote = (octaveText.getIntValue() + 1) * 12 + semitone;
        return juce::jlimit(0, 127, midiNote);
    };

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ "pitchMappingMode", 1 },
        "Pitch Mode",
        juce::StringArray{ "Range", "Midpoint + Spread" },
        0));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "lowestNote", 1 },
        "Lowest Note",
        0,
        127,
        48,
        juce::AudioParameterIntAttributes{}
            .withStringFromValueFunction(midiNoteToText)
            .withValueFromStringFunction(midiNoteFromText)));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "highestNote", 1 },
        "Highest Note",
        0,
        127,
        84,
        juce::AudioParameterIntAttributes{}
            .withStringFromValueFunction(midiNoteToText)
            .withValueFromStringFunction(midiNoteFromText)));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "centreNote", 1 },
        "Midpoint Note",
        0,
        127,
        60,
        juce::AudioParameterIntAttributes{}
            .withStringFromValueFunction(midiNoteToText)
            .withValueFromStringFunction(midiNoteFromText)));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "pitchSpread", 1 },
        "Pitch Spread",
        0,
        64,
        12,
        juce::AudioParameterIntAttributes{}
            .withLabel("st")
            .withStringFromValueFunction([](int value, int)
            {
                return juce::String(value);
            })));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "controllerNumber", 1 },
        "CC Number",
        0,
        127,
        74,
        juce::AudioParameterIntAttributes{}
            .withStringFromValueFunction([](int value, int)
            {
                return "CC " + juce::String(value);
            })));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "controllerMinimum", 1 },
        "CC Minimum",
        0,
        127,
        0));

    layout.add(std::make_unique<juce::AudioParameterInt>(
        juce::ParameterID{ "controllerMaximum", 1 },
        "CC Maximum",
        0,
        127,
        127));

    return layout;
}
