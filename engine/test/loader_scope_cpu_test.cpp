#include "../src/loader_call_scope.h"
#include <atomic>
#include <future>
#include <thread>
#include <vector>
#include <cstdio>

#ifdef K033_TEST_LEGACY
#include "fixtures/loader_scope_v32.h"
using Scope = LegacyScope;
#else
using Scope = loader033::CallScope;
#endif
static std::atomic<unsigned> checks{0}, failures{0};
static void Check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL %s\n", label); }
}
int main() {
    Check(!Scope::SkipDllChecks() && !Scope::ServeOriginal(), "clean thread");
    const auto outer = Scope::GetOwner(), inner = Scope::GetOwner();
    Scope::DisableChecks(outer, "dxgi"); Scope::DisableChecks(inner, "sl.interposer");
    Scope::EnableChecks(outer);
    Check(Scope::SkipDllName() == "sl.interposer", "out-of-order cleanup only removes its owner");
    Scope::EnableChecks(outer);
    Check(Scope::SkipDllChecks(), "duplicate cleanup preserves active inner call");
    Scope::EnableChecks(inner);
    Check(!Scope::SkipDllChecks(), "all checks restored");
    Scope::DisableChecks(outer, "dxgi");
    Scope::EnableServeOriginal(201); Scope::EnableServeOriginal(201); Scope::EnableServeOriginal(202);
    Scope::DisableServeOriginal(201);
    Check(Scope::ServeOriginal() && Scope::SkipDllName() == "dxgi", "original request does not corrupt check scope");
    Scope::DisableServeOriginal(202); Scope::DisableServeOriginal(999);
    Check(Scope::ServeOriginal(), "nested same-owner request survives inner cleanup");
    Scope::DisableServeOriginal(201);
    Check(!Scope::ServeOriginal() && Scope::SkipDllChecks(), "independent original/check scope lifetime");
    Scope::EnableChecks(outer);

    // Deterministic reproduction of the old cross-thread leak, without racing
    // writes into an unsafe std::map or deliberately corrupting the test heap.
    std::promise<void> livePromise, inspectedPromise;
    auto live = livePromise.get_future(), inspected = inspectedPromise.get_future();
    std::thread background([&] {
        auto token = Scope::GetOwner();
        Scope::DisableChecks(token, "dxgi"); Scope::EnableServeOriginal(201);
        livePromise.set_value(); inspected.wait();
        Check(Scope::SkipDllName() == "dxgi" && Scope::ServeOriginal(), "worker retains its own scopes");
        Scope::EnableChecks(token); Scope::DisableServeOriginal(201);
    });
    live.wait();
    Check(!Scope::SkipDllChecks() && !Scope::ServeOriginal(), "main thread cannot see worker suppression");
    const auto token = Scope::GetOwner(); Scope::DisableChecks(token, "sl."); Scope::EnableChecks(token);
    inspectedPromise.set_value(); background.join();
    Check(!Scope::SkipDllChecks(), "worker exit leaves main clean");

    if (failures.load()) {
        std::printf("LOADER SCOPE deterministic regression: %u checks, %u failures; stopping before concurrent writes\n", checks.load(), failures.load());
        return 1;
    }
    std::vector<std::thread> workers;
    for (unsigned t=0; t<8; ++t) workers.emplace_back([t] {
        for (unsigned i=0; i<2000; ++i) {
            auto a=Scope::GetOwner(), b=Scope::GetOwner();
            std::string name="thread-"+std::to_string(t);
            Scope::DisableChecks(a, name); Scope::DisableChecks(b, "");
            Check(Scope::SkipDllChecks() && Scope::SkipDllName().empty(), "inner all-library suppression");
            Scope::EnableChecks(b);
            Check(Scope::SkipDllName()==name, "parallel nested call restores correct thread filter");
            Scope::EnableChecks(a);
            Check(!Scope::SkipDllChecks(), "parallel scope cleanup complete");
        }
    });
    for (auto& worker:workers) worker.join();

    std::atomic_flag busy=ATOMIC_FLAG_INIT;
    {
        loader033::TryHook first(busy); Check(bool(first), "first hook attempt admitted");
        { loader033::TryHook nested(busy); Check(!nested, "recursive attempt defers without waiting"); }
        std::thread other([&] { loader033::TryHook racing(busy); Check(!racing, "other thread defers while attempt owns state"); });
        other.join();
        loader033::TryHook afterNested(busy); Check(!afterNested, "unsuccessful attempt cannot release another owner");
    }
    loader033::TryHook retry(busy); Check(bool(retry), "later factory discovery can retry");
    std::printf("LOADER SCOPE CPU: %u checks, %u failures; no DLL hooks, devices or games\n", checks.load(), failures.load());
    return failures.load()?1:0;
}
