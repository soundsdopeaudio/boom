#pragma once
#include <JuceHeader.h>
#include "EngineDefs.h"
#include <atomic>   // (at top of file if not already there)
#include <cstdint>
#include <functional>



class BoomAudioProcessor : public juce::AudioProcessor
{
public:
    BoomAudioProcessor();
    ~BoomAudioProcessor() override = default;

    std::atomic<std::uint64_t> genNonce_{ 1 };

    // ==== GEN: high-level generation entry points (engines) ====
    void generate808(int bars);
    void generateBass(int bars);
    void generateDrums(int bars);

    double getHostBpm() const noexcept;

    // ==== GEN: edit/variation tools ====
    void bumpDrumRowsUp();                                         // for Bumppit (Drums)
    void transposeMelodic(int semitones, const juce::String& newKey, const juce::String& newScale, int octaveOffset); // for Bumppit (808/Bass)

    // ==== GEN: Rolls (Drums only) ====
    void generateDrumRolls(const juce::String& style, int bars);

    // Call when patterns change so the UI refreshes
    void notifyPatternChanged();

    // Build a probability mask from the current kick pattern (row 0) at 16th-note resolution.
// Returns a vector<int> length = bars*16, values in [0..100] = probability boost.
    static std::vector<int> buildKickBiasMask(const BoomAudioProcessor& proc, int bars)
    {
        const int total16 = proc.q16(bars);
        std::vector<int> bias(total16, 0);

        const auto drums = proc.getDrumPattern();
        for (const auto& n : drums)
        {
            if (n.row != 0) continue; // row 0 assumed = Kick
            const int start16 = (n.startTick * 4) / BoomAudioProcessor::PPQ;
            const int len16 = juce::jmax(1, (n.lengthTicks * 4) / BoomAudioProcessor::PPQ);
            for (int s = 0; s < len16; ++s)
            {
                const int idx = start16 + s;
                if ((unsigned)idx < bias.size())
                    bias[idx] = juce::jlimit(0, 100, bias[idx] + juce::jmap(n.velocity, 1, 127, 15, 45));
            }

            // also nudge neighbors (so we "tend" toward kicks without being locked)
            const int pre = start16 - 1;
            const int pst = start16 + len16;
            if (pre >= 0)              bias[pre] = juce::jmax(bias[pre], 12);
            if (pst < (int)bias.size()) bias[pst] = juce::jmax(bias[pst], 12);
        }

        // tiny baseline so an empty kick lane still generates:
        for (auto& b : bias) b = juce::jmax(b, 6);
        return bias;
    }

    // AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override; // keep if already present
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet() != juce::AudioChannelSet::disabled();
    }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // DRUMS-ONLY Rolls generator for the Rolls window
    void generateRolls(const juce::String& styleName, int bars);
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    const juce::String getName() const override { return "BOOM"; }

    // MIDI effect flags
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Programs
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    int    getCaptureLengthSamples() const { return captureLengthSamples; }
    double getCaptureSampleRate()   const { return lastSampleRate; }

    // State
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Pattern model used by the Editor
    struct Note
    {
        int pitch{ 60 };   // used for 808/Bass
        int row{ 0 };      // used for Drums
        int startTick{ 0 };
        int lengthTicks{ 24 };
        int velocity{ 100 };
        int channel{ 1 };
    };
    using Pattern = juce::Array<Note>;

    const Pattern& getDrumPattern() const noexcept { return drumPattern; }
    const Pattern& getMelodicPattern() const noexcept { return melodicPattern; }
    void setDrumPattern(const Pattern& p) { drumPattern = p; }
    void setMelodicPattern(const Pattern& p) { melodicPattern = p; }

    const juce::StringArray& getDrumRows() const { return drumRows; }

    // PluginProcessor.cpp
    boom::Engine BoomAudioProcessor::getEngineSafe() const
    {
        // If the param is missing/null, default to Drums so we never crash.
        if (auto* p = apvts.getRawParameterValue("engine"))
            return static_cast<boom::Engine>(static_cast<int>(p->load()));

        return boom::Engine::Drums;
    }

    // Simple transforms
// ---- Bumppit (Drums path + Melodic path) ----                                               // DRUMS ONLY
    void bumppitTranspose(int targetKeyIndex, const juce::String& scaleName,  // 808/BASS ONLY
        int octaveDelta /* e.g., -2..+2 */);
    void flipMelodic(int seed, int density, int bars);
    void flipDrums(int seed, int density, int bars);

    // APVTS


    // === AI: Audio Capture Sources ===
    enum class CaptureSource { Loopback, Microphone };

    // Start/stop capture; in plugin context both read from processBlock input.
    // Allocate/resize the mono capture buffer for a given number of seconds
    void ensureCaptureCapacitySeconds(double seconds);
    void aiStartCapture(CaptureSource src);
    void aiStopCapture();
    bool aiIsCapturing() const { return isCapturing.load(); }

    // Transcribe captured audio into a drum pattern (kick/snare/hat) for given bars/bpm
    void aiAnalyzeCapturedToDrums(int bars, int bpm);

    // Slapsmith: expand current drum pattern (or provided seed pattern already set by editor) into a fuller groove
    void aiSlapsmithExpand(int bars);

    // StyleBlender: blend two style names from your style combo boxes into a new drum groove
    void aiStyleBlendDrums(juce::String styleA, juce::String styleB, int bars);

    // 808 generator
    void generate808(const juce::String& style, const juce::String& keyName,
        const juce::String& scaleName, int bars,
        int densityPercent, bool allowTriplets, bool allowDotted);

    int q16(int bars)  const { return bars * 16; }    // 16th-note grid per bar (simple mental model)
    static constexpr int PPQ = 96;

    using APVTS = juce::AudioProcessorValueTreeState;
    APVTS apvts;

private:
    Pattern drumPattern, melodicPattern;
    juce::StringArray drumRows { boom::defaultDrumRows() };

    std::atomic<double> lastHostBpm { 120.0 };

    // ---- Random helpers (use juce::Random to avoid std::mt19937 headaches) ----
    juce::Random prng;
    int irand(int lo, int hi) { return prng.nextInt(juce::Range<int>(lo, hi + 1)); }
    bool chance(int pct) { return prng.nextInt({ 0,100 }) < juce::jlimit(0, 100, pct); }

    // ---- Quantize helpers ----
     // 96 ticks/quarter (we used this elsewhere)
    int toTick16(int n)const { return n * (PPQ / 4); }  // 16th -> ticks

    // ---- Small melody utility for 808/Bass ----
    int pickScaleDegree(const juce::String& scaleName, int degree0to6);

    // ---- Internal: notify UI ----
    void sendUIChange();


    // === AI: capture ring buffer ===
    std::atomic<bool> isCapturing { false };
    CaptureSource currentCapture{ CaptureSource::Loopback };

    juce::AudioBuffer<float> captureBuffer;   // mono scratch buffer
    int captureWritePos = 0;
    int captureLengthSamples = 0;             // how many valid samples recorded
    double lastSampleRate = 44100.0;

    void appendCaptureFrom(const juce::AudioBuffer<float>& in);

    // Analysis helpers
    Pattern transcribeAudioToDrums(const float* mono, int numSamples, int bars, int bpm) const;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BoomAudioProcessor)
};