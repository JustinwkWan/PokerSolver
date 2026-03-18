import { useMemo } from 'react';
import type { RangeCombo } from '../api/client';
import type { RangeMap } from '../data/presets';

const RANKS = ['A', 'K', 'Q', 'J', 'T', '9', '8', '7', '6', '5', '4', '3', '2'];

interface CellData {
  actions: string[];
  probs: number[];
}

interface Props {
  combos?: RangeCombo[];
  presetRange?: RangeMap;
  onSelect?: (hand: string) => void;
  selectedHand?: string;
}

function getGridLabel(row: number, col: number): string {
  if (row === col) return RANKS[row] + RANKS[col];
  if (col > row) return RANKS[row] + RANKS[col] + 's';
  return RANKS[col] + RANKS[row] + 'o';
}

function handToGrid(hand: string): string {
  const r0 = RANKS.indexOf(hand[0]);
  const r1 = RANKS.indexOf(hand[2]);
  if (r0 === r1) return RANKS[r0] + RANKS[r1];
  const suited = hand[1] === hand[3];
  const hi = Math.min(r0, r1);
  const lo = Math.max(r0, r1);
  return suited ? RANKS[hi] + RANKS[lo] + 's' : RANKS[hi] + RANKS[lo] + 'o';
}

function aggregateCombos(combos: RangeCombo[]): Map<string, CellData> {
  const map = new Map<string, { actions: string[]; totals: number[]; count: number }>();
  for (const c of combos) {
    const key = handToGrid(c.hand);
    const entry = map.get(key);
    if (!entry) {
      map.set(key, { actions: c.actions, totals: [...c.probabilities], count: 1 });
    } else {
      for (let i = 0; i < c.probabilities.length; i++) {
        entry.totals[i] = (entry.totals[i] || 0) + c.probabilities[i];
      }
      entry.count++;
    }
  }
  const result = new Map<string, CellData>();
  for (const [key, val] of map) {
    result.set(key, { actions: val.actions, probs: val.totals.map(t => t / val.count) });
  }
  return result;
}

function presetToGrid(range: RangeMap): Map<string, CellData> {
  const result = new Map<string, CellData>();
  for (const [hand, freqs] of Object.entries(range)) {
    result.set(hand, {
      actions: ['Raise', 'Call', 'Fold'],
      probs: [freqs.raise, freqs.call, freqs.fold],
    });
  }
  return result;
}

function cellColor(probs: number[], actions: string[]): string {
  let r = 0, g = 0, b = 0;
  for (let i = 0; i < actions.length; i++) {
    const p = probs[i];
    const a = actions[i].toLowerCase();
    if (a.includes('fold')) {
      r += 59 * p; g += 130 * p; b += 246 * p;
    } else if (a.includes('call') || a.includes('check')) {
      r += 34 * p; g += 197 * p; b += 94 * p;
    } else {
      r += 239 * p; g += 68 * p; b += 68 * p;
    }
  }
  return `rgb(${Math.round(r)}, ${Math.round(g)}, ${Math.round(b)})`;
}

export default function HandGrid({ combos, presetRange, onSelect, selectedHand }: Props) {
  const grid = useMemo(() => {
    if (presetRange) return presetToGrid(presetRange);
    if (combos) return aggregateCombos(combos);
    return new Map<string, CellData>();
  }, [combos, presetRange]);

  return (
    <div className="inline-block">
      <div className="grid gap-0.5" style={{ gridTemplateColumns: 'repeat(13, 1fr)' }}>
        {RANKS.map((_, row) =>
          RANKS.map((_, col) => {
            const label = getGridLabel(row, col);
            const data = grid.get(label);
            const bg = data ? cellColor(data.probs, data.actions) : '#1e293b';
            const isSelected = selectedHand === label;

            return (
              <button
                key={label}
                onClick={() => onSelect?.(label)}
                className={`w-10 h-10 text-xs font-mono font-semibold rounded-sm flex items-center justify-center
                  transition-all hover:opacity-80
                  ${isSelected ? 'ring-2 ring-yellow-400' : ''}
                  ${row === col ? 'border border-white/20' : ''}`}
                style={{ backgroundColor: bg }}
                title={data ? data.actions.map((a, i) => `${a}: ${(data.probs[i] * 100).toFixed(0)}%`).join(', ') : label}
              >
                {label}
              </button>
            );
          })
        )}
      </div>
      <div className="flex gap-4 mt-3 text-xs justify-center">
        <span className="flex items-center gap-1">
          <span className="w-3 h-3 rounded-sm" style={{ backgroundColor: 'rgb(239, 68, 68)' }} /> Raise
        </span>
        <span className="flex items-center gap-1">
          <span className="w-3 h-3 rounded-sm" style={{ backgroundColor: 'rgb(34, 197, 94)' }} /> Call/Check
        </span>
        <span className="flex items-center gap-1">
          <span className="w-3 h-3 rounded-sm" style={{ backgroundColor: 'rgb(59, 130, 246)' }} /> Fold
        </span>
      </div>
    </div>
  );
}
