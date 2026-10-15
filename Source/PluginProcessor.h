#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>
#include <memory>

//==============================================================================
struct SliceParams
{
    std::atomic<double> start { 0.0 };
    std::atomic<double> end   { 0.0 };
    std::atomic<float>  pitchSt { 0.0f };  // -12..+12
    std::atomic<bool>   reverse { false };
    std::atomic<float>  level   { 1.0f };  // 0..1.5
};

//==============================================================================
class QChopProcessor : public juce::AudioProcessor,
                       private juce::ValueTree::Listener
{
public:
    static constexpr int numSlices = 16;
    static constexpr int numVoices  = 8;
    static constexpr int triggerFifoSize = 128;

    QChopProcessor();
    ~QChopProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- sample / slicing ----
    bool loadAudioFile (const juce::File& file);
    bool hasSampleLoaded() const;
    juce::String getLoadedFileName() const { return loadedFileName; }
    int getSampleVersion() const { return sampleVersion.load(); }
    std::shared_ptr<juce::AudioBuffer<float>> getSampleBuffer();
    double getSampleRate() const { return lastSampleRate; }

    int getNumActiveSlices() const { return numActiveSlices.load(); }
    void setNumActiveSlices (int n);               // re-slice evenly
    void setSliceBoundary (int sliceIndex, double newStartSamples); // drag marker
    void addSliceAt (double samplePos);            // double-click: split
    void removeSlice (int sliceIndex);

    void setSlicePitch   (int i, float st);
    void setSliceReverse (int i, bool r);
    void setSliceLevel   (int i, float l);
    void setSliceSelected(int i) { selectedSlice.store (i); }
    int  getSelectedSlice() const { return selectedSlice.load(); }

    const SliceParams& getSliceParams (int i) const { return sliceParams[(size_t) i]; }

    void triggerSlice (int i);                     // UI thread
    float getPadFlash (int i) const { return padFlash[(size_t) i].load(); }

    void resliceWithSensitivity (float sens);      // 0..1 onset sensitivity

    // ---- presets ----
    juce::StringArray getFactoryPresetNames() const;
    void applyFactoryPreset (const juce::String& name);
    void applyPresetState (const juce::ValueTree& state);

    juce::AudioProcessorValueTreeState apvts;
    static constexpr const char* kParamMaster = "master";

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void refreshSliceParamsFromTree();
    void ensureSliceTree();

    void detectOnsets (float sensitivity);
    void setSliceBoundaries (const std::vector<double>& bounds);

    struct Voice
    {
        bool active = false;
        int slice = 0;
        double pos = 0.0;
    };

    std::array<Voice, numVoices> voices;
    juce::AbstractFifo triggerFifo { triggerFifoSize };
    std::array<int, triggerFifoSize> triggerData {};

    std::array<SliceParams, numSlices> sliceParams;
    std::atomic<int> numActiveSlices { 0 };
    std::atomic<int> selectedSlice { 0 };
    std::array<std::atomic<float>, numSlices> padFlash {};

    juce::SpinLock sampleLock;
    std::shared_ptr<juce::AudioBuffer<float>> sampleBuffer;
    std::atomic<int> sampleVersion { 0 };
    juce::String loadedFileName;
    juce::AudioFormatManager formatManager;

    double lastSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QChopProcessor)
};
