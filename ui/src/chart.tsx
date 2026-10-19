import { useEffect, useMemo, useRef, useState } from "react";
import type { History, SeriesKind } from "./types";

const palette = ["#e3914a", "#7eb8b2", "#d4655d", "#ebe6dc", "#d6c07a", "#8fba7a", "#d7a0c2", "#9bb0c9"];

const tabs: { kind: SeriesKind; label: string }[] = [
  { kind: "temperature", label: "Температура" },
  { kind: "load", label: "Загрузка" },
  { kind: "rpm", label: "Обороты" },
  { kind: "power", label: "Мощность" },
  { kind: "clock", label: "Частоты" },
];

export function Telemetry({ history }: { history: History | null }) {
  const available = tabs.filter((tab) => history?.series.some((series) => series.kind === tab.kind));
  const [kind, setKind] = useState<SeriesKind>("temperature");
  const active = available.some((tab) => tab.kind === kind) ? kind : available[0]?.kind ?? "temperature";
  const series = (history?.series ?? []).filter((item) => item.kind === active);
  const [hidden, setHidden] = useState<string[]>([]);
  const visible = series.filter((item) => !hidden.includes(item.id));

  return (
    <aside className="telemetry">
      <div className="kicker">Телеметрия</div>
      <div className="spread">
        <h2>Живые ряды</h2>
        <span className="faint">{history ? "3 мин и дальше" : "жду первые точки"}</span>
      </div>
      <div className="tabs">
        {available.map((tab) => (
          <button key={tab.kind} className={tab.kind === active ? "on" : ""} onClick={() => setKind(tab.kind)}>
            {tab.label}
          </button>
        ))}
      </div>
      {visible.length === 0 ? (
        <p className="note">Для этого среза ещё нет точек. Они появятся, как только датчик начнёт отвечать.</p>
      ) : (
        <Chart series={visible} />
      )}
      <div className="legend">
        {series.map((item, index) => (
          <button
            key={item.id}
            className={hidden.includes(item.id) ? "off" : ""}
            onClick={() => setHidden((current) => (current.includes(item.id) ? current.filter((id) => id !== item.id) : [...current, item.id]))}
          >
            <i style={{ background: palette[index % palette.length] }} />
            {item.label}
          </button>
        ))}
      </div>
    </aside>
  );
}

function Chart({ series }: { series: History["series"] }) {
  const canvas = useRef<HTMLCanvasElement>(null);
  const [hover, setHover] = useState<number | null>(null);
  const bounds = useMemo(() => {
    let minT = Infinity;
    let maxT = -Infinity;
    let minV = Infinity;
    let maxV = -Infinity;
    for (const item of series) {
      for (const [t, value] of item.points) {
        minT = Math.min(minT, t);
        maxT = Math.max(maxT, t);
        minV = Math.min(minV, value);
        maxV = Math.max(maxV, value);
      }
    }
    if (!Number.isFinite(minT)) return null;
    if (maxV - minV < 1) {
      minV -= 1;
      maxV += 1;
    }
    if (maxT - minT < 1) maxT = minT + 1;
    return { minT, maxT, minV, maxV };
  }, [series]);

  useEffect(() => {
    const node = canvas.current;
    if (!node || !bounds) return;
    const rect = node.getBoundingClientRect();
    const ratio = window.devicePixelRatio || 1;
    node.width = Math.max(1, Math.floor(rect.width * ratio));
    node.height = Math.max(1, Math.floor(rect.height * ratio));
    const ctx = node.getContext("2d");
    if (!ctx) return;
    ctx.setTransform(ratio, 0, 0, ratio, 0, 0);
    const width = rect.width;
    const height = rect.height;
    ctx.clearRect(0, 0, width, height);
    const left = 42;
    const right = 12;
    const top = 12;
    const bottom = 22;
    const plotW = width - left - right;
    const plotH = height - top - bottom;
    ctx.strokeStyle = "rgba(236,226,210,0.08)";
    ctx.fillStyle = "#9c9488";
    ctx.font = "11px IBM Plex Mono, monospace";
    for (let i = 0; i < 4; i++) {
      const y = top + (plotH * i) / 3;
      ctx.beginPath();
      ctx.moveTo(left, y);
      ctx.lineTo(width - right, y);
      ctx.stroke();
      const value = bounds.maxV - ((bounds.maxV - bounds.minV) * i) / 3;
      ctx.fillText(Math.round(value).toString(), 4, y + 4);
    }
    const xOf = (t: number) => left + ((t - bounds.minT) / (bounds.maxT - bounds.minT)) * plotW;
    const yOf = (value: number) => top + ((bounds.maxV - value) / (bounds.maxV - bounds.minV)) * plotH;
    series.forEach((item, index) => {
      if (item.points.length === 0) return;
      ctx.beginPath();
      item.points.forEach((point, pointIndex) => {
        const x = xOf(point[0]);
        const y = yOf(point[1]);
        if (pointIndex === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      });
      ctx.strokeStyle = palette[index % palette.length];
      ctx.lineWidth = 1.8;
      ctx.stroke();
    });
    if (hover != null) {
      const x = left + hover * plotW;
      ctx.strokeStyle = "rgba(235,230,220,0.35)";
      ctx.beginPath();
      ctx.moveTo(x, top);
      ctx.lineTo(x, top + plotH);
      ctx.stroke();
    }
  }, [series, bounds, hover]);

  return (
    <canvas
      ref={canvas}
      className="chart"
      onMouseMove={(event) => {
        const rect = event.currentTarget.getBoundingClientRect();
        const x = (event.clientX - rect.left - 42) / Math.max(1, rect.width - 54);
        setHover(Math.min(1, Math.max(0, x)));
      }}
      onMouseLeave={() => setHover(null)}
    />
  );
}
