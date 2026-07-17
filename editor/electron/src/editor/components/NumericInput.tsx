import React, { useEffect, useRef, useState } from 'react';

export function NumericInput({ value, onCommit, disabled = false, step = 0.1, min, max, integer = false, className }: {
  value: number;
  onCommit(value: number): void;
  disabled?: boolean;
  step?: number;
  min?: number;
  max?: number;
  integer?: boolean;
  className?: string;
}): React.JSX.Element {
  const [text, setText] = useState(String(value));
  const drag = useRef<{ startX: number; startValue: number; value: number } | undefined>(undefined);

  useEffect(() => setText(String(value)), [value]);

  const constrain = (next: number): number => {
    let result = integer ? Math.round(next) : next;
    if (min !== undefined) result = Math.max(min, result);
    if (max !== undefined) result = Math.min(max, result);
    return result;
  };
  const commitText = (): void => {
    const parsed = Number(text);
    if (!Number.isFinite(parsed)) {
      setText(String(value));
      return;
    }
    const next = constrain(parsed);
    setText(String(next));
    if (next !== value) onCommit(next);
  };

  return <input
    className={`numeric-input${className ? ` ${className}` : ''}`}
    disabled={disabled}
    type="text"
    inputMode="decimal"
    value={text}
    onChange={(event) => setText(event.currentTarget.value)}
    onBlur={commitText}
    onKeyDown={(event) => {
      if (event.key === 'Enter') event.currentTarget.blur();
      if (event.key === 'Escape') { setText(String(value)); event.currentTarget.blur(); }
    }}
    onPointerDown={(event) => {
      if (disabled || event.button !== 0) return;
      drag.current = { startX: event.clientX, startValue: Number(text) || 0, value: Number(text) || 0 };
      event.currentTarget.setPointerCapture(event.pointerId);
    }}
    onPointerMove={(event) => {
      if (!drag.current || !event.currentTarget.hasPointerCapture(event.pointerId)) return;
      const next = constrain(drag.current.startValue + (event.clientX - drag.current.startX) * step);
      drag.current.value = next;
      setText(String(Number(next.toFixed(6))));
    }}
    onPointerUp={(event) => {
      if (!drag.current) return;
      const moved = Math.abs(event.clientX - drag.current.startX) >= 2;
      const next = drag.current.value;
      drag.current = undefined;
      event.currentTarget.releasePointerCapture(event.pointerId);
      if (moved && next !== value) onCommit(next);
      else event.currentTarget.select();
    }}
  />;
}
