"""Physical carried-slot evidence; not a native save-durability proof."""


def free_carried_slots(rows, sizes):
    positions, guids, bags = set(), set(), {}
    for bag, slot, guid, entry in rows:
        if any(type(v) is not int for v in (bag, slot, guid, entry)) or bag < 0 or not 0 <= slot <= 255 or guid <= 0 or entry <= 0:
            raise ValueError('invalid inventory row')
        if (bag, slot) in positions or guid in guids:
            raise ValueError('duplicate inventory position or GUID')
        positions.add((bag, slot))
        guids.add(guid)
        if bag == 0 and 19 <= slot < 23:
            size = sizes.get(entry)
            if type(size) is not int or not 1 <= size <= 36:
                raise ValueError(f'missing or invalid carried bag size for {entry}')
            bags[guid] = size
    free = sum((0, slot) not in positions for slot in range(23, 39))
    for guid, size in bags.items():
        if any(bag == guid and slot >= size for bag, slot in positions):
            raise ValueError(f'carried bag {guid} has out-of-range slot')
        free += sum((guid, slot) not in positions for slot in range(size))
    return free


def full_bag_problems(rows, sizes):
    try:
        free = free_carried_slots(rows, sizes)
    except (ValueError, TypeError) as exc:
        return [str(exc)]
    problems = []
    if free != 0:
        problems.append(f'R3 incomplete: {free} carried slots remain free')
    if not any(bag == 0 and 0 <= slot < 19 and slot not in (3, 18)
               for bag, slot, _, _ in rows):
        problems.append('R3 incomplete: no equipped combat item to strip')
    return problems
