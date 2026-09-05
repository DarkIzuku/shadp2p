# Bloodborne Beckoning Bell Insight trace

Status: **runtime trace required; no Insight behavior is patched yet**.

Static analysis was performed against the supported CUSA03173 01.09 executable with SHA-256
`6764938B23539D29C936BCA9880FC4A774E7B0099CE31C7E8C4B0F8BD0BEFB80`.

The analysis established that:

- the current Insight (`san_value`) is stored at offset `0x84` in `PlayerGameData`;
- `0x01B63F10` applies a general transaction containing multiple costs;
- `0x01B63F89` is the direct `sub [rcx+0x84], eax` that debits Insight;
- that transaction machinery is also reachable from non-summon flows, so suppressing it globally
  would break legitimate Insight spending;
- the observed summon-success flag `0x442` is internal native state and is not referenced by the
  shipped EMEVD scripts.

Because static evidence does not yet prove the unique Beckoning Bell caller, the observer is
intentionally read-only. Set:

```text
SHADPS4_BLOODBORNE_RE_TRACE=1
```

The Windows artifact also includes `Run Bloodborne Insight Trace.cmd`. Keep it next to
`shadPS4.exe` and double-click it to start the same read-only trace without setting the variable
manually.

Then start with a known Insight value, use the Beckoning Bell, complete one successful summon, and
close the emulator. The generated `captures/bloodborne-re/*.jsonl` includes a
`PlayerGameData.InsightDebit` entry with:

- old and predicted new Insight values;
- debit amount and all three transaction cost fields;
- most recently used tracked goods ID;
- timing relative to the tracked item use and native summon-success callback;
- a short native frame chain.

This is sufficient to identify the summon-only caller without guessing. A later patch may suppress
that proven caller while leaving the Insight Shop and all other Insight consumers untouched.
