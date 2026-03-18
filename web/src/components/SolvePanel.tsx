import { useState, useEffect, useRef } from 'react';
import { api } from '../api/client';
import type { JobStatus } from '../api/client';

interface Props {
  onSolveComplete?: () => void;
}

export default function SolvePanel({ onSolveComplete }: Props) {
  const [stack, setStack] = useState(10);
  const [iterations, setIterations] = useState(10000);
  const [outputPath, setOutputPath] = useState('data/strategy');
  const [jobId, setJobId] = useState('');
  const [status, setStatus] = useState<JobStatus | null>(null);
  const [error, setError] = useState('');
  const pollRef = useRef<ReturnType<typeof setInterval>>(undefined);

  const startSolve = async () => {
    setError('');
    try {
      const result = await api.solve({
        stack_depth_bb: stack,
        num_iterations: iterations,
        output_path: outputPath,
      });
      setJobId(result.job_id);
    } catch (e) {
      setError(String(e));
    }
  };

  const cancelSolve = async () => {
    if (!jobId) return;
    try {
      await api.cancelSolve(jobId);
    } catch (e) {
      setError(String(e));
    }
  };

  // Poll for status
  useEffect(() => {
    if (!jobId) return;

    const poll = async () => {
      try {
        const s = await api.solveStatus(jobId);
        setStatus(s);
        if (s.status !== 'running') {
          clearInterval(pollRef.current);
          if (s.status === 'completed') onSolveComplete?.();
        }
      } catch {
        clearInterval(pollRef.current);
      }
    };

    poll();
    pollRef.current = setInterval(poll, 1000);
    return () => clearInterval(pollRef.current);
  }, [jobId]);

  const isRunning = status?.status === 'running';
  const progress = status?.progress ?? 0;

  return (
    <div className="bg-slate-800 rounded-lg p-5 space-y-4">
      <h2 className="text-lg font-semibold">Solve Configuration</h2>

      <div className="grid grid-cols-2 gap-4">
        <div>
          <label className="block text-sm text-slate-400 mb-1">Stack (BB)</label>
          <input
            type="number"
            value={stack}
            onChange={e => setStack(Number(e.target.value))}
            disabled={isRunning}
            className="w-full bg-slate-900 border border-slate-700 rounded px-3 py-2 text-sm
              disabled:opacity-50"
          />
        </div>
        <div>
          <label className="block text-sm text-slate-400 mb-1">Iterations</label>
          <input
            type="number"
            value={iterations}
            onChange={e => setIterations(Number(e.target.value))}
            disabled={isRunning}
            className="w-full bg-slate-900 border border-slate-700 rounded px-3 py-2 text-sm
              disabled:opacity-50"
          />
        </div>
      </div>

      <div>
        <label className="block text-sm text-slate-400 mb-1">Output Path</label>
        <input
          type="text"
          value={outputPath}
          onChange={e => setOutputPath(e.target.value)}
          disabled={isRunning}
          className="w-full bg-slate-900 border border-slate-700 rounded px-3 py-2 text-sm
            disabled:opacity-50"
        />
      </div>

      {error && <div className="text-red-400 text-sm">{error}</div>}

      {/* Progress bar */}
      {status && (
        <div>
          <div className="flex justify-between text-xs text-slate-400 mb-1">
            <span>
              {status.status === 'completed' ? 'Completed' :
               status.status === 'failed' ? 'Failed' :
               status.status === 'cancelled' ? 'Cancelled' :
               'Solving...'}
            </span>
            <span>{progress}%</span>
          </div>
          <div className="h-2 bg-slate-900 rounded-full overflow-hidden">
            <div
              className={`h-full transition-all duration-300 rounded-full
                ${status.status === 'completed' ? 'bg-green-500' :
                  status.status === 'failed' ? 'bg-red-500' :
                  'bg-purple-500'}`}
              style={{ width: `${progress}%` }}
            />
          </div>
          {status.error && (
            <div className="text-red-400 text-xs mt-1">{status.error}</div>
          )}
        </div>
      )}

      <div className="flex gap-2">
        <button
          onClick={startSolve}
          disabled={isRunning}
          className="bg-purple-600 hover:bg-purple-700 px-5 py-2 rounded text-sm font-medium
            disabled:opacity-50 transition-colors"
        >
          {isRunning ? 'Solving...' : 'Start Solve'}
        </button>
        {isRunning && (
          <button
            onClick={cancelSolve}
            className="bg-red-600 hover:bg-red-700 px-5 py-2 rounded text-sm font-medium
              transition-colors"
          >
            Cancel
          </button>
        )}
      </div>
    </div>
  );
}
