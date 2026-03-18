import { useState, useEffect } from 'react';
import RangeViewer from './components/RangeViewer';
import BoardSelector from './components/BoardSelector';
import StrategyBar from './components/StrategyBar';
import HandGrid from './components/HandGrid';
import QueryPanel from './components/QueryPanel';
import { api } from './api/client';
import { POSITIONS } from './data/presets';
import { SOLVE_PRESETS } from './data/solvePresets';
import type { SolvePreset } from './data/solvePresets';
import type { ActionFreqs } from './data/presets';

type Tab = 'ranges' | 'solver' | 'query';

function App() {
  const [tab, setTab] = useState<Tab>('ranges');

  // Position range state
  const [posIdx, setPosIdx] = useState(0);
  const [selectedHand, setSelectedHand] = useState('');

  // Solver tab state
  const [presetIdx, setPresetIdx] = useState(0);
  const [presetStatus, setPresetStatus] = useState<Record<string, { exists: boolean; iterations: number }>>({});
  const [solverBoard, setSolverBoard] = useState('');
  const [solverHistory, setSolverHistory] = useState('');

  const pos = POSITIONS[posIdx];
  const handFreqs: ActionFreqs | undefined = selectedHand ? pos.range[selectedHand] : undefined;
  const activePreset: SolvePreset = SOLVE_PRESETS[presetIdx];
  const activeStatus = presetStatus[activePreset.strategy_path];

  // Check which strategy directories exist on mount and when switching to solver tab
  useEffect(() => {
    if (tab === 'solver') {
      api.strategyStatus(SOLVE_PRESETS.map(p => p.strategy_path))
        .then(setPresetStatus)
        .catch(() => {});
    }
  }, [tab]);

  const tabs: { key: Tab; label: string }[] = [
    { key: 'ranges', label: 'Ranges' },
    { key: 'solver', label: 'Solver' },
    { key: 'query', label: 'Hand Query' },
  ];

  return (
    <div className="min-h-screen flex flex-col">
      {/* Header */}
      <header className="bg-slate-900 border-b border-slate-800 px-6 py-4">
        <div className="max-w-7xl mx-auto flex items-center justify-between">
          <h1 className="text-xl font-bold text-white">GTO Poker Solver</h1>
          <div className="flex items-center gap-4 text-sm text-slate-400">
            <span>6-Max NLHE | Heads-Up Postflop</span>
          </div>
        </div>
      </header>

      {/* Tab navigation */}
      <nav className="bg-slate-900/50 border-b border-slate-800">
        <div className="max-w-7xl mx-auto flex">
          {tabs.map(t => (
            <button
              key={t.key}
              onClick={() => setTab(t.key)}
              className={`px-5 py-3 text-sm font-medium transition-colors border-b-2
                ${tab === t.key
                  ? 'border-purple-500 text-white'
                  : 'border-transparent text-slate-400 hover:text-slate-200'}`}
            >
              {t.label}
            </button>
          ))}
        </div>
      </nav>

      {/* Content */}
      <main className="flex-1 max-w-7xl mx-auto w-full p-6">

        {/* ── Ranges tab (default) ─────────────────────────────────────── */}
        {tab === 'ranges' && (
          <div className="space-y-5">
            {/* Position selector */}
            <div className="flex gap-2">
              {POSITIONS.map((p, i) => (
                <button
                  key={p.abbr}
                  onClick={() => { setPosIdx(i); setSelectedHand(''); }}
                  className={`px-4 py-2 rounded-lg text-sm font-semibold transition-all
                    ${posIdx === i
                      ? 'bg-purple-600 text-white shadow-lg shadow-purple-600/30'
                      : 'bg-slate-800 text-slate-400 hover:bg-slate-700 hover:text-slate-200'}`}
                >
                  {p.abbr}
                </button>
              ))}
            </div>

            {/* Position info */}
            <div className="flex items-baseline gap-3">
              <h2 className="text-lg font-semibold text-white">{pos.name}</h2>
              <span className="text-sm text-slate-400">{pos.description}</span>
            </div>

            {/* Grid + detail panel */}
            <div className="flex gap-6 flex-wrap items-start">
              <HandGrid
                presetRange={pos.range}
                onSelect={setSelectedHand}
                selectedHand={selectedHand}
              />

              {/* Detail panel */}
              <div className="flex-1 min-w-[280px]">
                {handFreqs ? (
                  <div className="bg-slate-800 rounded-lg p-5 space-y-4">
                    <h3 className="text-lg font-mono font-bold text-white">{selectedHand}</h3>
                    <StrategyBar
                      hand={selectedHand}
                      actions={['Raise', 'Call', 'Fold']}
                      probabilities={[handFreqs.raise, handFreqs.call, handFreqs.fold]}
                    />
                    <div className="text-sm text-slate-400 space-y-1 pt-2">
                      <div>
                        {selectedHand.length === 2 ? 'Pocket pair' :
                         selectedHand.endsWith('s') ? 'Suited' : 'Offsuit'}
                        {' \u2022 '}
                        {selectedHand.length === 2 ? '6 combos' :
                         selectedHand.endsWith('s') ? '4 combos' : '12 combos'}
                      </div>
                      {handFreqs.raise > 0 && handFreqs.raise < 1 && (
                        <div className="text-yellow-400/80 text-xs">
                          Mixed strategy — raise {(handFreqs.raise * 100).toFixed(0)}% of the time
                        </div>
                      )}
                    </div>
                  </div>
                ) : (
                  <div className="bg-slate-800/50 rounded-lg p-5 text-slate-500 text-sm">
                    Click a hand in the grid to view its strategy breakdown
                  </div>
                )}

                {/* Quick stats */}
                <div className="mt-4 bg-slate-800/50 rounded-lg p-4 space-y-2 text-sm">
                  <h4 className="text-slate-300 font-medium">Range Stats</h4>
                  {(() => {
                    let totalRaise = 0, totalCall = 0, count = 0;
                    for (const freqs of Object.values(pos.range)) {
                      totalRaise += freqs.raise;
                      totalCall += freqs.call;
                      count++;
                    }
                    const openPct = ((totalRaise + totalCall) / count * 100).toFixed(1);
                    const raisePct = (totalRaise / count * 100).toFixed(1);
                    const callPct = (totalCall / count * 100).toFixed(1);
                    return (
                      <>
                        <div className="flex justify-between text-slate-400">
                          <span>Total VPIP</span>
                          <span className="text-white font-medium">{openPct}%</span>
                        </div>
                        <div className="flex justify-between text-slate-400">
                          <span>Raise</span>
                          <span className="text-red-400">{raisePct}%</span>
                        </div>
                        <div className="flex justify-between text-slate-400">
                          <span>Call</span>
                          <span className="text-green-400">{callPct}%</span>
                        </div>
                      </>
                    );
                  })()}
                </div>
              </div>
            </div>
          </div>
        )}

        {/* ── Solver tab — browse pre-solved strategies ─────────────────── */}
        {tab === 'solver' && (
          <div className="space-y-6">
            <h2 className="text-lg font-semibold">Solved Strategies</h2>

            {/* Preset selector */}
            <div className="flex gap-2 flex-wrap">
              {SOLVE_PRESETS.map((p, i) => {
                const status = presetStatus[p.strategy_path];
                const solved = status?.exists;
                return (
                  <button
                    key={p.id}
                    onClick={() => setPresetIdx(i)}
                    className={`px-4 py-2.5 rounded-lg text-sm font-semibold transition-all relative
                      ${presetIdx === i
                        ? 'bg-purple-600 text-white shadow-lg shadow-purple-600/30'
                        : solved
                          ? 'bg-slate-800 text-slate-300 hover:bg-slate-700'
                          : 'bg-slate-800/50 text-slate-500 hover:bg-slate-800'}`}
                  >
                    {p.name}
                    {solved && (
                      <span className="ml-2 text-green-400 text-xs">
                        {(status.iterations / 1000).toFixed(0)}K
                      </span>
                    )}
                    {!solved && status !== undefined && (
                      <span className="ml-2 text-yellow-500 text-xs">not solved</span>
                    )}
                  </button>
                );
              })}
            </div>

            {/* Active preset details */}
            <div className="bg-slate-800 rounded-lg p-5 space-y-3">
              <div className="flex items-baseline gap-3">
                <h3 className="text-base font-semibold text-white">{activePreset.name}</h3>
                <span className="text-sm text-slate-400">{activePreset.description}</span>
              </div>

              <div className="grid grid-cols-3 gap-4 text-sm">
                <div>
                  <span className="text-slate-500">Stack</span>
                  <div className="text-white font-medium">{activePreset.stack_depth_bb}bb</div>
                </div>
                <div>
                  <span className="text-slate-500">Iterations</span>
                  <div className="text-white font-medium">{activePreset.iterations.toLocaleString()}</div>
                </div>
                <div>
                  <span className="text-slate-500">Status</span>
                  <div className={activeStatus?.exists ? 'text-green-400 font-medium' : 'text-yellow-500 font-medium'}>
                    {activeStatus?.exists
                      ? `Solved (${activeStatus.iterations.toLocaleString()} iters)`
                      : 'Not solved'}
                  </div>
                </div>
              </div>

              {!activeStatus?.exists && (
                <div className="bg-slate-900 rounded p-3 mt-2">
                  <div className="text-xs text-slate-500 mb-1">Run this from the terminal to solve:</div>
                  <code className="text-sm text-green-400 font-mono select-all">
                    {activePreset.solve_command}
                  </code>
                </div>
              )}
            </div>

            {/* Range viewer for solved strategy */}
            {activeStatus?.exists && (
              <div className="border-t border-slate-800 pt-6 space-y-4">
                <h3 className="text-base font-semibold">Browse Solved Range</h3>
                <div className="flex items-center gap-4 flex-wrap">
                  <div>
                    <label className="block text-sm text-slate-400 mb-1">Board</label>
                    <BoardSelector value={solverBoard} onChange={setSolverBoard} />
                  </div>
                  <div>
                    <label className="block text-sm text-slate-400 mb-1">History</label>
                    <input
                      type="text"
                      value={solverHistory}
                      onChange={e => setSolverHistory(e.target.value)}
                      placeholder="e.g. rc"
                      className="bg-slate-800 border border-slate-700 rounded px-3 py-2 text-sm w-32 font-mono"
                    />
                  </div>
                </div>
                <RangeViewer
                  strategyPath={activePreset.strategy_path}
                  board={solverBoard}
                  history={solverHistory}
                  stack={activePreset.stack_depth_bb}
                />
              </div>
            )}
          </div>
        )}

        {/* ── Hand Query tab ───────────────────────────────────────────── */}
        {tab === 'query' && <QueryPanel />}
      </main>
    </div>
  );
}

export default App;
