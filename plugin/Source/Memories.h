#pragma once

#include "ErEngine.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>

namespace glide90
{

// One SPX90 memory: everything but the INPUT knob.
struct Memory
{
    bool used = false;
    juce::String title;  // up to 16 characters, as on the LCD
    int algorithm = 17;  // the ROM program whose algorithm runs
    spx::ErSettings er {};
    float balance = 1.0f;
    float outLevel = 1.0f;
};

// Memories 1-30 are the factory programs (read only), 31-90 the user memories. The first few user
// memories come pre-filled with reverse gate variations.
class MemoryBank
{
public:
    static constexpr int kFirstUser = 31;
    static constexpr int kCount = 90;
    static constexpr int kTitleLength = 16;

    MemoryBank();

    Memory get(int number) const;
    // False (and nothing stored) for the read-only memories 1-30.
    bool store(int number, const Memory& m);

    juce::ValueTree toTree() const;
    void fromTree(const juce::ValueTree& tree);

    static Memory rom(int number);
    static juce::Identifier treeType() { return "MEMORIES"; }

private:
    void loadFactoryUserMemories();

    mutable juce::SpinLock lock;
    std::array<Memory, kCount - kFirstUser + 1> user;
};

} // namespace glide90
