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

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
            transportIsPlaying = position->getIsPlaying();
    }

    // Ableton has just been stopped.
    if (!transportIsPlaying)
    {
        // Prevent any incoming MIDI events from passing through.
        midiMessages.clear();

        // Send a note-off once if a note was active when Stop was pressed.
        if (wasTransportPlaying && currentlyPlayingNote >= 0)
        {
            midiMessages.addEvent(
                juce::MidiMessage::noteOff(
                    1,
                    currentlyPlayingNote),
                0);
        }

        // Reset playback so the next Play starts from the first CSV value.
        currentSeriesIndex = 0;
        samplesUntilNextNote = 0;
        samplesUntilNoteOff = -1;
        currentlyPlayingNote = -1;
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
            samplesUntilNoteOff = -1;
        }

        // Start a new C4 note.
        // Move to the next value once per second.
        if (samplesUntilNextNote == 0)
        {
            constexpr int midiChannel = 1;
            constexpr float velocity = 0.8f;

            if (notes != nullptr && !notes->empty())
            {
                // Keep the index valid if a new CSV series has a different size.
                currentSeriesIndex %= notes->size();

                const int noteNumber =
                    (*notes)[currentSeriesIndex];

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

                    samplesUntilNoteOff =
                        static_cast<int>(currentSampleRate * 0.2);
                }

                ++currentSeriesIndex;

                if (currentSeriesIndex >= notes->size())
                    currentSeriesIndex = 0;
            }

            // Move to the next data value based on playback speed.
            const double notesPerSecond =
                playbackSpeed.load();

            samplesUntilNextNote =
                static_cast<int>(
                    currentSampleRate / notesPerSecond);
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
    // You should use this method to store your parameters in the memory block.
    // You could do that either as raw data, or use the XML or ValueTree classes
    // as intermediaries to make it easy to save and load complex data.
}

void DataSonifierAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // You should use this method to restore your parameters from this memory block,
    // whose contents will have been created by the getStateInformation() call.
}

//==============================================================================
// This creates new instances of the plugin..
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DataSonifierAudioProcessor();
}

void DataSonifierAudioProcessor::setProcessedSeries(
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

void DataSonifierAudioProcessor::setPlaybackSpeed(
    double notesPerSecond)
{
    playbackSpeed.store(
        juce::jlimit(0.25, 8.0, notesPerSecond));
}