#pragma once
#include <atomic>
#include <map>
#include <string>

namespace loader033 {
// Recursion suppression belongs to the calling thread, not to the process.
// No mutex may be held across LoadLibrary/COM calls under the loader lock.
class CallScope {
    struct Registry {
        std::map<unsigned, std::string> skipped;
        std::map<unsigned, unsigned> original;
    };
    static Registry& Current() { static thread_local Registry state; return state; }
    inline static std::atomic<unsigned> nextOwner{0};
public:
    static unsigned GetOwner() {
        unsigned owner;
        do { owner = nextOwner.fetch_add(1, std::memory_order_relaxed) + 1; } while (!owner);
        return owner;
    }
    static void DisableChecks(unsigned owner, std::string dllName = "") {
        if (owner) Current().skipped[owner] = std::move(dllName);
    }
    static void EnableChecks(unsigned owner) { Current().skipped.erase(owner); }
    static bool SkipDllChecks() { return !Current().skipped.empty(); }
    static std::string SkipDllName() {
        const auto& scopes = Current().skipped;
        return scopes.empty() ? std::string{} : scopes.rbegin()->second;
    }
    // Explicit original-library requests must not overwrite skip-check owners.
    static void EnableServeOriginal(unsigned owner) { if (owner) ++Current().original[owner]; }
    static void DisableServeOriginal(unsigned owner) {
        auto& scopes = Current().original;
        auto found = scopes.find(owner);
        if (found != scopes.end() && --found->second == 0) scopes.erase(found);
    }
    static bool ServeOriginal() { return !Current().original.empty(); }
};

// Factory discovery may recurse or race another thread. Defer without waiting.
class TryHook {
    std::atomic_flag& busy;
    bool owned;
public:
    explicit TryHook(std::atomic_flag& value) : busy(value), owned(!busy.test_and_set(std::memory_order_acquire)) {}
    ~TryHook() { if (owned) busy.clear(std::memory_order_release); }
    explicit operator bool() const { return owned; }
    TryHook(const TryHook&) = delete;
    TryHook& operator=(const TryHook&) = delete;
};
}
