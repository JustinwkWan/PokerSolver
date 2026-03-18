import { useState, useEffect } from 'react';
import { api } from '../api/client';
import type { NodeInfo, NodeAction } from '../api/client';

interface Props {
  stack: number;
  onNodeSelect?: (nodeIdx: number, history: string) => void;
}

const STREET_NAMES = ['Preflop', 'Flop', 'Turn', 'River'];

function actionLabel(a: NodeAction): string {
  if (a.type === 'raise' && a.amount > 0) return `Raise ${a.amount}`;
  return a.type.charAt(0).toUpperCase() + a.type.slice(1);
}

function actionBadgeColor(type: string): string {
  switch (type) {
    case 'fold': return 'bg-blue-600';
    case 'check': return 'bg-green-700';
    case 'call': return 'bg-green-600';
    case 'raise': return 'bg-red-600';
    default: return 'bg-slate-600';
  }
}

interface HistoryEntry {
  nodeIdx: number;
  action: string;
}

export default function TreeBrowser({ stack, onNodeSelect }: Props) {
  const [node, setNode] = useState<NodeInfo | null>(null);
  const [history, setHistory] = useState<HistoryEntry[]>([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  const loadNode = async (idx: number) => {
    setLoading(true);
    setError('');
    try {
      const n = idx === 0
        ? (await api.tree(stack)).root
        : await api.treeNode(idx, stack);
      if (idx === 0) n.index = 0;
      setNode(n);
    } catch (e) {
      setError(String(e));
    }
    setLoading(false);
  };

  useEffect(() => {
    loadNode(0);
    setHistory([]);
  }, [stack]);

  const navigateTo = (action: NodeAction) => {
    if (node) {
      setHistory(h => [...h, { nodeIdx: node.index ?? 0, action: action.type }]);
    }
    loadNode(action.child_index);
    // Build history string
    const histStr = [...history.map(h => h.action[0]), action.type[0]].join('');
    onNodeSelect?.(action.child_index, histStr);
  };

  const goBack = () => {
    if (history.length === 0) return;
    const prev = history[history.length - 1];
    setHistory(h => h.slice(0, -1));
    loadNode(prev.nodeIdx);
    const histStr = history.slice(0, -1).map(h => h.action[0]).join('');
    onNodeSelect?.(prev.nodeIdx, histStr);
  };

  const goRoot = () => {
    setHistory([]);
    loadNode(0);
    onNodeSelect?.(0, '');
  };

  if (loading) return <div className="text-slate-400">Loading tree...</div>;
  if (error) return <div className="text-red-400">{error}</div>;
  if (!node) return null;

  return (
    <div className="space-y-3">
      {/* Breadcrumb */}
      <div className="flex items-center gap-2 text-sm">
        <button onClick={goRoot} className="text-blue-400 hover:underline">Root</button>
        {history.map((h, i) => (
          <span key={i} className="text-slate-500">
            {'>'} <span className="text-slate-300">{h.action}</span>
          </span>
        ))}
      </div>

      {/* Node info */}
      <div className="bg-slate-800 rounded-lg p-4">
        <div className="flex items-center gap-3 mb-3">
          <span className={`px-2 py-0.5 rounded text-xs font-semibold
            ${node.type === 'action' ? 'bg-purple-600' : 'bg-slate-600'}`}>
            {node.type === 'action' ? 'Action' : 'Terminal'}
          </span>
          <span className="text-sm text-slate-400">
            {STREET_NAMES[node.street]} | Player {node.player}
          </span>
          {node.pot !== undefined && node.pot > 0 && (
            <span className="text-sm text-yellow-400">Pot: {node.pot}</span>
          )}
        </div>

        {node.type === 'terminal' && (
          <div className="text-sm text-slate-400">
            {node.fold_player !== undefined && node.fold_player >= 0
              ? `Player ${node.fold_player} folded`
              : 'Showdown'}
          </div>
        )}

        {node.actions && node.actions.length > 0 && (
          <div className="flex flex-wrap gap-2">
            {node.actions.map((a, i) => (
              <button
                key={i}
                onClick={() => navigateTo(a)}
                className={`${actionBadgeColor(a.type)} px-3 py-1.5 rounded text-sm font-medium
                  text-white hover:opacity-80 transition-opacity`}
              >
                {actionLabel(a)}
              </button>
            ))}
          </div>
        )}
      </div>

      {history.length > 0 && (
        <button
          onClick={goBack}
          className="text-sm text-blue-400 hover:underline"
        >
          Back
        </button>
      )}
    </div>
  );
}
