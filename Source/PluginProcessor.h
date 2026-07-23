/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
*/
class DataSonifierAudioProcessor  : public juce::AudioProcessor
{
public:
    struct DataStateSnapshot
    {
        std::vector<juce::StringArray> displayData;
        juce::StringArray columnHeaders;
        std::vector<double> processedSeries;
        juce::String loadedFilePath;
        int selectedColumn = -1;
        int selectedTransform = 1;
    };

    //==============================================================================
    DataSonifierAudioProcessor();
    ~DataSonifierAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    bool loadCsvFile(const juce::File& file);
    void setSelectedColumn(int columnId);
    void setSelectedTransform(int transformId);
    DataStateSnapshot getDataStateSnapshot() const;

    juce::AudioProcessorValueTreeState& getParameterState() noexcept;
    int getCurrentPlaybackIndex() const noexcept;

private:
    //==============================================================================
    void rebuildProcessedSeries();
    void rebuildMidiNoteSeries(const std::vector<double>& series);
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameterState;

    mutable juce::CriticalSection dataStateLock;
    std::vector<juce::StringArray> originalCsvData;
    std::vector<juce::StringArray> displayCsvData;
    juce::StringArray columnHeaders;
    std::vector<double> processedSeries;
    juce::String loadedFilePath;
    int selectedColumn = -1;
    int selectedTransform = 1;

    std::shared_ptr<const std::vector<int>> midiNoteSeries;

    size_t currentSeriesIndex = 0;
    std::atomic<int> currentPlaybackIndex{ -1 };

    double currentSampleRate = 44100.0;

    int samplesUntilNextNote = 0;
    int samplesUntilNoteOff = -1;

    int currentlyPlayingNote = -1;

    bool wasTransportPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DataSonifierAudioProcessor)
};
