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
}

DataSonifierAudioProcessor::~DataSonifierAudioProcessor()
{
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

    auto notes = std::atomic_load(&midiNoteSeries);

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

            if (notes != nullptr && !notes->empty())
            {
                // Keep the index valid if a new CSV series has a different size.
                currentSeriesIndex %= notes->size();

                const int noteNumber =
                    (*notes)[currentSeriesIndex];

                currentPlaybackIndex.store(-1);

                // -1 represents an invalid/missing value, so play a rest.
                if (noteNumber >= 0)
                {
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

                ++currentSeriesIndex;

                if (currentSeriesIndex >= notes->size())
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
    lines.addLines(file.loadFileAsString());

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

        rebuildProcessedSeries();
        seriesForMidi = processedSeries;
    }

    rebuildMidiNoteSeries(seriesForMidi);
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
        seriesForMidi = processedSeries;
    }

    rebuildMidiNoteSeries(seriesForMidi);
}

void DataSonifierAudioProcessor::setSelectedTransform(int transformId)
{
    std::vector<double> seriesForMidi;

    {
        const juce::ScopedLock lock(dataStateLock);
        selectedTransform = juce::jlimit(1, 3, transformId);
        rebuildProcessedSeries();
        seriesForMidi = processedSeries;
    }

    rebuildMidiNoteSeries(seriesForMidi);
}

DataSonifierAudioProcessor::DataStateSnapshot
DataSonifierAudioProcessor::getDataStateSnapshot() const
{
    const juce::ScopedLock lock(dataStateLock);

    return {
        displayCsvData,
        columnHeaders,
        processedSeries,
        loadedFilePath,
        selectedColumn,
        selectedTransform
    };
}

void DataSonifierAudioProcessor::rebuildProcessedSeries()
{
    displayCsvData = originalCsvData;
    processedSeries.clear();

    if (selectedColumn <= 0 || originalCsvData.empty())
        return;

    const int col = selectedColumn - 1;
    const double missingValue = std::numeric_limits<double>::quiet_NaN();

    std::vector<double> rawValues;
    rawValues.reserve(originalCsvData.size());

    for (const auto& row : originalCsvData)
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

void DataSonifierAudioProcessor::rebuildMidiNoteSeries(
    const std::vector<double>& series)
{
    constexpr int lowestMidiNote = 48;  // C3
    constexpr int highestMidiNote = 84; // C6

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

    auto mappedNotes = std::make_shared<std::vector<int>>();
    mappedNotes->reserve(series.size());

    const bool hasValidValues =
        std::isfinite(minimum) && std::isfinite(maximum);

    for (double value : series)
    {
        // Missing values become rests.
        if (!hasValidValues || !std::isfinite(value))
        {
            mappedNotes->push_back(-1);
            continue;
        }

        int midiNote = 60;

        if (minimum != maximum)
        {
            const double normalised =
                (value - minimum) / (maximum - minimum);

            midiNote = static_cast<int>(std::round(
                lowestMidiNote
                + normalised
                * (highestMidiNote - lowestMidiNote)));
        }

        midiNote = juce::jlimit(
            lowestMidiNote,
            highestMidiNote,
            midiNote);

        mappedNotes->push_back(midiNote);
    }

    std::atomic_store(
        &midiNoteSeries,
        std::shared_ptr<const std::vector<int>>(mappedNotes));

    DBG("Mapped "
        << mappedNotes->size()
        << " values to MIDI notes");
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

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{ "speed", 1 },
        "Speed",
        juce::NormalisableRange<float>{ 0.25f, 8.0f, 0.25f },
        1.0f,
        juce::AudioParameterFloatAttributes{}
        .withLabel("notes/s")
        .withStringFromValueFunction([](float value, int)
            {
                return juce::String(value, 2);
            })));

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

    return layout;
}
