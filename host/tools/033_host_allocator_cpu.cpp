// CPU-only fixture. The build extracts unedited production method bodies into
// host_allocator_under_test.inl; these objects have no D3D/COM implementation.
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#define RESHADE_ADDON 0
#define RESHADE_VERBOSE_LOG 0
#define STDMETHODCALLTYPE
#define SUCCEEDED(x) ((x) >= 0)
#define FAILED(x) ((x) < 0)
#define IID_PPV_ARGS(p) test_iid, reinterpret_cast<void **>(p)
using HRESULT = int32_t;
using UINT = unsigned;
using UINT64 = uint64_t;
using HANDLE = void *;
using REFIID = const int &;
constexpr int test_iid = 1, FALSE = 0, D3D12_FENCE_FLAG_NONE = 0;
constexpr unsigned INFINITE = ~0u;
enum D3D12_COMMAND_LIST_TYPE { D3D12_COMMAND_LIST_TYPE_DIRECT, D3D12_COMMAND_LIST_TYPE_BUNDLE, D3D12_COMMAND_LIST_TYPE_COMPUTE, D3D12_COMMAND_LIST_TYPE_COPY, VIDEO };
using D3D12_COMMAND_LIST_FLAGS = unsigned;
static unsigned checks = 0, failures = 0, log_count = 0, event_clock = 0;
static void check(bool value, const char *name) { ++checks; if (!value) { ++failures; std::printf("FAIL %s\n", name); } }
static HANDLE CreateEvent(void *, bool, bool, void *) { return reinterpret_cast<void *>(1); }
static void CloseHandle(HANDLE) {}
static void WaitForSingleObject(HANDLE, unsigned) {}
namespace reshade::log {
enum class level { warning, error };
template <typename... T> void message(level, const char *, T...) { ++log_count; }
static std::string iid_to_string(REFIID) { return "fake"; }
static std::string hr_to_string(HRESULT) { return "fake"; }
}
struct ID3D12PipelineState {};
struct ID3D12CommandAllocator { HRESULT reset_result = 0; unsigned resets = 0; HRESULT Reset() { ++resets; return reset_result; } };
struct ID3D12GraphicsCommandList {
    HRESULT reset_result = 0, close_result = 0;
    ID3D12CommandAllocator *last_allocator = nullptr;
    ID3D12PipelineState *last_pipeline = nullptr;
    unsigned resets = 0, releases = 0, native_event = 0;
    HRESULT Reset(ID3D12CommandAllocator *a, ID3D12PipelineState *p) { ++resets; last_allocator = a; last_pipeline = p; native_event = ++event_clock; return reset_result; }
    HRESULT Close() { return close_result; }
    void Release() { ++releases; }
    void SetName(const wchar_t *) {}
};
using ID3D12CommandList = ID3D12GraphicsCommandList;
struct ID3D12Fence {
    UINT64 completed = 1000000;
    UINT64 GetCompletedValue() { return completed; }
    HRESULT SetEventOnCompletion(UINT64, HANDLE) { return 0; }
};
struct ID3D12CommandQueue {
    unsigned executes = 0;
    void ExecuteCommandLists(UINT, ID3D12CommandList *const *) { ++executes; }
    HRESULT Signal(ID3D12Fence *, UINT64) { return 0; }
};
struct ID3D12Device4 {
    HRESULT create_result = 0;
    ID3D12GraphicsCommandList *next = nullptr;
    std::vector<ID3D12GraphicsCommandList *> lists;
    std::vector<ID3D12CommandAllocator *> allocators;
    std::vector<ID3D12Fence *> fences;
    ~ID3D12Device4() { for (auto p : lists) delete p; for (auto p : allocators) delete p; for (auto p : fences) delete p; }
    HRESULT CreateCommandList(UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator *a, ID3D12PipelineState *, REFIID, void **out) {
        if (FAILED(create_result)) return create_result;
        auto list = next; next = nullptr;
        if (!list) { list = new ID3D12GraphicsCommandList; lists.push_back(list); }
        list->last_allocator = a; list->native_event = ++event_clock; *out = list; return create_result;
    }
    HRESULT CreateCommandList1(UINT n, D3D12_COMMAND_LIST_TYPE t, D3D12_COMMAND_LIST_FLAGS, REFIID id, void **out) { return CreateCommandList(n, t, nullptr, nullptr, id, out); }
    HRESULT CreateFence(UINT64, int, REFIID, void **out) { auto p = new ID3D12Fence; fences.push_back(p); *out = p; return 0; }
    HRESULT CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE, REFIID, void **out) { auto p = new ID3D12CommandAllocator; allocators.push_back(p); *out = p; return 0; }
};
struct D3D12Device {
    ID3D12Device4 *_orig;
    unsigned _interface_version = 4;
    HRESULT CreateCommandList(UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator *, ID3D12PipelineState *, REFIID, void **);
    HRESULT CreateCommandList1(UINT, D3D12_COMMAND_LIST_TYPE, D3D12_COMMAND_LIST_FLAGS, REFIID, void **);
};
static bool support_interface = true;
struct D3D12GraphicsCommandList {
    ID3D12GraphicsCommandList *_orig;
    void *_current_root_signature[2] {}, *_current_descriptor_heaps[2] {};
    D3D12GraphicsCommandList(D3D12Device *, ID3D12GraphicsCommandList *p) : _orig(p) {}
    bool check_and_upgrade_interface(REFIID) { return support_interface; }
    HRESULT Reset(ID3D12CommandAllocator *, ID3D12PipelineState *);
};
template <typename T> struct fake_ptr { T *p = nullptr; T *get() { return p; } T *operator->() { return p; } T **operator&() { return &p; } };
namespace reshade::d3d12 {
struct device_impl { ID3D12Device4 *_orig; };
struct command_list_impl {
    device_impl *_device;
    ID3D12GraphicsCommandList *_orig;
    bool _has_commands = false;
    void *_current_root_signature[2] {}, *_current_descriptor_heaps[2] {};
    command_list_impl(device_impl *d, ID3D12GraphicsCommandList *p) : _device(d), _orig(p) {}
};
struct command_list_immediate_impl : command_list_impl {
    static thread_local command_list_immediate_impl *s_last_immediate_command_list;
    static constexpr unsigned NUM_COMMAND_FRAMES = 3;
    ID3D12CommandQueue *_parent_queue;
    fake_ptr<ID3D12Fence> _fence[NUM_COMMAND_FRAMES];
    fake_ptr<ID3D12CommandAllocator> _cmd_alloc[NUM_COMMAND_FRAMES];
    UINT64 _fence_value[NUM_COMMAND_FRAMES] {};
    unsigned _cmd_index = 0, init_count = 0;
    HANDLE _fence_event = nullptr;
    std::vector<std::pair<ID3D12Fence *, UINT64>> _current_query_fences;
    command_list_immediate_impl(device_impl *, ID3D12CommandQueue *);
    void on_init() { ++init_count; }
    bool flush(bool);
};
thread_local command_list_immediate_impl *command_list_immediate_impl::s_last_immediate_command_list = nullptr;
}

// Includes the production atomic callback declarations, exact forwarder, two
// device Create methods, wrapper Reset, and full immediate constructor/flush.
#include "host_allocator_under_test.inl"
struct Event { uint32_t version; ID3D12GraphicsCommandList *list; ID3D12CommandAllocator *allocator; unsigned native_event, bind_event; };
static std::vector<Event> events;
static int backend_status = 0;
static int __cdecl sink(uint32_t v, ID3D12GraphicsCommandList *l, ID3D12CommandAllocator *a) { events.push_back({v,l,a,l->native_event,++event_clock}); return backend_status; }
static void clean() { events.clear(); backend_status = 0; core_allocator.store(sink); allocator_failure_logged.store(false); support_interface = true; }
static void pair_is(ID3D12GraphicsCommandList *l, ID3D12CommandAllocator *a) {
    check(events.size() == 1, "one notification"); if (events.size() != 1) return;
    check(events[0].version == 1, "ABI version 1"); check(events[0].list == l, "native list identity"); check(events[0].allocator == a, "actual allocator identity");
    check(events[0].native_event && events[0].native_event < events[0].bind_event, "notify after native return");
}
int main() {
    ID3D12GraphicsCommandList raw;
    ID3D12CommandAllocator a, b;
    ID3D12PipelineState pipeline;
    clean(); core_allocator.store(nullptr); k033_bind_command_allocator(&raw,&a); check(events.empty(), "unpublished callback no-op");
    core_allocator.store(sink); k033_bind_command_allocator(nullptr,&a); k033_bind_command_allocator(&raw,nullptr); check(events.empty(), "null pair no-op");
    clean(); backend_status = -2; const auto logs_before = log_count; k033_bind_command_allocator(&raw,&a); k033_bind_command_allocator(&raw,&b);
    check(events.size() == 2, "failed observer still receives future exact pairs"); check(log_count == logs_before + 1, "failure log once");
    ID3D12Device4 native; D3D12Device dev { &native };
    void *out = nullptr;
    clean(); native.create_result = -9; check(dev.CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,&a,nullptr,test_iid,&out) == -9, "create HRESULT unchanged"); check(events.empty(), "failed create no bind");
    clean(); native.create_result = 0; native.next = &raw; check(dev.CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,&a,nullptr,test_iid,&out) == 0, "create success"); pair_is(&raw,&a); check(out != &raw, "published wrapper differs from native"); delete static_cast<D3D12GraphicsCommandList *>(out);
    clean(); support_interface = false; native.next = &raw; dev.CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,&a,nullptr,test_iid,&out); check(events.empty(), "unsupported wrapper no bind"); check(out == &raw, "unsupported original unchanged");
    clean(); native.next = &raw; dev.CreateCommandList(0,VIDEO,&a,nullptr,test_iid,&out); check(events.empty(), "video list excluded");
    clean(); native.next = &raw; dev.CreateCommandList1(0,D3D12_COMMAND_LIST_TYPE_DIRECT,0,test_iid,&out); check(events.empty(), "CreateCommandList1 allocator stays unknown"); delete static_cast<D3D12GraphicsCommandList *>(out);
    D3D12GraphicsCommandList wrapper(&dev,&raw);
    clean(); raw.reset_result = -7; check(wrapper.Reset(&b,&pipeline) == -7, "reset failure HRESULT unchanged"); check(events.empty(), "failed reset no bind");
    clean(); raw.reset_result = 0; check(wrapper.Reset(&b,&pipeline) == 0, "reset success"); pair_is(&raw,&b); check(raw.last_pipeline == &pipeline, "pipeline argument preserved");
    clean(); backend_status = -3; check(wrapper.Reset(&a,nullptr) == 0, "observer failure cannot alter successful reset"); pair_is(&raw,&a);
    clean(); reshade::d3d12::device_impl immediate_device { &native }; ID3D12CommandQueue queue;
    reshade::d3d12::command_list_immediate_impl immediate(&immediate_device,&queue); pair_is(immediate._orig,immediate._cmd_alloc[0].get());
    clean(); check(immediate.flush(false), "empty flush preserved"); check(events.empty(), "empty flush no synthetic bind");
    clean(); immediate._has_commands = true; check(immediate.flush(false), "immediate flush success"); pair_is(immediate._orig,immediate._cmd_alloc[1].get()); check(queue.executes == 1, "execute count unchanged");
    clean(); immediate._has_commands = true; immediate._orig->reset_result = -4; check(!immediate.flush(true), "immediate reset failure preserved"); check(events.empty(), "failed immediate reset no bind");
    clean(); immediate._has_commands = true; immediate._orig->close_result = -5; auto retired = immediate._orig; check(!immediate.flush(true), "close failure preserved after recreate"); pair_is(immediate._orig,immediate._cmd_alloc[1].get()); check(immediate._orig != retired && retired->releases == 1, "recreate exact new list");
    clean(); immediate._has_commands = true; immediate._orig->close_result = -5; native.create_result = -6; check(!immediate.flush(true), "recreate failure preserved"); check(events.empty(), "failed recreate no bind");
    clean(); reshade::d3d12::command_list_immediate_impl failed_initial(&immediate_device,&queue); check(failed_initial._orig == nullptr && events.empty(), "failed initial immediate create no bind");
    std::printf("host allocator CPU: %u checks, %u failures; no DLL, D3D, COM or GPU calls\n", checks, failures);
    return failures ? 1 : 0;
}
