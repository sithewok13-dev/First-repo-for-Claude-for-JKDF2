// Adapter registry: maps exact ROM content to a verified adapter.

import { ATC_ADAPTERS } from './atc.ts';
import type { Adapter } from './types.ts';
import type { SystemId } from '../../shared/systems.ts';

const ALL: Adapter[] = [...ATC_ADAPTERS];

export function findAdapter(system: SystemId, romSha256: string, options: Record<string, string> = {}): Adapter | null {
  for (const a of ALL) {
    if (a.meta.system !== system) continue;
    if (!a.meta.romSha256.includes(romSha256)) continue;
    // An adapter verified under specific core options does not apply if the
    // room overrides any of them with a different value.
    const req = a.meta.options ?? {};
    if (Object.entries(req).some(([k, v]) => options[k] !== undefined && options[k] !== v)) continue;
    return a;
  }
  return null;
}

export function adapterById(id: string): Adapter | null {
  return ALL.find((a) => a.meta.id === id) ?? null;
}

export function allAdapters(): readonly Adapter[] {
  return ALL;
}
