 #include "PluginEditor.h"
using boomui::loadSkin;
using boomui::setButtonImages;
using boomui::setToggleImages;
#include "EngineDefs.h"
#include "Theme.h"

// Local helper that does NOT touch headers; safe and scoped to this .cpp only.

class BpmPoller : private juce::Timer
{
public:
    BpmPoller(BoomAudioProcessor& p, std::function<void(double)> uiUpdate)
        : proc(p), onUiUpdate(std::move(uiUpdate))
    {
        startTimerHz(8);
    }
    ~BpmPoller() override { stopTimer(); }

private:
    void timerCallback() override
    {
        const double bpm = proc.getHostBpm();
        juce::MessageManager::callAsync([fn = onUiUpdate, bpm]
            {
                if (fn) fn(bpm);
            });
    }

    BoomAudioProcessor& proc;
    std::function<void(double)>    onUiUpdate;
};

// Now that BpmPoller is complete, define the editor dtor here:
BoomAudioProcessorEditor::~BoomAudioProcessorEditor()
{
    bpmPoller.reset();
}

namespace {
    void updateEngineButtonSkins(boom::Engine e, juce::ImageButton& btn808, juce::ImageButton& btnBass, juce::ImageButton& btnDrums)
    {
        boomui::setButtonImages(btn808, "808Btn");
        boomui::setButtonImages(btnBass, "bassBtn");
        boomui::setButtonImages(btnDrums, "drumsBtn");

        if (e == boom::Engine::e808)  boomui::setButtonImagesSelected(btn808, "808Btn");
        if (e == boom::Engine::Bass)  boomui::setButtonImagesSelected(btnBass, "bassBtn");
        if (e == boom::Engine::Drums) boomui::setButtonImagesSelected(btnDrums, "drumsBtn");
    }
}

// ======= small helper =======
int BoomAudioProcessorEditor::barsFromBox(const juce::ComboBox& b) { return b.getSelectedId() == 2 ? 8 : 4; }

// ================== Editor ==================
BoomAudioProcessorEditor::BoomAudioProcessorEditor(BoomAudioProcessor& p)
    : AudioProcessorEditor(&p), proc(p),
    drumGrid(p), pianoRoll(p)
{
    setLookAndFeel(&boomui::LNF());
    setResizable(true, true);
    setSize(783, 714);



    // Engine label + buttons
    logoImg.setImage(loadSkin("logo.png")); addAndMakeVisible(logoImg);
    lockToBpmLbl.setImage(loadSkin("lockToBpmLbl.png")); addAndMakeVisible(lockToBpmLbl);
    bpmLbl.setImage(loadSkin("bpmLbl.png")); addAndMakeVisible(bpmLbl);
    bpmPoller = std::make_unique<BpmPoller>(proc, [this](double bpm)
        {
            // Replace 'bpmValueLbl' with your actual label variable name.
            // If you don’t have a label yet, use an empty lambda: [](double){}
            bpmValueLbl.setText(juce::String(juce::roundToInt(bpm)), juce::dontSendNotification);
        });

    // --- BPM Lock checkbox (uses existing helper + APVTS attachment) ---
    addAndMakeVisible(bpmLockChk);
    bpmLockChk.setClickingTogglesState(true);

    // IMPORTANT: we use your existing helper so there are NO new functions introduced.
    // This expects files: checkBoxOffBtn.png and checkBoxOnBtn.png in BinaryData.
    boomui::setToggleImages(bpmLockChk, "checkBoxOffBtn", "checkBoxOnBtn");

    // Bind to APVTS param "bpmLock"
    bpmLockAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        proc.apvts, "bpmLock", bpmLockChk);

    // Position – keep your existing coordinates if you already had them
    // (these numbers are just an example; do not copy if you already placed it)


    soundsDopeLbl.setImage(loadSkin("soundsDopeLbl.png")); addAndMakeVisible(soundsDopeLbl);
    engineLblImg.setImage(loadSkin("engineLbl.png")); addAndMakeVisible(engineLblImg);
    setButtonImages(btn808, "808Btn");    addAndMakeVisible(btn808);
    setButtonImages(btnBass, "bassBtn");   addAndMakeVisible(btnBass);
    setButtonImages(btnDrums, "drumsBtn");  addAndMakeVisible(btnDrums);
    btn808.onClick = [this] { setEngine(boom::Engine::e808);  };
    btnBass.onClick = [this] { setEngine(boom::Engine::Bass);  };
    btnDrums.onClick = [this] { setEngine(boom::Engine::Drums); };

    updateEngineButtonSkins((boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load(),
        btn808, btnBass, btnDrums);

    // Left labels
    scaleLblImg.setImage(loadSkin("scaleLbl.png"));          addAndMakeVisible(scaleLblImg);
    timeSigLblImg.setImage(loadSkin("timeSigLbl.png"));          addAndMakeVisible(timeSigLblImg);
    barsLblImg.setImage(loadSkin("barsLbl.png"));                addAndMakeVisible(barsLblImg);
    humanizeLblImg.setImage(loadSkin("humanizeLbl.png"));        addAndMakeVisible(humanizeLblImg);
    tripletsLblImg.setImage(loadSkin("tripletsLbl.png"));        addAndMakeVisible(tripletsLblImg);
    dottedNotesLblImg.setImage(loadSkin("dottedNotesLbl.png"));  addAndMakeVisible(dottedNotesLblImg);
    restDensityLblImg.setImage(loadSkin("restDensityLbl.png"));  addAndMakeVisible(restDensityLblImg);
    keyLblImg.setImage(loadSkin("keyLbl.png"));                  addAndMakeVisible(keyLblImg);
    octaveLblImg.setImage(loadSkin("octaveLbl.png"));            addAndMakeVisible(octaveLblImg);
    bassSelectorLblImg.setImage(loadSkin("bassSelectorLbl.png"));  addAndMakeVisible(bassSelectorLblImg);
    drumsSelectorLblImg.setImage(loadSkin("drumsSelectorLbl.png")); addAndMakeVisible(drumsSelectorLblImg);
    eightOhEightLblImg.setImage(loadSkin("808BassLbl.png"));     addAndMakeVisible(eightOhEightLblImg);
    styleLblImg.setImage(loadSkin("styleLbl.png"));     addAndMakeVisible(styleLblImg);
    

    // Left controls
    addAndMakeVisible(timeSigBox); timeSigBox.addItemList(boom::timeSigChoices(), 1);
    addAndMakeVisible(barsBox);    barsBox.addItemList(boom::barsChoices(), 1);

    addAndMakeVisible(humanizeTiming);   humanizeTiming.setSliderStyle(juce::Slider::LinearHorizontal);
    humanizeTiming.setRange(0, 100);
    addAndMakeVisible(humanizeVelocity); humanizeVelocity.setSliderStyle(juce::Slider::LinearHorizontal);
    humanizeVelocity.setRange(0, 100);
    addAndMakeVisible(swing);            swing.setSliderStyle(juce::Slider::LinearHorizontal);
    swing.setRange(0, 100);
    addAndMakeVisible(tripletDensity); tripletDensity.setSliderStyle(juce::Slider::LinearHorizontal);
    tripletDensity.setRange(0, 100);
    addAndMakeVisible(dottedDensity);  dottedDensity.setSliderStyle(juce::Slider::LinearHorizontal);
    dottedDensity.setRange(0, 100);
    dottedDensity.setLookAndFeel(&purpleLNF);
    tripletDensity.setLookAndFeel(&purpleLNF);
    boomui::makePercentSlider(dottedDensity);
    boomui::makePercentSlider(tripletDensity);
    dottedDensity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    tripletDensity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    humanizeTiming.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    humanizeVelocity.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    swing.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    // Switches as ImageButtons
    addAndMakeVisible(useTriplets); setToggleImages(useTriplets, "checkBoxOffBtn", "checkBoxOnBtn");
    addAndMakeVisible(useDotted);   setToggleImages(useDotted, "checkBoxOffBtn", "checkBoxOnBtn");

    // APVTS attachments
    timeSigAtt = std::make_unique<Attachment>(proc.apvts, "timeSig", timeSigBox);
    barsAtt = std::make_unique<Attachment>(proc.apvts, "bars", barsBox);
    humanizeTimingAtt = std::make_unique<SAttachment>(proc.apvts, "humanizeTiming", humanizeTiming);
    humanizeVelocityAtt = std::make_unique<SAttachment>(proc.apvts, "humanizeVelocity", humanizeVelocity);
    swingAtt = std::make_unique<SAttachment>(proc.apvts, "swing", swing);
    useTripletsAtt = std::make_unique<BAttachment>(proc.apvts, "useTriplets", useTriplets);
    tripletDensityAtt = std::make_unique<SAttachment>(proc.apvts, "tripletDensity", tripletDensity);
    useDottedAtt = std::make_unique<BAttachment>(proc.apvts, "useDotted", useDotted);
    dottedDensityAtt = std::make_unique<SAttachment>(proc.apvts, "dottedDensity", dottedDensity);

    // 808/Bass
    addAndMakeVisible(keyBox);   keyBox.addItemList(boom::keyChoices(), 1);
    addAndMakeVisible(scaleBox); scaleBox.addItemList(boom::scaleChoices(), 1);
    addAndMakeVisible(octaveBox); octaveBox.addItemList(juce::StringArray("-2", "-1", "0", "+1", "+2"), 1);
    addAndMakeVisible(rest808);  rest808.setSliderStyle(juce::Slider::LinearHorizontal);
    rest808.setRange(0, 100); rest808.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);

    keyAtt = std::make_unique<Attachment>(proc.apvts, "key", keyBox);
    scaleAtt = std::make_unique<Attachment>(proc.apvts, "scale", scaleBox);
    octaveAtt = std::make_unique<Attachment>(proc.apvts, "octave", octaveBox);
    rest808Att = std::make_unique<SAttachment>(proc.apvts, "restDensity808", rest808);

    addAndMakeVisible(bassStyleBox); bassStyleBox.addItemList(boom::styleChoices(), 1);
    bassStyleAtt = std::make_unique<Attachment>(proc.apvts, "bassStyle", bassStyleBox);

    // Drums
    addAndMakeVisible(drumStyleBox); drumStyleBox.addItemList(boom::styleChoices(), 1);
    addAndMakeVisible(restDrums); restDrums.setSliderStyle(juce::Slider::LinearHorizontal);
    restDrums.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    restDrums.setRange(0, 100);
    drumStyleAtt = std::make_unique<Attachment>(proc.apvts, "drumStyle", drumStyleBox);
    restDrumsAtt = std::make_unique<SAttachment>(proc.apvts, "restDensityDrums", restDrums);

    // Center views
    drumGrid.setRows(proc.getDrumRows());
    drumGrid.onToggle = [this](int row, int tick) { toggleDrumCell(row, tick); };
    // DRUM GRID
    addAndMakeVisible(drumGridView);
    drumGridView.setViewedComponent(&drumGrid, false);  // we own the child elsewhere
    drumGridView.setScrollBarsShown(true, true);        // horizontal + vertical
    drumGridView.setScrollOnDragEnabled(true);          // drag to scroll

    // PIANO ROLL
    addAndMakeVisible(pianoRollView);
    pianoRollView.setViewedComponent(&pianoRoll, false);
    pianoRollView.setScrollBarsShown(true, true);
    pianoRollView.setScrollOnDragEnabled(true);

    // Right action buttons
    setButtonImages(btnAITools, "aiToolsBtn");  addAndMakeVisible(btnAITools);
    setButtonImages(btnRolls, "rollsBtn");    addAndMakeVisible(btnRolls);
    setButtonImages(btnBumppit, "bumppitBtn");  addAndMakeVisible(btnBumppit);
    setButtonImages(btnFlippit, "flippitBtn");  addAndMakeVisible(btnFlippit);
    setButtonImages(diceBtn, "diceBtn");      addAndMakeVisible(diceBtn);

    btnFlippit.onClick = [this]
    {
        auto engine = (boom::Engine)proc.apvts.getRawParameterValue("engine")->load();

        flippit.reset(new FlippitWindow(
            proc,
            [this] { flippit.reset(); },
            [this](int density)
            {
                const auto eng = proc.getEngineSafe();
                const int bars = barsFromBox(barsBox);
                const int seed = (int)proc.apvts.getRawParameterValue("seed")->load();
                if (eng == boom::Engine::Drums) proc.flipDrums(seed, density, bars);
                else                             proc.flipMelodic(seed, density, bars);
                regenerate();
            },
            engine));

        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned(flippit.release());
        o.dialogTitle = "FLIPPIT";
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.componentToCentreAround = this;
        o.launchAsync();
    };

    btnBumppit.onClick = [this]
    {
        auto engine = (boom::Engine)proc.apvts.getRawParameterValue("engine")->load();

        bumppit.reset(new BumppitWindow(
            proc,
            [this] { bumppit.reset(); },
            [this] { proc.bumpDrumRowsUp(); regenerate(); },
            engine));

        juce::DialogWindow::LaunchOptions o;
        o.content.setOwned(bumppit.release());
        o.dialogTitle = "BUMPPIT";
        o.useNativeTitleBar = true;
        o.resizable = false;
        o.componentToCentreAround = this;
        o.launchAsync();
    };

    btnRolls.onClick = [this]
    {
        rolls.reset(new RollsWindow(proc, [this] { rolls.reset(); },
            [this](juce::String style, int bars, int density)
            { juce::ignoreUnused(style, density); proc.setDrumPattern(makeDemoPatternDrums(bars)); regenerate(); }));
        juce::DialogWindow::LaunchOptions o; o.content.setOwned(rolls.release());
        o.dialogTitle = "ROLLS"; o.useNativeTitleBar = true; o.resizable = false; o.componentToCentreAround = this; o.launchAsync();
    };


    btnAITools.onClick = [this]
    {
        juce::DialogWindow::LaunchOptions opts;
        opts.dialogTitle = "AI Tools";
        opts.componentToCentreAround = this;
        opts.useNativeTitleBar = true;
        opts.escapeKeyTriggersCloseButton = true;
        opts.resizable = true;

        opts.content.setOwned(new AIToolsWindow(proc));  // uses default {} onClose
        if (auto* dw = opts.launchAsync())
        {
            dw->setResizable(true, true);
            dw->centreAroundComponent(this, 700, 750);
            dw->setVisible(true);
        }
    };



    // Bottom bar: Generate + Drag (ImageButtons)
    setButtonImages(btnGenerate, "generateBtn"); addAndMakeVisible(btnGenerate);
    setButtonImages(btnDragMidi, "dragBtn");     addAndMakeVisible(btnDragMidi);
    btnDragMidi.addMouseListener(this, true); // start drag on mouseDown

    // === Correct Generate wiring that matches our existing Processor APIs ===
    btnGenerate.onClick = [this]
    {
        // Which engine?
        const auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();

        // Bars from the main Bars combo
        const int bars = barsFromBox(barsBox); // you already have this helper

        // Triplet/Dotted from APVTS (you already created these parameters)
        const bool allowTriplets = proc.apvts.getRawParameterValue("useTriplets")->load() > 0.5f;
        const bool allowDotted = proc.apvts.getRawParameterValue("useDotted")->load() > 0.5f;

        if (engine == boom::Engine::Drums)
        {
            // Main "Generate" for drums: for now we’ll leverage your rolls-style generator 
            // so at least a usable drums pattern is produced (style influenced + bars).
            // Style comes from your existing drumStyleBox.
            const juce::String style = drumStyleBox.getText();
            proc.generateRolls(style, bars);
        }
        else
        {
            // 808 / Bass use your existing generate808(...) signature
            // Params you already have in the UI: bassStyleBox, keyBox, scaleBox, rest808.
            const juce::String style = bassStyleBox.getText();
            const juce::String keyName = keyBox.getText();
            const juce::String scale = scaleBox.getText();

            // rest808 is 0..100 (your slider). We’ll pass it straight through as “densityPercent”
            const int densityPercent = (int)std::round(proc.apvts.getRawParameterValue("rest808")->load());

            proc.generate808(style, keyName, scale, bars, densityPercent, allowTriplets, allowDotted);
        }

        // Refresh both grid/roll according to engine & current patterns
        regenerate();
    };
    // Init engine & layout
    setEngine((boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load());
    syncVisibility();
    regenerate();
}

void BoomAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
}

void BoomAudioProcessorEditor::resized()
{
    const float W = 783.f, H = 714.f;
    auto bounds = getLocalBounds();

    auto sx = bounds.getWidth() / W;
    auto sy = bounds.getHeight() / H;
    auto S = [sx, sy](int x, int y, int w, int h)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx),
            juce::roundToInt(y * sy),
            juce::roundToInt(w * sx),
            juce::roundToInt(h * sy));
    };




    // Header
    engineLblImg.setBounds(S(241, 10, 300, 40));
    btn808.setBounds(S(232, 50, 100, 52));
    btnBass.setBounds(S(341, 50, 100, 52));
    btnDrums.setBounds(S(451, 50, 100, 52));

    logoImg.setBounds(S(255, 95, 290, 290));


    // Right column
    diceBtn.setBounds(S(723, 15, 50, 50));
    tripletsLblImg.setBounds(S(610, 10, 73, 26));
    useTriplets.setBounds(S(690, 18, 20, 20));
    tripletDensity.setBounds(S(583, 30, 100, 20));
    dottedNotesLblImg.setBounds(S(565, 45, 114, 26));
    dottedDensity.setBounds(S(568, 65, 100, 20));
    useDotted.setBounds(S(685, 50, 20, 20));
    soundsDopeLbl.setBounds(S(15, 15, 100, 49));
    lockToBpmLbl.setBounds(S(20, 65, 100, 20));
    bpmLockChk.setBounds(S(125, 60, 24, 24));
    bpmLbl.setBounds(S(18, 85,  100, 20));

    
    // Left Column
    int y = 130;
    const int x = 10;
    const int lblWidth = 100;
    const int ctlWidth = 125;
    const int height = 26;
    const int spacing = 30;

    keyLblImg.setBounds(S(x, y, lblWidth, height));
    keyBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    scaleLblImg.setBounds(S(x, y, lblWidth, height));
    scaleBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    octaveLblImg.setBounds(S(x, y, lblWidth, height));
    octaveBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    timeSigLblImg.setBounds(S(x, y, lblWidth, height));
    timeSigBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    barsLblImg.setBounds(S(x, y, lblWidth, height));
    barsBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    restDensityLblImg.setBounds(S(x, y, lblWidth, height));
    rest808.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    restDrums.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    styleLblImg.setBounds(S(x, y, lblWidth, height));
    bassStyleBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    drumStyleBox.setBounds(S(x + lblWidth + 5, y, ctlWidth, height));
    y += spacing;

    // Right Column
    int right_x = 550;
    y = 150;
    humanizeLblImg.setBounds(S(right_x, y, 200, 26));
    y += spacing;
    humanizeTiming.setBounds(S(right_x, y, 200, 50));
    y += spacing;
    humanizeVelocity.setBounds(S(right_x, y, 200, 50));
    y += spacing;
    swing.setBounds(S(right_x, y, 200, 50));


    // Buttons
    btnBumppit.setBounds(S(580, 280, 200, 60));
    btnFlippit.setBounds(S(580, 350, 200, 60));
    btnRolls.setBounds(S(40, 350, 200, 60));
    btnAITools.setBounds(S(290, 350, 200, 60));

    // DRUM GRID (main window) — exact size
    {
        auto gridArea = S(40, 420, 700, 200);
        drumGridView.setBounds(gridArea);
        drumGrid.setTopLeftPosition(0, 0);
        drumGrid.setSize(gridArea.getWidth() * 2, gridArea.getHeight());
    }

    // PIANO ROLL (main window) — exact size
    {
        auto rollArea = S(40, 420, 700, 200);
        pianoRollView.setBounds(rollArea);
        pianoRoll.setTopLeftPosition(0, 0);
        pianoRoll.setSize(rollArea.getWidth() * 2, rollArea.getHeight() * 2);
    }

    // Make sure they’re visible (constructor must setViewedComponent already)
    pianoRollView.toFront(false);
    drumGridView.toFront(false);

    // Bottom bar
    btnGenerate.setBounds(S(40, 640, 300, 70));
    btnDragMidi.setBounds(S(443, 640, 300, 70));

    syncVisibility();
}

void BoomAudioProcessorEditor::mouseDown(const juce::MouseEvent& e)
{
    if (e.eventComponent == &btnDragMidi || e.originalComponent == &btnDragMidi)
        startExternalMidiDrag();
}

void BoomAudioProcessorEditor::setEngine(boom::Engine e)
{
    proc.apvts.getParameter("engine")->beginChangeGesture();
    dynamic_cast<juce::AudioParameterChoice*>(proc.apvts.getParameter("engine"))->operator=((int)e);
    proc.apvts.getParameter("engine")->endChangeGesture();
    syncVisibility();
    resized();
    updateEngineButtonSkins(e, btn808, btnBass, btnDrums);
}

void BoomAudioProcessorEditor::syncVisibility()
{
    auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
    const bool isDrums = engine == boom::Engine::Drums;
    const bool is808 = engine == boom::Engine::e808;
    const bool isBass = engine == boom::Engine::Bass;

    drumGridView.setVisible(isDrums);
    pianoRollView.setVisible(!isDrums);

    // --- Left Column based on user request ---

    // 808: KEY, SCALE, BARS, OCTAVE, Rest Density
    // BASS: KEY, SCALE, BARS, OCTAVE, STYLE, Rest Density
    // DRUMS: TIME SIGNATURE, BARS, STYLE

    keyLblImg.setVisible(is808 || isBass);
    keyBox.setVisible(is808 || isBass);

    scaleLblImg.setVisible(is808 || isBass);
    scaleBox.setVisible(is808 || isBass);

    barsLblImg.setVisible(true); // all engines
    barsBox.setVisible(true); // all engines

    octaveLblImg.setVisible(is808 || isBass);
    octaveBox.setVisible(is808 || isBass);

    styleLblImg.setVisible(isBass || isDrums);
    bassStyleBox.setVisible(isBass);
    drumStyleBox.setVisible(isDrums);

    restDensityLblImg.setVisible(true);
    rest808.setVisible(is808 || isBass);
    restDrums.setVisible(true);

    timeSigLblImg.setVisible(true);
    timeSigBox.setVisible(true);

    // --- Other UI elements from original implementation ---
    // These seem to be independent of the combo box changes.
    tripletDensity.setVisible(is808 || isDrums || isBass);
    dottedDensity.setVisible(is808 || isDrums || isBass);
    eightOhEightLblImg.setVisible(is808);
    
    // Hide these as they are replaced by the generic "style" label.
    bassSelectorLblImg.setVisible(false);
    drumsSelectorLblImg.setVisible(false);
    
    // --- Buttons ---
    btnRolls.setVisible(isDrums);

}

void BoomAudioProcessorEditor::regenerate()
{
    auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
    const int bars = barsFromBox(barsBox);

    if (engine == boom::Engine::Drums)
    {
        if (proc.getDrumPattern().isEmpty())
            proc.setDrumPattern(makeDemoPatternDrums(bars));
        drumGrid.setPattern(proc.getDrumPattern());
    }
    else
    {
        if (proc.getMelodicPattern().isEmpty())
            proc.setMelodicPattern(makeDemoPatternMelodic(bars));
        pianoRoll.setPattern(proc.getMelodicPattern());
    }

    repaint();
}

void BoomAudioProcessorEditor::toggleDrumCell(int row, int tick)
{
    auto pat = proc.getDrumPattern();
    for (int i = 0; i < pat.size(); ++i)
        if (pat[i].row == row && pat[i].startTick == tick)
        {
            pat.remove(i); proc.setDrumPattern(pat); drumGrid.setPattern(pat); repaint(); return;
        }
    BoomAudioProcessor::Note n; n.row = row; n.startTick = tick; n.lengthTicks = 24; n.velocity = 100; n.pitch = 0;
    pat.add(n); proc.setDrumPattern(pat); drumGrid.setPattern(pat); repaint();
}

BoomAudioProcessor::Pattern BoomAudioProcessorEditor::makeDemoPatternDrums(int bars) const
{
    BoomAudioProcessor::Pattern pat;
    const int stepsPerBar = 16; const int ticksPerStep = 24;
    const int totalSteps = stepsPerBar * juce::jmax(1, bars);
    for (int c = 0; c < totalSteps; ++c)
    {
        if (c % stepsPerBar == 0) { pat.add({ 0, 0, c * ticksPerStep, 24, 110 }); }
        if (c % stepsPerBar == 8) { pat.add({ 0, 0, c * ticksPerStep, 24, 105 }); }
        if (c % 4 == 0) { pat.add({ 0, 2, c * ticksPerStep, 12,  80 }); }
        if (c % stepsPerBar == 4) { pat.add({ 0, 1, c * ticksPerStep, 24, 110 }); }
        if (c % stepsPerBar == 12) { pat.add({ 0, 1, c * ticksPerStep, 24, 110 }); }
    }
    return pat;
}

BoomAudioProcessor::Pattern BoomAudioProcessorEditor::makeDemoPatternMelodic(int bars) const
{
    BoomAudioProcessor::Pattern pat;
    const int ticks = 24;
    const int base = 36; // C2
    for (int b = 0; b < juce::jmax(1, bars); ++b)
    {
        pat.add({ base + 0, 0, (b * 16 + 0) * ticks, 8 * ticks, 100 });
        pat.add({ base + 7, 0, (b * 16 + 8) * ticks, 8 * ticks, 100 });
    }
    return pat;
}

juce::File BoomAudioProcessorEditor::writeTempMidiFile() const
{
    auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
    juce::MidiFile mf;
    if (engine == boom::Engine::Drums)
    {
        boom::midi::DrumPattern mp;
        for (const auto& n : proc.getDrumPattern())
            mp.add({ n.row, n.startTick, n.lengthTicks, n.velocity });
        mf = boom::midi::buildMidiFromDrums(mp, 96);
    }
    else
    {
        boom::midi::MelodicPattern mp;
        for (const auto& n : proc.getMelodicPattern())
            mp.add({ n.pitch, n.startTick, n.lengthTicks, n.velocity, 1 });
        mf = boom::midi::buildMidiFromMelodic(mp, 96);
    }
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("BOOM_Pattern.mid");
    boom::midi::writeMidiToFile(mf, tmp);
    return tmp;
}

void BoomAudioProcessorEditor::startExternalMidiDrag()
{
    const juce::File f = writeTempMidiFile();
    juce::StringArray files; files.add(f.getFullPathName());
    performExternalDragDropOfFiles(files, true);
}


AIToolsWindow::AIToolsWindow(BoomAudioProcessor& p, std::function<void()> onClose)
    : proc(p), onCloseFn(std::move(onClose))
{
    setLookAndFeel(&boomui::LNF());
    setSize(700, 700);



    // ---- Non-interactive labels from your artwork (no mouse) ----
    auto addLbl = [this](juce::ImageComponent& ic, const juce::String& png)
    {
        ic.setImage(loadSkin(png));
        ic.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(ic);
    };
    addLbl(titleLbl, "aiToolsLbl.png");
    addLbl(selectAToolLbl, "selectAToolLbl.png");
    addLbl(rhythmimickLbl, "rhythmimickLbl.png");
    addLbl(rhythmimickDescLbl, "rhythmimickDescriptionLbl.png");
    addLbl(slapsmithLbl, "slapsmithLbl.png");
    addLbl(slapsmithDescLbl, "slapsmithDescriptionLbl.png");
    addLbl(slapSmithGridLbl, "slapSmithGridLbl.png");
    addLbl(lockToBpmLbl, "lockToBpmLbl.png");
    addLbl(bpmLbl, "bpmLbl.png");
    addLbl(styleBlenderLbl, "styleBlenderLbl.png");
    addLbl(styleBlenderDescLbl, "styleBlenderDescriptionLbl.png");
    addLbl(beatboxLbl, "beatboxLbl.png");
    addLbl(beatboxDescLbl, "beatboxDescriptionLbl.png");

    addLbl(recordUpTo60LblTop, "recordUpTo60SecLbl.png");
    addLbl(recordUpTo60LblBottom, "recordUpTo60SecLbl.png");

    addAndMakeVisible(bpmLockChk);
    bpmLockChk.setClickingTogglesState(true);

    // IMPORTANT: we use your existing helper so there are NO new functions introduced.
    // This expects files: checkBoxOffBtn.png and checkBoxOnBtn.png in BinaryData.
    boomui::setToggleImages(bpmLockChk, "checkBoxOffBtn", "checkBoxOnBtn");

    // ---- Toggles (right side) ----
    addAndMakeVisible(toggleRhythm);
    addAndMakeVisible(toggleSlap);
    addAndMakeVisible(toggleBlend);
    addAndMakeVisible(toggleBeat);

    // Ensure only one tool is ON at a time
    toggleRhythm.onClick = [this] { makeToolActive(Tool::Rhythmimick); };
    toggleSlap.onClick = [this] { makeToolActive(Tool::Slapsmith);   };
    toggleBlend.onClick = [this] { makeToolActive(Tool::StyleBlender); };
    toggleBeat.onClick = [this] { makeToolActive(Tool::Beatbox);     };

    // Default active tool (pick one; Rhythmimick here)
    makeToolActive(Tool::Rhythmimick);


    // ---- Rhythmimick row (recording block) ----
    addAndMakeVisible(btnRec1);   setButtonImages(btnRec1, "recordBtn");
    addAndMakeVisible(playBtn);   setButtonImages(playBtn, "playBtn");
    addAndMakeVisible(btnStop1);  setButtonImages(btnStop1, "stopBtn");
    addAndMakeVisible(btnGen1);   setButtonImages(btnGen1, "generateBtn");
    addAndMakeVisible(btnSave1);  setButtonImages(btnSave1, "saveMidiBtn");
    addAndMakeVisible(btnDrag1);  setButtonImages(btnDrag1, "dragBtn");

    // ---- Slapsmith row ----
    addAndMakeVisible(btnGen2);   setButtonImages(btnGen2, "generateBtn");
    addAndMakeVisible(btnSave2);  setButtonImages(btnSave2, "saveMidiBtn");
    addAndMakeVisible(btnDrag2);  setButtonImages(btnDrag2, "dragBtn");

    auto addArrow = [this](juce::ImageComponent& ic)
    {
        addAndMakeVisible(ic);
        ic.setInterceptsMouseClicks(false, false);
        ic.setImage(loadSkin("arrowLbl.png"));
        ic.setImagePlacement(juce::RectanglePlacement::centred);
    };
    addArrow(arrowRhythm);
    addArrow(arrowSlap);
    addArrow(arrowBlend);
    addArrow(arrowBeat);

    // ---- Style Blender blend slider (0..100%, no textbox)
    addAndMakeVisible(blendSlider);
    blendSlider.setLookAndFeel(&boomui::LNF());
    blendSlider.setRange(0.0, 100.0, 1.0);
    blendSlider.setValue(50.0);
    blendSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    blendSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);


    addAndMakeVisible(rhythmSeek);
    rhythmSeek.setLookAndFeel(&boomui::LNF());
    rhythmSeek.setSliderStyle(juce::Slider::LinearBar);
    rhythmSeek.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    rhythmSeek.setRange(0.0, 60.0, 0.01);
    rhythmSeek.setEnabled(false);

    addAndMakeVisible(beatboxSeek);
    beatboxSeek.setLookAndFeel(&boomui::LNF());
    beatboxSeek.setSliderStyle(juce::Slider::LinearBar);
    beatboxSeek.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    beatboxSeek.setRange(0.0, 60.0, 0.01);
    beatboxSeek.setEnabled(false);


    // ---- Slapsmith row Save/Drag: export only ENABLED rows from miniGrid ----
    auto hookupRow = [this](juce::ImageButton& save, juce::ImageButton& drag, const juce::String& baseFile)
    {
        save.onClick = [this, baseFile]
        {
            juce::File src = buildTempMidi(baseFile);     // you already have buildTempMidi()
            saveWithChooserOrDesktop(baseFile, src);      // launches chooser; falls back to Desktop
        };

        drag.onClick = [this, baseFile]
        {
            juce::File f = buildTempMidi(baseFile);
            if (!f.existsAsFile()) return;
            if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor(this))
            {
                juce::StringArray files; files.add(f.getFullPathName());
                dnd->performExternalDragDropOfFiles(files, true);
            }
        };
    };

    hookupRow(btnSave1, btnDrag1, "BOOM_Rhythmimick");
    hookupRow(btnSave2, btnDrag2, "BOOM_Slapsmith");
    hookupRow(btnSave3, btnDrag3, "BOOM_StyleBlender");
    hookupRow(btnSave4, btnDrag4, "BOOM_Beatbox");


    // ---- Style Blender row (two style sliders in the art; we show density sliders) ----
    addAndMakeVisible(btnGen3);   setButtonImages(btnGen3, "generateBtn");
    addAndMakeVisible(btnSave3);  setButtonImages(btnSave3, "saveMidiBtn");
    addAndMakeVisible(btnDrag3);  setButtonImages(btnDrag3, "dragBtn");

    // ---- Beatbox row (recording block) ----
    addAndMakeVisible(btnRec4);   setButtonImages(btnRec4, "recordBtn");
	addAndMakeVisible(btnPlay4);  setButtonImages(btnPlay4, "playBtn");
    addAndMakeVisible(btnStop4);  setButtonImages(btnStop4, "stopBtn");
    addAndMakeVisible(btnGen4);   setButtonImages(btnGen4, "generateBtn");
    addAndMakeVisible(btnSave4);  setButtonImages(btnSave4, "saveMidiBtn");
    addAndMakeVisible(btnDrag4);  setButtonImages(btnDrag4, "dragBtn");

    // ---- Home ----
    addAndMakeVisible(btnHome);   setButtonImages(btnHome, "homeBtn");
    btnHome.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };

    // --- Slapsmith mini-grid ---
    addAndMakeVisible(miniGrid);
    miniGrid.setRows(proc.getDrumRows()); // or a subset like first 4 rows
    miniGrid.onCellEdited = [this](int row, int step, bool value)
    {
        // Convert grid step -> ticks (grid uses 24 ticks/step)
        const int startTick = step * 24;

        auto pat = proc.getDrumPattern();
        int found = -1;
        for (int i = 0; i < pat.size(); ++i)
            if (pat[i].row == row && pat[i].startTick == startTick)
            {
                found = i; break;
            }

        if (value) // cell turned ON
        {
            if (found < 0) pat.add({ 0, row, startTick, 24, 100 });
        }
        else        // cell turned OFF
        {
            if (found >= 0) pat.remove(found);
        }

        proc.setDrumPattern(pat);
    };

    // --- Style blender combo boxes ---
    addAndMakeVisible(styleABox); styleABox.addItemList(boom::styleChoices(), 1); styleABox.setSelectedId(1);
    addAndMakeVisible(styleBBox); styleBBox.addItemList(boom::styleChoices(), 1); styleBBox.setSelectedId(2);

    // Wire Generate buttons to correct behavior
// ---- Generate buttons ----
    btnGen1.onClick = [this] {                // Rhythmimick: analyze captured audio → drums
        proc.aiStopCapture();
        proc.aiAnalyzeCapturedToDrums(/*bars*/4, /*bpm*/120); // keep your actual args
    };

    btnGen2.onClick = [this] {                // Slapsmith: expand miniGrid pattern
        proc.aiSlapsmithExpand(/*bars*/4);
    };

    btnGen3.onClick = [this] {                // Style Blender: use A/B + blend %
        const auto a = styleABox.getText();
        const auto b = styleBBox.getText();
        const double weightB = blendSlider.getValue() / 100.0;
        const double weightA = 1.0 - weightB;
        proc.aiStyleBlendDrums(a, b, 4);
    };

    btnGen4.onClick = [this] {                // Beatbox: analyze captured mic → drums
        proc.aiStopCapture();
        proc.aiAnalyzeCapturedToDrums(/*bars*/4, /*bpm*/120);
    };

    // Record/Stop behavior per row
    btnRec1.onClick = [this] { proc.aiStartCapture(BoomAudioProcessor::CaptureSource::Loopback); };
    btnStop1.onClick = [this] { proc.aiStopCapture(); };
    btnRec4.onClick = [this] { proc.aiStartCapture(BoomAudioProcessor::CaptureSource::Microphone); };
    btnStop4.onClick = [this] { proc.aiStopCapture(); };




    // ---- Shared actions (Save/Drag) use real MIDI from processor ----


    hookupRow(btnSave1, btnDrag1, "BOOM_Rhythmimick");
    hookupRow(btnSave2, btnDrag2, "BOOM_Slapsmith");
    hookupRow(btnSave3, btnDrag3, "BOOM_StyleBlender");
    hookupRow(btnSave4, btnDrag4, "BOOM_Beatbox");

    // You can wire Generate/Record/Stop callbacks to your engines here later.
    // For now they're present and clickable (no-op), zero TextButtons.
}

void AIToolsWindow::makeToolActive(Tool t)
{
    activeTool_ = t;
    
    auto setActive = [](juce::ImageButton& b, bool on) {
        b.setToggleState(on, juce::dontSendNotification);
        if (on)
        {
            setButtonImages(b, "toggleBtnOn");
        }
        else
        {
            auto offImage = loadSkin("toggleBtnOff.png");
            b.setImages(false, true, true,
                offImage, 1.0f, juce::Colour(),
                offImage, 1.0f, juce::Colour(),
                offImage, 1.0f, juce::Colour());
        }
    };

    setActive(toggleRhythm, t == Tool::Rhythmimick);
    setActive(toggleSlap, t == Tool::Slapsmith);
    setActive(toggleBlend, t == Tool::StyleBlender);
    setActive(toggleBeat, t == Tool::Beatbox);

    // Optional: enable/disable row controls based on active tool
    const bool r = (t == Tool::Rhythmimick);
    const bool s = (t == Tool::Slapsmith);
    const bool b = (t == Tool::Beatbox);
    const bool y = (t == Tool::StyleBlender);

    btnRec1.setEnabled(r);   playBtn.setEnabled(r); btnStop1.setEnabled(r);   btnGen1.setEnabled(r);   btnSave1.setEnabled(r);   btnDrag1.setEnabled(r);
    btnGen2.setEnabled(s);   btnSave2.setEnabled(s);   btnDrag2.setEnabled(s);  miniGrid.setEnabled(s);
    btnGen4.setEnabled(b);   btnRec4.setEnabled(b);    btnPlay4.setEnabled(b); btnStop4.setEnabled(b);  btnSave4.setEnabled(b);   btnDrag4.setEnabled(b);
    btnGen3.setEnabled(y);   btnSave3.setEnabled(y);   btnDrag3.setEnabled(y);  blendSlider.setEnabled(y); styleABox.setEnabled(y); styleBBox.setEnabled(y);

    repaint();
}

AIToolsWindow::~AIToolsWindow()
{
 
    rhythmSeek.setLookAndFeel(nullptr);
    beatboxSeek.setLookAndFeel(nullptr);
}

void AIToolsWindow::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
    // If you want a full static background, uncomment:
    // g.drawImageWithin(loadSkin("aiToolsWindowMockUp.png"), 0, 0, getWidth(), getHeight(), juce::RectanglePlacement::fillDestination);
}

void AIToolsWindow::resized()
{
    const float W = 700.f, H = 800.f; // Adjusted Height for more space
    auto r = getLocalBounds();
    auto sx = r.getWidth() / W, sy = r.getHeight() / H;
    auto S = [sx, sy](int x, int y, int w, int h)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
            juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };

    // Title + "Select a tool"
    titleLbl.setBounds(S(250, 24, 200, 44));
    selectAToolLbl.setBounds(S(550, 10, 160, 60));
    lockToBpmLbl.setBounds(S(10, 15, 100, 20));
    bpmLbl.setBounds(S(10, 35,  100, 20));
    bpmLockChk.setBounds(S(115, 10, 24, 24));

    // ---- Rhythmimick row (top block) ----
    int y_offset = 115;
    rhythmimickLbl.setBounds(S(250, y_offset, 120, 20));
    toggleRhythm.setBounds(S(550, y_offset-5, 120, 40));
    rhythmimickDescLbl.setBounds(S(10, y_offset -10, 250, 40));
    
    recordUpTo60LblTop.setBounds(S(280, y_offset + 25, 180, 20));
    btnRec1.setBounds(S(280, y_offset + 45, 30, 30));
    playBtn.setBounds(S(320, y_offset + 45, 30, 30));
    rhythmSeek.setBounds(S(360, y_offset + 45, 140, 30));
    btnStop1.setBounds(S(360, y_offset + 45, 30, 30));

    btnGen1.setBounds(S(280, y_offset + 80, 90, 30));
    btnSave1.setBounds(S(380, y_offset + 80, 90, 30));
    btnDrag1.setBounds(S(480, y_offset + 80, 90, 30));

    // ---- Slapsmith row ----
    y_offset = 240;
    slapsmithLbl.setBounds(S(250, y_offset, 120, 20));
    toggleSlap.setBounds(S(550, y_offset-5, 120, 40));
    slapsmithDescLbl.setBounds(S(10, y_offset -10, 250, 40));
    slapSmithGridLbl.setBounds(S(280, y_offset - 20, 180, 20));
    miniGrid.setBounds(S(280, y_offset, 250, 80));
    

    btnGen2.setBounds(S(280, y_offset + 90, 90, 30));
    btnSave2.setBounds(S(380, y_offset + 90, 90, 30));
    btnDrag2.setBounds(S(480, y_offset + 90, 90, 30));

    // ---- Style Blender row ----
    y_offset = 380;
    styleBlenderLbl.setBounds(S(250, y_offset, 120, 20));
    toggleBlend.setBounds(S(550, y_offset-5, 120, 40));
    styleBlenderDescLbl.setBounds(S(10, y_offset -10, 250, 40));

    // Style A/B combo boxes
    styleABox.setBounds(S(280, y_offset + 20, 120, 28));
    styleBBox.setBounds(S(410, y_offset + 20, 120, 28));

    // One blended slider between them
    blendSlider.setBounds(S(280, y_offset + 55, 250, 20)); // below the boxes; adjust to taste

    btnGen3.setBounds(S(280, y_offset + 90, 90, 30));
    btnSave3.setBounds(S(380, y_offset + 90, 90, 30));
    btnDrag3.setBounds(S(480, y_offset + 90, 90, 30));

    // Arrows between toggles and the text blocks (adjust Y to your art)
    arrowRhythm.setBounds(S(450, 115, 40, 20));
    arrowSlap.setBounds(S(450, 240, 40, 20));
    arrowBlend.setBounds(S(450, 380, 40, 20));
    arrowBeat.setBounds(S(450, 505, 40, 20));

    // ---- Beatbox row (bottom) ----
    y_offset = 500;
    beatboxLbl.setBounds(S(250, y_offset, 120, 20));
    toggleBeat.setBounds(S(550, y_offset-5, 120, 40));
    beatboxDescLbl.setBounds(S(10, y_offset-10, 250, 40));

    recordUpTo60LblBottom.setBounds(S(280, y_offset + 25, 180, 20));
    btnRec4.setBounds(S(280, y_offset + 45, 30, 30));
	btnPlay4.setBounds(S(320, y_offset + 45, 30, 30));
    beatboxSeek.setBounds(S(360, y_offset + 45, 140, 30));
    btnStop4.setBounds(S(360, y_offset + 45, 30, 30));

    btnGen4.setBounds(S(280, y_offset + 90, 90, 30));
    btnSave4.setBounds(S(380, y_offset + 90, 90, 30));
    btnDrag4.setBounds(S(480, y_offset + 90, 90, 30));

    // ---- Home button ----
    btnHome.setBounds(S(600, 600, 80, 80));
}

juce::File AIToolsWindow::buildTempMidi(const juce::String& base) const
{
    auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
    juce::MidiFile mf;
    if (engine == boom::Engine::Drums)
    {
        boom::midi::DrumPattern mp;
        for (const auto& n : proc.getDrumPattern())
            mp.add({ n.row, n.startTick, n.lengthTicks, n.velocity });
        mf = boom::midi::buildMidiFromDrums(mp, 96);
    }
    else
    {
        boom::midi::MelodicPattern mp;
        for (const auto& n : proc.getMelodicPattern())
            mp.add({ n.pitch, n.startTick, n.lengthTicks, n.velocity, 1 });
        mf = boom::midi::buildMidiFromMelodic(mp, 96);
    }
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(base + ".mid");
    boom::midi::writeMidiToFile(mf, tmp);
    return tmp;
}

void AIToolsWindow::performFileDrag(const juce::File& f)
{
    if (!f.existsAsFile()) return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        juce::StringArray files; files.add(f.getFullPathName());
        dnd->performExternalDragDropOfFiles(files, true);
    }
}

// ================== Modals: ALL ImageButtons ==================
// --- FlippitWindow ---
FlippitWindow::FlippitWindow(BoomAudioProcessor& p,
    std::function<void()> onClose,
    std::function<void(int density)> onFlip,
    boom::Engine engine)
    : proc(p), onCloseFn(std::move(onClose)), onFlipFn(std::move(onFlip))
{
    setLookAndFeel(&boomui::LNF());
    setSize(700, 450);

    const bool isDrums = (engine == boom::Engine::Drums);

    {
        const juce::String lblFile = isDrums ? "flippitDrumsLbl.png" : "flippitLbl.png";
        titleLbl.setImage(boomui::loadSkin(lblFile));
        titleLbl.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(titleLbl);
    }
    // Engine-specific button bases
    const juce::String flipArtBase = isDrums ? "flippitBtnDrums" : "flippitBtn808Bass";
    const juce::String saveArtBase = isDrums ? "saveMidiFlippitDrums" : "saveMidiFlippit808Bass";
    const juce::String dragArtBase = isDrums ? "dragBtnFlippitDrums" : "dragBtnFlippit808Bass";

    // Controls
    addAndMakeVisible(variation);
    variation.setRange(0, 100, 1);
    variation.setValue(35);
    variation.setSliderStyle(juce::Slider::LinearHorizontal);

    addAndMakeVisible(btnFlip);     setButtonImages(btnFlip, flipArtBase);
    addAndMakeVisible(btnSaveMidi); setButtonImages(btnSaveMidi, saveArtBase);
    addAndMakeVisible(btnDragMidi); setButtonImages(btnDragMidi, dragArtBase);
    addAndMakeVisible(btnHome);     setButtonImages(btnHome, "homeBtn");

    btnFlip.onClick = [this]
    {
        if (onFlipFn) onFlipFn((int)juce::jlimit(0.0, 100.0, variation.getValue()));
    };

    btnSaveMidi.onClick = [this]
    {
        juce::File src = buildTempMidi(); // the temp MIDI we generated

        juce::File defaultDir = src.getParentDirectory();
        juce::File defaultFile = defaultDir.getChildFile("BOOM_Flippit.mid");

        juce::FileChooser fc("Save MIDI...", defaultFile, "*.mid");
        fc.launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [src](const juce::FileChooser& chooser)
            {
                juce::File dest = chooser.getResult();
                if (dest.getFullPathName().isEmpty())
                    return; // user cancelled

                if (!dest.hasFileExtension(".mid"))
                    dest = dest.withFileExtension(".mid");

                // overwrite if exists
                if (dest.existsAsFile())
                    dest.deleteFile();

                src.copyFileTo(dest);
            });
    };


    btnDragMidi.onClick = [this]
    {
        juce::File f = buildTempMidi();
        performFileDrag(f);
    };

    btnHome.onClick = [this] { if (onCloseFn) onCloseFn(); };
}

void FlippitWindow::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
}

void FlippitWindow::resized()
{
    auto r = getLocalBounds();

    // 700x450 reference
    const float W = 700.f, H = 450.f;
    auto sx = r.getWidth() / W, sy = r.getHeight() / H;
    auto S = [sx, sy](int x, int y, int w, int h)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
            juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };

    // Center the title at the top using the image's natural size scaled by sx/sy.
    {
        auto img = titleLbl.getImage();
        const int iw = juce::roundToInt(img.getWidth() * sx);
        const int ih = juce::roundToInt(img.getHeight() * sy);
        const int x = (r.getWidth() - iw) / 2;
        const int y = juce::roundToInt(24 * sy);
        titleLbl.setBounds(x, y, iw, ih);
    }

    // Positions tuned to the provided flippit mockups
    btnFlip.setBounds(S(270, 150, 160, 72));
    variation.setBounds(S(40, 250, 620, 24));
    btnSaveMidi.setBounds(S(40, 350, 120, 40));
    btnDragMidi.setBounds(S(220, 340, 260, 50));
    btnHome.setBounds(S(600, 350, 60, 60));
}

// --- Flippit helpers ---
juce::File FlippitWindow::buildTempMidi() const
{
    auto engine = (boom::Engine)(int)proc.apvts.getRawParameterValue("engine")->load();
    juce::MidiFile mf;

    if (engine == boom::Engine::Drums)
    {
        boom::midi::DrumPattern mp;
        for (const auto& n : proc.getDrumPattern())
            mp.add({ n.row, n.startTick, n.lengthTicks, n.velocity });
        mf = boom::midi::buildMidiFromDrums(mp, 96);
    }
    else
    {
        boom::midi::MelodicPattern mp;
        for (const auto& n : proc.getMelodicPattern())
            mp.add({ n.pitch, n.startTick, n.lengthTicks, n.velocity, 1 });
        mf = boom::midi::buildMidiFromMelodic(mp, 96);
    }

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("BOOM_Flippit.mid");
    boom::midi::writeMidiToFile(mf, tmp);
    return tmp;
}

void FlippitWindow::performFileDrag(const juce::File& f)
{
    if (!f.existsAsFile()) return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        juce::StringArray files; files.add(f.getFullPathName());
        dnd->performExternalDragDropOfFiles(files, true);
    }
}

// --- BumppitWindow ---
BumppitWindow::BumppitWindow(BoomAudioProcessor& p,
    std::function<void()> onClose,
    std::function<void()> onBump,
    boom::Engine engine)
    : proc(p), onCloseFn(std::move(onClose)), onBumpFn(std::move(onBump))
{
    setLookAndFeel(&boomui::LNF());
    setSize(700, 462);

    // Background per engine
    const bool isDrums = (engine == boom::Engine::Drums);
    // Top label depending on engine
    {
        const juce::String lblFile = isDrums ? "bumppitDrumsLbl.png" : "bumppitLbl.png";
        titleLbl.setImage(boomui::loadSkin(lblFile));
        titleLbl.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(titleLbl);
    }

    // Engine-specific Bumppit button art
    const juce::String bumpArtBase = isDrums ? "bumppitBtnDrums" : "bumppitBtn808Bass";

    addAndMakeVisible(btnBump);  setButtonImages(btnBump, bumpArtBase);
    addAndMakeVisible(btnHome);  setButtonImages(btnHome, "homeBtn");

    btnBump.onClick = [this] { if (onBumpFn) onBumpFn(); };
    btnHome.onClick = [this] { if (onCloseFn) onCloseFn(); };

    // Melodic options only for 808/Bass layout
    showMelodicOptions = !isDrums;
    if (showMelodicOptions)
    {
        addAndMakeVisible(keyBox);    keyBox.addItemList(boom::keyChoices(), 1);     keyBox.setSelectedId(1);
        addAndMakeVisible(scaleBox);  scaleBox.addItemList(boom::scaleChoices(), 1); scaleBox.setSelectedId(1);
        addAndMakeVisible(octaveBox); octaveBox.addItemList(juce::StringArray("-2", "-1", "0", "+1", "+2"), 1); octaveBox.setSelectedId(3);
        addAndMakeVisible(barsBox);   barsBox.addItemList(juce::StringArray("1", "2", "4", "8"), 1);          barsBox.setSelectedId(3);
    }
}

void BumppitWindow::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
}

void BumppitWindow::resized()
{
    auto r = getLocalBounds();

    // scale helper for 700x462
    const float W = 700.f, H = 462.f;
    auto sx = r.getWidth() / W, sy = r.getHeight() / H;
    auto S = [sx, sy](int x, int y, int w, int h)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
            juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };

    // Center the title at the top using the image's natural size scaled by sx/sy.
    {
        auto img = titleLbl.getImage();
        const int iw = juce::roundToInt(img.getWidth() * sx);
        const int ih = juce::roundToInt(img.getHeight() * sy);
        const int x = (r.getWidth() - iw) / 2;
        const int y = juce::roundToInt(24 * sy);
        titleLbl.setBounds(x, y, iw, ih);
    }

    // layout per mockups:
    if (showMelodicOptions)
    {
        // 808/Bass mockup (first image): four combo boxes centered column
        keyBox.setBounds(S(215, 130, 270, 46));
        scaleBox.setBounds(S(215, 180, 270, 46));
        octaveBox.setBounds(S(215, 230, 270, 46));
        barsBox.setBounds(S(215, 280, 270, 46));

        // place the Bumppit button beneath those controls (centered)
        btnBump.setBounds(S(175, 340, 350, 74));
    }
    else
    {
        // Drums mockup (second image): one big Bumppit button in the middle
        btnBump.setBounds(S(130, 171, 440, 120));
    }

    // Home button bottom-right (as in mockups)
    btnHome.setBounds(S(620, 382, 60, 60));
}

RollsWindow::RollsWindow(BoomAudioProcessor& p, std::function<void()> onClose, std::function<void(juce::String, int, int)> onGen)
    : proc(p), onCloseFn(std::move(onClose)), onGenerateFn(std::move(onGen))
{
    setLookAndFeel(&boomui::LNF());
    setSize(700, 447);

    // STYLE box (same list as main)
    styleBox.addItemList(boom::styleChoices(), 1);
    styleBox.setSelectedId(1, juce::dontSendNotification);

    // BARS box (Rolls-specific: 1,2,4,8)
    barsBox.clear();
    barsBox.addItem("1", 1);
    barsBox.addItem("2", 2);
    barsBox.addItem("4", 3);
    barsBox.addItem("8", 4);
    barsBox.setSelectedId(3, juce::dontSendNotification); // default to "4"


    addAndMakeVisible(rollsTitleImg);
    rollsTitleImg.setInterceptsMouseClicks(false, false);
    rollsTitleImg.setImage(loadSkin("rollGerneratorLbl.png")); // exact filename from your asset list
    rollsTitleImg.setImagePlacement(juce::RectanglePlacement::centred); // keep aspect when we size the box
    addAndMakeVisible(variation); variation.setRange(0, 100, 1); variation.setValue(35); variation.setSliderStyle(juce::Slider::LinearHorizontal);


    addAndMakeVisible(btnGenerate); setButtonImages(btnGenerate, "generateBtn");
    addAndMakeVisible(btnHome);    setButtonImages(btnHome, "homeBtn");
    addAndMakeVisible(btnSaveMidi); setButtonImages(btnSaveMidi, "saveMidiBtn");
    addAndMakeVisible(btnDragMidi); setButtonImages(btnDragMidi, "dragBtn");


    btnGenerate.onClick = [this]
    {
        const juce::String style = styleBox.getText();

        int bars = 4;
        switch (barsBox.getSelectedId())
        {
        case 1: bars = 1; break;
        case 2: bars = 2; break;
        case 3: bars = 4; break;
        case 4: bars = 8; break;
        default: bars = 4; break;
        }

        proc.generateRolls(style, bars);
    };

    btnHome.onClick = [this] { if (onCloseFn) onCloseFn(); };

    btnSaveMidi.onClick = [this]
    {
        juce::File src = buildTempMidi();

        juce::File defaultDir = src.getParentDirectory();
        juce::File defaultFile = defaultDir.getChildFile("BOOM_Roll.mid");

        juce::FileChooser fc("Save MIDI...", defaultFile, "*.mid");
        fc.launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
            [src](const juce::FileChooser& chooser)
            {
                juce::File dest = chooser.getResult();
                if (dest.getFullPathName().isEmpty())
                    return;

                if (!dest.hasFileExtension(".mid"))
                    dest = dest.withFileExtension(".mid");

                if (dest.existsAsFile())
                    dest.deleteFile();

                src.copyFileTo(dest);
            });
    };

    btnDragMidi.onClick = [this]
    {
        juce::File f = buildTempMidi();
        performFileDrag(f);
    };
}
void RollsWindow::paint(juce::Graphics& g)
{
    g.fillAll(boomtheme::MainBackground());
}

void RollsWindow::resized()
{
    auto r = getLocalBounds();
    auto bounds = getLocalBounds();

    // 700x447 reference
    const float W = 700.f, H = 447.f;
    auto sx = r.getWidth() / W, sy = r.getHeight() / H;
    auto S = [sx, sy](int x, int y, int w, int h)
    {
        return juce::Rectangle<int>(juce::roundToInt(x * sx), juce::roundToInt(y * sy),
            juce::roundToInt(w * sx), juce::roundToInt(h * sy));
    };
    styleBox.setBounds(S(240, 175, 100, 20));
    barsBox.setBounds(S(350, 175, 50, 20));
    // Centered buttons
    const int buttonWidth = 150;
    const int buttonHeight = 50;
    const int spacing = 20;
    const int totalWidth = (buttonWidth * 3) + (spacing * 2);
    int startX = (bounds.getWidth() - totalWidth) / 2;

    // Centered title image
    const int titleImageWidth = 258;
    const int titleImageX = (W - titleImageWidth) / 2;
    rollsTitleImg.setBounds(S(titleImageX, 15, titleImageWidth, 131));
    styleBox.setBounds(S(240, 205, 100, 20));
    barsBox.setBounds(S(350, 205, 50, 20));
    btnGenerate.setBounds(S(225, 255, 190, 60));
    btnSaveMidi.setBounds(startX, 225, buttonWidth, buttonHeight);
    startX += buttonWidth + spacing;
    btnDragMidi.setBounds(startX, 225, buttonWidth, buttonHeight);

    btnHome.setBounds(S(600, 350, 60, 60));
}

juce::File RollsWindow::buildTempMidi() const
{
    juce::MidiFile mf;
    boom::midi::DrumPattern mp;
    for (const auto& n : proc.getDrumPattern())
        mp.add({ n.row, n.startTick, n.lengthTicks, n.velocity });
    mf = boom::midi::buildMidiFromDrums(mp, 96);

    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("BOOM_Roll.mid");
    boom::midi::writeMidiToFile(mf, tmp);
    return tmp;
}

void RollsWindow::performFileDrag(const juce::File& f)
{
    if (!f.existsAsFile()) return;
    if (auto* dnd = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        juce::StringArray files; files.add(f.getFullPathName());
        dnd->performExternalDragDropOfFiles(files, true);
    }
} 