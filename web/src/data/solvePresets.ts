// Preset solve configurations for HUNL at different stack depths.
// Each preset maps to a strategy directory on disk produced by:
//   poker-solver solve --stack <bb> --iterations <n> --output <path>
//
// The UI reads from these paths — it does NOT run the solve itself.

export interface SolvePreset {
  id: string;
  name: string;
  description: string;
  stack_depth_bb: number;
  iterations: number;
  strategy_path: string;
  solve_command: string;  // CLI command to produce this strategy
}

export const SOLVE_PRESETS: SolvePreset[] = [
  {
    id: 'hu_100bb',
    name: 'Heads-Up 100bb',
    description: 'Standard 100bb deep heads-up',
    stack_depth_bb: 100,
    iterations: 1_000_000,
    strategy_path: 'data/strategy/hu_100bb',
    solve_command: 'poker-solver solve --stack 100 --iterations 1000000 --output data/strategy/hu_100bb',
  },
  {
    id: 'hu_200bb',
    name: 'Heads-Up 200bb',
    description: 'Deep-stacked 200bb heads-up',
    stack_depth_bb: 200,
    iterations: 1_000_000,
    strategy_path: 'data/strategy/hu_200bb',
    solve_command: 'poker-solver solve --stack 200 --iterations 1000000 --output data/strategy/hu_200bb',
  },
];
