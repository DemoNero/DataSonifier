/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin processor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>
#include <map>

//==============================================================================
/**
*/
class DataSonifierAudioProcessor  : public juce::AudioProcessor,
                                    private juce::AudioProcessorValueTreeState::Listener,
                                    private juce::AsyncUpdater
{
public:
    struct AdditivePartial
    {
        int sourceBin = 1;
        float frequencyRatio = 1.0f;
        float amplitude = 0.0f;
        float phase = 0.0f;
    };

    struct DataStateSnapshot
    {
        std::vector<juce::StringArray> displayData;
        juce::StringArray columnHeaders;
        std::vector<double> processedSeries;
        juce::String loadedFilePath;
        int selectedColumn = -1;
        int selectedTransform = 1;
        int dateColumn = -1;
        int dateRangeStartRow = -1;
        int dateRangeEndRow = -1;
        juce::StringArray dateValues;
        int modulationColumn = -1;
        int modulationColumn2 = -1;
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
    void setDateColumn(int columnId);
    void setDateRange(int startRow, int endRow);
    void setModulationColumn(int columnId);
    void setModulationColumn2(int columnId);
    void setWavetableCycleRange(int startIndex, int endIndex);
    void setWavetableSource(const std::vector<double>& source);
    void setWavetableFrames(
        const std::vector<std::vector<double>>& frames);
    void auditionWavetable();
    DataStateSnapshot getDataStateSnapshot() const;
    std::vector<double> getWavetableSnapshot() const;
    std::vector<AdditivePartial> getAdditivePartialsSnapshot() const;
    float getAdditivePartialLevel(int index) const;
    float getAdditivePartialBaseRatio(int index) const;
    float getAdditivePartialRatio(int index) const;
    void setAdditivePartialLevel(int index, float level);
    void setAdditivePartialRatio(int index, float ratio);
    void resetAdditivePartial(int index);
    void resetAllAdditivePartials();
    void captureAdditiveMorphSource(int slot, int cycleIndex);
    void clearAdditiveMorphSources();
    int getAdditiveMorphCycle(int slot) const;

    juce::AudioProcessorValueTreeState& getParameterState() noexcept;
    int getCurrentPlaybackIndex() const noexcept;

private:
    //==============================================================================
    void rebuildProcessedSeries();
    void rebuildModulationSeries();
    void rebuildNormalisedMidiSeries(const std::vector<double>& series);
    void rebuildWavetable(const std::vector<double>& series);
    void rebuildMorphedWavetable();
    void processWavetableSynth(
        juce::AudioBuffer<float>& buffer,
        juce::MidiBuffer& midiMessages);
    void processAdditiveSynth(
        juce::AudioBuffer<float>& buffer,
        juce::MidiBuffer& midiMessages);
    void rebuildAdditivePartials(const std::vector<double>& series);
    void applyAdditiveOutputProcessing(juce::AudioBuffer<float>& buffer);
    void publishEditedAdditivePartials();
    void applyGlobalAdditiveShaping(std::vector<AdditivePartial>& partials) const;
    void advanceAdditiveAutoMorph(int numSamples);
    void saveAdditiveEditsToState();
    void loadAdditiveEditsFromState();
    std::vector<AdditivePartial> buildEditedAdditiveBaseLocked() const;
    void saveAdditiveMorphSourcesToState();
    void loadAdditiveMorphSourcesFromState();
    void parameterChanged(
        const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;
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
    int dateColumn = -1;
    int dateRangeStartRow = -1;
    int dateRangeEndRow = -1;
    int modulationColumn = -1;
    int modulationColumn2 = -1;

    std::shared_ptr<const std::vector<float>> normalisedMidiSeries;
    std::shared_ptr<const std::vector<float>> normalisedModulationSeries;
    std::shared_ptr<const std::vector<float>> normalisedModulationSeries2;
    std::atomic<int> modulationCycleStart{ 0 };
    std::atomic<int> modulationCycleEnd{ -1 };
    std::shared_ptr<const std::vector<float>> wavetable;
    std::shared_ptr<const std::vector<AdditivePartial>> additivePartials;
    std::shared_ptr<const std::vector<AdditivePartial>> activeAdditivePartials;
    std::shared_ptr<const std::vector<AdditivePartial>> previousAdditivePartials;
    int additiveCrossfadeSamplesRemaining = 0;
    int additiveCrossfadeLengthSamples = 1;
    struct AdditiveEdit
    {
        float level = 1.0f;
        float ratio = 1.0f;
    };
    mutable juce::CriticalSection additiveEditLock;
    std::vector<AdditivePartial> additiveBasePartials;
    std::map<int, AdditiveEdit> additiveEdits;
    std::vector<AdditivePartial> additiveMorphSourceA;
    std::vector<AdditivePartial> additiveMorphSourceB;
    int additiveMorphCycleA = -1;
    int additiveMorphCycleB = -1;
    std::atomic<bool> additiveShapingUpdatePending{ false };
    std::atomic<bool> wavetableRebuildPending{ false };
    std::atomic<float> additiveAutoMorphValue{ 0.0f };
    double additiveAutoMorphPhase = 0.0;
    int additiveAutoMorphUpdateCountdown = 0;
    struct AdditiveVoice
    {
        int midiNote = -1;
        float velocity = 0.0f;
        double phase = 0.0;
        uint64_t age = 0;
        bool active = false;
        bool keyDown = false;
        bool audition = false;
        juce::ADSR envelope;
    };
    static constexpr int additiveVoiceCount = 8;
    std::array<AdditiveVoice, additiveVoiceCount> additiveVoices;
    uint64_t additiveVoiceAgeCounter = 0;
    std::shared_ptr<const std::vector<float>> activeWavetable;
    std::shared_ptr<const std::vector<float>> previousWavetable;
    int wavetableCrossfadeSamplesRemaining = 0;
    int wavetableCrossfadeLengthSamples = 1;
    mutable juce::CriticalSection wavetableSourceLock;
    juce::CriticalSection wavetableBuildLock;
    std::vector<double> currentWavetableSource;
    std::vector<std::vector<double>> wavetableFrameSources;

    double wavetablePhase = 0.0;
    double modulationPhase = 0.0;
    int wavetableNote = 48;
    float wavetableVelocity = 0.0f;
    bool wavetableKeyDown = false;
    bool auditionEnvelopeActive = false;
    juce::ADSR wavetableEnvelope;
    juce::dsp::StateVariableTPTFilter<float> wavetableLowPassFilter;
    juce::dsp::StateVariableTPTFilter<float> wavetableHighPassFilter;
    juce::dsp::Limiter<float> outputLimiter;
    juce::SmoothedValue<float> outputGainLinear;
    std::atomic<int> auditionSamplesRemaining{ 0 };

    size_t currentSeriesIndex = 0;
    std::atomic<int> currentPlaybackIndex{ -1 };

    double currentSampleRate = 44100.0;

    int samplesUntilNextNote = 0;
    int samplesUntilNoteOff = -1;

    int currentlyPlayingNote = -1;

    bool wasTransportPlaying = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DataSonifierAudioProcessor)
};
