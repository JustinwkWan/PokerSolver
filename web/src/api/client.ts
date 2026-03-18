const BASE = '';

export interface StrategyResult {
  actions: string[];
  probabilities: number[];
}

export interface SolveConfig {
  stack_depth_bb: number;
  num_iterations: number;
  output_path: string;
  preflop_sizes: number[];
  flop_sizes: number[];
  turn_sizes: number[];
  river_sizes: number[];
}

export interface JobStatus {
  job_id: string;
  status: 'running' | 'completed' | 'failed' | 'cancelled';
  progress: number;
  iteration: number;
  total_iterations: number;
  error?: string;
}

export interface TreeInfo {
  num_nodes: number;
  num_action_nodes: number;
  num_terminal_nodes: number;
  root: NodeInfo;
}

export interface NodeAction {
  type: string;
  amount: number;
  child_index: number;
}

export interface NodeInfo {
  index?: number;
  type: 'action' | 'terminal';
  player: number;
  street: number;
  num_actions: number;
  pot?: number;
  fold_player?: number;
  actions?: NodeAction[];
}

export interface RangeCombo {
  hand: string;
  actions: string[];
  probabilities: number[];
}

async function post<T>(path: string, body: unknown): Promise<T> {
  const res = await fetch(`${BASE}${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ error: res.statusText }));
    throw new Error(err.error || res.statusText);
  }
  return res.json();
}

async function get<T>(path: string): Promise<T> {
  const res = await fetch(`${BASE}${path}`);
  if (!res.ok) {
    const err = await res.json().catch(() => ({ error: res.statusText }));
    throw new Error(err.error || res.statusText);
  }
  return res.json();
}

export const api = {
  solve(config: Partial<SolveConfig>) {
    return post<{ job_id: string }>('/api/solve', config);
  },

  solveStatus(jobId: string) {
    return get<JobStatus>(`/api/solve/${jobId}/status`);
  },

  cancelSolve(jobId: string) {
    return post<{ cancelled: boolean }>(`/api/solve/${jobId}/cancel`, {});
  },

  query(params: {
    strategy_path?: string;
    hand: string;
    board?: string;
    history?: string;
    stack_depth_bb?: number;
  }) {
    return post<StrategyResult>('/api/query', params);
  },

  queryRange(params: {
    strategy_path?: string;
    board?: string;
    history?: string;
    stack_depth_bb?: number;
  }) {
    return post<{ combos: RangeCombo[] }>('/api/query/range', params);
  },

  subgame(params: {
    hand: string;
    board: string;
    history?: string;
    iterations?: number;
    stack_depth_bb?: number;
  }) {
    return post<{ job_id: string }>('/api/subgame', params);
  },

  exploit(params: {
    strategy_path?: string;
    samples?: number;
    stack_depth_bb?: number;
  }) {
    return post<{ job_id: string }>('/api/exploit', params);
  },

  tree(stack?: number) {
    const q = stack ? `?stack=${stack}` : '';
    return get<TreeInfo>(`/api/tree${q}`);
  },

  treeNode(nodeIdx: number, stack?: number) {
    const q = stack ? `?stack=${stack}` : '';
    return get<NodeInfo>(`/api/tree/${nodeIdx}${q}`);
  },

  strategyStatus(paths: string[]) {
    return post<Record<string, { exists: boolean; iterations: number }>>('/api/strategy/status', { paths });
  },
};
