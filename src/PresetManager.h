#pragma once

/*  MasterDesk — preset system.

    • Factory presets compiled in (name → parameter overrides on top of
      defaults).
    • User presets as .mdpreset XML files in the per-user application data
      directory, discovered live.
    • Parameter locking: any parameter the user has locked keeps its value
      while presets are browsed — the lock set lives in the processor and is
      consulted on every preset load.
    • A/B snapshots with copy, for instant comparisons.
*/

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

class MasterDeskProcessor;

class PresetManager
{
public:
    PresetManager (juce::AudioProcessorValueTreeState& state,
                   std::function<bool (const juce::String&)> isParamLocked);

    //==========================================================================
    juce::StringArray getPresetNames() const;          // factory first, then user
    int  getNumFactoryPresets() const;
    void applyPreset (int index);
    void applyPresetByName (const juce::String& name);
    bool saveUserPreset (const juce::String& name);    // false if name invalid
    juce::File getUserPresetDirectory() const;

    juce::String getCurrentPresetName() const          { return currentName; }
    int getCurrentPresetIndex() const                  { return currentIndex; }

    //==========================================================================
    // A/B snapshots
    void storeCurrentTo (int slot);                    // 0 = A, 1 = B
    void recallSlot (int slot);
    void copySlot (int from, int to);
    int  getActiveSlot() const                         { return activeSlot; }
    void setActiveSlot (int slot);                     // stores current, recalls other

private:
    struct FactoryPreset
    {
        const char* name;
        std::vector<std::pair<const char*, float>> overrides;   // real-world values
    };

    static const std::vector<FactoryPreset>& factoryPresets();

    void applyValues (const std::vector<std::pair<const char*, float>>& values, bool resetOthers);
    void setParamReal (const juce::String& id, float realValue);
    juce::ValueTree captureState() const;
    void restoreState (const juce::ValueTree& v);

    juce::AudioProcessorValueTreeState& apvts;
    std::function<bool (const juce::String&)> isLocked;

    juce::String currentName { "Default" };
    int currentIndex = 0;

    juce::ValueTree slots[2];
    int activeSlot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};
