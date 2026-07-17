import React, { useEffect, useLayoutEffect, useRef } from 'react';
import { PanelFrame } from '../components/PanelFrame';

export function GameViewportPanel({ host, editor }: { host: HostState; editor?: EditorState }): React.JSX.Element {
  const viewport = useRef<HTMLDivElement>(null);
  const hasCamera = editor?.entities.some((entity) => entity.active && entity.components.some((component) => component.type === 'CameraComponent' && component.enabled)) ?? false;

  useLayoutEffect(() => {
    const element = viewport.current;
    if (!element) return;
    let animationFrame = 0;
    let previousBounds = '';
    const updateBounds = (): void => {
      const bounds = element.getBoundingClientRect();
      const scale = window.devicePixelRatio;
      const next = [bounds.left, bounds.top, bounds.width, bounds.height].map((value) => Math.round(value * scale));
      const key = next.join(',');
      if (key !== previousBounds && next[2] > 0 && next[3] > 0) {
        previousBounds = key;
        window.plutoEditor.setGameViewportBounds({ x: next[0], y: next[1], width: next[2], height: next[3] });
      }
      animationFrame = requestAnimationFrame(updateBounds);
    };
    updateBounds();
    return () => {
      cancelAnimationFrame(animationFrame);
      window.plutoEditor.setGameViewportVisible(false);
    };
  }, []);

  useEffect(() => {
    const updateVisibility = (): void => window.plutoEditor.setGameViewportVisible(!document.hidden);
    document.addEventListener('visibilitychange', updateVisibility);
    updateVisibility();
    return () => document.removeEventListener('visibilitychange', updateVisibility);
  }, []);

  return <PanelFrame id="game" title="Game View" className="viewport-panel game-viewport-panel">
    <div ref={viewport} className="viewport" aria-label="Game camera viewport">
      <div className="viewport-overlay"><span>Game Camera</span><span>Rendered</span></div>
      {host.status !== 'ready' && <div className="viewport-message">
        <strong>{host.status === 'starting' ? 'Starting game view…' : 'Game view unavailable'}</strong>
        {host.message && <span>{host.message}</span>}
        {host.status === 'error' && <button onClick={() => void window.plutoEditor.restartGameHost()}>Restart game view</button>}
      </div>}
      {host.status === 'ready' && !hasCamera && <div className="viewport-message"><strong>No active game camera</strong><span>Add or enable a Camera component to render the Game View.</span></div>}
    </div>
  </PanelFrame>;
}
