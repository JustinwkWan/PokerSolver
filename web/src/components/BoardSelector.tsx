import { useState } from 'react';

const RANKS = ['A', 'K', 'Q', 'J', 'T', '9', '8', '7', '6', '5', '4', '3', '2'];
const SUITS = [
  { char: 's', symbol: '\u2660', color: 'text-slate-300' },
  { char: 'h', symbol: '\u2665', color: 'text-red-500' },
  { char: 'd', symbol: '\u2666', color: 'text-blue-400' },
  { char: 'c', symbol: '\u2663', color: 'text-green-500' },
];

interface Props {
  value: string;
  onChange: (board: string) => void;
  maxCards?: number;
}

export default function BoardSelector({ value, onChange, maxCards = 5 }: Props) {
  const [open, setOpen] = useState(false);

  const selectedCards = new Set<string>();
  for (let i = 0; i < value.length; i += 2) {
    selectedCards.add(value.substring(i, i + 2));
  }

  const numCards = selectedCards.size;

  const toggleCard = (card: string) => {
    if (selectedCards.has(card)) {
      selectedCards.delete(card);
    } else {
      if (numCards >= maxCards) return;
      selectedCards.add(card);
    }
    onChange(Array.from(selectedCards).join(''));
  };

  const clear = () => {
    onChange('');
    setOpen(false);
  };

  // Display selected cards as visual card pills
  const displayCards = [];
  for (let i = 0; i < value.length; i += 2) {
    const rank = value[i];
    const suit = SUITS.find(s => s.char === value[i + 1]);
    displayCards.push({ rank, suit, card: value.substring(i, i + 2) });
  }

  return (
    <div className="relative">
      <div
        className="flex items-center gap-1 bg-slate-800 rounded px-3 py-2 cursor-pointer min-h-[40px] border border-slate-700"
        onClick={() => setOpen(!open)}
      >
        {displayCards.length === 0 ? (
          <span className="text-slate-500 text-sm">Select board cards...</span>
        ) : (
          displayCards.map(({ rank, suit, card }) => (
            <span key={card} className={`font-mono font-bold text-lg ${suit?.color}`}>
              {rank}{suit?.symbol}
            </span>
          ))
        )}
        {displayCards.length > 0 && (
          <button
            onClick={(e) => { e.stopPropagation(); clear(); }}
            className="ml-auto text-slate-500 hover:text-slate-300 text-sm"
          >
            Clear
          </button>
        )}
      </div>

      {open && (
        <div className="absolute z-50 mt-1 bg-slate-800 border border-slate-700 rounded-lg p-3 shadow-xl">
          {SUITS.map(suit => (
            <div key={suit.char} className="flex gap-1 mb-1">
              {RANKS.map(rank => {
                const card = rank + suit.char;
                const selected = selectedCards.has(card);
                return (
                  <button
                    key={card}
                    onClick={() => toggleCard(card)}
                    disabled={!selected && numCards >= maxCards}
                    className={`w-8 h-10 text-xs font-mono font-bold rounded
                      transition-all
                      ${selected
                        ? `${suit.color} bg-slate-600 ring-2 ring-yellow-400`
                        : `text-slate-400 bg-slate-900 hover:bg-slate-700
                           disabled:opacity-30 disabled:cursor-not-allowed`}`}
                  >
                    {rank}
                    <span className={suit.color}>{suit.symbol}</span>
                  </button>
                );
              })}
            </div>
          ))}
          <div className="text-xs text-slate-500 mt-2">
            {numCards}/{maxCards} cards selected
          </div>
        </div>
      )}
    </div>
  );
}
