"""Separate disposable enabled-BiS observation phase; never a release verdict."""

BIS_CASES = 2


def fixture_overrides(base, enabled):
    """Return a new mapping; never alter the caller's default or live settings."""
    if type(enabled) is not bool:
        raise ValueError('invalid fixture BiS mode')
    overrides = dict(base)
    if enabled:
        overrides.update({'Playerbots.AutoGearBisCommand': '1',
                          'Playerbots.AutoGearQualityLimit': '4'})
    return overrides


def run_bis_phase(runner):
    """Require both populations; an alt refusal alone proves no enabled path."""
    if not runner.alt or not runner.random:
        return
    if not runner.run_case('E1 alt bot BiS enabled is refused', runner.alt, 'autogear bis',
                           {'refusal': 1, 'ran': 0, 'unchanged': True,
                            'authority': True, 'form': 0})['pass']:
        return
    runner.run_case('E2 random bot BiS enabled runs', runner.random, 'autogear bis',
                    {'refusal': 0, 'ran': 1, 'authority': True, 'form': 5,
                     'reply': r'^Geared up to item level \d+'})
