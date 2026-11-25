"""Check case-specific outcomes before another explicit fixture save.

Only observations, not command correlation or durable-save proof.
"""
from acceptance_oracle import parse_result
from result_identity import RESULT


def case_outcome_problems(lines, expect):
    if len(lines) != 1:
        return ['case outcome requires exactly one observed result']
    match = RESULT.fullmatch(lines[0])
    result = parse_result(lines[0])
    if match is None or result is None:
        return ['case outcome result malformed']
    problems = []
    wanted = expect.get('refusal')
    if wanted is not None:
        allowed = wanted if isinstance(wanted, (set, tuple, list)) else (wanted,)
        if result['refusal'] not in {str(value) for value in allowed}:
            problems.append('case refusal differs before explicit save')
    if expect.get('ran') is not None and result['ran'] != str(expect['ran']):
        problems.append('case ran differs before explicit save')
    if expect.get('unchanged'):
        if result['ran'] != '0' or any(result[key] != '0' for key in ('stripped', 'new', 'revoked')):
            problems.append('unchanged case reported gear activity before explicit save')
    if 'authority' in expect:
        authority = expect['authority']
        if type(authority) is not bool or match.group(7) != str(authority).lower():
            problems.append('case progression authority differs before explicit save')
    return problems
