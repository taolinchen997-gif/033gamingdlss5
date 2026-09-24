"""Extract verbatim production exception boundaries. Never load a graphics DLL."""
from pathlib import Path
import hashlib,json,sys
root=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]).resolve()
source=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else root.parent
assert out.is_relative_to(root/'build');out.mkdir(parents=True,exist_ok=True)
rows=[]
layer_source=(source/'engine/src/hostnr.h').read_text(encoding='utf-8-sig')
layers='static bool BuildLayerCandidate(' in layer_source
(out/'fault_features.inc').write_text(f'#define NR_LAYER_FAULT_TEST {int(layers)}\n#define NR_RECOVERY_TEST {int("static bool EnsureBlitter(" in layer_source)}\n',encoding='utf8')
def section(rel,start,end,name,last=None):
    p=source/rel
    if rel=='engine/src/command_lifetime.h':p=root/'src/command_lifetime.h' # unchanged shared completion predicate
    raw=p.read_bytes();text=raw.decode('utf-8-sig').replace('\r\n','\n');assert text.count(start)==1,(rel,start)
    a=text.index(start);b=text.index(end,a)
    # Stop after the function `last` when newer code follows it before `end`
    # (YanYun S32 put the SR model state after ApiCapabilities).
    if last:b=min(b,text.index('\n}\n',text.index(last,a))+3)
    chunk=text[a:b]
    line=text[:a].count('\n')+1
    (out/name).write_text(f'#line {line} "{p.as_posix()}"\n'+chunk,encoding='utf8')
    rows.append(dict(source=str(p),sourceSha256=hashlib.sha256(raw).hexdigest(),startLine=line,
        endLine=text[:b].count('\n')+1,sha256=hashlib.sha256(chunk.encode()).hexdigest(),output=name))
section('engine/third_party/OptiScaler033/OptiScaler/integration/Core033.cpp','static int Invoke(', 'bool OfferDx12(', 'fault_invoke.inc')
section('engine/third_party/OptiScaler033/OptiScaler/integration/Core033.cpp','bool Claim(uint32_t owner)', 'bool UsesHost()', 'fault_claim.inc')
cap=(source/'engine/third_party/OptiScaler033/OptiScaler/integration/Core033.cpp').read_text(encoding='utf-8-sig')
cap_start='static void* CapabilitiesImpl(' if 'static void* CapabilitiesImpl(' in cap else 'static void* __cdecl ApiCapabilities('
section('engine/third_party/OptiScaler033/OptiScaler/integration/Core033.cpp',cap_start,'}\nextern "C" __declspec(dllexport) const k033core::Api*', 'fault_capabilities.inc',
    last='static void* __cdecl ApiCapabilities(')
section('engine/src/nrfwd.h','static NVSDK_NGX_Result core_create_guarded(', '// guideW/guideH','fault_create.inc')
section('engine/src/nrfwd.h','static bool release_core_guarded(', '// ── 「这是我们自己发的调用」', 'fault_release.inc')
section('engine/src/nrfwd.h','static int evaluate(', 'static bool release_core_guarded(', 'fault_evaluate.inc')
section('engine/src/hostnr.h','    DWORD seh = 0;\n    s_feat = nrfwd::create', '    ++s_full_builds;', 'fault_main_create.inc')
# The additional pass loop is taken verbatim, with allocation/size setup mocked.
section('engine/src/hostnr.h','        DWORD seh=0;\n        for(int i=0;', '    if(count>1){\n', 'fault_extra_create.inc')
section('engine/src/hostnr.h','static void PumpBuild()', '// 帮手每帧调这里。', 'fault_pump.inc')
if layers:
    section('engine/src/hostnr.h','static void RetireBank(', '// 在 present 里调', 'fault_layer_bank.inc')
section('engine/src/resolve_leases.h','static void CollectLocked()', 'static void Reset(', 'fault_collect.inc')
section('engine/src/command_lifetime.h','inline bool MayRetire(', '\n}\n}', 'fault_retire_predicate.inc')
(out/'nr-fault-source-receipt.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf8')

if 'static bool EnsureBlitter(' in layer_source:
    section('engine/src/hostnr.h','static bool EnsureBlitter(', 'static void PumpBuild()', 'recovery_helpers.inc')
else:
    start=layer_source.index('    if (!s_blit.ready && !scale::Create(s_blit, dev))')
    end=layer_source.index('    const bool holdRequested=',start)
    (out/'recovery_helpers.inc').write_text('static bool EnsureBlitter(ID3D12Device* dev){\n'+layer_source[start:end]+'return true;\n}\n',encoding='utf-8')
(out/'nr-fault-source-receipt.json').write_text(json.dumps(rows,indent=2)+'\n',encoding='utf8')
