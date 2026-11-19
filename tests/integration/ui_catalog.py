"""Check the compile-time catalog and printf contracts without requiring SDL."""
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / 'include/campuslink/ui_strings.inc').read_text(encoding='utf-8')
pattern = re.compile(r'CL_TEXT_KEY\((\w+), ("(?:[^"\\]|\\.)*"), ("(?:[^"\\]|\\.)*")\)')
format_spec = re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|ll|[hljztL])?[cdiouxXeEfgGaAsp])')
items = {}
for line in source.splitlines():
    if not line.startswith('CL_TEXT_KEY'):
        continue
    match = pattern.fullmatch(line)
    assert match, f'Cannot parse catalog entry: {line}'
    key, en, fr = match.groups()
    assert key not in items, f'Duplicate key: {key}'
    en, fr = json.loads(en), json.loads(fr)
    assert en.strip() and fr.strip(), f'Missing translation: {key}'
    assert format_spec.findall(en) == format_spec.findall(fr), f'Mismatched printf arguments: {key}'
    items[key] = (en, fr)
ui = (root / 'src/ui/desktop.cpp').read_text(encoding='utf-8')
for key in re.findall(r'Key::(\w+)', ui):
    assert key in items, f'Unknown key: {key}'
# Widget IDs and user data can be literal/dynamic; visible static properties belong in the catalog.
for line in ui.splitlines():
    assert not re.search(r'ImGui::TextColored\(palette\(color_theme\)\.\w+, tr\(Key::\w+\)\)', line), \
        'Use a literal format for colored translated text without arguments'
    for label in re.findall(r'ImGui::(?:Button|SmallButton|SeparatorText|Checkbox|SliderInt|TableSetupColumn)\("([^"\\]+)"', line):
        assert label.startswith('##'), f'Unlocalized visible widget label: {label}'
print(f'PASS: {len(items)} keys, both translations, checked format strings and static widget labels')
