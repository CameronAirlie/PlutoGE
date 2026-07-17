import React, { useEffect, useLayoutEffect, useRef } from 'react';
import { PanelFrame } from '../components/PanelFrame';

export function ViewportPanel({ host }: { host: HostState }): React.JSX.Element {
  const viewport = useRef<HTMLDivElement>(null);

  useLayoutEffect(() => {
    const element = viewport.current;
    if (!element) return;
    let animationFrame = 0;
    const updateBounds = (): void => {
      cancelAnimationFrame(animationFrame);
      animationFrame = requestAnimationFrame(() => {
        const bounds = element.getBoundingClientRect();
        const scale = window.devicePixelRatio;
        window.plutoEditor.setViewportBounds({ x: bounds.left * scale, y: bounds.top * scale, width: bounds.width * scale, height: bounds.height * scale });
      });
    };
    const observer = new ResizeObserver(updateBounds);
    observer.observe(element);
    window.addEventListener('resize', updateBounds);
    updateBounds();
    return () => { cancelAnimationFrame(animationFrame); observer.disconnect(); window.removeEventListener('resize', updateBounds); window.plutoEditor.setViewportVisible(false); };
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
