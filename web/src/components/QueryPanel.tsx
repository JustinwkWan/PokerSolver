import { useState } from 'react';
import { api } from '../api/client';
import type { StrategyResult } from '../api/client';
import BoardSelector from './BoardSelector';
import StrategyBar from './StrategyBar';
import { POSITIONS } from '../data/presets';
import { SOLVE_PRESETS } from '../data/solvePresets';

const POSITION_LABELS = POSITIONS.map(p => p.abbr);

// Valid preflop matchups: hero position vs villain position
// In 6-max, villain must be in a later position or blinds
function validVillains(heroIdx: number): number[] {
  const results: number[] = [];
  for (let i = 0; i < POSITION_LABELS.length; i++) {
    if (i !== heroIdx) results.push(i);
  }
  return results;
}

const STREETS = ['Preflop', 'Flop', 'Turn', 'River'];

function streetFromBoard(board: string): number {
  const n = board.length / 2;
  if (n >= 5) return 3;
  if (n >= 4) return 2;
  if (n >= 3) return 1;
  return 0;
}

// Common preflop action sequences
const PREFLOP_LINES = [
  { label: 'RFI (Open Raise)', history: 'r', desc: 'Hero opens with a raise' },
  { label: 'Facing RFI', history: '', desc: 'Villain opened, hero to act' },
  { label: 'vs 3-Bet', history: 'rr', desc: 'Hero raised, villain 3-bet' },
  { label: 'Call Open', history: 'rc', desc: 'Hero raised, villain called' },
];

// Common postflop lines
const POSTFLOP_LINES = [
  { label: 'First to Act', history: '', desc: 'OOP checks or bets' },
  { label: 'Facing C-Bet', history: 'b', desc: 'IP bet, hero to act' },
  { label: 'After Check', history: 'k', desc: 'Checked to hero' },
  { label: 'Check-Raise', history: 'brc', desc: 'Bet, raise, call line' },
];

interface ActionInfo {
  name: string;
  probability: number;
  ev?: number;
}

export default function QueryPanel() {
  // Spot definition
  const [heroPos, setHeroPos] = useState(5);   // BB
  const [villainPos, setVillainPos] = useState(3); // BTN
  const [presetIdx, setPresetIdx] = useState(0);
  const [board, setBoard] = useState('');
  const [hand, setHand] = useState('');
  const [history, setHistory] = useState('');

  // Results
  const [result, setResult] = useState<StrategyResult | null>(null);
  const [error, setError] = useState('');
  const [loading, setLoading] = useState(false);

  // Display mode
  const [displayMode, setDisplayMode] = useState<'strategy' | 'ev' | 'equity'>('strategy');

  const preset = SOLVE_PRESETS[presetIdx];
  const street = streetFromBoard(board);
  const heroLabel = POSITION_LABELS[heroPos];
  const villainLabel = POSITION_LABELS[villainPos];

  // Determine who is OOP/IP
  // Postflop: SB(4) and BB(5) are OOP vs later positions
  // SB is OOP to everyone except BB; BB is OOP to everyone
  const heroIsOOP = heroPos >= 4 || heroPos < villainPos;
  const oopLabel = heroIsOOP ? heroLabel : villainLabel;
  const ipLabel = heroIsOOP ? villainLabel : heroLabel;

  const runQuery = async () => {
    if (!hand) {
      setError('Enter a hand (e.g. AhKs)');
      return;
    }
    setError('');
    setLoading(true);
    try {
      const res = await api.query({
        strategy_path: preset.strategy_path,
        hand,
        board: board || undefined,
        history: history || undefined,
        stack_depth_bb: preset.stack_depth_bb,
      });
      setResult(res);
    } catch (e) {
      setError(String(e));
      setResult(null);
    }
    setLoading(false);
  };

  const actions: ActionInfo[] = result
    ? result.actions.map((name, i) => ({
        name,
        probability: result.probabilities[i],
      }))
    : [];

  // Determine hand type
  const handType = hand.length >= 4
    ? hand[0] === hand[2]
      ? 'Pocket Pair'
      : hand[1] === hand[3]
        ? 'Suited'
        : 'Offsuit'
    : '';

  const comboCount = handType === 'Pocket Pair' ? 6 : handType === 'Suited' ? 4 : handType === 'Offsuit' ? 12 : 0;

  return (
    <div className="space-y-5">
      {/* ── Spot Configuration ──────────────────────────────────────── */}
      <div className="bg-slate-800 rounded-lg p-5 space-y-4">
        <h3 className="text-base font-semibold text-white">Spot Configuration</h3>

        {/* Preset + Stack */}
        <div className="flex gap-4 items-end flex-wrap">
          <div>
            <label className="block text-xs text-slate-500 mb-1">Solution</label>
            <div className="flex gap-1">
              {SOLVE_PRESETS.map((p, i) => (
                <button
                  key={p.id}
                  onClick={() => setPresetIdx(i)}
                  className={`px-3 py-1.5 rounded text-xs font-medium transition-colors
                    ${presetIdx === i
                      ? 'bg-purple-600 text-white'
                      : 'bg-slate-700 text-slate-400 hover:bg-slate-600'}`}
                >
                  {p.name}
                </button>
              ))}
            </div>
          </div>
          <div className="text-sm text-slate-400">
            Stack: <span className="text-white font-medium">{preset.stack_depth_bb}bb</span>
          </div>
        </div>

        {/* Positions */}
        <div className="flex gap-6">
          <div>
            <label className="block text-xs text-slate-500 mb-1">Hero Position</label>
            <div className="flex gap-1">
              {POSITION_LABELS.map((label, i) => (
                <button
                  key={label}
                  onClick={() => {
                    setHeroPos(i);
                    if (i === villainPos) {
                      // Pick next valid villain
                      const valid = validVillains(i);
                      if (valid.length) setVillainPos(valid[0]);
                    }
                  }}
                  className={`px-2.5 py-1.5 rounded text-xs font-semibold transition-colors
                    ${heroPos === i
                      ? 'bg-blue-600 text-white'
                      : 'bg-slate-700 text-slate-400 hover:bg-slate-600'}`}
                >
                  {label}
                </button>
              ))}
            </div>
          </div>
          <div>
            <label className="block text-xs text-slate-500 mb-1">Villain Position</label>
            <div className="flex gap-1">
              {POSITION_LABELS.map((label, i) => {
                const valid = validVillains(heroPos);
                const isValid = valid.includes(i);
                return (
                  <button
                    key={label}
                    onClick={() => isValid && setVillainPos(i)}
                    disabled={!isValid}
                    className={`px-2.5 py-1.5 rounded text-xs font-semibold transition-colors
                      ${villainPos === i
                        ? 'bg-red-600 text-white'
                        : isValid
                          ? 'bg-slate-700 text-slate-400 hover:bg-slate-600'
                          : 'bg-slate-800 text-slate-600 cursor-not-allowed'}`}
                  >
                    {label}
                  </button>
                );
              })}
            </div>
          </div>
        </div>

        {/* Board + Hand + History */}
        <div className="flex gap-4 items-end flex-wrap">
          <div>
            <label className="block text-xs text-slate-500 mb-1">Board</label>
            <BoardSelector value={board} onChange={setBoard} />
          </div>
          <div>
            <label className="block text-xs text-slate-500 mb-1">Hero Hand</label>
            <input
              type="text"
              value={hand}
              onChange={e => setHand(e.target.value)}
              placeholder="AhKs"
              className="bg-slate-900 border border-slate-700 rounded px-3 py-2 text-sm w-24 font-mono"
            />
          </div>
          <div>
            <label className="block text-xs text-slate-500 mb-1">Action History</label>
            <input
              type="text"
              value={history}
              onChange={e => setHistory(e.target.value)}
              placeholder="rc"
              className="bg-slate-900 border border-slate-700 rounded px-3 py-2 text-sm w-24 font-mono"
            />
          </div>
          <button
            onClick={runQuery}
            disabled={loading}
            className="bg-purple-600 hover:bg-purple-700 px-5 py-2 rounded text-sm font-medium
              transition-colors disabled:opacity-50"
          >
            {loading ? 'Querying...' : 'Query'}
          </button>
        </div>

        {/* Quick action lines */}
        <div>
          <label className="block text-xs text-slate-500 mb-1">Quick Lines</label>
          <div className="flex gap-1 flex-wrap">
            {(street === 0 ? PREFLOP_LINES : POSTFLOP_LINES).map(line => (
              <button
                key={line.label}
                onClick={() => setHistory(line.history)}
                title={line.desc}
                className={`px-2.5 py-1 rounded text-xs transition-colors
                  ${history === line.history
                    ? 'bg-purple-600/30 text-purple-300 border border-purple-500/50'
                    : 'bg-slate-700 text-slate-400 hover:bg-slate-600'}`}
              >
                {line.label}
              </button>
            ))}
          </div>
        </div>
      </div>

      {error && <div className="text-red-400 text-sm">{error}</div>}

      {/* ── Spot Info Header ────────────────────────────────────────── */}
      {(result || hand) && (
        <div className="bg-slate-800/50 rounded-lg p-4 flex items-center gap-6 flex-wrap text-sm">
          <div className="flex items-center gap-2">
            <span className="text-slate-500">Positions:</span>
            <span className="bg-blue-600/20 text-blue-400 px-2 py-0.5 rounded text-xs font-semibold">
              {heroLabel}
            </span>
            <span className="text-slate-600">vs</span>
            <span className="bg-red-600/20 text-red-400 px-2 py-0.5 rounded text-xs font-semibold">
              {villainLabel}
            </span>
          </div>
          <div className="text-slate-500">
            OOP: <span className="text-slate-300">{oopLabel}</span>
            {' | '}
            IP: <span className="text-slate-300">{ipLabel}</span>
          </div>
          <div className="text-slate-500">
            Street: <span className="text-slate-300">{STREETS[street]}</span>
          </div>
          <div className="text-slate-500">
            Stack: <span className="text-slate-300">{preset.stack_depth_bb}bb</span>
          </div>
          {board && (
            <div className="text-slate-500">
              Board: <span className="font-mono text-slate-300">{board}</span>
            </div>
          )}
          {history && (
            <div className="text-slate-500">
              Line: <span className="font-mono text-slate-300">{history}</span>
            </div>
          )}
        </div>
      )}

      {/* ── Results ─────────────────────────────────────────────────── */}
      {result && (
        <div className="grid grid-cols-1 lg:grid-cols-2 gap-5">
          {/* Left: Strategy */}
          <div className="bg-slate-800 rounded-lg p-5 space-y-4">
            {/* Display mode toggle */}
            <div className="flex items-center justify-between">
              <h3 className="text-base font-semibold text-white">Strategy</h3>
              <div className="flex gap-1">
                {(['strategy', 'ev', 'equity'] as const).map(mode => (
                  <button
                    key={mode}
                    onClick={() => setDisplayMode(mode)}
                    className={`px-2.5 py-1 rounded text-xs font-medium transition-colors
                      ${displayMode === mode
                        ? 'bg-purple-600 text-white'
                        : 'bg-slate-700 text-slate-400 hover:bg-slate-600'}`}
                  >
                    {mode === 'strategy' ? 'Strategy' : mode === 'ev' ? 'EV' : 'Equity'}
                  </button>
                ))}
              </div>
            </div>

            {/* Hand info */}
            <div className="flex items-center gap-3">
              <span className="font-mono text-xl font-bold text-white">{hand}</span>
              {handType && (
                <span className="text-xs text-slate-400 bg-slate-700 px-2 py-0.5 rounded">
                  {handType} ({comboCount} combos)
                </span>
              )}
            </div>

            {/* Strategy bar */}
            <StrategyBar
              actions={result.actions}
              probabilities={result.probabilities}
            />

            {/* Action detail table */}
            <div className="space-y-1">
              {actions.map(a => (
                <div
                  key={a.name}
                  className="flex items-center justify-between py-1.5 px-3 rounded bg-slate-900/50 text-sm"
                >
                  <div className="flex items-center gap-2">
                    <span
                      className="w-2.5 h-2.5 rounded-sm"
                      style={{
                        backgroundColor: a.name.toLowerCase().includes('fold')
                          ? '#3b82f6'
                          : a.name.toLowerCase().includes('call') || a.name.toLowerCase().includes('check')
                            ? '#22c55e'
                            : '#ef4444',
                      }}
                    />
                    <span className="text-slate-300 font-medium">{a.name}</span>
                  </div>
                  <div className="flex items-center gap-4">
                    <span className="text-white font-mono">
                      {(a.probability * 100).toFixed(1)}%
                    </span>
                    {displayMode === 'ev' && (
                      <span className="text-slate-500 text-xs">EV: --</span>
                    )}
                  </div>
                </div>
              ))}
            </div>

            {displayMode === 'ev' && (
              <div className="text-xs text-slate-600 italic">
                EV data available after running a solve
              </div>
            )}
          </div>

          {/* Right: Additional info */}
          <div className="space-y-4">
            {/* Spot summary */}
            <div className="bg-slate-800 rounded-lg p-5 space-y-3">
              <h3 className="text-base font-semibold text-white">Spot Details</h3>
              <div className="grid grid-cols-2 gap-3 text-sm">
                <div className="bg-slate-900/50 rounded p-3">
                  <div className="text-xs text-slate-500 mb-1">Hero</div>
                  <div className="text-white font-semibold">{heroLabel}</div>
                  <div className="text-slate-400 font-mono text-sm">{hand || '—'}</div>
                </div>
                <div className="bg-slate-900/50 rounded p-3">
                  <div className="text-xs text-slate-500 mb-1">Villain</div>
                  <div className="text-white font-semibold">{villainLabel}</div>
                  <div className="text-slate-400 text-xs">Full range</div>
                </div>
                <div className="bg-slate-900/50 rounded p-3">
                  <div className="text-xs text-slate-500 mb-1">Effective Stack</div>
                  <div className="text-white font-semibold">{preset.stack_depth_bb}bb</div>
                </div>
                <div className="bg-slate-900/50 rounded p-3">
                  <div className="text-xs text-slate-500 mb-1">Street</div>
                  <div className="text-white font-semibold">{STREETS[street]}</div>
                </div>
              </div>
            </div>

            {/* EV / Equity panel */}
            <div className="bg-slate-800 rounded-lg p-5 space-y-3">
              <h3 className="text-base font-semibold text-white">Hand Metrics</h3>
              <div className="grid grid-cols-3 gap-3 text-sm">
                <div className="bg-slate-900/50 rounded p-3 text-center">
                  <div className="text-xs text-slate-500 mb-1">EV</div>
                  <div className="text-slate-500 font-mono text-lg">—</div>
                  <div className="text-xs text-slate-600">bb</div>
                </div>
                <div className="bg-slate-900/50 rounded p-3 text-center">
                  <div className="text-xs text-slate-500 mb-1">Equity</div>
                  <div className="text-slate-500 font-mono text-lg">—</div>
                  <div className="text-xs text-slate-600">%</div>
                </div>
                <div className="bg-slate-900/50 rounded p-3 text-center">
                  <div className="text-xs text-slate-500 mb-1">EQR</div>
                  <div className="text-slate-500 font-mono text-lg">—</div>
                  <div className="text-xs text-slate-600">%</div>
                </div>
              </div>
              <div className="text-xs text-slate-600 italic">
                EV, Equity, and EQR require a completed solve
              </div>
            </div>

            {/* Recommended action */}
            {actions.length > 0 && (
              <div className="bg-slate-800 rounded-lg p-5 space-y-2">
                <h3 className="text-base font-semibold text-white">GTO Recommendation</h3>
                {(() => {
                  const best = actions.reduce((a, b) => a.probability > b.probability ? a : b);
                  const isMixed = actions.filter(a => a.probability > 0.1).length > 1;
                  return (
                    <>
                      <div className="flex items-center gap-2">
                        <span className="text-2xl font-bold text-white">{best.name}</span>
                        <span className="text-slate-400">
                          ({(best.probability * 100).toFixed(0)}%)
                        </span>
                      </div>
                      {isMixed && (
                        <div className="text-sm text-yellow-400/80">
                          Mixed strategy spot — use a randomizer
                        </div>
                      )}
                      {!isMixed && best.probability > 0.95 && (
                        <div className="text-sm text-green-400/80">
                          Pure strategy — always {best.name.toLowerCase()}
                        </div>
                      )}
                    </>
                  );
                })()}
              </div>
            )}
          </div>
        </div>
      )}

      {/* Empty state */}
      {!result && !error && (
        <div className="bg-slate-800/30 rounded-lg p-8 text-center text-slate-500">
          <div className="text-lg mb-2">Configure your spot above and click Query</div>
          <div className="text-sm">
            Select hero/villain positions, enter your hand, and optionally set board cards and action history
          </div>
        </div>
      )}
    </div>
  );
}
