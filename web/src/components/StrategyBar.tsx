interface Props {
  actions: string[];
  probabilities: number[];
  hand?: string;
}

function actionColor(action: string): string {
  const a = action.toLowerCase();
  if (a.includes('fold')) return '#3b82f6';
  if (a.includes('call') || a.includes('check')) return '#22c55e';
  return '#ef4444'; // raise/bet
}

export default function StrategyBar({ actions, probabilities, hand }: Props) {
  if (!actions.length) {
    return <div className="text-slate-500 text-sm">No strategy data</div>;
  }

  return (
    <div className="w-full">
      {hand && <div className="text-sm font-mono mb-1 text-slate-300">{hand}</div>}

      {/* Stacked bar */}
      <div className="flex h-8 rounded overflow-hidden mb-2">
        {actions.map((action, i) => {
          const pct = probabilities[i] * 100;
          if (pct < 0.5) return null;
          return (
            <div
              key={action}
              className="flex items-center justify-center text-xs font-semibold text-white min-w-[20px]"
              style={{ width: `${pct}%`, backgroundColor: actionColor(action) }}
              title={`${action}: ${pct.toFixed(1)}%`}
            >
              {pct >= 8 ? `${pct.toFixed(0)}%` : ''}
            </div>
          );
        })}
      </div>

      {/* Legend */}
      <div className="flex gap-3 text-xs text-slate-400">
        {actions.map((action, i) => (
          <span key={action} className="flex items-center gap-1">
            <span
              className="w-2.5 h-2.5 rounded-sm inline-block"
              style={{ backgroundColor: actionColor(action) }}
            />
            {action}: {(probabilities[i] * 100).toFixed(1)}%
          </span>
        ))}
      </div>
    </div>
  );
}
