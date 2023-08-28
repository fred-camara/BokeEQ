/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"


void LookAndFeel::drawRotarySlider(juce::Graphics & g,
                                   int x,
                                   int y,
                                   int width,
                                   int height,
                                   float sliderPosProportional,
                                   float rotaryStartAngle,
                                   float rotaryEndAngle,
                                   juce::Slider & slider)
{
    using namespace juce;
    
    // bounding box
    auto bounds = Rectangle<float>(x, y, width, height);
    // navy blue
    g.setColour(Colour(0,0,128));
    g.fillEllipse(bounds);
    
    g.setColour(Colour(205,92,92));
    g.drawEllipse(bounds, 1.f);
    
    if (auto* rswl = dynamic_cast<RotarySliderWithLabels*>(&slider))
    {
        auto center = bounds.getCentre();
        Path p;
        Rectangle<float> r;
        // this is the actual slider thingy
        r.setLeft(center.getX() - 2);
        r.setRight(center.getX() + 2);
        r.setTop(bounds.getY());
        r.setBottom(center.getY() - rswl->getTextHeight() * 1.5);
        p.addRoundedRectangle(r, 2.f);
        jassert (rotaryStartAngle < rotaryEndAngle);
        
        // determines start and end limits for rotary slider values
        auto sliderAngRad = jmap(sliderPosProportional, 0.f, 1.f, rotaryStartAngle, rotaryEndAngle);
        
        p.applyTransform(AffineTransform().rotated(sliderAngRad, center.getX(), center.getY()));
        g.fillPath(p);
        
        g.setFont(rswl->getTextHeight());
        auto text = rswl->getDisplayString();
        auto strWidth = g.getCurrentFont().getStringWidth(text);
        
        r.setSize(strWidth + 4, rswl->getTextHeight() + 2);
        r.setCentre(bounds.getCentre());
        
        g.setColour(Colours::black);
        g.fillRect(r);
        g.setColour(Colours::white);
        g.drawFittedText(text, r.toNearestInt(), juce::Justification::centred, 1);
    }
    
}
  

void RotarySliderWithLabels::paint(juce::Graphics &g)
{
    using namespace juce;
    // 7 oclock is 0 and 5 oclock (using radians)
    
    auto startAng = degreesToRadians(180.f + 45.f);
    // Pi adds the rotation
    auto endAng = degreesToRadians(180.f - 45.f) + MathConstants<float>::twoPi;
    
    // normalized value
    auto range = getRange();
    auto sliderBounds = getSliderBounds();
    
//    g.setColour(Colour(205,92,92));
//    g.drawRect(getLocalBounds());
//    g.setColour(Colours::blue);
//    g.drawRect(sliderBounds);
    
    
    getLookAndFeel().drawRotarySlider(g,
                                      sliderBounds.getX(),
                                      sliderBounds.getY(),
                                      sliderBounds.getWidth(),
                                      sliderBounds.getHeight(),
                                      jmap(getValue(),
                                           range.getStart(),
                                           range.getEnd(),
                                           0.0,
                                           1.0),
                                      startAng,
                                      endAng,
                                      *this);
    
}

juce::Rectangle<int> RotarySliderWithLabels::getSliderBounds() const
{
    auto bounds = getLocalBounds();
    
    auto size = juce::jmin(bounds.getWidth(), bounds.getHeight());
    
    size -= getTextHeight() * 2;
    
    juce::Rectangle<int> r;
    r.setSize(size, size);
    
    r.setCentre(bounds.getCentreX(), 0);
    r.setY(2);
    
    return r;
}

juce::String RotarySliderWithLabels::getDisplayString() const
{
    // this would just get the current value
    // return juce::String(getValue());
    
    // choiceParam is a type of AudioParameterChoice
    if (auto* choiceParam = dynamic_cast<juce::AudioParameterChoice*>(param))
        return choiceParam->getCurrentChoiceName();
    
    juce::String str;
    bool addK = false;
    
    // floatParam is another tpye of parameter
    if (auto* floatParam = dynamic_cast<juce::AudioParameterFloat*>(param)) {
        float val = getValue();
        if (val > 999.f )
        {
            val /= 1000.f;
            addK = true;
        }
        str = juce::String(val, (addK ? 2 : 0));
        
    }
    else {
        jassertfalse;
    }
    
    if (suffix.isNotEmpty() ) {
        str << " ";
        if (addK)
            str << "k";
        str << suffix;
    }
    
    return str;
};


ResponseCurveComponent::ResponseCurveComponent(BokeEQAudioProcessor& p) : audioProcessor(p)
{
    const auto& params = audioProcessor.getParameters();
    
    for (auto param : params )
    {
        param->addListener(this);
    }
    
    startTimerHz(60);
}


// destructor
ResponseCurveComponent::~ResponseCurveComponent()
{
    const auto& params = audioProcessor.getParameters();
    
    for (auto param : params )
    {
        param->removeListener(this);
    }
}

void ResponseCurveComponent::parameterValueChanged(int parameterIndex, float newValue)
{
    parametersChanged.set(true);
};

void ResponseCurveComponent::timerCallback()
{
    if (parametersChanged.compareAndSetBool(false, true) )
    {
        // update the monochain
        double sampleRate = audioProcessor.getSampleRate();
        auto chainSettings = getChainSettings(audioProcessor.apvts);
        auto peakCoefficients = makePeakFilter(chainSettings, sampleRate);
        updateCoefficients(monoChain.get<ChainPositions::Peak>().coefficients, peakCoefficients);
        
        auto lowCutCoefficients = makeLowCutFilter(chainSettings, sampleRate);
        auto highCutCoefficients = makeHighCutFilter(chainSettings, sampleRate);
        
        updateCutFilter(monoChain.get<ChainPositions::LowCut>(), lowCutCoefficients, chainSettings.lowCutSlope);
        updateCutFilter(monoChain.get<ChainPositions::HighCut>(), highCutCoefficients, chainSettings.highCutSlope);
    
        // signal a repaint
        repaint();
    }
};


//==============================================================================
void ResponseCurveComponent::paint (juce::Graphics& g)
{
    using namespace juce;
    
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (Colours::black);
    
    auto responseArea = getLocalBounds();
    
    auto w = responseArea.getWidth();

    auto& lowcut = monoChain.get<ChainPositions::LowCut>();
    auto& peak = monoChain.get<ChainPositions::Peak>();
    auto& highcut = monoChain.get<ChainPositions::HighCut>();
    
    auto sampleRate = audioProcessor.getSampleRate();

    std::vector<double> mags;
    mags.resize(w);

    for (int i = 0; i < w; ++i)
    {
        double mag = 1.f;
        auto freq = mapToLog10(double(i) / double(w), 20.0, 20000.0);
        if (!monoChain.isBypassed<ChainPositions::Peak>() )
            mag *= peak.coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<0>() )
            mag *= lowcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<1>() )
            mag *= lowcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<2>() )
            mag *= lowcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!lowcut.isBypassed<3>() )
            mag *= lowcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        
        if (!highcut.isBypassed<0>() )
            mag *= highcut.get<0>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<1>() )
            mag *= highcut.get<1>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<2>() )
            mag *= highcut.get<2>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        if (!highcut.isBypassed<3>() )
            mag *= highcut.get<3>().coefficients->getMagnitudeForFrequency(freq, sampleRate);
        mags[i] = Decibels::gainToDecibels(mag);
    }
    
    Path responseCurve;
    
    const double outputMin = responseArea.getBottom();
    const double outputMax = responseArea.getY();
    
    auto map = [outputMin, outputMax](double input)
    {
        return jmap(input, -24.0, 24.0, outputMin, outputMax);
    };
    
    responseCurve.startNewSubPath(responseArea.getX(), map(mags.front()));
    
    for (size_t i = 1; i < mags.size(); i++)
    {
        responseCurve.lineTo(responseArea.getX() + i, map(mags[i]));
    }
    // steel blue
    g.setColour(Colour(176,196,222));
    g.drawRoundedRectangle(responseArea.toFloat(), 4.f, 1.f);
    
    g.setColour(Colours::white);
    g.strokePath(responseCurve, PathStrokeType(2.f));

}

//==============================================================================
BokeEQAudioProcessorEditor::BokeEQAudioProcessorEditor (BokeEQAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p),
peakFreqSlider(*audioProcessor.apvts.getParameter("Peak Freq"), "Hz"),
peakGainSlider(*audioProcessor.apvts.getParameter("Peak Gain"), "db"),
peakQualitySlider(*audioProcessor.apvts.getParameter("Peak Quality"), ""),
lowCutFreqSlider(*audioProcessor.apvts.getParameter("LowCut Freq"), "Hz"),
highCutFreqSlider(*audioProcessor.apvts.getParameter("HighCut Freq"), "Hz"),
lowCutSlopeSlider(*audioProcessor.apvts.getParameter("LowCut Slope"), "db/Oct"),
highCutSlopeSlider(*audioProcessor.apvts.getParameter("HighCut Slope"), "db/Oct"),
responseCurveComponent(audioProcessor),
    peakFreqSliderAttachment(audioProcessor.apvts, "Peak Freq", peakFreqSlider),
peakGainSliderAttachment(audioProcessor.apvts, "Peak Gain", peakGainSlider),
peakQualitySliderAttachment(audioProcessor.apvts, "Peak Quality", peakQualitySlider),
lowCutFreqSliderAttachment(audioProcessor.apvts, "LowCut Freq", lowCutFreqSlider),
highCutFreqSliderAttachment(audioProcessor.apvts, "HighCut Freq", highCutFreqSlider),
lowCutSlopeSliderAttachment(audioProcessor.apvts, "LowCut Slope", lowCutSlopeSlider),
highCutSlopeSliderAttachment(audioProcessor.apvts, "HighCut Slope", highCutSlopeSlider)
{
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    
    for (auto* comp : getComps())
    {
        addAndMakeVisible(comp);
    }
    
    setSize (600, 400);
}

BokeEQAudioProcessorEditor::~BokeEQAudioProcessorEditor()
{
    
}

//==============================================================================
void BokeEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    
    // (Our component is opaque, so we must completely fill the background with a solid colour) (gray)
    g.fillAll(Colour(211,211,211));
    
    auto bounds = getLocalBounds();

}

void BokeEQAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
    auto bounds = getLocalBounds();
    auto responseArea = bounds.removeFromTop(bounds.getHeight() * 0.33);
    
    responseCurveComponent.setBounds(responseArea);
    
    auto lowCutArea = bounds.removeFromLeft(bounds.getWidth() * 0.33);
    auto highCutArea = bounds.removeFromRight(bounds.getWidth() * 0.5);
    
    lowCutFreqSlider.setBounds(lowCutArea.removeFromTop(lowCutArea.getHeight() * 0.5));
    lowCutSlopeSlider.setBounds(lowCutArea);
    highCutFreqSlider.setBounds(highCutArea.removeFromTop(highCutArea.getHeight() * 0.5));
    highCutSlopeSlider.setBounds(highCutArea);
    
    peakFreqSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.33));
    peakGainSlider.setBounds(bounds.removeFromTop(bounds.getHeight() * 0.5));
    peakQualitySlider.setBounds(bounds);
     
}


// this is for the pieces in the GUI
std::vector<juce::Component*> BokeEQAudioProcessorEditor::getComps()
{
    return {
        &peakFreqSlider,
        &peakGainSlider,
        &peakQualitySlider,
        &lowCutFreqSlider,
        &highCutFreqSlider,
        &lowCutSlopeSlider,
        &highCutSlopeSlider,
        &responseCurveComponent,
    };
}
