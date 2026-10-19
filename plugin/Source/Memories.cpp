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

Memory reverseGate(const char* title, float roomSize, int liveness, float delayMs)
{
    Memory m = MemoryBank::rom(17);
    m.title = title;
    m.er.roomSize = roomSize;
    m.er.liveness = liveness;
    m.er.delayMs = delayMs;
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
    // Accounts differ; all three use program 17 at the largest ROOM SIZE with BALANCE and OUT LVL at
    // 100 %: a former roadie's (LIVENESS 3-5), and two published reconstructions.
    user[0] = reverseGate("LOVELESS RG ROAD", 20.0f, 4, 25.0f);
    user[1] = reverseGate("LOVELESS RG A", 20.0f, 6, 75.0f);
    user[2] = reverseGate("LOVELESS RG B", 20.0f, 10, 0.1f);
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

    const juce::SpinLock::ScopedLockType sl(lock);
    user.fill({});
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
