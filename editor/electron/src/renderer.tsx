import React, { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import './styles.css';

function App(): React.JSX.Element {
  const viewport = useRef<HTMLDivElement>(null);
  const [host, setHost] = useState<HostState>({ status: 'starting' });

  useEffect(() => {
    const unsubscribe = window.plutoEditor.onHostState(setHost);
    void window.plutoEditor.getHostState().then(setHost);
    return unsubscribe;
  }, []);

  useLayoutEffect(() => {
    const element = viewport.current;
    if (!element) return;

    let animationFrame = 0;
    const updateBounds = (): void => {
      cancelAnimationFrame(animationFrame);
      animationFrame = requestAnimationFrame(() => {
        const bounds = element.getBoundingClientRect();
        const scale = window.devicePixelRatio;
        window.plutoEditor.setViewportBounds({
          x: bounds.left * scale,
          y: bounds.top * scale,
          width: bounds.width * scale,
          height: bounds.height * scale,
        });
      });
    };

    const resizeObserver = new ResizeObserver(updateBounds);
    resizeObserver.observe(element);
    window.addEventListener('resize', updateBounds);
    updateBounds();
    return () => {
      cancelAnimationFrame(animationFrame);
      resizeObserver.disconnect();
      window.removeEventListener('resize', updateBounds);
      window.plutoEditor.setViewportVisible(false);
    };
  }, []);

  useEffect(() => {
    const updateVisibility = (): void => window.plutoEditor.setViewportVisible(!document.hidden);
    document.addEventListener('visibilitychange', updateVisibility);
    updateVisibility();
    return () => document.removeEventListener('visibilitychange', updateVisibility);
  }, []);

  return (
    <div className="editor-shell">
      <header className="toolbar">
        <div className="brand"><span className="brand-mark">P</span> PlutoGE</div>
        <div className="toolbar-group">
          <button className="tool-button" title="Select">↖</button>
          <button className="tool-button" title="Move">✥</button>
          <button className="tool-button" title="Rotate">↻</button>
          <button className="tool-button" title="Scale">↗</button>
        </div>
        <button className="play-button">▶ Run</button>
      </header>

      <aside className="scene-panel panel">
        <h2>Scene</h2>
        <div className="search">⌕ Search entities</div>
        <div className="tree-row selected"><span>◇</span> Main Camera</div>
        <div className="tree-row"><span>☼</span> Directional Light</div>
        <div className="tree-row"><span>◆</span> Environment</div>
      </aside>

      <main className="viewport-frame">
        <div ref={viewport} className="viewport" aria-label="Engine viewport">
          {host.status !== 'ready' && (
            <div className="viewport-message">
              <strong>{host.status === 'starting' ? 'Starting engine…' : 'Viewport unavailable'}</strong>
              {host.message && <span>{host.message}</span>}
              {host.status === 'error' && <button onClick={() => void window.plutoEditor.restartHost()}>Restart host</button>}
            </div>
          )}
        </div>
      </main>

      <aside className="inspector panel">
        <h2>Inspector</h2>
        <p className="eyebrow">ENTITY</p>
        <h3>Main Camera</h3>
        <section>
          <h4>Transform</h4>
          {['Position', 'Rotation', 'Scale'].map((label) => (
            <label key={label}>{label}<span><i>X</i> 0.00 <i>Y</i> 0.00 <i>Z</i> {label === 'Scale' ? '1.00' : '0.00'}</span></label>
          ))}
        </section>
        <section><h4>Camera</h4><label>Field of view <span>50°</span></label><label>Near clip <span>0.10</span></label></section>
      </aside>

      <footer className="statusbar">
        <span className={`status-dot ${host.status}`} /> Engine {host.status}
        <span className="status-hint">Right-drag to orbit · Wheel to zoom</span>
      </footer>
    </div>
  );
}

const root = document.getElementById('root');
if (root) createRoot(root).render(<App />);
