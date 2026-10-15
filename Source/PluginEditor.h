#pragma once

#include "PluginProcessor.h"

//==============================================================================
// 16 vivid RGB pad colors — the MPC identity.
static inline juce::Colour padColour (int i)
{
    static const juce::uint32 cols[] = {
        0xffFF3B30, 0xffFF9500, 0xffFFCC00, 0xff34C759,
        0xff00C7BE, 0xff32ADE6, 0xff0A84FF, 0xff5E5CF6,
        0xffBF5AF2, 0xffFF375F, 0xffFF6B35, 0xffFFD60A,
        0xff30D158, 0xff64D2FF, 0xff7D7AFF, 0xffFF6482
    };
    return juce::Colour (cols[i % 16]);
}

//==============================================================================
class QChopEditor;

// Forward drags from every child component to the editor.
class DropForwarder : public juce::FileDragAndDropTarget
{
public:
    explicit DropForwarder (QChopEditor* e) : editor (e) {}
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
private:
    QChopEditor* editor;
};

//==============================================================================
class WaveformStrip : public juce::Component,
                      public juce::FileDragAndDropTarget
{
public:
    explicit WaveformStrip (QChopProcessor& p, QChopEditor* e);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void refreshPeaksIfNeeded();

private:
    QChopProcessor& processor;
    QChopEditor* editor;
    int dragMarker = -1;
    int lastSampleVersion = -1;
    std::vector<float> peaks;
    double sampleToX (double s) const;
    double xToSample (double x) const;
    int markerAtX (double x) const;
};

//==============================================================================
class PadGrid : public juce::Component,
                public juce::FileDragAndDropTarget
{
public:
    explicit PadGrid (QChopProcessor& p, QChopEditor* e);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    int padAt (juce::Point<int> pt) const;

private:
    QChopProcessor& processor;
    QChopEditor* editor;
    juce::Rectangle<int> padBounds (int i) const;
};

//==============================================================================
class QChopEditor : public juce::AudioProcessorEditor,
                    public juce::Timer,
                    public juce::FileDragAndDropTarget,
                    private juce::ComboBox::Listener
{
public:
    explicit QChopEditor (QChopProcessor&);
    ~QChopEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void loadFile (const juce::File& file);

    QChopProcessor& processor;

private:
    void comboBoxChanged (juce::ComboBox*) override;

    // Presets
    void rebuildPresetMenu();
    void applyPresetById (int menuId);
    void selectPresetByName (const juce::String& name);
    void saveUserPreset();
    void toggleFavorite();
    bool isFavorite (const juce::String& name) const;
    juce::File getPresetDir() const;
    juce::File getFavoritesFile() const;
    juce::StringArray factoryNames;
    juce::StringArray userNames;
    juce::StringArray favoriteNames;
    juce::String currentPresetName;
    void loadFavorites();

    juce::Image logoImage;
    juce::Label titleLabel;

    juce::ComboBox presetBox;
    juce::TextButton heartButton { "\u2665" };
    juce::TextButton prevButton { "\u25C0" };
    juce::TextButton nextButton { "\u25B6" };
    juce::TextButton saveButton { "SAVE" };

    WaveformStrip waveform;
    PadGrid pads;

    // Minimal chrome: 5 controls for the selected slice.
    juce::Slider pitchKnob, levelKnob, sensKnob, masterKnob;
    juce::TextButton reverseButton { "REV" };
    juce::Label pitchLabel { {}, "PITCH" }, levelLabel { {}, "LEVEL" },
                sensLabel { {}, "SENS" }, masterLabel { {}, "MASTER" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterAttachment;

    void syncSliceControls();
    int lastSyncedSlice = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (QChopEditor)
};
