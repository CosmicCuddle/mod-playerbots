"""Fail-closed preflight for a fresh, disposable AutoGear acceptance fixture.

These checks are not a sandbox or proof of a dedicated MySQL instance. The
operator must supply an isolated instance and install; never use live resources.
"""
from pathlib import Path
import re

SYSTEM_SCHEMAS = frozenset({'information_schema', 'mysql', 'performance_schema', 'sys'})


def fresh_install(raw):
    root = Path(raw).absolute()
    if not root.is_dir() or root.resolve(strict=True) != root:
        raise ValueError('install must be an existing canonical directory, not a symlink')
    for name in ('bin', 'etc', 'etc/modules'):
        p = root / name
        if not p.is_dir() or p.is_symlink():
            raise ValueError(f'expected ordinary fixture directory: {name}')
    for name in ('bin/authserver', 'bin/worldserver', 'etc/authserver.conf.dist',
                 'etc/worldserver.conf.dist'):
        p = root / name
        if not p.is_file() or p.is_symlink():
            raise ValueError(f'expected ordinary fixture file: {name}')
    for p in (root / 'etc').rglob('*'):
        if p.is_symlink() or (p.is_file() and p.suffix == '.conf'):
            raise ValueError('existing config or symlink in fixture config tree')
    for name in ('data', 'logs'):
        p = root / name
        if p.exists() or p.is_symlink():
            raise ValueError(f'fixture output already exists: {name}')
    return root


def fresh_database_names(prefix, rows):
    if not re.fullmatch(r'agacc[a-z0-9_]{0,24}', prefix):
        raise ValueError('invalid fixture database prefix')
    names = {key: f'{prefix}_{key}' for key in ('auth', 'world', 'chars', 'pb')}
    if not rows or any(len(row) != 1 or not isinstance(row[0], str) for row in rows):
        raise ValueError('database inventory unavailable or malformed')
    schemas = {row[0].casefold() for row in rows}
    if 'information_schema' not in schemas or schemas - SYSTEM_SCHEMAS:
        raise ValueError('expected an empty, dedicated fixture database instance')
    return names
