#include "Memories.h"

#include "RomPrograms.h"

namespace glide90
{
namespace
{

const juce::Identifier kMemory { "MEMORY" };
const juce::Identifier kNumber { "number" }, kTitle { "title" }, kAlgorithm { "algorithm" }, kType { "type" },
    kDensity { "density" }, kRoomSize { "roomSize" }, kLiveness { "liveness" }, kDelay { "delayMs" }, kLpf { "lpfHz" },
    kBalance { "balance" }, kOutLevel { "outLevel" };

Memory variation(int program, const char* title, float roomSize, int liveness, float delayMs, float lpfHz = 0.0f)
{
    Memory m = MemoryBank::rom(program);
    m.title = title;
    m.er.roomSize = roomSize;
    m.er.liveness = liveness;
    m.er.delayMs = delayMs;
    m.er.lpfHz = lpfHz;
    return m;
}

} // namespace

MemoryBank::MemoryBank()
{
    loadFactoryUserMemories();
}

Memory MemoryBank::rom(int number)
{
    const auto& p = spx::romProgram(juce::jlimit(1, 30, number));
    Memory m;
    m.used = true;
    m.title = juce::String(p.name.data(), p.name.size());
    m.algorithm = p.number;
    m.er = p.er;
    m.balance = p.balance;
    return m;
}

void MemoryBank::loadFactoryUserMemories()
{
    const juce::SpinLock::ScopedLockType sl(lock);
    user.fill({});
    // Titled by their settings (ROOM SIZE / LIVENESS / DELAY). The first three follow accounts of
    // program 17 at the largest ROOM SIZE with BALANCE and OUT LVL at 100 %: LIVENESS 3-5 with the
    // delay unknown (factory 25 ms kept), and two reconstructions, LIVENESS 6-7 at 75 ms and
    // LIVENESS 10 with no delay. The rest are starting points of our own.
    user[0] = variation(17, "REV GATE 20/4", 20.0f, 4, 25.0f);
    user[1] = variation(17, "REV GATE 20/7/75", 20.0f, 7, 75.0f);
    user[2] = variation(17, "REV GATE 20/10/0", 20.0f, 10, 0.1f);
    user[3] = variation(17, "REV GATE DARK", 20.0f, 5, 25.0f, 2500.0f); // a guitar with its tone knob down
    user[4] = variation(17, "REV GATE SHORT", 8.0f, 5, 10.0f);
    user[5] = variation(16, "GATE SLAP", 1.0f, 3, 5.0f, 6300.0f);
}

Memory MemoryBank::get(int number) const
{
    if (number < kFirstUser)
        return rom(number);
    const juce::SpinLock::ScopedLockType sl(lock);
    return user[(size_t) (juce::jmin(number, kCount) - kFirstUser)];
}

bool MemoryBank::store(int number, const Memory& m)
{
    if (number < kFirstUser || number > kCount)
        return false;
    const juce::SpinLock::ScopedLockType sl(lock);
    auto& slot = user[(size_t) (number - kFirstUser)];
    slot = m;
    slot.used = true;
    slot.title = m.title.substring(0, kTitleLength);
    return true;
}

juce::ValueTree MemoryBank::toTree() const
{
    juce::ValueTree tree(treeType());
    const juce::SpinLock::ScopedLockType sl(lock);
    for (size_t i = 0; i < user.size(); ++i)
    {
        const auto& m = user[i];
        if (!m.used)
            continue;
        juce::ValueTree t(kMemory);
        t.setProperty(kNumber, (int) i + kFirstUser, nullptr)
            .setProperty(kTitle, m.title, nullptr)
            .setProperty(kAlgorithm, m.algorithm, nullptr)
            .setProperty(kType, (int) m.er.type, nullptr)
            .setProperty(kDensity, m.er.density, nullptr)
            .setProperty(kRoomSize, m.er.roomSize, nullptr)
            .setProperty(kLiveness, m.er.liveness, nullptr)
            .setProperty(kDelay, m.er.delayMs, nullptr)
            .setProperty(kLpf, m.er.lpfHz, nullptr)
            .setProperty(kBalance, m.balance, nullptr)
            .setProperty(kOutLevel, m.outLevel, nullptr);
        tree.appendChild(t, nullptr);
    }
    return tree;
}

void MemoryBank::fromTree(const juce::ValueTree& tree)
{
    if (!tree.hasType(treeType()))
        return; // older or foreign state: keep the factory user memories

    // Stored memories over the factory ones: a memory added in a later version stays filled.
    loadFactoryUserMemories();
    const juce::SpinLock::ScopedLockType sl(lock);
    for (const auto& t : tree)
    {
        const int number = t.getProperty(kNumber, 0);
        if (!t.hasType(kMemory) || number < kFirstUser || number > kCount)
            continue;
        Memory m;
        m.used = true;
        m.title = t.getProperty(kTitle).toString().substring(0, kTitleLength);
        m.algorithm = juce::jlimit(1, 30, (int) t.getProperty(kAlgorithm, 17));
        m.er.type = (spx::ErType) juce::jlimit(0, 3, (int) t.getProperty(kType, 2));
        m.er.density = juce::jlimit(1, 2, (int) t.getProperty(kDensity, 2));
        m.er.roomSize = juce::jlimit(0.1f, 20.0f, (float) t.getProperty(kRoomSize, 3.3f));
        m.er.liveness = juce::jlimit(0, 10, (int) t.getProperty(kLiveness, 5));
        m.er.delayMs = juce::jlimit(0.1f, 400.0f, (float) t.getProperty(kDelay, 25.0f));
        m.er.lpfHz = juce::jlimit(0.0f, 11000.0f, (float) t.getProperty(kLpf, 0.0f));
        m.balance = juce::jlimit(0.0f, 1.0f, (float) t.getProperty(kBalance, 1.0f));
        m.outLevel = juce::jlimit(0.0f, 1.0f, (float) t.getProperty(kOutLevel, 1.0f));
        user[(size_t) (number - kFirstUser)] = m;
    }
}

} // namespace glide90
