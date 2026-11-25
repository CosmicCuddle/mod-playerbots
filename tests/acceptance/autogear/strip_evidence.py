"""Fixture-only evidence for the random-bot over-cap strip case."""


def combat_rows(snapshot):
    return [r for r in snapshot['rows'] if r[0] == 0 and 0 <= r[1] < 19 and r[1] not in (3, 18)]


def over_cap_rows(snapshot, levels, cap):
    """Return the observed over-cap combat rows, or reject incomplete evidence."""
    if type(cap) is not int or not 0 < cap <= 0xffffffff:
        raise ValueError('invalid independent cap')
    rows = combat_rows(snapshot)
    for row in rows:
        level = levels.get(row[3])
        if type(level) is not int or level <= 0:
            raise ValueError(f'missing or invalid combat item level for {row[3]}')
    return [r for r in rows if levels[r[3]] > cap]


def strip_precondition(before, levels, cap):
    try:
        if not over_cap_rows(before, levels, cap):
            return ['R1 incomplete: no pre-existing over-cap combat item']
    except ValueError as exc:
        return [str(exc)]
    return []


def strip_outcome_problems(before, after, result, levels, cap):
    problems = strip_precondition(before, levels, cap)
    if problems:
        return problems
    try:
        old_over = over_cap_rows(before, levels, cap)
        new_over = over_cap_rows(after, levels, cap)
    except ValueError as exc:
        return [str(exc)]
    if new_over:
        problems.append('R1 still has over-cap combat equipment')
    equipped_bags = {r[2] for r in after['rows'] if r[0] == 0 and 19 <= r[1] < 23}
    carried = {(r[2], r[3]) for r in after['rows']
               if (r[0] == 0 and 23 <= r[1] < 39) or r[0] in equipped_bags}
    for row in old_over:
        if (row[2], row[3]) not in carried:
            problems.append(f'R1 over-cap item {row[2]} not observed in carried bags')
    if result is None or result.get('stripped') != str(len(old_over)):
        problems.append('R1 strip count does not match pre-existing over-cap items')
    return problems
