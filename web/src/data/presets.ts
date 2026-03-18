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

// ── UTG (Under the Gun) — tightest open, ~18% of hands ────────────────
const UTG_RANGE = buildRange(
  // Always raise
  [
    'AA', 'KK', 'QQ', 'JJ', 'TT', '99', '88',
    'AKs', 'AQs', 'AJs', 'ATs', 'A5s',
    'KQs', 'KJs', 'KTs',
    'QJs', 'JTs',
    'AKo', 'AQo',
  ],
  // Mixed raise
  {
    '77': 0.7, '66': 0.3,
    'A9s': 0.8, 'A4s': 0.6, 'A3s': 0.3,
    'QTs': 0.7, 'T9s': 0.6, '98s': 0.3,
    'K9s': 0.3,
    'AJo': 0.9, 'KQo': 0.7,
    'ATo': 0.3,
  },
);

// ── LJ (Lojack) — slightly wider, ~21% ────────────────────────────────
const LJ_RANGE = buildRange(
  [
    'AA', 'KK', 'QQ', 'JJ', 'TT', '99', '88', '77',
    'AKs', 'AQs', 'AJs', 'ATs', 'A9s', 'A5s',
    'KQs', 'KJs', 'KTs',
    'QJs', 'QTs',
    'JTs', 'T9s',
    'AKo', 'AQo', 'AJo',
  ],
  {
    '66': 0.7, '55': 0.3,
    'A4s': 0.8, 'A3s': 0.5, 'A8s': 0.3,
    'K9s': 0.6, 'Q9s': 0.5,
    '98s': 0.7, '87s': 0.3,
    'KQo': 1.0, 'ATo': 0.7, 'KJo': 0.3,
  },
);

// ── CO (Cutoff) — wider, ~28% ──────────────────────────────────────────
const CO_RANGE = buildRange(
  [
    'AA', 'KK', 'QQ', 'JJ', 'TT', '99', '88', '77', '66',
    'AKs', 'AQs', 'AJs', 'ATs', 'A9s', 'A8s', 'A5s', 'A4s', 'A3s',
    'KQs', 'KJs', 'KTs', 'K9s',
    'QJs', 'QTs', 'Q9s',
    'JTs', 'J9s',
    'T9s', 'T8s',
    '98s', '97s', '87s',
    'AKo', 'AQo', 'AJo', 'ATo',
    'KQo', 'KJo',
  ],
  {
    '55': 0.8, '44': 0.5, '33': 0.3,
    'A7s': 0.7, 'A6s': 0.7, 'A2s': 0.6,
    'K8s': 0.7, 'Q8s': 0.5,
    'J8s': 0.6, '76s': 0.8, '65s': 0.7, '86s': 0.4,
    'KTo': 0.9, 'QJo': 0.8, 'A9o': 0.7, 'QTo': 0.3,
  },
);

// ── BTN (Button) — widest open, ~43% ──────────────────────────────────
const BTN_RANGE = buildRange(
  [
    'AA', 'KK', 'QQ', 'JJ', 'TT', '99', '88', '77', '66', '55', '44',
    'AKs', 'AQs', 'AJs', 'ATs', 'A9s', 'A8s', 'A7s', 'A6s', 'A5s', 'A4s', 'A3s', 'A2s',
    'KQs', 'KJs', 'KTs', 'K9s', 'K8s', 'K7s', 'K6s',
    'QJs', 'QTs', 'Q9s', 'Q8s', 'Q7s',
    'JTs', 'J9s', 'J8s',
    'T9s', 'T8s', 'T7s',
    '98s', '97s', '96s',
    '87s', '86s',
    '76s', '75s',
    '65s', '64s', '54s',
    'AKo', 'AQo', 'AJo', 'ATo', 'A9o', 'A8o', 'A7o',
    'KQo', 'KJo', 'KTo',
    'QJo', 'QTo',
    'JTo',
  ],
  {
    '33': 0.9, '22': 0.7,
    'K5s': 0.7, 'K4s': 0.5, 'K3s': 0.3,
    'J7s': 0.6,
    '85s': 0.6, '53s': 0.5, '43s': 0.5,
    'A6o': 0.6, 'A5o': 0.7, 'A4o': 0.5, 'A3o': 0.3,
    'K9o': 0.9, 'Q9o': 0.6, 'J9o': 0.6, 'T9o': 0.7, '98o': 0.3,
  },
);

// ── SB (Small Blind) — raise or fold vs BB, ~44-48% ──────────────────
const SB_RANGE = buildRange(
  [
    'AA', 'KK', 'QQ', 'JJ', 'TT', '99', '88', '77', '66', '55',
    'AKs', 'AQs', 'AJs', 'ATs', 'A9s', 'A8s', 'A7s', 'A6s', 'A5s', 'A4s', 'A3s', 'A2s',
    'KQs', 'KJs', 'KTs', 'K9s', 'K8s', 'K7s', 'K6s', 'K5s',
    'QJs', 'QTs', 'Q9s', 'Q8s', 'Q7s',
    'JTs', 'J9s', 'J8s',
    'T9s', 'T8s', 'T7s',
    '98s', '97s',
    '87s', '86s',
    '76s', '75s',
    '65s', '54s',
    'AKo', 'AQo', 'AJo', 'ATo', 'A9o', 'A8o',
    'KQo', 'KJo', 'KTo',
    'QJo', 'QTo',
  ],
  {
    '44': 0.9, '33': 0.7, '22': 0.5,
    'K4s': 0.6, 'K3s': 0.4, 'K2s': 0.3,
    'Q6s': 0.5, 'J7s': 0.5,
    '96s': 0.6, '85s': 0.5, '64s': 0.5, '53s': 0.4, '43s': 0.3,
    'A7o': 0.7, 'A6o': 0.5, 'A5o': 0.7, 'A4o': 0.5, 'A3o': 0.4, 'A2o': 0.3,
    'K9o': 0.8, 'Q9o': 0.5, 'JTo': 0.6, 'T9o': 0.5, 'J9o': 0.3,
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
    'JJ': 0.6, 'TT': 0.4,
    'AJs': 0.6, 'ATs': 0.4, 'A5s': 0.5, 'A4s': 0.4,
    'KQs': 0.5, 'KJs': 0.4,
    'QJs': 0.3, 'JTs': 0.2,
    'AQo': 0.6, 'AJo': 0.3,
    'KQo': 0.3,
  },
  // Always call
  [
    'JJ', 'TT', '99', '88', '77', '66', '55', '44', '33',
    'AJs', 'ATs', 'A9s', 'A8s', 'A7s', 'A6s', 'A5s', 'A4s', 'A3s', 'A2s',
    'KQs', 'KJs', 'KTs', 'K9s', 'K8s', 'K7s', 'K6s', 'K5s', 'K4s',
    'QJs', 'QTs', 'Q9s', 'Q8s', 'Q7s', 'Q6s',
    'JTs', 'J9s', 'J8s', 'J7s',
    'T9s', 'T8s', 'T7s', 'T6s',
    '98s', '97s', '96s',
    '87s', '86s', '85s',
    '76s', '75s', '74s',
    '65s', '64s',
    '54s', '53s',
    '43s',
    'AQo', 'AJo', 'ATo', 'A9o', 'A8o',
    'KQo', 'KJo', 'KTo', 'K9o',
    'QJo', 'QTo', 'Q9o',
    'JTo', 'J9o',
    'T9o', 'T8o',
    '98o',
  ],
  // Mixed call
  {
    '22': 0.8,
    'K3s': 0.5, 'K2s': 0.3,
    'Q5s': 0.4,
    'J6s': 0.4,
    '95s': 0.4, '84s': 0.4,
    '63s': 0.4, '52s': 0.3, '42s': 0.3,
    'A7o': 0.6, 'A6o': 0.4, 'A5o': 0.5, 'A4o': 0.4, 'A3o': 0.3,
    'K8o': 0.5, 'Q8o': 0.3, 'J8o': 0.4, '87o': 0.3, '97o': 0.3,
  },
);

export const POSITIONS: PositionPreset[] = [
  { name: 'Under the Gun', abbr: 'UTG', description: 'First to act, tightest range (~18%)', range: UTG_RANGE },
  { name: 'Lojack',        abbr: 'LJ',  description: 'Second position, slightly wider (~21%)', range: LJ_RANGE },
  { name: 'Cutoff',        abbr: 'CO',  description: 'One off the button (~28%)', range: CO_RANGE },
  { name: 'Button',        abbr: 'BTN', description: 'Best position, wide range (~43%)', range: BTN_RANGE },
  { name: 'Small Blind',   abbr: 'SB',  description: 'Raise or fold vs BB (~46%)', range: SB_RANGE },
  { name: 'Big Blind',     abbr: 'BB',  description: 'Defending vs open — call-heavy', range: BB_RANGE },
];
