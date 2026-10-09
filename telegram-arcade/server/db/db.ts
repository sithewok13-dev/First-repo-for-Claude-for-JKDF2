// SQLite access through Node's built-in node:sqlite (no native addon).
// WAL mode, foreign keys on, versioned SQL migrations in ./migrations.

import { DatabaseSync, type StatementSync } from 'node:sqlite';
import { mkdirSync, readdirSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { log } from '../log.ts';

const MIGRATIONS_DIR = join(dirname(fileURLToPath(import.meta.url)), 'migrations');

export type Row = Record<string, any>;

export class Db {
  readonly raw: DatabaseSync;
  private stmts = new Map<string, StatementSync>();

  constructor(path: string) {
    if (path !== ':memory:') mkdirSync(dirname(path), { recursive: true });
    this.raw = new DatabaseSync(path);
    this.raw.exec('PRAGMA journal_mode = WAL');
    this.raw.exec('PRAGMA foreign_keys = ON');
    this.raw.exec('PRAGMA busy_timeout = 5000');
    this.raw.exec('PRAGMA synchronous = NORMAL');
  }

  private prep(sql: string): StatementSync {
    let s = this.stmts.get(sql);
    if (!s) {
      s = this.raw.prepare(sql);
      this.stmts.set(sql, s);
    }
    return s;
  }

  get<T = Row>(sql: string, ...params: any[]): T | undefined {
    return this.prep(sql).get(...params) as T | undefined;
  }

  all<T = Row>(sql: string, ...params: any[]): T[] {
    return this.prep(sql).all(...params) as T[];
  }

  run(sql: string, ...params: any[]): { changes: number; lastInsertRowid: number } {
    const r = this.prep(sql).run(...params);
    return { changes: Number(r.changes), lastInsertRowid: Number(r.lastInsertRowid) };
  }

  tx<T>(fn: () => T): T {
    this.raw.exec('BEGIN IMMEDIATE');
    try {
      const out = fn();
      this.raw.exec('COMMIT');
      return out;
    } catch (e) {
      this.raw.exec('ROLLBACK');
      throw e;
    }
  }

  migrate(): number {
    this.raw.exec('CREATE TABLE IF NOT EXISTS schema_migrations (version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at INTEGER NOT NULL)');
    const done = new Set(this.all<{ version: number }>('SELECT version FROM schema_migrations').map((r) => r.version));
    const files = readdirSync(MIGRATIONS_DIR).filter((f) => /^\d+_.+\.sql$/.test(f)).sort();
    let applied = 0;
    for (const f of files) {
      const version = parseInt(f, 10);
      if (done.has(version)) continue;
      const sql = readFileSync(join(MIGRATIONS_DIR, f), 'utf8');
      this.tx(() => {
        this.raw.exec(sql);
        this.run('INSERT INTO schema_migrations (version, name, applied_at) VALUES (?, ?, ?)', version, f, Date.now());
      });
      log.info('db', `applied migration ${f}`);
      applied++;
    }
    return applied;
  }

  // Consistent online backup (VACUUM INTO writes a compact copy).
  backupTo(path: string): void {
    mkdirSync(dirname(path), { recursive: true });
    this.raw.exec(`VACUUM INTO '${path.replace(/'/g, "''")}'`);
  }

  close(): void {
    this.raw.close();
  }
}
