#!/usr/bin/env python3
"""Fails when docs/GAME_DIFFERENCES.md has fallen behind the source.

It cannot judge the prose. It checks the things that go stale silently:
  - every behavioural field of struct GameProfile is named in the document
    (the `addr` block is addresses, not behaviour, and is exempt except for the
    optional entries that switch a feature on);
  - every profile in src/games/ has a column;
  - the signature counts in its table match tools/th*_signatures.json.
"""
import json, re, sys
from pathlib import Path
root = Path(__file__).resolve().parent.parent
doc = (root / 'docs/GAME_DIFFERENCES.md').read_text(encoding='utf-8')
header = (root / 'src/game_profile.h').read_text(encoding='utf-8')
errors = []

body = header[header.index('struct GameProfile {'):]
body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
addr = re.search(r'struct \{(.*?)\} addr;', body, flags=re.S)
body = body.replace(addr.group(0), '')
layout = re.search(r'struct \{(.*?)\} layout;', body, flags=re.S)
draw = re.search(r'struct \{(.*?)\} draw;', body, flags=re.S)
body = re.sub(r'\)\s*\([^)]*\)', ')()', body)      # drop callback parameter lists
fields = set()
for m in re.finditer(r'(?:\(\*\s*(\w+)\s*\)\s*\(\)|\b(\w+)\s*(?:\[[^\]]*\])?\s*[;,])', body):
    name = m.group(1) or m.group(2)
    if name and not name.isdigit(): fields.add(name)
# types, enum constants and the draw block's mechanical description are not behaviour switches
ignore = {'identity', 'addr', 'layout', 'draw', 'g_game', 'classes', 'class_count', 'speed_site_count',
          'rule_count', 'sprite_round_count', 'rules', 'dispatch', 'dispatch_len', 'node_reg', 'prio_off',
          'flush_fn', 'flush_reg', 'flush_this', 'vm_draw', 'vm_draw_len', 'vm_reg', 'vm_anm_off',
          'vm_layer_off', 'replay_stage', 'replay_frame', 'replay_stages', 'player_pos', 'player_timer',
          'enemy_flags', 'enemy_position', 'enemy_skip_mask', 'gm_pause_flags', 'install_sites',
          'REMOVE_NODE_NODE_FIRST', 'REMOVE_NODE_RUNNER_FIRST', 'REMOVE_NODE_RUNNER_THIS',
          'RUNNER_ARG_EBX', 'RUNNER_ARG_STACK', 'RUNNER_ARG_ECX', 'special_name'}
optional_addr = {'latency_cmp', 'data_dir', 'replay_saves', 'replay_load_calls', 'poll_input', 'game_input'}
for f in sorted((fields - ignore) | optional_addr):
    if f not in doc: errors.append(f'profile field `{f}` is not mentioned')

for src in sorted((root / 'src/games').glob('th*.c')):
    tag = src.stem.upper()
    label = 'New Classic' if tag.startswith('TH06NC') else tag  # one column for every New Classic build
    if label not in doc: errors.append(f'{src.name} has no column ({label})')

row = re.search(r'^\| Frozen signatures \|(.*)\|\s*$', doc, flags=re.M)
cols = re.search(r'^## 7\..*?\n\n\|(.*?)\|\s*\n', doc, flags=re.S | re.M)
if row and cols:
    names = [c.strip() for c in cols.group(1).split('|')][1:]
    counts = [c.strip() for c in row.group(1).split('|')]
    for name, count in zip(names, counts):
        j = root / f'tools/{name.lower()}_signatures.json'
        if j.exists() and count.isdigit() and int(count) != len(json.loads(j.read_text())):
            errors.append(f'{name}: the document says {count} signatures, {j.name} has {len(json.loads(j.read_text()))}')
else: errors.append('the signature-count row was not found')

for e in errors: print('GAME_DIFFERENCES.md:', e)
if errors: sys.exit(1)
print('GAME_DIFFERENCES.md covers every profile field, every profile and the signature counts')
