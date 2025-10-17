#include "PluginEditor.h"
#include "FlipUtils.h"
#include <random>
#include "EngineDefs.h"
#include <map>
#include <vector>
#include <algorithm> // for std::find
#include <cstdint>  // for std::uint64_t

using AP = juce::AudioProcessorValueTreeState;

juce::AudioProcessorEditor* BoomAudioProcessor::createEditor()
{
    return new BoomAudioProcessorEditor(*this);
}

static AP::ParameterLayout createLayout()
{
    using AP = juce::AudioProcessorValueTreeState;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    // ---------- NEW: BPM Lock ----------
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        "bpmLock",        // ID
        "BPM Lock",       // Name
        true              // default: locked (set false if you prefer unlocked by default)
    ));

    p.push_back(std::make_unique<juce::AudioParameterChoice>("engine", "Engine", boom::engineChoices(), (int)boom::Engine::Drums));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("timeSig", "Time Signature", boom::timeSigChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("bars", "Bars", boom::barsChoices(), 0));

    p.push_back(std::make_unique<juce::AudioParameterFloat>("humanizeTiming", "Humanize Timing", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("humanizeVelocity", "Humanize Velocity", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("swing", "Swing", juce::NormalisableRange<float>(0.f, 100.f), 0.f));

    p.push_back(std::make_unique<juce::AudioParameterBool>("useTriplets", "Triplets", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("tripletDensity", "Triplet Density", juce::NormalisableRange<float>(0.f, 100.f), 0.f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("useDotted", "Dotted Notes", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("dottedDensity", "Dotted Density", juce::NormalisableRange<float>(0.f, 100.f), 0.f));

    p.push_back(std::make_unique<juce::AudioParameterChoice>("key", "Key", boom::keyChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("scale", "Scale", boom::scaleChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterChoice>("octave", "Octave", juce::StringArray("-2", "-1", "0", "+1", "+2"), 2));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("restDensity808", "Rest Density 808", juce::NormalisableRange<float>(0.f, 100.f), 10.f));

    p.push_back(std::make_unique<juce::AudioParameterChoice>("bassStyle", "Bass Style", boom::styleChoices(), 0));

    p.push_back(std::make_unique<juce::AudioParameterChoice>("drumStyle", "Drum Style", boom::styleChoices(), 0));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("restDensityDrums", "Rest Density Drums", juce::NormalisableRange<float>(0.f, 100.f), 5.f));

    p.push_back(std::make_unique<juce::AudioParameterInt>("seed", "Seed", 0, 1000000, 0));


    return { p.begin(), p.end() };
}

// ----- 808 Generator (bars: 1/2/4/8; density 0..100; honors style/key/scale/TS) -----
void BoomAudioProcessor::generate808(const juce::String& style,
    const juce::String& keyName,
    const juce::String& scaleName,
    int bars,
    int densityPercent,
    bool allowTriplets,
    bool allowDotted)
{
    // Only for melodic engine (non-Drums). If Drums, do nothing.
    if (getEngineSafe() == boom::Engine::Drums)
        return;

    // ---- Config & helpers ----
    bars = juce::jlimit(1, 8, bars);
    densityPercent = juce::jlimit(0, 100, densityPercent);

    // time signature -> steps per bar
    // Your grid = 24 ticks per 1/16 step.
    auto tsParam = apvts.state.getProperty("timeSig").toString(); // however you store it; fallback to "4/4"
    if (tsParam.isEmpty()) tsParam = "4/4";
    auto tsParts = juce::StringArray::fromTokens(tsParam, "/", "");
    const int tsNum = tsParts.size() >= 1 ? tsParts[0].getIntValue() : 4;
    const int tsDen = tsParts.size() >= 2 ? tsParts[1].getIntValue() : 4;

    auto stepsPerBarFor = [&](int num, int den)->int
    {
        // 4/4 -> 16 steps, 3/4 -> 12, 6/8 -> 12, 7/8 -> 14, etc.
        if (den == 4) return num * 4;
        if (den == 8) return num * 2;
        if (den == 16) return num; // very fine
        return num * 4;            // default
    };
    const int stepsPerBar = stepsPerBarFor(tsNum, tsDen);
    const int tps = 24;                         // ticks per 1/16 step (your grid)
    const int ppq = 96;                         // for export (not used here)
    const int totalSteps = stepsPerBar * bars;

    // ---- Scale tables ----
    static const std::map<juce::String, std::vector<int>, std::less<>> kScales = {
        { "Major",          { 0,2,4,5,7,9,11 } },
        { "Natural Minor",  { 0,2,3,5,7,8,10 } },
        { "Harmonic Minor", { 0,2,3,5,7,8,11 } },
        { "Dorian",         { 0,2,3,5,7,9,10 } },
        { "Phrygian",       { 0,1,3,5,7,8,10 } },
        { "Lydian",         { 0,2,4,6,7,9,11 } },
        { "Mixolydian",     { 0,2,4,5,7,9,10 } },
        { "Aeolian",        { 0,2,3,5,7,8,10 } },
        { "Locrian",        { 0,1,3,5,6,8,10 } },
        { "Blues",          { 0,3,5,6,7,10 } },
        { "Pentatonic Maj", { 0,2,4,7,9 } },
        { "Pentatonic Min", { 0,3,5,7,10 } },
        { "Chromatic",      { 0,1,2,3,4,5,6,7,8,9,10,11 } }
    };
    static const juce::StringArray kKeys = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };

    auto keyIndex = juce::jmax(0, kKeys.indexOf(keyName.trim().toUpperCase()));
    const auto itScale = kScales.find(scaleName.trim());
    const auto& scalePCs = (itScale != kScales.end()) ? itScale->second : kScales.at("Chromatic");

    auto wrap12 = [](int v) { v %= 12; if (v < 0) v += 12; return v; };

    auto degreeToPitch = [&](int degree, int octave)->int
    {
        // degree is index in scalePCs (wrap), root = keyIndex
        const int pc = scalePCs[(degree % (int)scalePCs.size() + (int)scalePCs.size()) % (int)scalePCs.size()];
        return juce::jlimit(0, 127, octave * 12 + wrap12(keyIndex + pc));
    };

    // ---- Randomness (never same twice) ----
    // Mix in a monotonic nonce so even same UI values differ each time
// Robust seed: mix 32-bit millis, 64-bit hi-res ticks, and a monotonic nonce
    const auto now32 = juce::Time::getMillisecondCounter();      // uint32
    const auto ticks64 = (std::uint64_t)juce::Time::getHighResolutionTicks();
    const auto nonce = genNonce_.fetch_add(1, std::memory_order_relaxed) + 1; // or ++genNonce_ if not atomic

    const std::uint64_t mix = (std::uint64_t)now32
        ^ (ticks64)
        ^ (std::uint64_t)nonce;

    // keep it in signed-int range for juce::Random
    const int seed = (int)(mix & 0x7fffffff);
    juce::Random rng(seed);
    auto pct = [&](int prob)->bool { return rng.nextInt({ 100 }) < juce::jlimit(0, 100, prob); };

    // ---- Clear melodic pattern, we’re generating fresh 808s ----
    auto mp = getMelodicPattern();
    mp.clear();

    // Base octave and note length policy per style
    int baseOct = 3; // C3-ish base for 808
    int sustainStepsDefault = (tsDen == 8 ? 2 : 1); // default 1/8 in 4/4, 1/4 in 6/8 feel

    if (style.equalsIgnoreCase("trap") || style.equalsIgnoreCase("wxstie") || style.equalsIgnoreCase("drill"))
    {
        baseOct = 2;                 // lower 808s
        sustainStepsDefault = 1;     // staccato-ish, relies on repeated notes/rolls
    }

    // Density → how often we place notes per step
    auto placeProbability = [&](int step)->int
    {
        // swing accent: slightly more on offbeats for trap/drill
        int base = densityPercent;
        if (style.equalsIgnoreCase("trap") || style.equalsIgnoreCase("drill"))
            if ((step % 2) == 1) base = juce::jmin(100, base + 8);
        return base;
    };

    // Choose rhythmic subdivision for a “burst”
    auto chooseSubTick = [&]()
    {
        // 24 (1/16), 12 (1/32), 8 (~1/16T), 6 (fast), 4 (very fast)
        int pool[] = { 24, 12, 8, 6, 4 };
        int idx = rng.nextInt({ (int)std::size(pool) });
        int sub = pool[idx];
        if (!allowTriplets && sub == 8) sub = 12;           // avoid triplet-ish
        return sub;
    };

    // Decide dotted: multiply steps by 1.5
    auto dottedLen = [&](int steps)->int
    {
        if (!allowDotted) return steps;
        return steps + steps / 2; // *1.5
    };

    // Melodic motion choices by style (root, fifth, octave, neighbor tones)
    auto chooseDegreeDelta = [&]()
    {
        if (style.equalsIgnoreCase("trap"))
        {
            int r = rng.nextInt({ 100 });
            if (r < 40) return 0;   // root
            if (r < 65) return 4;   // fifth up
            if (r < 80) return -3;  // minor 3rd below (bluesy)
            if (r < 90) return 7;   // octave-ish (wrap by scale index)
            return (rng.nextBool() ? +1 : -1); // neighbor
        }
        else if (style.equalsIgnoreCase("drill"))
        {
            int r = rng.nextInt({ 100 });
            if (r < 35) return 0;
            if (r < 60) return 4;
            if (r < 75) return -2;  // lean into darker neighbor
            if (r < 90) return 7;
            return (rng.nextBool() ? +2 : -2);
        }
        else if (style.equalsIgnoreCase("wxstie"))
        {
            int r = rng.nextInt({ 100 });
            if (r < 45) return 0;
            if (r < 70) return 4;
            if (r < 85) return (rng.nextBool() ? +1 : -1);
            return 7;
        }
        else
        {
            // generic (pop/edm/rnb/rock/reggaeton, etc.)
            int r = rng.nextInt({ 100 });
            if (r < 50) return 0;
            if (r < 75) return 4;
            return (rng.nextBool() ? +1 : -1);
        }
    };

    // Build stepwise with bursts and sustains
    int currentDegree = 0;  // start on root
    int currentOct = baseOct;

    auto addNote = [&](int step, int lenSteps, int vel)
    {
        const int startTick = step * tps;
        const int lenTick = juce::jmax(6, lenSteps * tps);
        const int pitch = degreeToPitch(currentDegree, currentOct);
        mp.add({ pitch, startTick, lenTick, juce::jlimit(1,127,vel), 1 });
    };

    for (int step = 0; step < totalSteps; )
    {
        if (!pct(placeProbability(step))) { ++step; continue; }

        // Decide between a sustained note or a burst (roll)
        const bool doBurst = (style.equalsIgnoreCase("trap") || style.equalsIgnoreCase("drill")) ? pct(55) : pct(25);

        if (doBurst)
        {
            // Short rapid-fire notes in-place
            int sub = chooseSubTick();
            int durSteps = juce::jlimit(1, 4, 1 + rng.nextInt({ 3 }));
            int lenTickTotal = durSteps * tps;
            int t = step * tps;
            int endT = t + lenTickTotal;

            // small melodic wiggle inside the burst
            int localDeg = currentDegree;
            while (t < endT)
            {
                const int subTick = juce::jmax(3, juce::jmin(sub, endT - t));
                int v = 90 + rng.nextInt({ 25 });
                int pitch = degreeToPitch(localDeg, currentOct);
                mp.add({ pitch, t, subTick, juce::jlimit(1,127,v), 1 });

                // occasionally nudge degree
                if (pct(35)) localDeg += (rng.nextBool() ? +1 : -1);

                t += subTick;
            }

            step += durSteps; // advance by steps we consumed
            // occasional octave hop after bursts
            if (pct(20)) currentOct = juce::jlimit(1, 6, currentOct + (rng.nextBool() ? +1 : -1));
        }
        else
        {
            // Sustained hit, optionally dotted, then melodic move
            int lenSteps = sustainStepsDefault + rng.nextInt({ 2 });  // 1..2 (or 2..3 if default 2)
            if (pct(20)) lenSteps = dottedLen(lenSteps);

            addNote(step, lenSteps, 96 + rng.nextInt({ 20 }));
            step += lenSteps;

            // Move degree for next note
            currentDegree += chooseDegreeDelta();

            // Keep 808 in musical range
            if (pct(10)) currentOct = juce::jlimit(1, 6, currentOct + (rng.nextBool() ? +1 : -1));
        }
    }

    setMelodicPattern(mp);
    // repaint the UI (safe replacement for sendChangeMessage)
    if (auto* ed = getActiveEditor()) ed->repaint();
}


BoomAudioProcessor::BoomAudioProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    apvts(*this, nullptr, "PARAMS", createLayout())
{
}


void BoomAudioProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::MemoryOutputStream mos(dest, true);
    apvts.copyState().writeToStream(mos);
}

void BoomAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto vt = juce::ValueTree::readFromData(data, (size_t)sizeInBytes); vt.isValid())
        apvts.replaceState(vt);
}


// Notify the active editor that data changed (safe replacement for sendChangeMessage)
static inline void notifyEditor(BoomAudioProcessor& ap)
{
    if (auto* ed = ap.getActiveEditor())
        ed->repaint();
}



void BoomAudioProcessor::bumpDrumRowsUp()
{
    auto pat = getDrumPattern();
    if (pat.size() == 0) { notifyPatternChanged(); return; }

    // find max row index present
    int maxRow = 0;
    for (auto& n : pat) maxRow = std::max(maxRow, n.row);

    for (auto& n : pat)
        n.row = (n.row + 1) % (maxRow + 1);

    setDrumPattern(pat);
    notifyPatternChanged();
}

namespace
{
    // ---- Scale tables (semitones from root). Add more as you wish. ----
    // These cover common choices and won’t block your build if a rare name is passed.
    static const std::map<juce::String, std::vector<int>, std::less<>> kScales = {
    {"Major",         {0, 2, 4, 5, 7, 9, 11}},
    {"Natural Minor", {0, 2, 3, 5, 7, 8, 10}},
    {"Harmonic Minor",{0, 2, 3, 5, 7, 8, 11}},
    {"Dorian",        {0, 2, 3, 5, 7, 9, 10}},
    {"Phrygian",      {0, 1, 3, 5, 7, 8, 10}},
    {"Lydian",        {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolydian",    {0, 2, 4, 5, 7, 9, 10}},
    {"Aeolian",       {0, 2, 3, 5, 7, 8, 10}},
    {"Locrian",       {0, 1, 3, 5, 6, 8, 10}},
    {"Locrian Nat6",    {0, 1, 3, 5, 6, 9, 10}},
    {"Ionian #5",     {0, 2, 4, 6, 7, 9, 11}},
    {"Dorian #4",     {0, 2, 3, 6, 7, 9, 10}},
    {"Phrygian Dom",  {0, 1, 3, 5, 7, 9, 10}},
    {"Lydian #2",     {0, 3, 4, 6, 7, 9, 11}},
    {"Super Locrian", {0, 1, 3, 4, 6, 8, 10}},
    {"Dorian b2",     {0, 1, 3, 5, 7, 9, 10}},
    {"Lydian Aug",    {0, 2, 4, 6, 8, 9, 11}},
    {"Lydian Dom",    {0, 2, 4, 6, 7, 9, 10}},
    {"Mixo b6",       {0, 2, 4, 5, 7, 8, 10}},
    {"Locrian #2",    {0, 2, 3, 5, 6, 8, 10}},
    {"8 Tone Spanish", {0, 1, 3, 4, 5, 6, 8, 10}},
    {"Phyrgian Nat3",    {0, 1, 4, 5, 7, 8, 10}},
    {"Blues",         {0, 3, 5, 6, 7, 10}},
    {"Hungarian Min", {0, 3, 5, 8, 11}},
    {"Harmonic Maj(Ethopian)",  {0, 2, 4, 5, 7, 8, 11}},
    {"Dorian b5",     {0, 2, 3, 5, 6, 9, 10}},
    {"Phrygian b4",   {0, 1, 3, 4, 7, 8, 10}},
    {"Lydian b3",     {0, 2, 3, 6, 7, 9, 11}},
    {"Mixolydian b2", {0, 1, 4, 5, 7, 9, 10}},
    {"Lydian Aug2",   {0, 3, 4, 6, 8, 9, 11}},
    {"Locrian bb7",   {0, 1, 3, 5, 6, 8, 9}},
    {"Pentatonic Maj", {0, 2, 5, 7, 8}},
    {"Pentatonic Min", {0, 3, 5, 7, 10}},
    {"Neopolitan Maj", {0, 1, 3, 5, 7, 9, 11}},
    {"Neopolitan Min", {0, 1, 3, 5, 7, 8, 10}},
    {"Spanish Gypsy",  {0, 1, 4, 5, 7, 8, 10}},
    {"Romanian Minor", {0, 2, 3, 6, 7, 9, 10}},
    {"Chromatic",      {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"Bebop Major",  {0, 2, 4, 5, 7, 8, 9, 11}},
    {"Bebop Minor", {0, 2, 3, 5, 7, 8, 9, 10}}, };


    inline int wrap12(int v) { v %= 12; if (v < 0) v += 12; return v; }

    // Snap a MIDI pitch to the nearest pitch in (root + scale). Tie breaks upward.
    inline int snapToScale(int midiPitch, int rootPC, const std::vector<int>& scalePCs)
    {
        const int pc = wrap12(midiPitch);
        // Already in scale? keep it
        for (int s : scalePCs) if (wrap12(rootPC + s) == pc) return midiPitch;

        // Otherwise search nearest distance in semitones up/down (0..6)
        for (int d = 1; d <= 6; ++d)
        {
            // up
            if (std::find(scalePCs.begin(), scalePCs.end(), wrap12(pc - rootPC + d)) != scalePCs.end())
                return midiPitch + d;
            // down
            if (std::find(scalePCs.begin(), scalePCs.end(), wrap12(pc - rootPC - d)) != scalePCs.end())
                return midiPitch - d;
        }
        return midiPitch; // fallback (shouldn’t happen with Chromatic present)
    }
}

void BoomAudioProcessor::transposeMelodic(int semitones, const juce::String& /*newKey*/,
    const juce::String& /*newScale*/, int octaveOffset)
{
    auto pat = getMelodicPattern();
    for (auto& n : pat)
        n.pitch = juce::jlimit(0, 127, n.pitch + semitones + 12 * octaveOffset);

    setMelodicPattern(pat);
    notifyPatternChanged();
}

void BoomAudioProcessor::bumppitTranspose(int targetKeyIndex, const juce::String& scaleName, int octaveDelta)
{
    // Only act if 808 or Bass engine is selected
    auto eng = getEngineSafe();
    // Melodic-only: proceed for any non-Drums engine (covers 808 and Bass without naming them)
    if (eng == boom::Engine::Drums)
        return;

    // Guard inputs
    targetKeyIndex = juce::jlimit(0, 11, targetKeyIndex);
    octaveDelta = juce::jlimit(-4, 4, octaveDelta);

    // Find scale intervals; default to Chromatic if unknown
    const std::vector<int>* scale = nullptr;
    auto it = kScales.find(scaleName.trim());
    if (it != kScales.end()) scale = &it->second;
    if (scale == nullptr)   scale = &kScales.at("Chromatic");

    // Transpose every melodic note to (root + scale), keep rhythm/length/velocity.
    // We’ll do: (a) octave shift, (b) snap to target scale relative to chosen key.
    auto mp = getMelodicPattern();

    for (auto& n : mp)
    {
        // 1) octave
        int pitch = n.pitch + (octaveDelta * 12);

        // 2) snap to scale rooted at targetKeyIndex
        pitch = snapToScale(pitch, targetKeyIndex, *scale);

        // Keep safe MIDI range
        n.pitch = juce::jlimit(0, 127, pitch);
    }

    setMelodicPattern(mp);
    notifyEditor(*this);
}

void BoomAudioProcessor::sendUIChange()
{
    // if you’re already an AudioProcessorValueTreeState::Listener or using ChangeBroadcaster,
    // this can be your change call. Otherwise, have the editor poll. For now:
    // (No-op is safe; editor will pull on timer. Keep method so calls compile.)
}

void BoomAudioProcessor::notifyPatternChanged()
{
    sendUIChange();
}

void BoomAudioProcessor::generate808(int bars)
{
    auto pat = getMelodicPattern();
    pat.clear();

    const int total16 = q16(bars);
    const int bar16 = 16;

    // choose a root register around C2–C3 to stay in 808 territory
    int baseMidi = 36 + irand(0, 4) * 2; // 36..44 even numbers

    // choose “activity” density and approach-note flavor
    const int hitPct = 55 + irand(-10, 15); // how often to place notes
    const int approachPct = 30 + irand(-5, 15);

    for (int i = 0; i < total16; ++i)
    {
        const bool strong = (i % bar16 == 0) || (i % 4 == 0); // bar downbeat or quarter
        if (strong || chance(hitPct))
        {
            int len16 = strong ? (chance(40) ? 4 : 2) : (chance(30) ? 2 : 1);
            int vel = strong ? irand(95, 120) : irand(80, 105);
            int pitch = baseMidi;

            // occasional scale steps (use your key/scale later: here we just nudge by 0/2/3/5)
            if (!strong && chance(45))
            {
                static const int steps[] = { 0, 2, 3, 5, 7, 9 };
                pitch += steps[irand(0, (int)std::size(steps) - 1)];
            }

            // approach notes (grace 16th before a strong)
            if (strong && i > 0 && chance(approachPct))
            {
                const int apPitch = pitch - (chance(50) ? 1 : 2);
                pat.add({ apPitch, toTick16(i - 1), toTick16(1), irand(60,85), 1 });
            }

            // main hit
            pat.add({ pitch, toTick16(i), toTick16(juce::jmax(1, len16)), vel, 1 });

            i += (len16 - 1); // skip through duration
        }
    }

    setMelodicPattern(pat);
    notifyPatternChanged();
}

void BoomAudioProcessor::generateBass(int bars)
{
    // For now, use same generator as 808 (different velocity range and fewer long holds)
    auto pat = getMelodicPattern();
    pat.clear();

    const int total16 = q16(bars);
    int baseMidi = 40 + irand(0, 6); // slightly higher than 808

    const int hitPct = 50 + irand(-10, 10);

    for (int i = 0; i < total16; ++i)
    {
        const bool strong = (i % 4 == 0);
        if (strong || chance(hitPct))
        {
            int len16 = strong ? (chance(35) ? 3 : 2) : 1;
            int vel = strong ? irand(90, 110) : irand(75, 100);
            int pitch = baseMidi;

            if (!strong && chance(35))
            {
                static const int steps[] = { -2, 0, 2, 3, 5 };
                pitch += steps[irand(0, (int)std::size(steps) - 1)];
            }

            pat.add({ pitch, toTick16(i), toTick16(juce::jmax(1, len16)), vel, 1 });
            i += (len16 - 1);
        }
    }

    setMelodicPattern(pat);
    notifyPatternChanged();
}

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

double BoomAudioProcessor::getHostBpm() const noexcept
{
    return lastHostBpm;
}



void BoomAudioProcessor::generateDrumRolls(const juce::String& style, int bars)
{
    auto pat = getDrumPattern();
    pat.clear();

    const int total16 = q16(bars);

    auto add = [&](int row, int step16, int len16, int vel)
    {
        pat.add({ 0, row, toTick16(step16), toTick16(len16), vel });
    };

    // We’ll target the snare row = 1 for “rolls”; add supportive hats row=2
    int rollRow = 1, hatRow = 2;

    if (style.compareIgnoreCase("trap") == 0)
    {
        // dense cresc/dim + 32nd trills sprinkled
        for (int b = 0; b < bars; ++b)
        {
            const int start = b * 16;
            for (int i = 0; i < 16; ++i)
            {
                if (i % 2 == 0 || chance(35)) add(hatRow, start + i, 1, irand(60, 85));
                if (chance(25))          add(rollRow, start + i, 1, irand(80, 110));
                if (chance(10) && i < 15)  add(rollRow, start + i + 1, 1, irand(70, 95)); // 32nd-feel echo
            }
        }
    }
    else if (style.compareIgnoreCase("drill") == 0)
    {
        // triplet-y rolls & late snares (beat 4 of bar 2/4 feel)
        for (int b = 0; b < bars; ++b)
        {
            const int start = b * 16;
            // strong “late” accent near beat 4 (index 12)
            add(rollRow, start + 12, 1, irand(100, 120));
            for (int t = 0; t < 6; ++t) // triplet grid approx (16th-triplet-ish)
            {
                int p = start + (t * 2);
                if (p < start + 16 && chance(60)) add(rollRow, p, 1, irand(75, 100));
            }
            for (int i = 0; i < 16; ++i)
                if (i % 2 == 0 || chance(20)) add(hatRow, start + i, 1, irand(60, 85));
        }
    }
    else if (style.compareIgnoreCase("edm") == 0)
    {
        // classic build: 1/4 -> 1/8 -> 1/16 -> 1/32 across bars
        int seg = bars * 16;
        int a = seg / 4, b = seg / 4, c = seg / 4, d = seg - (a + b + c);
        auto stamp = [&](int every16, int begin, int count)
        {
            for (int i = 0; i < count; i++)
            {
                int base = begin + i * every16;
                if (base < total16) add(rollRow, base, 1, irand(85, 115));
            }
        };
        stamp(4, 0, a / 4);              // quarters
        stamp(2, a, b / 2);              // eighths
        stamp(1, a + b, c);              // sixteenths
        for (int i = 0; i < d; ++i)        // 32nd-ish final rush: double up
        {
            add(rollRow, a + b + c + i, 1, irand(95, 120));
            if (chance(70) && a + b + c + i + 1 < total16) add(rollRow, a + b + c + i + 1, 1, irand(85, 110));
        }
    }
    else if (style.compareIgnoreCase("wxstie") == 0)
    {
        // sparser + choppy accents
        for (int b = 0; b < bars; ++b)
        {
            int start = b * 16;
            for (int i = 0; i < 16; ++i)
                if (chance(30)) add(rollRow, start + i, 1, irand(80, 110));
            for (int q = 0; q < 4; ++q)
                if (chance(60)) add(hatRow, start + q * 4, 1, irand(70, 95));
        }
    }
    else // pop / r&b / rock / reggaeton – tasteful single-bar fills
    {
        for (int b = 0; b < bars; ++b)
        {
            int start = b * 16;
            for (int i = 0; i < 16; ++i)
                if (chance(25)) add(rollRow, start + i, 1, irand(80, 105));
            // end-bar push
            add(rollRow, start + 14, 1, irand(95, 115));
            add(rollRow, start + 15, 1, irand(95, 115));
        }
    }

    setDrumPattern(pat);
    notifyPatternChanged();
}

void BoomAudioProcessor::generateDrums(int bars)
{
    auto pat = getDrumPattern();
    pat.clear();

    const int total16 = q16(bars);

    // row mapping: 0=Kick, 1=Snare, 2=Hat (match your DrumGrid row order)
    auto add = [&](int row, int step16, int len16, int vel)
    {
        pat.add({ 0 /*chan*/, row, toTick16(step16), toTick16(len16), vel });
    };

    // backbeat snares & variations
    for (int b = 0; b < bars; ++b)
    {
        int s = b * 16 + 8; // beat 3
        add(1, s, 1, irand(95, 115));
        if (chance(25)) add(1, s - 1, 1, irand(70, 90)); // flam-ish before
        if (chance(20)) add(1, s + 1, 1, irand(70, 90)); // after-beat
    }

    // kicks: downbeats + syncopation
    for (int i = 0; i < total16; i += 4)
    {
        if (i % 16 == 0 || chance(55)) add(0, i, 1, irand(95, 120));
        if (chance(35)) add(0, i + 2, 1, irand(80, 100));
        if (chance(25) && i + 3 < total16) add(0, i + 3, 1, irand(75, 95));
    }

    // hats: steady with occasional 32nd tickle
    for (int i = 0; i < total16; ++i)
    {
        if (i % 2 == 0 || chance(20)) add(2, i, 1, irand(70, 95)); // 8ths + random fills
        if (chance(12) && i + 1 < total16) add(2, i + 1, 1, irand(60, 85)); // light 16th echo
    }

    setDrumPattern(pat);
    notifyPatternChanged();
}


void BoomAudioProcessor::flipMelodic(int densityPct, int addPct, int removePct)
{
    auto pat = getMelodicPattern();

    // remove some
    for (int i = pat.size() - 1; i >= 0; --i)
        if (chance(removePct)) pat.remove(i);

    // add small grace/echo notes around existing
    for (int i = 0; i < pat.size(); ++i)
    {
        if (chance(addPct))
        {
            auto n = pat[i];
            const bool before = chance(50);
            const int  off16 = before ? -1 : 1;
            int start16 = (n.startTick / (PPQ / 4)) + off16;
            if (start16 >= 0)
                pat.add({ n.pitch + (chance(50) ? 0 : (chance(50) ? 1 : -1)),
                          toTick16(start16), toTick16(1), juce::jlimit(40,120, n.velocity - 10), n.channel });
        }
    }

    setMelodicPattern(pat);
    notifyPatternChanged();
}

void BoomAudioProcessor::flipDrums(int densityPct, int addPct, int removePct)
{
    auto pat = getDrumPattern();

    // remove
    for (int i = pat.size() - 1; i >= 0; --i)
        if (chance(removePct)) pat.remove(i);

    // add ghost notes (mostly hats / occasional kick)
    const int total16 = q16(4); // assume up to 4 bars; safe to overfill
    for (int i = 0; i < total16; ++i)
    {
        if (chance(addPct))
        {
            int row = chance(70) ? 2 : 0; // mostly hats
            int vel = row == 2 ? irand(50, 80) : irand(70, 95);
            pat.add({ 0, row, toTick16(i), toTick16(1), vel });
        }
    }

    setDrumPattern(pat);
    notifyPatternChanged();
}


void BoomAudioProcessor::generateRolls(const juce::String& styleName, int bars)
{
    // Rolls window is DRUMS-ONLY. If engine != Drums, do nothing.
    auto eng = getEngineSafe();
    if (eng != boom::Engine::Drums)
        return;

    // ---- Setup / constants ----
    const int stepsPerBar = 16;   // 1 bar = 16 grid steps (1/16 each)
    const int tps = 24;   // ticks per step (your grid uses 24 = 1/16 note)
    bars = juce::jlimit(1, 8, bars);

    const auto& R = boom::rulesForStyle(styleName);
    juce::Random rng;

    // If you have a "seed" param, use it for repeatability (optional)
    if (auto* ps = apvts.getParameter("seed"))
        rng = juce::Random((int)ps->getValue());

    // We’ll build a fresh roll pattern; if you prefer to APPEND to existing,
    // remove this clear(). For the Rolls window, "generate" implies new content.
    drumPattern.clear();

    // ---- Quick helpers ----
    auto pct = [&](int prob) -> bool { return rng.nextInt({ 100 }) < juce::jlimit(0, 100, prob); };

    auto addHit = [&](int row, int step, int lenSteps, int vel)
    {
        if (step < 0) step = 0;
        const int startTick = step * tps;
        const int lenTick = juce::jmax(6, lenSteps * tps);
        drumPattern.add({ 0, row, startTick, lenTick, juce::jlimit(1,127,vel) });
    };

    auto addRollTicks = [&](int row, int startStep, int durSteps, int subTick, int velBase, bool rampUp)
    {
        const int startTick = startStep * tps;
        const int endTick = startTick + juce::jmax(6, durSteps * tps);
        int t = startTick;
        int i = 0;
        while (t < endTick)
        {
            const int sub = juce::jmax(3, juce::jmin(subTick, endTick - t));
            int v = rampUp ? juce::jlimit(30, 127, velBase + i * 10)
                : juce::jlimit(30, 127, velBase - i * 8);
            drumPattern.add({ 0, row, t, sub, v });
            t += sub;
            ++i;
        }
    };

    auto emitGrid = [&](int row, int fromStep, int toStep, int everySteps, int vel)
    {
        for (int s = fromStep; s < toStep; s += juce::jmax(1, everySteps))
            addHit(row, s, 1, vel);
    };

    // Choose a roll subdivision (in ticks) from style tendencies
    auto chooseRollTick = [&]() -> int
    {
        if (!R.hatRollRates.isEmpty())
            return R.hatRollRates[(int)rng.nextInt({ R.hatRollRates.size() })];

        // Fallback: 12 = 1/32, 8 ≈ 1/16T, 24 = 1/16
        const int pool[] = { 12, 8, 24, 6, 4 };
        return pool[rng.nextInt({ (int)std::size(pool) })];
    };

    // EDM “build up” plan generator: returns per-bar base grid size (steps)
    auto edmBuildPlan = [&](int totalBars) -> juce::Array<int>
    {
        // Gradually increase density: 4 steps (quarters), 2 (8ths), 1 (16ths), then tick rolls
        juce::Array<int> plan;
        if (totalBars == 1) { plan.add(2); } // compact build if 1 bar
        else if (totalBars == 2) { plan.add(4); plan.add(1); }
        else if (totalBars == 3) { plan.add(4); plan.add(2); plan.add(1); }
        else
        {
            // 4+ bars: Q / 8th / 16th / 32nd-ish
            plan.add(4); plan.add(2); plan.add(1);
            for (int i = 3; i < totalBars; ++i) plan.add(1);
        }
        return plan;
    };

    // Row mapping we’re using: 0=Kick, 1=Snare/Clap, 2=Closed Hat, 3=Tom/Perc
    // If your rows differ, tell me your exact order and I’ll align these indices.
    auto doTrapBar = [&](int base)
    {
        // Base grids vary: 8ths, 16ths, or mixed
        const bool use8ths = pct(40);
        const bool use16ths = pct(60);
        if (use8ths)  emitGrid(2, base + 0, base + 16, 2, 74);
        if (use16ths) emitGrid(2, base + (rng.nextBool() ? 0 : 1), base + 16, 2, 66); // overlays/gaps

        // Add several fast rolls at mid and end — lots of variety
        for (int r = 0, rolls = rng.nextInt({ 2,5 }); r < rolls; ++r)
        {
            const int pos = base + rng.nextInt({ 16 });             // anywhere in bar
            const int dur = juce::jlimit(1, 4, 1 + rng.nextInt({ 4 })); // 1..4 steps
            int sub = chooseRollTick(); // 24/12/8/6/4
            // Encourage very fast bursts sometimes
            if (pct(35)) sub = (rng.nextBool() ? 12 : 8);
            addRollTicks(2, pos, dur, sub, 72, rng.nextBool());   // hat rolls
        }

        // Ghost notes around snare backbeats
        for (int sIdx : R.snareBeats)
        {
            const int s = base + juce::jlimit(0, 15, sIdx);
            addHit(1, s, 1, 110);
            if (pct(30)) addHit(1, juce::jmax(base, s - 1), 1, 55);  // pre-ghost
            if (pct(30)) addHit(1, juce::jmin(base + 15, s + 1), 1, 58); // post-ghost
        }

        // Random kick pickups/offbeats
        if (pct(25)) addHit(0, base + rng.nextInt({ 16 }), 1, 105);
        if (pct(25)) addHit(0, base + rng.nextInt({ 16 }), 1, 100);
    };

    auto doDrillBar = [&](int base)
    {
        // Choppy hats with triplet feel dominance
        // Start from sparse 8ths, then swap regions to triplet ticks
        emitGrid(2, base + 0, base + 16, 2, 72);
        // Clear a few holes for choppiness (we “overwrite” by simply not adding into those slots)
        for (int holes = 0, n = rng.nextInt({ 2,6 }); holes < n; ++holes)
        {
            int s = base + rng.nextInt({ 16 });
            // no-op means we just don’t add anything at 's' this bar slot (we’re generating fresh anyway)
            // (kept for readability)
            (void)s;
        }

        // Triplet-heavy rolls (prefer 12/8/4 ticks)
        for (int r = 0, rolls = rng.nextInt({ 2,5 }); r < rolls; ++r)
        {
            const int pos = base + rng.nextInt({ 16 });
            const int dur = juce::jlimit(1, 4, 1 + rng.nextInt({ 4 }));
            int sub = (rng.nextInt({ 100 }) < 70) ? (rng.nextBool() ? 12 : 8) : 4;
            addRollTicks(2, pos, dur, sub, 70, rng.nextBool());
        }

        // Snare placement: sometimes emphasize late hit at step 15 (end of bar)
        for (int sIdx : R.snareBeats)
            addHit(1, base + juce::jlimit(0, 15, sIdx), 1, 108);
        if (pct(55)) addHit(1, base + 15, 1, 95);

        // Kicks choppy / offbeats
        if (pct(40)) addHit(0, base + rng.nextInt({ 16 }), 1, 104);
        if (pct(35)) addHit(0, base + rng.nextInt({ 16 }), 1, 98);
    };

    auto doWxstieBar = [&](int base)
    {
        // Sparser hats; intermittent bursts; trap-influenced but aired out
        if (pct(60)) emitGrid(2, base + (rng.nextBool() ? 0 : 1), base + 16, 4, 72); // quarters or offbeat 8ths
        if (pct(40)) emitGrid(2, base + 2, base + 16, 4, 68); // offbeats at 2,6,10,14

        // Occasional quick burst
        if (pct(50))
        {
            const int pos = base + rng.nextInt({ 16 });
            addRollTicks(2, pos, juce::jlimit(1, 3, 1 + rng.nextInt({ 3 })), (rng.nextBool() ? 12 : 24), 70, rng.nextBool());
        }

        // Flexible snare placement around 2 and 4
        for (int sIdx : R.snareBeats)
        {
            int s = base + juce::jlimit(0, 15, sIdx);
            if (pct(30)) s += (rng.nextBool() ? -1 : +1); // drift one step
            s = juce::jlimit(base, base + 15, s);
            addHit(1, s, 1, 108);
        }

        // Off-beat adventurous kicks
        if (pct(45)) addHit(0, base + rng.nextInt({ 16 }), 1, 104);
        if (pct(30)) addHit(0, base + rng.nextInt({ 16 }), 1, 98);
    };

    auto doEDMBar = [&](int base, int gridStep)
    {
        // four-on-the-floor feel + offbeat open hats + build density
        // Kicks (4x)
        emitGrid(0, base + 0, base + 16, 4, 118); // 0,4,8,12
        // Offbeat hats (2,6,10,14) baseline — we’ll enrich with rolls later in the build
        emitGrid(2, base + 2, base + 16, 4, 78);

        // Snare/clap on 2 and 4
        addHit(1, base + 4, 1, 112);
        addHit(1, base + 12, 1, 112);

        // Base hat grid according to build stage
        emitGrid(2, base + 0, base + 16, juce::jmax(1, gridStep), 74);

        // End-of-bar ramp roll (increasing speed toward the drop)
        const int sub = chooseRollTick(); // will naturally pick faster values sometimes
        addRollTicks(2, base + 14, 2, sub, 76, /*rampUp*/ true);
    };

    auto doReggaetonBar = [&](int base)
    {
        // Dembow backbone (simplified skeleton across hats/percs):
        // Kick-ish accents at 0 and 8; clap/snare at 4 and 12; gentle hats
        addHit(0, base + 0, 1, 108);
        addHit(0, base + 8, 1, 100);
        addHit(1, base + 4, 1, 110);
        addHit(1, base + 12, 1, 110);

        // Closed hats: moderate density, slight offbeats
        emitGrid(2, base + (rng.nextBool() ? 1 : 0), base + 16, 2, 72);

        // Short tasteful rolls (snare or tom) near bar ends
        if (pct(35))
            addRollTicks(rng.nextBool() ? 1 : 3, base + 12, 4, (rng.nextBool() ? 12 : 24), 74, rng.nextBool());
    };

    auto doRnbBar = [&](int base)
    {
        // Laid back: hats with gaps, ghosted snares
        if (pct(60)) emitGrid(2, base + 0, base + 16, 2, 68);
        // remove some mid hats by just not adding any extra (we generate from scratch)

        // Snare on 2/4 with ghost
        addHit(1, base + 4, 1, 108);
        addHit(1, base + 12, 1, 108);
        if (pct(40)) addHit(1, base + 3, 1, 58);
        if (pct(40)) addHit(1, base + 5, 1, 58);

        // Gentle short buzz near end
        if (pct(30)) addRollTicks(1, base + 14, 2, (rng.nextBool() ? 12 : 24), 70, /*rampUp*/ false);
    };

    auto doPopBar = [&](int base)
    {
        // Clean backbeat, light hat grid, modest fills
        emitGrid(2, base + 0, base + 16, 2, 70); // 8ths
        addHit(1, base + 4, 1, 110);
        addHit(1, base + 12, 1, 110);
        // Occasional end-of-bar snare buzz
        if (pct(25)) addRollTicks(1, base + 14, 2, (rng.nextBool() ? 12 : 24), 72, /*rampUp*/ true);
        // Kick anchors
        addHit(0, base + 0, 1, 110);
        if (pct(35)) addHit(0, base + 8, 1, 100);
    };

    auto doRockBar = [&](int base)
    {
        // Straight hats, backbeat snare, tom fills
        emitGrid(2, base + 0, base + 16, 2, 70); // 8ths hats
        addHit(1, base + 4, 1, 112);
        addHit(1, base + 12, 1, 112);
        addHit(0, base + 0, 1, 112);
        if (pct(30))
        {
            // Tom fill at end
            addHit(3, base + 12, 1, 96);
            addHit(3, base + 13, 1, 94);
            addHit(3, base + 14, 1, 92);
            addHit(3, base + 15, 1, 90);
        }
    };

    // ---- Build bars according to the selected style ----
    if (R.name.equalsIgnoreCase("trap"))
    {
        for (int b = 0; b < bars; ++b) doTrapBar(b * stepsPerBar);
    }
    else if (R.name.equalsIgnoreCase("drill"))
    {
        for (int b = 0; b < bars; ++b) doDrillBar(b * stepsPerBar);
    }
    else if (R.name.equalsIgnoreCase("wxstie"))
    {
        for (int b = 0; b < bars; ++b) doWxstieBar(b * stepsPerBar);
    }
    else if (R.name.equalsIgnoreCase("edm"))
    {
        auto plan = edmBuildPlan(bars); // per-bar base grid size
        for (int b = 0; b < bars; ++b) doEDMBar(b * stepsPerBar, plan[juce::jmin(b, plan.size() - 1)]);
    }
    else if (R.name.equalsIgnoreCase("reggaeton"))
    {
        for (int b = 0; b < bars; ++b) doReggaetonBar(b * stepsPerBar);
    }
    else if (R.name.equalsIgnoreCase("r&b") || R.name.equalsIgnoreCase("rnb"))
    {
        for (int b = 0; b < bars; ++b) doRnbBar(b * stepsPerBar);
    }
    else if (R.name.equalsIgnoreCase("pop"))
    {
        for (int b = 0; b < bars; ++b) doPopBar(b * stepsPerBar);
    }
    else if (R.name.equalsIgnoreCase("rock"))
    {
        for (int b = 0; b < bars; ++b) doRockBar(b * stepsPerBar);
    }
    else
    {
        // Unknown style fallback = pop-ish
        for (int b = 0; b < bars; ++b) doPopBar(b * stepsPerBar);
    }

    notifyEditor(*this);
}

// --- Timer tick: refresh the BPM label (and anything else lightweight) ---
void BoomAudioProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    // Store the real sample rate for capture/transcription and size the ring buffer.
    lastSampleRate = (sampleRate > 0.0 ? sampleRate : 44100.0);
    ensureCaptureCapacitySeconds(65.0);
    captureBuffer.clear(); // ~60s cap + a little margin
    captureWritePos = 0;
    captureLengthSamples = 0;
    isCapturing.store(false);
}

// IMPORTANT: This is where we append input audio to the capture ring buffer when recording.
// We keep the audio buffer silent because BOOM is a MIDI generator/transformer.
void BoomAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{

    if (auto* ph = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo info;
        if (ph->getCurrentPosition(info))
            if (info.bpm > 0.0)
                lastHostBpm = info.bpm;
    }

    juce::ignoreUnused(midi);

    const int numInCh = getTotalNumInputChannels();
    const int numSmps = buffer.getNumSamples();

    if (isCapturing.load(std::memory_order_acquire) && captureBuffer.getNumSamples() > 0)
    {
        // Mix input to mono
        const float* inL = numInCh > 0 ? buffer.getReadPointer(0) : nullptr;
        const float* inR = numInCh > 1 ? buffer.getReadPointer(1) : nullptr;

        float* dstMono = captureBuffer.getWritePointer(0);
        const int capLen = captureBuffer.getNumSamples();

        for (int i = 0; i < numSmps; ++i)
        {
            float s = 0.0f;
            if (inL) s += *inL++;
            if (inR) s += *inR++;
            if (numInCh >= 2) s *= 0.5f; // average L+R when stereo

            // Write into ring buffer
            dstMono[captureWritePos] = s;

            captureWritePos = (captureWritePos + 1) % capLen;

            // Track how many valid samples we have (caps at buffer size)
            if (captureLengthSamples < capLen)
                ++captureLengthSamples;
        }
    }

    // ... your existing audio generation/output here ...
}

void BoomAudioProcessor::releaseResources()
{
    // Nothing heavy to free, but make sure capture is stopped and pointers reset.
    isCapturing.store(false);
    captureWritePos = 0;
    captureLengthSamples = 0;
}

void BoomAudioProcessor::ensureCaptureCapacitySeconds(double seconds)
{
    const int needed = (int)std::ceil(seconds * lastSampleRate);
    if (captureBuffer.getNumSamples() < needed)
        captureBuffer.setSize(1, needed, false, true, true);
}

void BoomAudioProcessor::appendCaptureFrom(const juce::AudioBuffer<float>& in)
{
    if (in.getNumSamples() <= 0) return;

    // mix all channels to mono temp
    juce::AudioBuffer<float> mono(1, in.getNumSamples());
    mono.clear();
    const int chans = in.getNumChannels();
    for (int ch = 0; ch < chans; ++ch)
        mono.addFrom(0, 0, in, ch, 0, in.getNumSamples(), 1.0f / juce::jmax(1, chans));

    // write into ring buffer (linear until full; stop if full ~ 60s)
    const int free = captureBuffer.getNumSamples() - captureWritePos;
    const int n = juce::jmin(free, mono.getNumSamples());
    if (n > 0)
        captureBuffer.copyFrom(0, captureWritePos, mono, 0, 0, n);

    captureWritePos += n;
    captureLengthSamples = juce::jmax(captureLengthSamples, captureWritePos);

    // once full, stop capturing (hard stop at ~ 60s)
    if (captureWritePos >= captureBuffer.getNumSamples())
        isCapturing.store(false);
}

BoomAudioProcessor::Pattern BoomAudioProcessor::transcribeAudioToDrums(const float* mono, int N, int bars, int bpm) const
{
    Pattern pat;
    if (mono == nullptr || N <= 0) return pat;

    const int fs = (int)lastSampleRate;
    const int hop = 512;
    const int win = 1024;
    const float preEmph = 0.97f;

    const int stepsPerBar = 16;
    const int totalSteps = juce::jmax(1, bars) * stepsPerBar;
    const double secPerBeat = 60.0 / juce::jlimit(40, 240, bpm);
    const double secPerStep = secPerBeat / 4.0; // 16th
    const int ticksPerStep = 24;

    auto bandEnergy = [&](int start, int end) -> std::vector<float>
    {
        std::vector<float> env;
        env.reserve(N / hop + 8);

        for (int i = 0; i + win <= N; i += hop)
        {
            float e = 0.f;
            for (int n = 0; n < win; ++n)
            {
                float x = mono[i + n] - preEmph * (n > 0 ? mono[i + n - 1] : 0.f);
                float w = 1.f;
                if (start >= 200 && end <= 2000) w = 0.7f;
                if (start >= 5000)               w = 0.5f;
                e += std::abs(x) * w;
            }
            e /= (float)win;
            env.push_back(e);
        }

        float mx = 1e-6f;
        for (auto v : env) mx = juce::jmax(mx, v);
        for (auto& v : env) v /= mx;

        return env;
    };

    auto low = bandEnergy(20, 200);
    auto mid = bandEnergy(200, 2000);
    auto high = bandEnergy(5000, 20000);

    auto detectPeaks = [&](const std::vector<float>& e, float thr, int minGapFrames)
    {
        std::vector<int> frames;
        int last = -minGapFrames;
        for (int i = 1; i + 1 < (int)e.size(); ++i)
        {
            if (e[i] > thr && e[i] > e[i - 1] && e[i] >= e[i + 1] && (i - last) >= minGapFrames)
            {
                frames.push_back(i);
                last = i;
            }
        }
        return frames;
    };

    auto kFrames = detectPeaks(low, 0.35f, (int)std::round(0.040 * fs / hop));
    auto sFrames = detectPeaks(mid, 0.30f, (int)std::round(0.050 * fs / hop));
    auto hFrames = detectPeaks(high, 0.28f, (int)std::round(0.030 * fs / hop));

    auto frameToTick = [&](int frame) -> int
    {
        double t = (double)(frame * hop) / fs;
        int step = (int)std::round(t / secPerStep);
        step = (step % totalSteps + totalSteps) % totalSteps;
        return step * ticksPerStep;
    };

    auto addHits = [&](const std::vector<int>& frames, int row, int vel)
    {
        for (auto f : frames)
            pat.add({ 0, row, frameToTick(f), 12, vel });
    };

    // rows: 0 kick, 1 snare, 2 hat
    addHits(kFrames, 0, 115);
    addHits(sFrames, 1, 108);
    addHits(hFrames, 2, 80);

    return pat;
}

void BoomAudioProcessor::aiAnalyzeCapturedToDrums(int bars, int bpm)
{
    if (captureLengthSamples <= 0) return;
    const int N = juce::jmin(captureLengthSamples, captureBuffer.getNumSamples());
    auto* mono = captureBuffer.getReadPointer(0);
    auto pat = transcribeAudioToDrums(mono, N, bars, bpm);
    setDrumPattern(pat);
}

void BoomAudioProcessor::aiStartCapture(CaptureSource src)
{
    // Stop any previous capture first
    aiStopCapture();

    // Remember what we’re capturing (Loopback or Microphone — your enum has only those two)
    currentCapture = src;

    // Make sure buffer exists for ~60 seconds (we’ll use 65s margin)
    lastSampleRate = getSampleRate() > 0.0 ? getSampleRate() : lastSampleRate;
    ensureCaptureCapacitySeconds(65.0);
    captureBuffer.clear();
    captureWritePos = 0;
    captureLengthSamples = 0;

    // Mark as capturing
    isCapturing.store(true, std::memory_order_release);

    if (auto* ed = getActiveEditor()) ed->repaint();
}

void BoomAudioProcessor::aiStopCapture()
{
    if (!isCapturing.load(std::memory_order_acquire))
        return;

    isCapturing.store(false, std::memory_order_release);
    if (auto* ed = getActiveEditor()) ed->repaint();
}

void BoomAudioProcessor::aiSlapsmithExpand(int bars)
{
    Pattern src = getDrumPattern(); // mini grid should have already set this
    Pattern out;

    const int stepsPerBar = 16, tps = 24, total = juce::jmax(1, bars) * stepsPerBar;

    // copy seed
    for (auto n : src) out.add(n);

    // add hats on 8ths, add ghost hats on off-8ths
    for (int s = 0; s < total; ++s)
    {
        int tick = s * tps;
        bool hasHat = false;
        for (auto& n : src) if (n.row == 2 && n.startTick == tick) { hasHat = true; break; }
        if (!hasHat && (s % 2 == 0)) out.add({ 0, 2, tick, 12, 78 });
        if (s % 2 == 1) out.add({ 0, 2, tick, 8, 58 }); // ghost
    }

    // add snare rolls into 4 and 12
    for (int s : { 4, 12 })
    {
        int t = s * tps;
        out.add({ 0, 1, t - 6, 8, 72 });
        out.add({ 0, 1, t,   24,110 });
        out.add({ 0, 1, t + 6, 8, 72 });
    }

    // occasional kick pickups before downbeats
    for (int b = 0; b < bars; ++b)
    {
        int t = b * stepsPerBar * tps;
        out.add({ 0, 0, t - 3 * tps, 12, 95 }); // & of 1
        out.add({ 0, 0, t,           24, 118 });
        out.add({ 0, 0, t + 8 * tps, 24, 112 });
    }

    setDrumPattern(out);
}

void BoomAudioProcessor::aiStyleBlendDrums(juce::String styleA, juce::String styleB, int bars)
{
    // Map style names to seeds/weights
    auto styleToSeed = [](const juce::String& s) -> int
    {
        return juce::String(s).hashCode(); // works across JUCE versions
    };

    const int seedA = styleToSeed(styleA);
    const int seedB = styleToSeed(styleB);
    std::mt19937 rngA((unsigned)seedA), rngB((unsigned)seedB);

    Pattern out;
    const int stepsPerBar = 16, tps = 24, total = juce::jmax(1, bars) * stepsPerBar;

    // A side: straight grooves
    for (int s = 0; s < total; ++s)
    {
        int t = s * tps;
        if (s % stepsPerBar == 0 || s % stepsPerBar == 8) out.add({ 0,0,t,24,118 }); // kicks
        if (s % stepsPerBar == 4 || s % stepsPerBar == 12) out.add({ 0,1,t,24,112 }); // snares
        if (s % 2 == 0) out.add({ 0,2,t,12,78 }); // hats
    }

    // B side: syncopations and tom/percs
    for (int s = 0; s < total; ++s)
    {
        int t = s * tps;
        if (s % 4 == 3) out.add({ 0,0,t,12,95 });   // pickup kicks
        if (s % 8 == 6) out.add({ 0,3,t,12,80 });   // perc
        if (s % 2 == 1) out.add({ 0,2,t,8,60 });    // ghost hats
    }

    setDrumPattern(out);
}

