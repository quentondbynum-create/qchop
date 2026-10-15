#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <BinaryData.h>

namespace
{
    const juce::Colour kBg       = juce::Colour (0xff101014);
    const juce::Colour kPanel    = juce::Colour (0xff18181e);
    const juce::Colour kPanelEdge= juce::Colour (0xff26262e);
    const juce::Colour kText     = juce::Colour (0xfff2f2f5);
    const juce::Colour kTextDim  = juce::Colour (0xff8a8a96);
    const juce::Colour kAccent   = juce::Colour (0xffFF375F);

    bool isAudioFile (const juce::String& path)
    {
        auto ext = juce::File (path).getFileExtension().toLowerCase();
        return ext == ".wav" || ext == ".mp3" || ext == ".aif" || ext == ".aiff"
            || ext == ".flac" || ext == ".ogg";
    }
}

//==============================================================================
bool DropForwarder::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (isAudioFile (f)) return true;
    return false;
}
void DropForwarder::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() > 0 && editor != nullptr)
        editor->loadFile (juce::File (files[0]));
}

//==============================================================================
WaveformStrip::WaveformStrip (QChopProcessor& p, QChopEditor* e)
    : processor (p), editor (e)
{
}

double WaveformStrip::sampleToX (double s) const
{
    auto buf = processor.getSampleBuffer();
    const double n = buf != nullptr ? (double) buf->getNumSamples() : 1.0;
    return (s / juce::jmax (1.0, n)) * getWidth();
}

double WaveformStrip::xToSample (double x) const
{
    auto buf = processor.getSampleBuffer();
    const double n = buf != nullptr ? (double) buf->getNumSamples() : 1.0;
    return juce::jlimit (0.0, n, (x / juce::jmax (1, getWidth())) * n);
}

int WaveformStrip::markerAtX (double x) const
{
    const int active = processor.getNumActiveSlices();
    for (int i = 1; i < active; ++i)  // marker i = start of slice i
    {
        const double mx = sampleToX (processor.getSliceParams (i).start.load());
        if (std::abs (mx - x) < 7.0) return i;
    }
    return -1;
}

void WaveformStrip::refreshPeaksIfNeeded()
{
    const int ver = processor.getSampleVersion();
    if (ver == lastSampleVersion && ! peaks.empty()) return;
    lastSampleVersion = ver;
    peaks.assign (256, 0.0f);
    auto buf = processor.getSampleBuffer();
    if (buf == nullptr || buf->getNumSamples() == 0) return;
    const int n = buf->getNumSamples();
    const float* l = buf->getReadPointer (0);
    const float* r = buf->getNumChannels() > 1 ? buf->getReadPointer (1) : l;
    const int per = juce::jmax (1, n / 256);
    for (int i = 0; i < 256; ++i)
    {
        float peak = 0.0f;
        const int s0 = i * per;
        for (int j = 0; j < per && s0 + j < n; j += 7)
            peak = juce::jmax (peak, std::abs (0.5f * (l[s0 + j] + r[s0 + j])));
        peaks[(size_t) i] = peak;
    }
}

void WaveformStrip::paint (juce::Graphics& g)
{
    refreshPeaksIfNeeded();
    g.fillAll (kPanel);
    g.setColour (kPanelEdge);
    g.drawRect (getLocalBounds(), 1);

    if (! processor.hasSampleLoaded())
    {
        g.setColour (kTextDim);
        g.setFont (juce::Font (16.0f, juce::Font::bold));
        g.drawText ("DROP AUDIO HERE", getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    const int w = getWidth(), h = getHeight();
    // Slice region tint.
    const int active = processor.getNumActiveSlices();
    for (int i = 0; i < active; ++i)
    {
        const auto& sp = processor.getSliceParams (i);
        const int x0 = (int) sampleToX (sp.start.load());
        const int x1 = (int) sampleToX (sp.end.load());
        g.setColour (padColour (i).withAlpha (i == processor.getSelectedSlice() ? 0.22f : 0.08f));
        g.fillRect (x0, 0, juce::jmax (1, x1 - x0), h);
    }

    // Waveform.
    g.setColour (juce::Colour (0xffc9c9d4));
    const float mid = h * 0.5f, amp = h * 0.46f;
    for (int x = 0; x < w; ++x)
    {
        const float pk = peaks[(size_t) (x * 256 / juce::jmax (1, w))];
        g.drawLine ((float) x, mid - pk * amp, (float) x, mid + pk * amp, 1.0f);
    }

    // Markers.
    for (int i = 1; i < active; ++i)
    {
        const double mx = sampleToX (processor.getSliceParams (i).start.load());
        g.setColour (padColour (i));
        g.drawLine ((float) mx, 0.0f, (float) mx, (float) h, 2.0f);
        g.fillEllipse ((float) mx - 5.0f, 2.0f, 10.0f, 10.0f);
        g.setColour (juce::Colours::black);
        g.setFont (9.0f);
        g.drawText (juce::String (i + 1), (int) mx - 5, 2, 10, 10,
                    juce::Justification::centred, false);
    }
}

void WaveformStrip::resized() {}

void WaveformStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        const int m = markerAtX (e.position.x);
        if (m > 0) processor.removeSlice (m);
        repaint();
        return;
    }
    dragMarker = markerAtX (e.position.x);
    if (dragMarker > 0)
        processor.setSliceSelected (dragMarker - 1);
    repaint();
}

void WaveformStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMarker > 0)
    {
        processor.setSliceBoundary (dragMarker, xToSample (e.position.x));
        repaint();
    }
}

void WaveformStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (markerAtX (e.position.x) < 0)
    {
        processor.addSliceAt (xToSample (e.position.x));
        repaint();
    }
}

bool WaveformStrip::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (isAudioFile (f)) return true;
    return false;
}
void WaveformStrip::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() > 0 && editor != nullptr)
        editor->loadFile (juce::File (files[0]));
}

//==============================================================================
PadGrid::PadGrid (QChopProcessor& p, QChopEditor* e) : processor (p), editor (e) {}

juce::Rectangle<int> PadGrid::padBounds (int i) const
{
    const int gap = 10;
    const int cols = 4;
    const int cw = (getWidth() - gap * (cols - 1)) / cols;
    const int ch = (getHeight() - gap * 3) / 4;
    const int r = i / cols, c = i % cols;
    return { c * (cw + gap), r * (ch + gap), cw, ch };
}

int PadGrid::padAt (juce::Point<int> pt) const
{
    const int active = processor.getNumActiveSlices();
    for (int i = 0; i < active; ++i)
        if (padBounds (i).contains (pt)) return i;
    return -1;
}

void PadGrid::paint (juce::Graphics& g)
{
    const int active = processor.getNumActiveSlices();
    const int selected = processor.getSelectedSlice();

    for (int i = 0; i < 16; ++i)
    {
        auto r = padBounds (i).toFloat();
        const bool inUse = i < active;
        auto col = padColour (i);
        const float flash = inUse ? processor.getPadFlash (i) : 0.0f;

        if (! inUse)
        {
            g.setColour (kPanel);
            g.fillRoundedRectangle (r, 10.0f);
            g.setColour (kPanelEdge);
            g.drawRoundedRectangle (r, 10.0f, 1.0f);
            continue;
        }

        // Base pad: darker version of its color; flash drives brightness.
        auto base = col.darker (0.55f).interpolatedWith (col, juce::jlimit (0.0f, 1.0f, flash));
        g.setColour (base);
        g.fillRoundedRectangle (r, 10.0f);

        if (flash > 0.03f)
        {
            g.setColour (col.withAlpha (juce::jlimit (0.0f, 0.55f, flash * 0.55f)));
            g.fillRoundedRectangle (r.reduced (2.0f), 9.0f);
        }

        // Top highlight for tactility.
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.4f), 10.0f);

        g.setColour (kPanelEdge);
        g.drawRoundedRectangle (r, 10.0f, 1.5f);

        if (i == selected)
        {
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawRoundedRectangle (r.reduced (3.0f), 8.0f, 2.0f);
        }

        // Slice number + pitch tag.
        const auto& sp = processor.getSliceParams (i);
        g.setColour (juce::Colours::white.withAlpha (0.92f));
        g.setFont (juce::Font (22.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), r.reduced (8), juce::Justification::topLeft, false);
        const float pst = sp.pitchSt.load();
        if (std::abs (pst) > 0.01f || sp.reverse.load())
        {
            g.setFont (11.0f);
            juce::String tag = (sp.reverse.load() ? juce::String ("R ") : juce::String())
                             + (pst > 0 ? "+" : "") + juce::String (pst, 0);
            g.drawText (tag, r.reduced (8), juce::Justification::bottomRight, false);
        }
    }
}

void PadGrid::resized() {}

void PadGrid::mouseDown (const juce::MouseEvent& e)
{
    const int p = padAt (e.position.toInt());
    if (p >= 0)
    {
        processor.setSliceSelected (p);
        processor.triggerSlice (p);
        repaint();
    }
}

bool PadGrid::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (isAudioFile (f)) return true;
    return false;
}
void PadGrid::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() > 0 && editor != nullptr)
        editor->loadFile (juce::File (files[0]));
}

//==============================================================================
QChopEditor::QChopEditor (QChopProcessor& p)
    : AudioProcessorEditor (p), processor (p),
      waveform (p, this), pads (p, this)
{
    setSize (680, 780);

    logoImage = juce::ImageCache::getFromMemory (BinaryData::headroomlogo_png,
                                                BinaryData::headroomlogo_pngSize);

    titleLabel.setText ("QCHOP", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (30.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, kText);
    addAndMakeVisible (titleLabel);

    presetBox.setTextWhenNothingSelected ("Presets");
    presetBox.addListener (this);
    addAndMakeVisible (presetBox);

    heartButton.setClickingTogglesState (true);
    heartButton.onClick = [this] { toggleFavorite(); };
    heartButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffFF375F));
    addAndMakeVisible (heartButton);

    prevButton.onClick = [this]
    {
        juce::StringArray all = factoryNames;
        all.addArray (userNames);
        if (all.isEmpty()) return;
        int idx = all.indexOf (currentPresetName);
        idx = (idx - 1 + all.size()) % all.size();
        selectPresetByName (all[idx]);
    };
    addAndMakeVisible (prevButton);

    nextButton.onClick = [this]
    {
        juce::StringArray all = factoryNames;
        all.addArray (userNames);
        if (all.isEmpty()) return;
        int idx = all.indexOf (currentPresetName);
        idx = (idx + 1) % all.size();
        selectPresetByName (all[idx]);
    };
    addAndMakeVisible (nextButton);

    saveButton.onClick = [this] { saveUserPreset(); };
    addAndMakeVisible (saveButton);

    addAndMakeVisible (waveform);
    addAndMakeVisible (pads);

    auto setupKnob = [] (juce::Slider& s)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    };
    setupKnob (pitchKnob);  pitchKnob.setRange (-12.0, 12.0, 0.1);
    setupKnob (levelKnob);  levelKnob.setRange (0.0, 1.5, 0.01);
    setupKnob (sensKnob);   sensKnob.setRange (0.0, 100.0, 1.0); sensKnob.setValue (50.0);
    setupKnob (masterKnob); masterKnob.setRange (0.0, 1.5, 0.01);

    pitchKnob.onValueChange = [this]
    {
        const int s = processor.getSelectedSlice();
        if (s >= 0) processor.setSlicePitch (s, (float) pitchKnob.getValue());
    };
    levelKnob.onValueChange = [this]
    {
        const int s = processor.getSelectedSlice();
        if (s >= 0) processor.setSliceLevel (s, (float) levelKnob.getValue());
    };
    sensKnob.onDragEnd = [this]
    {
        processor.resliceWithSensitivity ((float) sensKnob.getValue() / 100.0f);
        waveform.repaint();
    };
    reverseButton.setClickingTogglesState (true);
    reverseButton.onClick = [this]
    {
        const int s = processor.getSelectedSlice();
        if (s >= 0) processor.setSliceReverse (s, reverseButton.getToggleState());
    };

    auto setupLabel = [] (juce::Label& l)
    {
        l.setFont (juce::Font (11.0f, juce::Font::bold));
        l.setColour (juce::Label::textColourId, kTextDim);
        l.setJustificationType (juce::Justification::centred);
    };
    setupLabel (pitchLabel); setupLabel (levelLabel);
    setupLabel (sensLabel);  setupLabel (masterLabel);

    addAndMakeVisible (pitchKnob);  addAndMakeVisible (pitchLabel);
    addAndMakeVisible (levelKnob);  addAndMakeVisible (levelLabel);
    addAndMakeVisible (sensKnob);   addAndMakeVisible (sensLabel);
    addAndMakeVisible (masterKnob); addAndMakeVisible (masterLabel);
    addAndMakeVisible (reverseButton);

    masterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processor.apvts, QChopProcessor::kParamMaster, masterKnob);

    factoryNames = processor.getFactoryPresetNames();
    loadFavorites();
    rebuildPresetMenu();
    syncSliceControls();

    startTimerHz (30);
}

QChopEditor::~QChopEditor()
{
    stopTimer();
}

void QChopEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    if (logoImage.isValid())
        g.drawImageWithin (logoImage, 12, 14, 34, 34, juce::RectanglePlacement::centred);
}

void QChopEditor::resized()
{
    auto area = getLocalBounds().reduced (14);

    auto top = area.removeFromTop (52);
    // logo drawn in paint at (12,14); title starts after it
    titleLabel.setBounds (top.removeFromLeft (170).withTrimmedLeft (44));
    top.removeFromLeft (8);
    saveButton.setBounds (top.removeFromRight (64));
    top.removeFromRight (6);
    nextButton.setBounds (top.removeFromRight (34));
    prevButton.setBounds (top.removeFromRight (34));
    top.removeFromRight (6);
    heartButton.setBounds (top.removeFromRight (40));
    top.removeFromRight (6);
    presetBox.setBounds (top);

    area.removeFromTop (10);
    waveform.setBounds (area.removeFromTop (128));
    area.removeFromTop (12);

    // Bottom chrome strip (5 controls).
    auto bottom = area.removeFromBottom (96);
    const int cw = bottom.getWidth() / 5;
    for (int i = 0; i < 5; ++i)
    {
        auto col = juce::Rectangle<int> (bottom.getX() + i * cw, bottom.getY(), cw, bottom.getHeight());
        if (i == 0)      { pitchKnob.setBounds (col.withTrimmedBottom (20).reduced (10)); pitchLabel.setBounds (col.withTrimmedTop (col.getHeight() - 20)); }
        else if (i == 1) { reverseButton.setBounds (col.withTrimmedBottom (20).reduced (14)); }
        else if (i == 2) { levelKnob.setBounds (col.withTrimmedBottom (20).reduced (10)); levelLabel.setBounds (col.withTrimmedTop (col.getHeight() - 20)); }
        else if (i == 3) { sensKnob.setBounds (col.withTrimmedBottom (20).reduced (10)); sensLabel.setBounds (col.withTrimmedTop (col.getHeight() - 20)); }
        else             { masterKnob.setBounds (col.withTrimmedBottom (20).reduced (10)); masterLabel.setBounds (col.withTrimmedTop (col.getHeight() - 20)); }
    }

    area.removeFromBottom (10);
    pads.setBounds (area); // the hero: everything left in the middle
}

void QChopEditor::timerCallback()
{
    pads.repaint();
    if (processor.getSelectedSlice() != lastSyncedSlice)
        syncSliceControls();
}

void QChopEditor::syncSliceControls()
{
    const int s = processor.getSelectedSlice();
    lastSyncedSlice = s;
    if (s < 0 || s >= processor.getNumActiveSlices()) return;
    const auto& sp = processor.getSliceParams (s);
    pitchKnob.setValue (sp.pitchSt.load(), juce::dontSendNotification);
    levelKnob.setValue (sp.level.load(), juce::dontSendNotification);
    reverseButton.setToggleState (sp.reverse.load(), juce::dontSendNotification);
    waveform.repaint();
    pads.repaint();
}

bool QChopEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (isAudioFile (f)) return true;
    return false;
}

void QChopEditor::filesDropped (const juce::StringArray& files, int, int)
{
    if (files.size() > 0) loadFile (juce::File (files[0]));
}

void QChopEditor::loadFile (const juce::File& file)
{
    if (processor.loadAudioFile (file))
    {
        waveform.repaint();
        pads.repaint();
        syncSliceControls();
    }
}

//==============================================================================
// Presets
juce::File QChopEditor::getPresetDir() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Headroom Audio").getChildFile ("QChop").getChildFile ("Presets");
}
juce::File QChopEditor::getFavoritesFile() const
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
        .getChildFile ("Headroom Audio").getChildFile ("QChop").getChildFile ("favorites.txt");
}

void QChopEditor::loadFavorites()
{
    favoriteNames.clear();
    auto f = getFavoritesFile();
    if (f.existsAsFile())
        favoriteNames = juce::StringArray::fromLines (f.loadFileAsString());
    favoriteNames.removeEmptyStrings();
}

bool QChopEditor::isFavorite (const juce::String& name) const
{
    return favoriteNames.contains (name);
}

void QChopEditor::toggleFavorite()
{
    if (currentPresetName.isEmpty()) return;
    if (isFavorite (currentPresetName))
        favoriteNames.removeString (currentPresetName);
    else
        favoriteNames.add (currentPresetName);
    getFavoritesFile().replaceWithText (favoriteNames.joinIntoString ("\n"));
    heartButton.setToggleState (isFavorite (currentPresetName), juce::dontSendNotification);
    rebuildPresetMenu();
}

void QChopEditor::rebuildPresetMenu()
{
    presetBox.clear (juce::dontSendNotification);
    userNames.clear();
    auto dir = getPresetDir();
    if (dir.isDirectory())
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.qchop"))
            userNames.add (f.getFileNameWithoutExtension());
    userNames.sort (true);

    juce::StringArray sectionNames[3];
    for (auto& n : favoriteNames)
        if (factoryNames.contains (n) || userNames.contains (n)) sectionNames[0].add (n);
    sectionNames[1] = factoryNames;
    sectionNames[2] = userNames;

    int menuId = 1;
    juce::HashMap<juce::String, int> nameToId;
    const char* headers[3] = { "\u2665 Favorites", "Factory", "User" };
    for (int s = 0; s < 3; ++s)
    {
        if (sectionNames[s].isEmpty()) continue;
        presetBox.addSectionHeading (headers[s]);
        for (auto& n : sectionNames[s])
        {
            if (! nameToId.contains (n))
            {
                nameToId.set (n, menuId);
                presetBox.addItem ((s == 0 ? juce::String ("\u2665 ") : juce::String()) + n, menuId++);
            }
        }
    }
    presetBox.setSelectedId (nameToId[currentPresetName], juce::dontSendNotification);
    heartButton.setToggleState (isFavorite (currentPresetName), juce::dontSendNotification);
}

void QChopEditor::comboBoxChanged (juce::ComboBox* box)
{
    if (box != &presetBox) return;
    const int id = presetBox.getSelectedId();
    if (id <= 0) return;
    applyPresetById (id);
}

void QChopEditor::applyPresetById (int menuId)
{
    // Rebuild the same ordering used in rebuildPresetMenu.
    struct Entry { juce::String name; bool factory; };
    std::vector<Entry> entries;
    auto pushUnique = [&] (const juce::String& n, bool fac)
    {
        for (auto& e : entries) if (e.name == n) return;
        entries.push_back ({ n, fac });
    };
    for (auto& n : favoriteNames)
        if (factoryNames.contains (n) || userNames.contains (n))
            pushUnique (n, factoryNames.contains (n));
    for (auto& n : factoryNames) pushUnique (n, true);
    for (auto& n : userNames) pushUnique (n, false);

    if (menuId < 1 || menuId > (int) entries.size()) return;
    selectPresetByName (entries[(size_t) menuId - 1].name);
}

void QChopEditor::selectPresetByName (const juce::String& name)
{
    if (factoryNames.contains (name))
    {
        processor.applyFactoryPreset (name);
    }
    else
    {
        auto f = getPresetDir().getChildFile (name + ".qchop");
        if (f.existsAsFile())
        {
            std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (f));
            if (xml != nullptr)
                processor.applyPresetState (juce::ValueTree::fromXml (*xml));
        }
    }
    currentPresetName = name;
    syncSliceControls();
    waveform.repaint();
    pads.repaint();
    rebuildPresetMenu();
}

void QChopEditor::saveUserPreset()
{
    auto* aw = new juce::AlertWindow ("Save Preset", "Name your preset:", juce::AlertWindow::NoIcon);
    aw->addTextEditor ("name", currentPresetName.isEmpty() ? "My Chop" : currentPresetName);
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    aw->enterModalState (true, juce::ModalCallbackFunction::create ([this, aw] (int result)
    {
        if (result == 1)
        {
            const juce::String name = aw->getTextEditorContents ("name").trim();
            if (name.isNotEmpty())
            {
                auto dir = getPresetDir();
                dir.createDirectory();
                std::unique_ptr<juce::XmlElement> xml (processor.apvts.state.createXml());
                if (xml != nullptr)
                    dir.getChildFile (name + ".qchop").replaceWithText (xml->toString());
                if (! userNames.contains (name)) userNames.add (name);
                currentPresetName = name;
                rebuildPresetMenu();
            }
        }
        delete aw;
    }));
}
