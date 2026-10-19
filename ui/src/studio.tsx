import { useEffect, useRef, useState, type PointerEvent } from "react";
import { curvePresets, effectLabel, formatNumber, hexOf, hsvToRgb, modeLabel, rgbOf, rgbToHsv, swatches } from "./format";
import type { AppState, CurvePoint, Device, Fan, FanMode, Tick, Zone } from "./types";

interface Actions {
  onZone: (zoneId: string, body: object) => Promise<void>;
  onFan: (fanId: string, body: object) => Promise<void>;
  onPaint: (body: object) => Promise<void>;
}

export function Stage({ device, state, tick, actions }: { device: Device; state: AppState; tick: Tick | null; actions: Actions }) {
  const [zoneId, setZoneId] = useState(device.zones[0]?.id ?? "");
  const [fanId, setFanId] = useState(device.fans[0]?.id ?? "");
  useEffect(() => {
    setZoneId(device.zones[0]?.id ?? "");
    setFanId(device.fans[0]?.id ?? "");
  }, [device.id, device.zones, device.fans]);
  const zone = device.zones.find((item) => item.id === zoneId) ?? device.zones[0];
  const fan = device.fans.find((item) => item.id === fanId) ?? device.fans[0];
  const sources = state.devices.flatMap((item) => item.sensors.filter((sensor) => sensor.kind === "temperature"));

  return (
    <main className="stage">
      <div className="kicker">{device.vendor} · {device.connection}</div>
      <div className="spread">
        <h1>{device.name}</h1>
        <span className={`pill ${device.hardware}`}>{device.hardware === "live" ? "Управление" : device.hardware === "profile" ? "В профиле" : "Наблюдение"}</span>
      </div>
      <p className="note">{device.note}</p>
      {device.status && <p className="status">{device.status}</p>}
      <div className="readouts">
        {device.sensors.map((sensor) => {
          const live = tick?.sensors.find((item) => item.id === sensor.id);
          const value = live ? live.value : sensor.value;
          const digits = sensor.kind === "temperature" ? 1 : 0;
          return (
            <div className="readout" key={sensor.id}>
              <span>{sensor.label}</span>
              <strong>
                {formatNumber(value, digits)}
                <i>{sensor.unit}</i>
              </strong>
            </div>
          );
        })}
        {device.sensors.length === 0 && <div className="readout"><span>Датчики</span><strong>—</strong></div>}
      </div>

      {zone && (
        <section className="card">
          <div className="card-head">
            <h2>Цвет</h2>
            {device.zones.length > 1 && (
              <button className="btn ghost" onClick={() => actions.onPaint({ color: zone.color, effect: zone.effect, speed: zone.speed, brightness: zone.brightness })}>
                На все зоны
              </button>
            )}
          </div>
          <div className="zones">
            {device.zones.map((item) => (
              <button key={item.id} className={item.id === zone.id ? "zone on" : "zone"} onClick={() => setZoneId(item.id)}>
                <i className="swatch" style={{ background: item.color, display: "inline-block", marginRight: 8 }} />
                {item.name}
              </button>
            ))}
          </div>
          <LedStrip zone={zone} />
          <ColorStudio zone={zone} onChange={(body) => actions.onZone(zone.id, body)} />
        </section>
      )}

      {!zone && (
        <section className="card">
          <h2>Цвет</h2>
          <p className="note">
            У этого узла Helios цвет не ставит. На B450 нет USB ITE 048D, SuperIO пока только читается. Открой слева память
            (JAD / ENE DRAM) или периферию с зелёной точкой — там цвет уходит в OpenRGB кнопкой «Применить». Хаб Aerocool на
            SYS_FAN1 крутит крыльчатки, цвет у него с пульта или с тонкого ARGB в колодку платы.
          </p>
        </section>
      )}

      {fan && (
        <section className="card">
          <div className="card-head">
            <h2>Вентиляторы</h2>
            <span className="faint">{fan.controllable ? "порог не даёт остановить крыльчатку" : "этот канал только наблюдается"}</span>
          </div>
          {device.fans.map((item) => (
            <FanRow
              key={item.id}
              fan={item}
              open={item.id === fan.id}
              tick={tick}
              deviceId={device.id}
              sources={sources}
              onOpen={() => setFanId(item.id)}
              onChange={(body) => actions.onFan(item.id, body)}
            />
          ))}
        </section>
      )}

    </main>
  );
}

function LedStrip({ zone }: { zone: Zone }) {
  const count = Math.min(18, Math.max(1, zone.leds));
  const duration = `${Math.max(0.45, 2.6 - zone.speed / 45).toFixed(2)}s`;
  return (
    <div className={`preview effect-${zone.effect}`} style={{ ["--led" as string]: zone.color, ["--alpha" as string]: zone.effect === "off" ? 0.4 : zone.brightness / 100, ["--dur" as string]: duration }}>
      {Array.from({ length: count }, (_, index) => (
        <i key={index} className="led" style={{ ["--i" as string]: index }} />
      ))}
    </div>
  );
}

function ColorStudio({ zone, onChange }: { zone: Zone; onChange: (body: object) => Promise<void> }) {
  const initial = rgbToHsv(...Object.values(rgbOf(zone.color)) as [number, number, number]);
  const [hsv, setHsv] = useState(initial);
  const [hex, setHex] = useState(zone.color);
  const [busy, setBusy] = useState(false);
  const dragging = useRef(false);
  useEffect(() => {
    const next = rgbToHsv(...Object.values(rgbOf(zone.color)) as [number, number, number]);
    setHsv(next);
    setHex(zone.color);
  }, [zone.id, zone.color]);

  function colorOf(next = hsv) {
    const rgb = hsvToRgb(next.h, next.s, next.v);
    return hexOf(rgb.r, rgb.g, rgb.b);
  }

  function send(body: object) {
    setBusy(true);
    void onChange(body).finally(() => setBusy(false));
  }

  function applyNow(extra: object = {}) {
    const color = colorOf();
    setHex(color);
    send({ color, effect: zone.effect, speed: zone.speed, brightness: zone.brightness, ...extra });
  }

  function finishDrag() {
    if (!dragging.current) return;
    dragging.current = false;
    applyNow();
  }

  function drag(event: PointerEvent<HTMLDivElement>, mode: "sv" | "hue") {
    const rect = event.currentTarget.getBoundingClientRect();
    const x = Math.min(1, Math.max(0, (event.clientX - rect.left) / rect.width));
    const y = Math.min(1, Math.max(0, (event.clientY - rect.top) / rect.height));
    const next = mode === "hue" ? { ...hsv, h: x * 360 } : { ...hsv, s: x, v: 1 - y };
    setHsv(next);
    setHex(colorOf(next));
  }

  return (
    <div className="studio">
      <div>
        <div
          className="sv"
          style={{ ["--h" as string]: hsv.h }}
          onPointerDown={(event) => {
            dragging.current = true;
            event.currentTarget.setPointerCapture(event.pointerId);
            drag(event, "sv");
          }}
          onPointerMove={(event) => {
            if (event.currentTarget.hasPointerCapture(event.pointerId)) drag(event, "sv");
          }}
          onPointerUp={finishDrag}
          onLostPointerCapture={finishDrag}
        >
          <i style={{ left: `${hsv.s * 100}%`, top: `${(1 - hsv.v) * 100}%` }} />
        </div>
        <div
          className="hue"
          onPointerDown={(event) => {
            dragging.current = true;
            event.currentTarget.setPointerCapture(event.pointerId);
            drag(event, "hue");
          }}
          onPointerMove={(event) => {
            if (event.currentTarget.hasPointerCapture(event.pointerId)) drag(event, "hue");
          }}
          onPointerUp={finishDrag}
          onLostPointerCapture={finishDrag}
        >
          <i style={{ left: `${(hsv.h / 360) * 100}%`, top: "50%" }} />
        </div>
        <div className="hexrow">
          <input
            className="field"
            value={hex}
            onChange={(event) => setHex(event.target.value)}
            onBlur={() => {
              if (/^#[0-9a-fA-F]{6}$/.test(hex)) {
                const rgb = rgbOf(hex);
                setHsv(rgbToHsv(rgb.r, rgb.g, rgb.b));
                send({ color: hex.toLowerCase(), effect: zone.effect, speed: zone.speed, brightness: zone.brightness });
              } else setHex(zone.color);
            }}
          />
          <button className="btn primary" type="button" disabled={busy} onClick={() => applyNow()}>
            {busy ? "…" : "Применить"}
          </button>
        </div>
        <div className="swatches">
          {swatches.map((color) => (
            <button
              key={color}
              style={{ background: color }}
              onClick={() => {
                const rgb = rgbOf(color);
                setHsv(rgbToHsv(rgb.r, rgb.g, rgb.b));
                setHex(color);
                send({ color, effect: zone.effect, speed: zone.speed, brightness: zone.brightness });
              }}
              aria-label={color}
            />
          ))}
        </div>
      </div>
      <div>
        <div className="effects">
          {zone.effects.map((effect) => (
            <button key={effect} className={zone.effect === effect ? "on" : ""} onClick={() => applyNow({ effect })}>
              {effectLabel[effect] ?? effect}
            </button>
          ))}
        </div>
        <div className="sliders">
          <label>
            Скорость {zone.speed}
            <input type="range" min={0} max={100} value={zone.speed} onChange={(event) => send({ speed: Number(event.target.value) })} />
          </label>
          <label>
            Яркость {zone.brightness}
            <input type="range" min={0} max={100} value={zone.brightness} onChange={(event) => send({ brightness: Number(event.target.value) })} />
          </label>
        </div>
      </div>
    </div>
  );
}

function FanRow({
  fan,
  open,
  tick,
  deviceId,
  sources,
  onOpen,
  onChange,
}: {
  fan: Fan;
  open: boolean;
  tick: Tick | null;
  deviceId: string;
  sources: { id: string; label: string }[];
  onOpen: () => void;
  onChange: (body: object) => Promise<void>;
}) {
  const live = tick?.fans.find((item) => item.deviceId === deviceId && item.id === fan.id);
  const rpm = live?.rpmKnown ? live.rpm : fan.rpmKnown ? fan.rpm : null;
  const duty = live ? live.duty : fan.duty;
  return (
    <article className="fan">
      <div className="fan-main">
        <button className="btn ghost" onClick={onOpen}>
          {fan.name}
          {fan.isPump ? " · помпа" : ""}
        </button>
        <div className="rpm">{rpm == null ? "— об/мин" : `${formatNumber(rpm)} об/мин`}</div>
      </div>
      {open && (
        <>
          <div className="modes">
            {(["auto", "curve", "manual"] as FanMode[]).map((mode) => (
              <button key={mode} className={fan.mode === mode ? "on" : ""} disabled={!fan.controllable && mode !== "auto"} onClick={() => void onChange({ mode })}>
                {modeLabel[mode]}
              </button>
            ))}
          </div>
          {fan.mode === "manual" && (
            <label className="sliders" style={{ marginTop: 12 }}>
              Скважность {formatNumber(duty)}%
              <input type="range" min={fan.allowStop ? 0 : fan.minDuty} max={100} value={duty} disabled={!fan.controllable} onChange={(event) => void onChange({ duty: Number(event.target.value), mode: "manual" })} />
            </label>
          )}
          {fan.mode === "curve" && (
            <CurveEditor fan={fan} sources={sources} currentTemp={sourceValue(sources, fan, tick)} onChange={onChange} />
          )}
          <label className="check" style={{ marginTop: 10 }}>
            <input type="checkbox" checked={fan.allowStop} disabled={!fan.controllable} onChange={(event) => void onChange({ allowStop: event.target.checked })} />
            <span>Разрешить остановку. Без этого пол — {fan.minDuty}%. Ниже крыльчатка может не стартовать.</span>
          </label>
          {fan.mode === "manual" && <p className="note">Ручной режим снимает заводскую кривую. Следи за температурой.</p>}
        </>
      )}
    </article>
  );
}

function sourceValue(sources: { id: string; label: string }[], fan: Fan, tick: Tick | null) {
  if (!sources.some((source) => source.id === fan.source)) return null;
  return tick?.sensors.find((sensor) => sensor.id === fan.source)?.value ?? null;
}

function CurveEditor({
  fan,
  sources,
  currentTemp,
  onChange,
}: {
  fan: Fan;
  sources: { id: string; label: string }[];
  currentTemp: number | null;
  onChange: (body: object) => Promise<void>;
}) {
  const [points, setPoints] = useState(fan.curve);
  const [drag, setDrag] = useState<number | null>(null);
  useEffect(() => setPoints(fan.curve), [fan.id, fan.curve]);

  function move(event: PointerEvent<SVGSVGElement>, index = drag) {
    if (index == null) return;
    const rect = event.currentTarget.getBoundingClientRect();
    const x = Math.min(1, Math.max(0, (event.clientX - rect.left - 36) / (rect.width - 52)));
    const y = Math.min(1, Math.max(0, (event.clientY - rect.top - 16) / (rect.height - 42)));
    const temp = 20 + x * 80;
    const duty = Math.min(100, Math.max(fan.allowStop ? 0 : fan.minDuty, (1 - y) * 100));
    setPoints((current) => {
      const next = current.map((point, pointIndex) => (pointIndex === index ? { temp, duty } : point));
      next.sort((a, b) => a.temp - b.temp);
      for (let i = 1; i < next.length; i++) {
        if (next[i].temp < next[i - 1].temp + 1) next[i] = { ...next[i], temp: next[i - 1].temp + 1 };
      }
      return next;
    });
  }

  const path = points
    .map((point, index) => `${index === 0 ? "M" : "L"} ${36 + ((point.temp - 20) / 80) * 250} ${16 + (1 - point.duty / 100) * 160}`)
    .join(" ");

  return (
    <div className="curve-wrap">
      <svg
        className="curve"
        viewBox="0 0 300 200"
        onPointerMove={(event) => move(event)}
        onPointerUp={() => {
          if (drag != null) void onChange({ curve: sanitize(points), mode: "curve" });
          setDrag(null);
        }}
        onDoubleClick={(event) => {
          if (points.length >= 8) return;
          const rect = event.currentTarget.getBoundingClientRect();
          const temp = 20 + ((event.clientX - rect.left - 36) / (rect.width - 52)) * 80;
          const duty = (1 - (event.clientY - rect.top - 16) / (rect.height - 42)) * 100;
          const next = sanitize([...points, { temp, duty }]);
          setPoints(next);
          void onChange({ curve: next, mode: "curve" });
        }}
      >
        <rect x="36" y="16" width="250" height="160" fill="none" stroke="rgba(236,226,210,0.12)" />
        {currentTemp != null && currentTemp >= 20 && currentTemp <= 100 && (
          <line x1={36 + ((currentTemp - 20) / 80) * 250} x2={36 + ((currentTemp - 20) / 80) * 250} y1="16" y2="176" stroke="#7eb8b2" strokeDasharray="3 3" />
        )}
        <path d={path} fill="none" stroke="#e3914a" strokeWidth="2" />
        {points.map((point, index) => (
          <circle
            key={`${point.temp}-${index}`}
            cx={36 + ((point.temp - 20) / 80) * 250}
            cy={16 + (1 - point.duty / 100) * 160}
            r="6"
            fill="#ebe6dc"
            onPointerDown={(event) => {
              event.currentTarget.ownerSVGElement?.setPointerCapture(event.pointerId);
              setDrag(index);
            }}
          />
        ))}
        <text x="36" y="194" fill="#9c9488" fontSize="11">
          20°
        </text>
        <text x="250" y="194" fill="#9c9488" fontSize="11">
          100°
        </text>
      </svg>
      <div className="presets">
        {curvePresets.map((preset) => (
          <button key={preset.id} onClick={() => void onChange({ curve: preset.points, mode: "curve" })}>
            {preset.label}
          </button>
        ))}
        {points.length > 2 && (
          <button
            onClick={() => {
              const next = points.slice(0, -1);
              setPoints(next);
              void onChange({ curve: next, mode: "curve" });
            }}
          >
            Убрать точку
          </button>
        )}
        <select className="select" value={fan.source} onChange={(event) => void onChange({ source: event.target.value })}>
          {sources.map((source) => (
            <option key={source.id} value={source.id}>
              {source.label}
            </option>
          ))}
        </select>
      </div>
      <p className="note">
        Сейчас {currentTemp == null ? "нет датчика" : `${formatNumber(currentTemp, 1)}°C`} → {formatNumber(dutyAt(points, currentTemp ?? 40))}%
      </p>
    </div>
  );
}

function sanitize(points: CurvePoint[]) {
  const sorted = [...points].sort((a, b) => a.temp - b.temp).slice(0, 8);
  return sorted.map((point, index) => ({
    temp: Math.min(120, Math.max(index === 0 ? 0 : sorted[index - 1].temp + 1, Math.round(point.temp))),
    duty: Math.min(100, Math.max(0, Math.round(point.duty))),
  }));
}

function dutyAt(points: CurvePoint[], temp: number) {
  if (points.length === 0) return 0;
  if (temp <= points[0].temp) return points[0].duty;
  if (temp >= points[points.length - 1].temp) return points[points.length - 1].duty;
  for (let i = 1; i < points.length; i++) {
    if (temp <= points[i].temp) {
      const span = points[i].temp - points[i - 1].temp;
      const t = span <= 0 ? 0 : (temp - points[i - 1].temp) / span;
      return points[i - 1].duty + t * (points[i].duty - points[i - 1].duty);
    }
  }
  return points[points.length - 1].duty;
}
