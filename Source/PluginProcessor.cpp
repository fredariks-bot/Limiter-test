#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <cmath>
#include <algorithm>

// True-Peak-Limiter: 8x Oversampling, Lookahead-Brickwall, Stereo-linked.
// Peak-Detektion und Gain-Reduktion laufen im 8x-Bereich, dadurch werden
// Inter-Sample-Peaks erfasst.
class ClaudeLimiterProcessor : public juce::AudioProcessor
{
public:
    ClaudeLimiterProcessor()
        : AudioProcessor (BusesProperties()
                              .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                              .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMS", createLayout()),
          oversampler (2, 3, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true)
    {
    }

    //==============================================================================
    void prepareToPlay (double sampleRate, int maxBlockSize) override
    {
        oversampler.initProcessing ((size_t) maxBlockSize);
        oversampler.reset();

        osRate = sampleRate * 8.0;
        L = 8 * juce::jmax (1, juce::roundToInt (lookaheadMs * 0.001 * sampleRate));

        for (auto& d : delay)
            d.assign ((size_t) L, 0.0f);
        delayPos = 0;

        dqVal.assign ((size_t) L + 1, 1.0f);
        dqIdx.assign ((size_t) L + 1, 0);
        dqHead = 0;
        dqCount = 0;
        sampleIndex = 0;

        envBuf.assign ((size_t) L, 1.0f);
        envPos = 0;
        envSum = (double) L;
        env = 1.0f;

        setLatencySamples (juce::roundToInt (oversampler.getLatencyInSamples()) + (L - 1 + 7) / 8);
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto out = layouts.getMainOutputChannelSet();
        return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
               && out == layouts.getMainInputChannelSet();
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;

        for (int c = 2; c < buffer.getNumChannels(); ++c)
            buffer.clear (c, 0, buffer.getNumSamples());

        const float inGain  = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("gain")->load());
        const float ceiling = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("ceiling")->load());
        const double relMs  = (double) apvts.getRawParameterValue ("release")->load();
        const float relCoef = (float) (1.0 - std::exp (-1.0 / (relMs * 0.001 * osRate)));

        buffer.applyGain (inGain);

        juce::dsp::AudioBlock<float> block (buffer);
        auto osBlock = oversampler.processSamplesUp (block);

        const size_t numSamples = osBlock.getNumSamples();
        const size_t nc = juce::jmin ((size_t) 2, osBlock.getNumChannels());
        float* ch[2] = { osBlock.getChannelPointer (0), nc > 1 ? osBlock.getChannelPointer (1) : nullptr };
        const int cap = L + 1;

        for (size_t i = 0; i < numSamples; ++i)
        {
            // 1) Peak ueber alle Kanaele (Stereo-Link) und benoetigte Gain-Reduktion
            float peak = 0.0f;
            for (size_t c = 0; c < nc; ++c)
                peak = std::max (peak, std::abs (ch[c][i]));

            const float gReq = peak > ceiling ? ceiling / peak : 1.0f;

            // 2) Gleitendes Minimum ueber L Samples (monotone Deque)
            while (dqCount > 0 && dqVal[(size_t) ((dqHead + dqCount - 1) % cap)] >= gReq)
                --dqCount;
            const int back = (dqHead + dqCount) % cap;
            dqVal[(size_t) back] = gReq;
            dqIdx[(size_t) back] = sampleIndex;
            ++dqCount;
            while (dqIdx[(size_t) dqHead] <= sampleIndex - L)
            {
                dqHead = (dqHead + 1) % cap;
                --dqCount;
            }
            const float gMin = dqVal[(size_t) dqHead];

            // 3) Release: Attack sofort, Release exponentiell
            if (gMin < env)
                env = gMin;
            else
                env += (gMin - env) * relCoef;

            // 4) Gleitender Mittelwert ueber L Samples = sanfte Lookahead-Attack
            envSum += (double) env - (double) envBuf[(size_t) envPos];
            envBuf[(size_t) envPos] = env;
            envPos = (envPos + 1) % L;
            const float gain = (float) (envSum / (double) L);

            // 5) Verzoegertes Signal mit Gain multiplizieren
            for (size_t c = 0; c < nc; ++c)
                delay[c][(size_t) delayPos] = ch[c][i];
            delayPos = (delayPos + 1) % L;
            for (size_t c = 0; c < nc; ++c)
                ch[c][i] = delay[c][(size_t) delayPos] * gain;

            ++sampleIndex;
        }

        oversampler.processSamplesDown (block);
    }

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Claude Limiter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override
    {
        if (auto xml = apvts.copyState().createXml())
            copyXmlToBinary (*xml, destData);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using P = juce::AudioParameterFloat;
        using R = juce::NormalisableRange<float>;
        using A = juce::AudioParameterFloatAttributes;

        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        layout.add (std::make_unique<P> (juce::ParameterID { "gain", 1 },    "Gain",
                                         R (0.0f, 24.0f, 0.01f), 0.0f,  A().withLabel ("dB")));
        layout.add (std::make_unique<P> (juce::ParameterID { "ceiling", 1 }, "Ceiling (dBTP)",
                                         R (-12.0f, 0.0f, 0.01f), -1.0f, A().withLabel ("dBTP")));
        layout.add (std::make_unique<P> (juce::ParameterID { "release", 1 }, "Release",
                                         R (10.0f, 1000.0f, 0.1f, 0.4f), 100.0f, A().withLabel ("ms")));
        return layout;
    }

    juce::AudioProcessorValueTreeState apvts;
    juce::dsp::Oversampling<float> oversampler;

    static constexpr double lookaheadMs = 1.5;
    double osRate = 48000.0 * 8.0;
    int L = 8;

    std::vector<float> delay[2];
    int delayPos = 0;

    std::vector<float> dqVal;
    std::vector<juce::int64> dqIdx;
    int dqHead = 0, dqCount = 0;
    juce::int64 sampleIndex = 0;

    std::vector<float> envBuf;
    int envPos = 0;
    double envSum = 0.0;
    float env = 1.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClaudeLimiterProcessor)
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ClaudeLimiterProcessor();
}
