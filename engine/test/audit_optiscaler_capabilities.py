"""Source/menu coverage only, deliberately separate from runtime validation."""
from pathlib import Path
import json, xml.etree.ElementTree as ET
root = Path(__file__).resolve().parent.parent
ns = {'m': 'http://schemas.microsoft.com/developer/msbuild/2003'}
def units(base):
    tree = ET.parse(base / 'OptiScaler/OptiScaler.vcxproj')
    return {e.attrib['Include'].replace('\\', '/') for e in tree.findall('.//m:ClCompile', ns) if 'Include' in e.attrib}
baseline = json.loads((root / 'test/optiscaler_upstream_compile_units.json').read_text(encoding='utf-8'))
original = set(baseline['units'])
integrated = units(root / 'third_party/OptiScaler033')
missing = sorted(original - integrated)
assert not missing, missing
menu = (root / 'third_party/OptiScaler033/OptiScaler/menu/menu_common.cpp').read_text(encoding='utf-8-sig')
sections = ['RenderActiveUpscalerSettings', 'RenderFrameGenerationSelection', 'RenderFrameGenerationRuntimeSettings',
            'RenderFsrCommonSettings', 'RenderFramerateSettings', 'RenderFakenvapiSettings', 'RenderActiveImageSettings',
            'RenderMagnifierSettings', 'RenderQuirksSettings', 'RenderAdvancedSettings', 'RenderLoggingSettings',
            'RenderThemeSettings', 'RenderFpsOverlaySettings', 'RenderUpscalerInputsSettings', 'RenderApiAndTextureSettings', 'RenderKeybindSettings']
assert all(menu.count(name + '(ctx);') >= 1 for name in sections)
ledger = json.loads((root / 'test/033_capability_ledger.json').read_text(encoding='utf-8'))
assert ledger['baseline']['optiscaler_commit'] == baseline['commit'], 'Capability baseline drift'
ids = set()
for item in ledger['capabilities']:
    assert item['id'] not in ids, item['id']
    ids.add(item['id'])
    assert item['integration'] in {'wired', 'partial', 'unwired', 'unported'}, item
    assert item['origin'] and item['verification'] and item['remaining'], item
    for path in item['paths']:
        assert (root / path).exists(), f"Missing evidence: {path}"
assert {'nr_multipass', 'exposure_scan', 'scan_anchors', 'renodx_game_hdr'} <= ids
gaps = [item['id'] for item in ledger['capabilities'] if item['integration'] != 'wired']
assert not ledger['scope_complete'] or not gaps, 'Unfinished capability cannot be declared complete'
print(json.dumps({'upstream_compile_units': len(original), 'integrated_compile_units': len(integrated),
                  'omitted_compile_units': missing, 'upstream_menu_sections_preserved': sections,
                  'capability_entries': len(ids), 'capability_gaps': gaps,
                  'all_capabilities_complete': ledger['scope_complete'] and ledger['game_acceptance_complete'] and not gaps,
                  'ledger': str(root / 'test/033_capability_ledger.json'),
                  'note': 'Project/menu coverage only. Runtime availability still depends on API, hardware and provider libraries.'}, indent=2))
