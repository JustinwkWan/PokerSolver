// Preset GTO opening ranges for 6-max NLHE.
// Each position has RFI (raise first in) frequencies for the 13x13 hand grid.
// Format: { [handLabel]: { raise: pct, call: pct, fold: pct } }
// Hand labels: "AA", "AKs", "AKo", etc.

export type ActionFreqs = { raise: number; call: number; fold: number };
export type RangeMap = Record<string, ActionFreqs>;

export interface PositionPreset {
  name: string;
  abbr: string;
  description: string;
  range: RangeMap;
}

const RANKS = ['A', 'K', 'Q', 'J', 'T', '9', '8', '7', '6', '5', '4', '3', '2'];

function allHands(): string[] {
  const hands: string[] = [];
  for (let r = 0; r < 13; r++) {
    for (let c = 0; c < 13; c++) {
      if (r === c) hands.push(RANKS[r] + RANKS[c]);
      else if (c > r) hands.push(RANKS[r] + RANKS[c] + 's');
      else hands.push(RANKS[c] + RANKS[r] + 'o');
    }
  }
  return hands;
}

// Helper: build a range from a set of "always raise" hands, "mixed raise" hands, rest fold.
function buildRange(
  alwaysRaise: string[],
  mixedRaise: Record<string, number>,  // hand -> raise frequency
  alwaysCall: string[] = [],
  mixedCall: Record<string, number> = {},
): RangeMap {
  const raiseSet = new Set(alwaysRaise);
  const callSet = new Set(alwaysCall);
  const range: RangeMap = {};

  for (const h of allHands()) {
    if (raiseSet.has(h)) {
      range[h] = { raise: 1, call: 0, fold: 0 };
    } else if (h in mixedRaise) {
      const r = mixedRaise[h];
      const c = mixedCall[h] ?? 0;
      range[h] = { raise: r, call: c, fold: Math.max(0, 1 - r - c) };
    } else if (callSet.has(h)) {
      range[h] = { raise: 0, call: 1, fold: 0 };
    } else if (h in mixedCall) {
      range[h] = { raise: 0, call: mixedCall[h], fold: 1 - mixedCall[h] };
    } else {
      range[h] = { raise: 0, call: 0, fold: 1 };
    }
  }
  return range;
}

// ── UTG (Under the Gun) — tightest open, ~15% of hands ────────────────
const UTG_RANGE = buildRange(
  // Always raise
  [
    'AA', 'KK', 'QQ', 'JJ', 'TT', '99',
    'AKs', 'AQs', 'AJs', 'ATs',
    'KQs', 'KJs',
    'QJs',
    'AKo', 'AQo',
  ],
  // Mixed raise
  {
    '88': 0.9, '77': 0.5,
    'A9s': 0.6, 'A5s': 0.8, 'A4s': 0.5,
    'KTs': 0.7, 'QTs': 0.5, 'JTs': 0.8,
    'T9s': 0.4,
    'AJo': 0.7,
    'KQo': 0.5,
  },
);

// ── LJ (Lojack) — slightly wider, ~18% ────────────────────────────────
const LJ_RANGE = buildRange(
  [
    'AA', 'KK', 'QQ', 'JJ', 'TT', '99', '88',
    'AKs', 'AQs', 'AJs', 'ATs', 'A5s',
    'KQs', 'KJs', 'KTs',
    'QJs', 'QTs',
    'JTs',
    'AKo', 'AQo', 'AJo',
  ],
  {
    '77': 0.8, '66': 0.4,
    'A9s': 0.8, 'A4s': 0.7, 'A3s': 0.4,
    'K9s': 0.4, 'Q9s': 0.3,
    'T9s': 0.7, '98s': 0.5,
    'KQo': 0.8, 'ATo': 0.5,
  },
);

// ── CO (Cutoff) — wider, ~25% ──────────────────────────────────────────
const CO_RANGE = buildRange(
  [
    'AA', 'KK', 'QQ', 'JJ', 'TT', '99', '88', '77',
    'AKs', 'AQs', 'AJs', 'ATs', 'A9s', 'A5s', 'A4s', 'A3s',
    'KQs', 'KJs', 'KTs', 'K9s',
    'QJs', 'QTs', 'Q9s',
    'JTs', 'J9s',
    'T9s', 'T8s',
    '98s', '87s',
    'AKo', 'AQo', 'AJo', 'ATo',
    'KQo', 'KJo',
  ],
  {
    '66': 0.9, '55': 0.6, '44': 0.3,
    'A8s': 0.6, 'A7s': 0.5, 'A6s': 0.6, 'A2s': 0.5,
    'K8s': 0.5, 'Q8s': 0.3,
    'J8s': 0.4, '97s': 0.6, '76s': 0.7, '65s': 0.5,
    'KTo': 0.7, 'QJo': 0.6, 'A9o': 0.5,
  },
);

// ── BTN (Button) — widest open, ~40% ──────────────────────────────────
const BTN_RANGE = buildRange(
  [
    'AA', 'KK', 'QQ', 'JJ', 'TT', '99', '88', '77', '66', '55',
    'AKs', 'AQs', 'AJs', 'ATs', 'A9s', 'A8s', 'A7s', 'A6s', 'A5s', 'A4s', 'A3s', 'A2s',
    'KQs', 'KJs', 'KTs', 'K9s', 'K8s', 'K7s',
    'QJs', 'QTs', 'Q9s', 'Q8s',
    'JTs', 'J9s', 'J8s',
    'T9s', 'T8s', 'T7s',
    '98s', '97s',
    '87s', '86s',
    '76s', '75s',
    '65s', '54s',
    'AKo', 'AQo', 'AJo', 'ATo', 'A9o', 'A8o',
    'KQo', 'KJo', 'KTo',
    'QJo', 'QTo',
    'JTo',
  ],
  {
    '44': 0.9, '33': 0.7, '22': 0.5,
    'K6s': 0.7, 'K5s': 0.6, 'K4s': 0.4,
    'Q7s': 0.5, 'J7s': 0.4,
    '96s': 0.6, '85s': 0.5, '64s': 0.5, '53s': 0.4, '43s': 0.3,
    'A7o': 0.6, 'A6o': 0.4, 'A5o': 0.5, 'A4o': 0.3,
    'K9o': 0.7, 'Q9o': 0.4, 'J9o': 0.4, 'T9o': 0.5,
  },
);

// ── SB (Small Blind) — raise or fold vs BB, ~40-45% ──────────────────
const SB_RANGE = buildRange(
  [
    'AA', 'KK', 'QQ', 'JJ', 'TT', '99', '88', '77', '66',
    'AKs', 'AQs', 'AJs', 'ATs', 'A9s', 'A8s', 'A7s', 'A6s', 'A5s', 'A4s', 'A3s', 'A2s',
    'KQs', 'KJs', 'KTs', 'K9s', 'K8s', 'K7s', 'K6s',
    'QJs', 'QTs', 'Q9s', 'Q8s',
    'JTs', 'J9s', 'J8s',
    'T9s', 'T8s',
    '98s', '97s',
    '87s', '86s',
    '76s', '75s',
    '65s', '54s',
    'AKo', 'AQo', 'AJo', 'ATo', 'A9o',
    'KQo', 'KJo', 'KTo',
    'QJo',
  ],
  {
    '55': 0.9, '44': 0.7, '33': 0.5, '22': 0.4,
    'K5s': 0.7, 'K4s': 0.5, 'K3s': 0.3,
    'Q7s': 0.5, 'Q6s': 0.3,
    'J7s': 0.4, 'T7s': 0.5,
    '96s': 0.5, '85s': 0.4, '64s': 0.4, '53s': 0.3,
    'A8o': 0.7, 'A7o': 0.5, 'A6o': 0.4, 'A5o': 0.6, 'A4o': 0.4, 'A3o': 0.3,
    'K9o': 0.6, 'Q9o': 0.3, 'QTo': 0.5, 'JTo': 0.4, 'T9o': 0.3,
  },
);

// ── BB (Big Blind) — defending vs open, lots of calling ───────────────
const BB_RANGE = buildRange(
  // 3-bet (raise)
  [
    'AA', 'KK', 'QQ',
    'AKs', 'AQs',
    'AKo',
  ],
  // Mixed 3-bet
  {
    'JJ': 0.5, 'TT': 0.3,
    'AJs': 0.5, 'ATs': 0.3, 'A5s': 0.4, 'A4s': 0.3,
    'KQs': 0.4, 'KJs': 0.3,
    'QJs': 0.2,
    'AQo': 0.5, 'AJo': 0.2,
    'KQo': 0.2,
  },
  // Always call
  [
    'JJ', 'TT', '99', '88', '77', '66', '55', '44',
    'AJs', 'ATs', 'A9s', 'A8s', 'A7s', 'A6s', 'A5s', 'A4s', 'A3s', 'A2s',
    'KQs', 'KJs', 'KTs', 'K9s', 'K8s', 'K7s', 'K6s', 'K5s',
    'QJs', 'QTs', 'Q9s', 'Q8s', 'Q7s',
    'JTs', 'J9s', 'J8s', 'J7s',
    'T9s', 'T8s', 'T7s',
    '98s', '97s', '96s',
    '87s', '86s', '85s',
    '76s', '75s',
    '65s', '64s',
    '54s', '53s',
    '43s',
    'AQo', 'AJo', 'ATo', 'A9o',
    'KQo', 'KJo', 'KTo', 'K9o',
    'QJo', 'QTo',
    'JTo', 'J9o',
    'T9o',
  ],
  // Mixed call
  {
    '33': 0.7, '22': 0.6,
    'K4s': 0.5, 'K3s': 0.3,
    'Q6s': 0.4, 'Q5s': 0.3,
    'J6s': 0.3, 'T6s': 0.3,
    '95s': 0.3, '84s': 0.3,
    '74s': 0.3, '63s': 0.3,
    '52s': 0.2, '42s': 0.2,
    'A8o': 0.6, 'A7o': 0.4, 'A6o': 0.3, 'A5o': 0.4, 'A4o': 0.3,
    'K8o': 0.3, 'Q9o': 0.4, 'J8o': 0.3, 'T8o': 0.3, '98o': 0.3,
  },
);

export const POSITIONS: PositionPreset[] = [
  { name: 'Under the Gun', abbr: 'UTG', description: 'First to act, tightest range (~15%)', range: UTG_RANGE },
  { name: 'Lojack',        abbr: 'LJ',  description: 'Second position, slightly wider (~18%)', range: LJ_RANGE },
  { name: 'Cutoff',        abbr: 'CO',  description: 'One off the button (~25%)', range: CO_RANGE },
  { name: 'Button',        abbr: 'BTN', description: 'Best position, wide range (~40%)', range: BTN_RANGE },
  { name: 'Small Blind',   abbr: 'SB',  description: 'Raise or fold vs BB (~40-45%)', range: SB_RANGE },
  { name: 'Big Blind',     abbr: 'BB',  description: 'Defending vs open — call-heavy', range: BB_RANGE },
];
