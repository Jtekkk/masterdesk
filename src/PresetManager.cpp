#include "PresetManager.h"
#include "Parameters.h"

namespace
{
constexpr auto presetFileExt = ".mdpreset";
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state,
                              std::function<bool (const juce::String&)> isParamLocked)
    : apvts (state), isLocked (std::move (isParamLocked))
{
    slots[0] = captureState();
    slots[1] = slots[0].createCopy();
}

//==============================================================================
const std::vector<PresetManager::FactoryPreset>& PresetManager::factoryPresets()
{
    using namespace md::param;
    static const std::vector<FactoryPreset> presets {
        { "Default",         { } },
        { "Gentle Glue",     { { volume, 2.5f }, { foundation, 2.0f }, { thd, -66.0f },
                               { tone, 10.0f }, { compMode, 0.0f }, { stereoEnh, 4.0f } } },
        { "Loud & Proud",    { { volume, 6.5f }, { foundation, 4.0f }, { thd, -44.0f },
                               { tone, 30.0f }, { toneStyle, 0.0f }, { compMode, 1.0f },
                               { stereoEnh, 12.0f }, { ceiling, -0.1f } } },
        { "Warm Tape",       { { volume, 3.0f }, { foundation, 5.0f }, { thd, -38.0f },
                               { tone, 45.0f }, { toneStyle, 3.0f }, { compMode, 0.0f },
                               { analog, 160.0f }, { temperature, 35.0f } } },
        { "Airy Master",     { { volume, 2.0f }, { foundation, 1.5f }, { thd, -60.0f },
                               { tone, 55.0f }, { toneStyle, 2.0f }, { stereoEnh, 22.0f } } },
        { "Deep Foundation", { { volume, 3.5f }, { foundation, 7.5f }, { thd, -50.0f },
                               { tone, 12.0f }, { toneStyle, 3.0f }, { compMode, 0.0f } } },
        { "Presence Push",   { { volume, 4.0f }, { foundation, 2.5f }, { thd, -48.0f },
                               { tone, 40.0f }, { toneStyle, 1.0f }, { compMode, 1.0f },
                               { channelMode, 1.0f } } },
        { "Transparent TP",  { { volume, 1.0f }, { foundation, 0.5f }, { thd, -90.0f },
                               { tone, 0.0f }, { analog, 0.0f }, { autoGain, 1.0f },
                               { oversampling, 3.0f }, { phaseMode, 1.0f } } },
    };
    return presets;
}

int PresetManager::getNumFactoryPresets() const
{
    return (int) factoryPresets().size();
}

juce::File PresetManager::getUserPresetDirectory() const
{
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("MasterDesk").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    for (auto& fp : factoryPresets())
        names.add (fp.name);

    auto files = getUserPresetDirectory().findChildFiles (juce::File::findFiles, false,
                                                          "*" + juce::String (presetFileExt));
    files.sort();
    for (auto& f : files)
        names.add (f.getFileNameWithoutExtension());
    return names;
}

//==============================================================================
void PresetManager::setParamReal (const juce::String& id, float realValue)
{
    if (isLocked != nullptr && isLocked (id))
        return;

    if (auto* p = apvts.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (realValue));
        p->endChangeGesture();
    }
}

void PresetManager::applyValues (const std::vector<std::pair<const char*, float>>& values,
                                 bool resetOthers)
{
    if (resetOthers)
    {
        for (auto& id : md::param::allIds())
        {
            bool overridden = false;
            for (auto& [oid, ov] : values)
                if (id == oid) { overridden = true; break; }

            if (! overridden)
                if (auto* p = apvts.getParameter (id))
                    if (! (isLocked != nullptr && isLocked (id)))
                    {
                        p->beginChangeGesture();
                        p->setValueNotifyingHost (p->getDefaultValue());
                        p->endChangeGesture();
                    }
        }
    }

    for (auto& [id, v] : values)
        setParamReal (id, v);
}

void PresetManager::applyPreset (int index)
{
    auto names = getPresetNames();
    if (index < 0 || index >= names.size())
        return;

    const int numFactory = getNumFactoryPresets();
    if (index < numFactory)
    {
        applyValues (factoryPresets()[(size_t) index].overrides, true);
    }
    else
    {
        auto file = getUserPresetDirectory().getChildFile (names[index] + presetFileExt);
        if (auto xml = juce::parseXML (file))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            if (tree.isValid())
                restoreState (tree);
        }
    }

    currentIndex = index;
    currentName  = names[index];
}

void PresetManager::applyPresetByName (const juce::String& name)
{
    auto names = getPresetNames();
    const int idx = names.indexOf (name);
    if (idx >= 0)
        applyPreset (idx);
}

bool PresetManager::saveUserPreset (const juce::String& nameIn)
{
    const auto name = juce::File::createLegalFileName (nameIn.trim());
    if (name.isEmpty())
        return false;

    auto file = getUserPresetDirectory().getChildFile (name + presetFileExt);
    if (auto xml = captureState().createXml())
    {
        if (! xml->writeTo (file))
            return false;
        currentName = name;
        currentIndex = getPresetNames().indexOf (name);
        return true;
    }
    return false;
}

//==============================================================================
juce::ValueTree PresetManager::captureState() const
{
    juce::ValueTree v ("MasterDeskPreset");
    for (auto& id : md::param::allIds())
        if (auto* p = apvts.getParameter (id))
            v.setProperty (id, p->convertFrom0to1 (p->getValue()), nullptr);
    return v;
}

void PresetManager::restoreState (const juce::ValueTree& v)
{
    if (! v.isValid())
        return;
    for (auto& id : md::param::allIds())
        if (v.hasProperty (id))
            setParamReal (id, (float) (double) v.getProperty (id));
}

//==============================================================================
void PresetManager::storeCurrentTo (int slot)
{
    slots[slot & 1] = captureState();
}

void PresetManager::recallSlot (int slot)
{
    restoreState (slots[slot & 1]);
}

void PresetManager::copySlot (int from, int to)
{
    slots[to & 1] = slots[from & 1].createCopy();
}

void PresetManager::setActiveSlot (int slot)
{
    slot &= 1;
    if (slot == activeSlot)
        return;
    storeCurrentTo (activeSlot);
    activeSlot = slot;
    recallSlot (activeSlot);
}
