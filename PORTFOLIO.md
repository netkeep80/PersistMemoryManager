# Portfolio roadmap

`PersistMemoryManager` является частью portfolio [`netkeep80`](https://github.com/netkeep80).

## Authoritative portfolio source

Portfolio-level направление, приоритет, lifecycle, cross-repo dependencies и следующий gate **намеренно не дублируются в этом репозитории**. Их source of truth:

- [netkeep80/roadmap](https://github.com/netkeep80/roadmap) — главный portfolio control plane;
- [Current status](https://github.com/netkeep80/roadmap/blob/main/STATUS.md) — автоматически обновляемое состояние repositories/issues/PRs;
- [Execution order](https://github.com/netkeep80/roadmap/blob/main/EXECUTION.md) — порядок cross-repo gates;
- [Architecture](https://github.com/netkeep80/roadmap/blob/main/ARCHITECTURE.md) — canonical ownership и dependency direction.

## Local scope

Issues, epics, code, tests и release mechanics этого репозитория остаются локальным source of truth для **implementation work**.

Правило:

```text
roadmap decides portfolio direction;
this repository executes its local part;
GitHub facts feed the central live status.
```

Если локальный gate изменил cross-repo dependency или следующий portfolio step, обновляется central `netkeep80/roadmap`, а не создаётся конкурирующая portfolio-карта здесь.
