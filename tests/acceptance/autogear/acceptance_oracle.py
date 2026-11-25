"""Pure evidence validation for the disposable PB59 acceptance runner.

This checks observed log fields and saved inventory rows, not save durability,
source eligibility, native effects, or provenance of a server binary.
"""
import re

FIELDS = ('cap', 'refusal', 'ran', 'stripped', 'new', 'revoked', 'capHeld',
          'bisRestoreFailed', 'ownedMissing')
BOOLS = frozenset(('ran', 'capHeld', 'bisRestoreFailed'))


def parse_result(line):
    if not isinstance(line, str) or 'PB59 AutoGear bot=' not in line:
        return None
    pairs = re.findall(r'(\w+)=([^\s]+)', line)
    result = {}
    for key, value in pairs:
        if key not in FIELDS:
            continue
        if key in result:
            return None
        if key in BOOLS:
            value = {'true': '1', 'false': '0'}.get(value, value)
            if value not in ('0', '1'):
                return None
        elif not re.fullmatch(r'[0-9]{1,10}', value) or int(value) > 0xffffffff:
            return None
        result[key] = value
    if set(result) != set(FIELDS) or int(result['refusal']) > 9:
        return None
    return result


def inventory_index(snapshot):
    if not isinstance(snapshot, dict) or type(snapshot.get('mail')) is not int or snapshot['mail'] < 0:
        raise ValueError('invalid mail observation')
    rows = snapshot.get('rows')
    if not isinstance(rows, (list, tuple)):
        raise ValueError('missing inventory rows')
    items, positions = {}, set()
    for row in rows:
        if not isinstance(row, (list, tuple)) or len(row) != 4 or any(type(v) is not int for v in row):
            raise ValueError('malformed inventory row')
        bag, slot, guid, entry = row
        if bag < 0 or not 0 <= slot <= 255 or guid <= 0 or entry <= 0:
            raise ValueError('invalid inventory row')
        if guid in items or (bag, slot) in positions:
            raise ValueError('duplicate item GUID or slot')
        items[guid] = entry
        positions.add((bag, slot))
    return items


def evidence_problems(before, after, result, levels, *, expected_cap):
    """Fail closed when observations are incomplete; return diagnostic strings."""
    try:
        old, new = inventory_index(before), inventory_index(after)
    except ValueError as exc:
        return [str(exc)]
    problems = []
    if type(expected_cap) is not int or not 0 < expected_cap <= 0xffffffff:
        problems.append('missing or invalid independent expected cap')
    for guid, entry in old.items():
        if new.get(guid) != entry:
            problems.append(f'original item {guid} missing or entry changed')
    if result is None:
        return problems + ['missing or malformed result evidence']
    if result['ownedMissing'] != '0' or result['bisRestoreFailed'] != '0':
        problems.append('result indicates incomplete custody or restoration')
    if result['refusal'] == '9':
        problems.append('recovery required; not acceptance evidence')
    if result['ran'] == '1':
        cap = int(result['cap'])
        if cap != expected_cap:
            problems.append(f'reported cap {cap} differs from independent expected cap {expected_cap}')
        if cap <= 0 or result['capHeld'] != '1':
            problems.append('positive cap and cap-held proof required')
        if result['refusal'] != '0':
            problems.append('ran result is not successful')
        for bag, slot, guid, entry in after['rows']:
            if bag == 0 and slot < 19 and guid not in old:
                level = levels.get(entry) if isinstance(levels, dict) else None
                if type(level) is not int or level <= 0:
                    problems.append(f'missing or invalid item level for {entry}')
                elif level > cap:
                    problems.append(f'new equipped item {entry} above cap {cap}')
    elif result['refusal'] == '0':
        problems.append('successful result did not run')
    return problems
