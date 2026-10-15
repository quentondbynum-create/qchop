#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
QChopProcessor::QChopProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "QCHOP", createParameterLayout())
{
    formatManager.registerBasicFormats();
    ensureSliceTree();
    refreshSliceParamsFromTree();
    apvts.state.addListener (this);
}

QChopProcessor::~QChopProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout QChopProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        kParamMaster, "Master", juce::NormalisableRange<float> (0.0f, 1.5f, 0.001f), 1.0f));
    return { params.begin(), params.end() };
}

void QChopProcessor::ensureSliceTree()
{
    auto state = apvts.state;
    if (! state.getChildWithName ("SLICES").isValid())
    {
        juce::ValueTree slices ("SLICES");
        for (int i = 0; i < numSlices; ++i)
        {
            juce::ValueTree s ("SLICE");
            s.setProperty ("start", 0.0, nullptr);
            s.setProperty ("end", 0.0, nullptr);
            s.setProperty ("pitch", 0.0, nullptr);
            s.setProperty ("reverse", false, nullptr);
            s.setProperty ("level", 1.0, nullptr);
            slices.appendChild (s, nullptr);
        }
        state.appendChild (slices, nullptr);
    }
}

void QChopProcessor::refreshSliceParamsFromTree()
{
    auto slices = apvts.state.getChildWithName ("SLICES");
    if (! slices.isValid())
        return;

    int active = 0;
    for (int i = 0; i < numSlices; ++i)
    {
        auto s = slices.getChild (i);
        if (! s.isValid())
            continue;
        sliceParams[(size_t) i].start.store ((double) s.getProperty ("start", 0.0));
        sliceParams[(size_t) i].end.store   ((double) s.getProperty ("end", 0.0));
        sliceParams[(size_t) i].pitchSt.store ((float) s.getProperty ("pitch", 0.0));
        sliceParams[(size_t) i].reverse.store ((bool) s.getProperty ("reverse", false));
        sliceParams[(size_t) i].level.store  ((float) s.getProperty ("level", 1.0));
        if ((double) s.getProperty ("end", 0.0) > (double) s.getProperty ("start", 0.0))
            active = i + 1;
    }
    numActiveSlices.store (active);
}

void QChopProcessor::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    refreshSliceParamsFromTree();
}
void QChopProcessor::valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&)
{
    refreshSliceParamsFromTree();
}
void QChopProcessor::valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int)
{
    refreshSliceParamsFromTree();
}

//==============================================================================
const juce::String QChopProcessor::getName() const { return "QChop"; }
bool QChopProcessor::acceptsMidi() const { return true; }
bool QChopProcessor::producesMidi() const { return false; }
bool QChopProcessor::isMidiEffect() const { return false; }
double QChopProcessor::getTailLengthSeconds() const { return 0.5; }
juce::AudioProcessorEditor* QChopProcessor::createEditor() { return new QChopEditor (*this); }
bool QChopProcessor::hasEditor() const { return true; }
int QChopProcessor::getNumPrograms() { return 1; }
int QChopProcessor::getCurrentProgram() { return 0; }
void QChopProcessor::setCurrentProgram (int) {}
const juce::String QChopProcessor::getProgramName (int) { return {}; }
void QChopProcessor::changeProgramName (int, const juce::String&) {}

bool QChopProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void QChopProcessor::prepareToPlay (double sampleRate, int)
{
    lastSampleRate = sampleRate;
}

void QChopProcessor::releaseResources() {}

void QChopProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::MemoryOutputStream mos (destData, true);
    apvts.state.writeToStream (mos);
}

void QChopProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto tree = juce::ValueTree::readFromData (data, (size_t) sizeInBytes);
    if (tree.isValid())
    {
        ensureSliceTree();
        apvts.replaceState (tree);
        refreshSliceParamsFromTree();
    }
}

//==============================================================================
bool QChopProcessor::loadAudioFile (const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const double fileRate = reader->sampleRate;
    const auto numSamples = (int) reader->lengthInSamples;
    if (numSamples <= 0)
        return false;

    juce::AudioBuffer<float> tmp ((int) reader->numChannels, numSamples);
    reader->read (&tmp, 0, numSamples, 0, true, true);

    // Resample to session rate if needed.
    std::shared_ptr<juce::AudioBuffer<float>> cooked;
    if (std::abs (fileRate - lastSampleRate) > 1.0)
    {
        const double ratio = fileRate / lastSampleRate;
        const int outLen = juce::jmax (1, (int) std::round (numSamples / ratio));
        cooked = std::make_shared<juce::AudioBuffer<float>> (2, outLen);
        cooked->clear();
        juce::LagrangeInterpolator interp;
        std::vector<float> in ((size_t) numSamples), out ((size_t) outLen);
        for (int ch = 0; ch < 2; ++ch)
        {
            const float* src = tmp.getReadPointer (juce::jmin (ch, tmp.getNumChannels() - 1));
            std::copy (src, src + numSamples, in.begin());
            interp.process (1.0 / ratio, in.data(), out.data(), outLen);
            cooked->copyFrom (ch, 0, out.data(), outLen);
        }
    }
    else
    {
        cooked = std::make_shared<juce::AudioBuffer<float>> (2, numSamples);
        cooked->clear();
        for (int ch = 0; ch < 2; ++ch)
            cooked->copyFrom (ch, 0, tmp.getReadPointer (juce::jmin (ch, tmp.getNumChannels() - 1)), numSamples);
    }

    {
        juce::SpinLock::ScopedLockType lock (sampleLock);
        sampleBuffer = cooked;
    }
    loadedFileName = file.getFileName();
    ++sampleVersion;

    detectOnsets (0.5f);
    return true;
}

bool QChopProcessor::hasSampleLoaded() const
{
    juce::SpinLock::ScopedLockType lock (const_cast<juce::SpinLock&> (sampleLock));
    return sampleBuffer != nullptr && sampleBuffer->getNumSamples() > 0;
}

std::shared_ptr<juce::AudioBuffer<float>> QChopProcessor::getSampleBuffer()
{
    juce::SpinLock::ScopedLockType lock (sampleLock);
    return sampleBuffer;
}

//==============================================================================
void QChopProcessor::detectOnsets (float sensitivity)
{
    auto buf = getSampleBuffer();
    if (buf == nullptr || buf->getNumSamples() < 2048)
        return;

    const int N = buf->getNumSamples();
    const int fftSize = 1024, hop = 512;
    juce::dsp::FFT fft (10);
    juce::dsp::WindowingFunction<float> window ((size_t) fftSize,
                                               juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> flux;
    flux.reserve ((size_t) (N / hop));
    std::vector<float> fftData ((size_t) fftSize * 2);
    std::vector<float> prevMag ((size_t) fftSize / 2, 0.0f);

    const float* l = buf->getReadPointer (0);
    const float* r = buf->getNumChannels() > 1 ? buf->getReadPointer (1) : l;

    for (int pos = 0; pos + fftSize <= N; pos += hop)
    {
        for (int i = 0; i < fftSize; ++i)
        {
            fftData[(size_t) (2 * i)]     = 0.5f * (l[pos + i] + r[pos + i]);
            fftData[(size_t) (2 * i) + 1] = 0.0f;
        }
        window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        float f = 0.0f;
        for (int k = 0; k < fftSize / 2; ++k)
        {
            const float m = fftData[(size_t) k] / (fftSize / 2);
            f += juce::jmax (0.0f, m - prevMag[(size_t) k]);
            prevMag[(size_t) k] = m;
        }
        flux.push_back (f);
    }

    if (flux.size() < 8)
        return;

    // Adaptive threshold: median * (2.2 - sensitivity * 1.4)
    std::vector<float> sorted = flux;
    std::nth_element (sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const float median = sorted[sorted.size() / 2];
    const float thresh = median * (2.2f - sensitivity * 1.4f);
    const int minGap = juce::jmax (2, (int) (0.08 * lastSampleRate / hop));

    std::vector<std::pair<float,int>> peaks;
    for (size_t i = 1; i + 1 < flux.size(); ++i)
        if (flux[i] > thresh && flux[i] >= flux[i-1] && flux[i] > flux[i+1])
            peaks.emplace_back (flux[i], (int) i);

    std::sort (peaks.begin(), peaks.end(),
               [] (auto& a, auto& b) { return a.first > b.first; });

    std::vector<int> chosen;
    for (auto& p : peaks)
    {
        bool ok = true;
        for (int c : chosen)
            if (std::abs (c - p.second) < minGap) { ok = false; break; }
        if (ok) chosen.push_back (p.second);
        if ((int) chosen.size() >= numSlices - 1) break;
    }
    std::sort (chosen.begin(), chosen.end());

    std::vector<double> bounds;
    bounds.push_back (0.0);
    for (int c : chosen) bounds.push_back ((double) c * hop);
    bounds.push_back ((double) N);
    setSliceBoundaries (bounds);
}

void QChopProcessor::setSliceBoundaries (const std::vector<double>& bounds)
{
    auto slices = apvts.state.getChildWithName ("SLICES");
    if (! slices.isValid()) return;
    const int n = juce::jmin ((int) bounds.size() - 1, numSlices);
    for (int i = 0; i < numSlices; ++i)
    {
        auto s = slices.getChild (i);
        if (! s.isValid()) continue;
        if (i < n)
        {
            s.setProperty ("start", bounds[(size_t) i], nullptr);
            s.setProperty ("end", bounds[(size_t) i + 1], nullptr);
        }
        else
        {
            s.setProperty ("start", 0.0, nullptr);
            s.setProperty ("end", 0.0, nullptr);
        }
    }
}

void QChopProcessor::setNumActiveSlices (int n)
{
    auto buf = getSampleBuffer();
    if (buf == nullptr) return;
    n = juce::jlimit (1, numSlices, n);
    std::vector<double> bounds;
    const double N = (double) buf->getNumSamples();
    for (int i = 0; i <= n; ++i) bounds.push_back (N * i / n);
    setSliceBoundaries (bounds);
}

void QChopProcessor::setSliceBoundary (int sliceIndex, double newStartSamples)
{
    auto slices = apvts.state.getChildWithName ("SLICES");
    if (! slices.isValid()) return;
    if (sliceIndex < 0 || sliceIndex >= numActiveSlices.load()) return;
    auto s = slices.getChild (sliceIndex);
    auto prev = sliceIndex > 0 ? slices.getChild (sliceIndex - 1) : juce::ValueTree();
    const double lo = prev.isValid() ? (double) prev.getProperty ("start", 0.0) + 64.0 : 0.0;
    const double hi = (double) s.getProperty ("end", 0.0) - 64.0;
    s.setProperty ("start", juce::jlimit (lo, hi, newStartSamples), nullptr);
}

void QChopProcessor::addSliceAt (double samplePos)
{
    auto slices = apvts.state.getChildWithName ("SLICES");
    if (! slices.isValid()) return;
    const int active = numActiveSlices.load();
    if (active >= numSlices) return;
    // Find which slice contains samplePos and split it.
    for (int i = 0; i < active; ++i)
    {
        auto s = slices.getChild (i);
        const double st = (double) s.getProperty ("start", 0.0);
        const double en = (double) s.getProperty ("end", 0.0);
        if (samplePos > st + 128.0 && samplePos < en - 128.0)
        {
            // Shift later slices up by one.
            for (int j = active; j > i + 1; --j)
            {
                auto dst = slices.getChild (j), src = slices.getChild (j - 1);
                for (auto prop : { "start", "end", "pitch", "reverse", "level" })
                    dst.setProperty (prop, src.getProperty (prop), nullptr);
            }
            auto ns = slices.getChild (i + 1);
            ns.setProperty ("start", samplePos, nullptr);
            ns.setProperty ("end", en, nullptr);
            ns.setProperty ("pitch", 0.0, nullptr);
            ns.setProperty ("reverse", false, nullptr);
            ns.setProperty ("level", 1.0, nullptr);
            s.setProperty ("end", samplePos, nullptr);
            break;
        }
    }
}

void QChopProcessor::removeSlice (int sliceIndex)
{
    auto slices = apvts.state.getChildWithName ("SLICES");
    if (! slices.isValid()) return;
    const int active = numActiveSlices.load();
    if (active <= 1 || sliceIndex < 0 || sliceIndex >= active) return;
    // Merge with neighbour: extend previous slice's end, shift later ones down.
    if (sliceIndex > 0)
    {
        auto prev = slices.getChild (sliceIndex - 1);
        auto cur = slices.getChild (sliceIndex);
        prev.setProperty ("end", cur.getProperty ("end"), nullptr);
    }
    for (int j = sliceIndex; j < active - 1; ++j)
    {
        auto dst = slices.getChild (j), src = slices.getChild (j + 1);
        for (auto prop : { "start", "end", "pitch", "reverse", "level" })
            dst.setProperty (prop, src.getProperty (prop), nullptr);
    }
    auto last = slices.getChild (active - 1);
    last.setProperty ("start", 0.0, nullptr);
    last.setProperty ("end", 0.0, nullptr);
}

void QChopProcessor::setSlicePitch (int i, float st)
{
    auto s = apvts.state.getChildWithName ("SLICES").getChild (i);
    if (s.isValid()) s.setProperty ("pitch", juce::jlimit (-12.0f, 12.0f, st), nullptr);
}
void QChopProcessor::setSliceReverse (int i, bool r)
{
    auto s = apvts.state.getChildWithName ("SLICES").getChild (i);
    if (s.isValid()) s.setProperty ("reverse", r, nullptr);
}
void QChopProcessor::setSliceLevel (int i, float l)
{
    auto s = apvts.state.getChildWithName ("SLICES").getChild (i);
    if (s.isValid()) s.setProperty ("level", juce::jlimit (0.0f, 1.5f, l), nullptr);
}

void QChopProcessor::resliceWithSensitivity (float sens)
{
    detectOnsets (juce::jlimit (0.0f, 1.0f, sens));
}

//==============================================================================
void QChopProcessor::triggerSlice (int i)
{
    if (i < 0 || i >= numActiveSlices.load()) return;
    int start1, size1, start2, size2;
    triggerFifo.prepareToWrite (1, start1, size1, start2, size2);
    if (size1 > 0) triggerData[(size_t) start1] = i;
    else if (size2 > 0) triggerData[(size_t) start2] = i;
    triggerFifo.finishedWrite (size1 + size2);
}

void QChopProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // MIDI note 36..51 -> slices 0..15
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            const int s = msg.getNoteNumber() - 36;
            if (s >= 0 && s < numSlices) triggerSlice (s);
        }
    }

    // Drain trigger FIFO into voices.
    {
        int start1, size1, start2, size2;
        triggerFifo.prepareToRead (triggerFifo.getNumReady(), start1, size1, start2, size2);
        auto fire = [this] (int slice)
        {
            // Steal quietest/oldest voice: simple round-robin.
            static int rr = 0;
            Voice& v = voices[(size_t) (rr++ % numVoices)];
            const auto& sp = sliceParams[(size_t) slice];
            v.active = true;
            v.slice = slice;
            v.pos = sp.reverse.load() ? sp.end.load() - 1.0 : sp.start.load();
            padFlash[(size_t) slice].store (1.0f);
        };
        for (int i = 0; i < size1; ++i) fire (triggerData[(size_t) (start1 + i)]);
        for (int i = 0; i < size2; ++i) fire (triggerData[(size_t) (start2 + i)]);
        triggerFifo.finishedRead (size1 + size2);
    }

    std::shared_ptr<juce::AudioBuffer<float>> sample;
    {
        juce::SpinLock::ScopedTryLockType lock (sampleLock);
        if (lock.isLocked()) sample = sampleBuffer;
    }
    if (sample == nullptr || sample->getNumSamples() == 0)
    {
        for (auto& v : voices) v.active = false;
        return;
    }

    const int srcLen = sample->getNumSamples();
    const float* srcL = sample->getReadPointer (0);
    const float* srcR = sample->getNumChannels() > 1 ? sample->getReadPointer (1) : srcL;
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    const float master = apvts.getRawParameterValue (kParamMaster)->load();

    auto readLin = [] (const float* src, int len, double pos) -> float
    {
        const int i0 = (int) pos;
        const float frac = (float) (pos - i0);
        const int i1 = juce::jmin (i0 + 1, len - 1);
        const int ic = juce::jlimit (0, len - 1, i0);
        return src[(size_t) ic] + frac * (src[(size_t) i1] - src[(size_t) ic]);
    };

    for (int n = 0; n < numSamples; ++n)
    {
        float mixL = 0.0f, mixR = 0.0f;
        for (auto& v : voices)
        {
            if (! v.active) continue;
            const auto& sp = sliceParams[(size_t) v.slice];
            const double st = sp.start.load(), en = sp.end.load();
            const bool rev = sp.reverse.load();
            const float rate = std::pow (2.0f, sp.pitchSt.load() / 12.0f);
            const float lvl = sp.level.load();

            mixL += readLin (srcL, srcLen, v.pos) * lvl;
            mixR += readLin (srcR, srcLen, v.pos) * lvl;

            v.pos += rev ? -rate : rate;
            if ((! rev && v.pos >= en) || (rev && v.pos <= st))
                v.active = false;
        }
        outL[n] = mixL * master;
        if (outR != nullptr) outR[n] = mixR * master;
    }

    // Decay pad flashes.
    for (auto& f : padFlash)
        f.store (f.load() * 0.92f);
}

//==============================================================================
juce::StringArray QChopProcessor::getFactoryPresetNames() const
{
    return { "Init", "Chromatic Up", "Octave Jumps", "Reverse Party", "Faded Out" };
}

void QChopProcessor::applyFactoryPreset (const juce::String& name)
{
    auto slices = apvts.state.getChildWithName ("SLICES");
    if (! slices.isValid()) return;
    const int active = juce::jmax (1, numActiveSlices.load());

    for (int i = 0; i < numSlices; ++i)
    {
        auto s = slices.getChild (i);
        if (! s.isValid()) continue;
        float pitch = 0.0f, level = 1.0f;
        bool rev = false;
        if (name == "Chromatic Up" && i < active)       pitch = (float) (i - active / 2);
        else if (name == "Octave Jumps" && i < active)  pitch = (i % 2 == 0) ? 12.0f : -12.0f;
        else if (name == "Reverse Party" && i < active) rev = (i % 2 == 1);
        else if (name == "Faded Out" && i < active)     level = 1.0f - 0.06f * i;
        s.setProperty ("pitch", pitch, nullptr);
        s.setProperty ("reverse", rev, nullptr);
        s.setProperty ("level", level, nullptr);
    }
}

void QChopProcessor::applyPresetState (const juce::ValueTree& state)
{
    if (state.isValid() && state.hasType ("QCHOP"))
    {
        apvts.replaceState (state);
        ensureSliceTree();
        refreshSliceParamsFromTree();
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new QChopProcessor();
}
