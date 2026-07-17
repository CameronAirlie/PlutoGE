import React, { useEffect, useLayoutEffect, useRef } from 'react';
import { PanelFrame } from '../components/PanelFrame';

export function ViewportPanel({ host }: { host: HostState }): React.JSX.Element {
  const viewport = useRef<HTMLDivElement>(null);

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
        window.plutoEditor.setViewportBounds({ x: next[0], y: next[1], width: next[2], height: next[3] });
      }
      animationFrame = requestAnimationFrame(updateBounds);
    };
    updateBounds();
    return () => { cancelAnimationFrame(animationFrame); window.plutoEditor.setViewportVisible(false); };
  }, []);

  useEffect(() => {
    const updateVisibility = (): void => window.plutoEditor.setViewportVisible(!document.hidden);
    document.addEventListener('visibilitychange', updateVisibility);
    updateVisibility();
    return () => document.removeEventListener('visibilitychange', updateVisibility);
  }, []);

  return <PanelFrame id="viewport" title="Scene View" className="viewport-panel">
    <div
      ref={viewport}
      className="viewport"
      aria-label="Engine viewport"
      onDragOver={(event) => { if (event.dataTransfer.types.includes('application/x-plutoge-asset')) event.preventDefault(); }}
      onDrop={(event) => {
        const reference = event.dataTransfer.getData('application/x-plutoge-asset');
        if (reference) window.plutoEditor.instantiateAsset(reference);
      }}
    >
      <div className="viewport-overlay"><span>Perspective</span><span>Lit</span></div>
      {host.status !== 'ready' && <div className="viewport-message">
        <strong>{host.status === 'starting' ? 'Starting engine…' : 'Viewport unavailable'}</strong>
        {host.message && <span>{host.message}</span>}
        {host.status === 'error' && <button onClick={() => void window.plutoEditor.restartHost()}>Restart host</button>}
      </div>}
    </div>
  </PanelFrame>;
}
