import { useState } from 'react';
import { api } from '../api/client';
import type { RangeCombo } from '../api/client';
import HandGrid from './HandGrid';
import StrategyBar from './StrategyBar';

interface Props {
  strategyPath: string;
  board: string;
  history: string;
  stack: number;
}

export default function RangeViewer({ strategyPath, board, history, stack }: Props) {
  const [combos, setCombos] = useState<RangeCombo[]>([]);
  const [selectedHand, setSelectedHand] = useState<string>('');
  const [selectedCombo, setSelectedCombo] = useState<RangeCombo | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  const loadRange = async () => {
    setLoading(true);
    setError('');
    try {
      const result = await api.queryRange({
        strategy_path: strategyPath,
        board: board || undefined,
        history: history || undefined,
        stack_depth_bb: stack,
      });
      setCombos(result.combos);
    } catch (e) {
      setError(String(e));
    }
    setLoading(false);
  };

  const handleHandSelect = (hand: string) => {
    setSelectedHand(hand);
    // Find a matching combo
    const match = combos.find(c => {
      const r0 = hand[0], r1 = hand[1];
      const cr0 = c.hand[0], cr1 = c.hand[2];
      const suited = hand.length === 3 && hand[2] === 's';
      const offsuit = hand.length === 3 && hand[2] === 'o';
      const pair = hand.length === 2;

      if (pair) return cr0 === r0 && cr1 === r1;
      if (suited) return ((cr0 === r0 && cr1 === r1) || (cr0 === r1 && cr1 === r0)) && c.hand[1] === c.hand[3];
      if (offsuit) return ((cr0 === r0 && cr1 === r1) || (cr0 === r1 && cr1 === r0)) && c.hand[1] !== c.hand[3];
      return false;
    });
    setSelectedCombo(match || null);
  };

  return (
    <div className="space-y-4">
      <button
        onClick={loadRange}
        disabled={loading}
        className="bg-purple-600 hover:bg-purple-700 px-4 py-2 rounded text-sm font-medium
          disabled:opacity-50 transition-colors"
      >
        {loading ? 'Loading Range...' : 'Load Range'}
      </button>

      {error && <div className="text-red-400 text-sm">{error}</div>}

      {combos.length > 0 && (
        <div className="flex gap-6 flex-wrap">
          <HandGrid
            combos={combos}
            onSelect={handleHandSelect}
            selectedHand={selectedHand}
          />

          <div className="flex-1 min-w-[250px] space-y-4">
            {selectedCombo ? (
              <>
                <h3 className="text-lg font-semibold">{selectedHand}</h3>
                <StrategyBar
                  actions={selectedCombo.actions}
                  probabilities={selectedCombo.probabilities}
                />
              </>
            ) : (
              <div className="text-slate-500 text-sm">
                Click a hand in the grid to view its strategy
              </div>
            )}

            <div className="text-xs text-slate-500 mt-4">
              {combos.length} combos loaded
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
