import { useEffect, useRef, useState } from "react";
import * as api from "./api";
import { Telemetry } from "./chart";
import { driverLabel, formatNumber, kindLabel } from "./format";
import { Stage } from "./studio";
import type { AppState, Device, History, Tick } from "./types";

const order = ["cooler", "fan_controller", "motherboard", "gpu", "memory", "cpu", "storage", "peripheral", "unknown"];

export function App() {
  const [state, setState] = useState<AppState | null>(null);
  const [tick, setTick] = useState<Tick | null>(null);
  const [history, setHistory] = useState<History | null>(null);
  const [selected, setSelected] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [notice, setNotice] = useState<string | null>(null);
  const [entered, setEntered] = useState(() => sessionStorage.getItem("helios-seen") === "1");
  const [driversOpen, setDriversOpen] = useState(false);
  const [profileOpen, setProfileOpen] = useState(false);
  const [busOpen, setBusOpen] = useState(false);
  const [seconds, setSeconds] = useState(180);
  const generation = useRef(0);
  const secondsRef = useRef(seconds);
  const historyReady = useRef(false);
  secondsRef.current = seconds;

  useEffect(() => {
    api.loadState().then((next) => {
      generation.current = next.generation;
      setState(next);
      setSelected(next.devices[0]?.id ?? null);
    }).catch((reason: Error) => setError(reason.message));
  }, []);

  useEffect(() => {
    if (!state || state.scanning) return;
    historyReady.current = false;
    api.loadHistory(seconds).then((loaded) => {
      historyReady.current = loaded.series.length > 0;
      setHistory(loaded);
    }).catch(() => undefined);
  }, [seconds, state?.scanning]);

  useEffect(() => {
    const source = new EventSource("/api/stream");
    source.addEventListener("tick", (event) => {
      const next = JSON.parse((event as MessageEvent).data) as Tick;
      setTick(next);
      if (!historyReady.current) {
        historyReady.current = true;
        api.loadHistory(secondsRef.current).then((loaded) => {
          historyReady.current = loaded.series.length > 0;
          setHistory((current) => (loaded.series.length > 0 ? appendHistory(loaded, next) : current));
        }).catch(() => {
          historyReady.current = false;
        });
      } else {
        setHistory((current) => appendHistory(current, next));
      }
      if (next.generation !== generation.current) {
        generation.current = next.generation;
        api.loadState().then((fresh) => {
          setState(fresh);
          setSelected((currentId) => (currentId && fresh.devices.some((device) => device.id === currentId) ? currentId : fresh.devices[0]?.id ?? null));
        }).catch((reason: Error) => setNotice(reason.message));
      }
    });
    source.onerror = () => setNotice("Поток телеметрии прервался. Обнови страницу, если цифры застыли.");
    return () => source.close();
  }, []);

  async function run(action: () => Promise<unknown>) {
    try {
      await action();
      const fresh = await api.loadState();
      generation.current = fresh.generation;
      setState(fresh);
    } catch (reason) {
      setNotice(reason instanceof Error ? reason.message : "Не получилось");
    }
  }

  if (error && !state) {
    return (
      <div className="fatal">
        <div>
          <h1>Нет связи со службой</h1>
          <p className="note">{error}</p>
          <button className="btn primary" onClick={() => window.location.reload()}>Повторить</button>
        </div>
      </div>
    );
  }
  if (!state) return <div className="boot"><p>Смотрю устройства…</p></div>;

  const device = state.devices.find((item) => item.id === selected) ?? state.devices[0];
  const grouped = order
    .map((kind) => ({ kind, devices: state.devices.filter((item) => item.kind === kind) }))
    .filter((group) => group.devices.length > 0);

  return (
    <div className="app">
      {!entered && <ScanScreen state={state} onEnter={() => { sessionStorage.setItem("helios-seen", "1"); setEntered(true); }} />}
      <header className="top">
        <div className="brand">
          <Mark />
          <div>
            <b>HELIOS</b>
            <span>свет и воздух</span>
          </div>
        </div>
        <div className="computer">
          <strong>{state.platform.computer}</strong>
          <span>{state.scanning ? "Идёт повторный обзор…" : state.platform.detail}</span>
        </div>
        <div className="spacer" />
        <div className="cluster">
          {state.platform.mode === "simulated" && <span className="pill sim">Стенд</span>}
          <select className="select" value={seconds} onChange={(event) => setSeconds(Number(event.target.value))}>
            <option value={60}>1 мин</option>
            <option value={180}>3 мин</option>
            <option value={600}>10 мин</option>
          </select>
          <select
            className="select"
            defaultValue=""
            onChange={(event) => {
              const id = event.target.value;
              if (id) void run(() => api.applyProfile(id));
              event.target.value = "";
            }}
          >
            <option value="">Профиль</option>
            {state.profiles.map((profile) => (
              <option key={profile.id} value={profile.id}>{profile.name}</option>
            ))}
          </select>
          <button className="btn" onClick={() => setProfileOpen(true)}>Сохранить</button>
          <button className="btn" onClick={() => setDriversOpen(true)}>Драйверы</button>
          <button className="btn primary" onClick={() => void run(() => api.rescan())}>Обзор</button>
        </div>
      </header>
      <div className="shell">
        <aside className="rail">
          {grouped.map((group) => (
            <div key={group.kind}>
              <div className="group-label">{kindLabel[group.kind] ?? group.kind}</div>
              {group.devices.map((item) => (
                <DeviceButton key={item.id} device={item} active={item.id === device?.id} tick={tick} onClick={() => setSelected(item.id)} />
              ))}
            </div>
          ))}
          <button className="btn ghost" style={{ marginTop: 12 }} onClick={() => setBusOpen((value) => !value)}>
            Шина USB · {state.bus.length}
          </button>
          {busOpen && state.bus.map((item) => (
            <div className="bus" key={`${item.vid}-${item.pid}-${item.name}`}>
              <span>{item.name}</span>
              <span>{item.vid.toString(16).padStart(4, "0").toUpperCase()}:{item.pid.toString(16).padStart(4, "0").toUpperCase()}</span>
            </div>
          ))}
        </aside>
        {device ? (
          <Stage
            device={device}
            state={state}
            tick={tick}
            actions={{
              onZone: (zoneId, body) => run(() => api.patchZone(device.id, zoneId, body)),
              onFan: (fanId, body) => run(() => api.patchFan(device.id, fanId, body)),
              onPaint: (body) => run(() => api.paintDevice(device.id, body)),
            }}
          />
        ) : (
          <main className="stage"><h1>Пусто</h1><p className="note">Обзор не нашёл компонентов. Нажми «Обзор» ещё раз.</p></main>
        )}
        <Telemetry history={history} />
      </div>
      {driversOpen && (
        <div className="sheet" onClick={() => setDriversOpen(false)}>
          <div className="sheet-card" onClick={(event) => event.stopPropagation()}>
            <div className="spread"><h2>Драйверы</h2><button className="btn ghost" onClick={() => setDriversOpen(false)}>Закрыть</button></div>
            {state.drivers.map((driver) => (
              <article className="driver" key={driver.id}>
                <div className="spread"><b>{driver.name}</b><span className={`pill ${driver.state === "missing" ? "warn" : driver.state === "ready" ? "live" : ""}`}>{driverLabel[driver.state]}</span></div>
                <p className="note">{driver.detail}</p>
                {driver.action === "elevate" && (
                  <button
                    className="btn primary"
                    onClick={() => {
                      setNotice("Windows спросит права администратора. После согласия Helios откроется заново.");
                      void run(() => api.elevate());
                    }}
                  >
                    Запустить от администратора
                  </button>
                )}
              </article>
            ))}
          </div>
        </div>
      )}
      {profileOpen && (
        <ProfileDialog
          onClose={() => setProfileOpen(false)}
          onSave={(name) => run(() => api.saveProfile(name)).then(() => setProfileOpen(false))}
          profiles={state.profiles}
          onDelete={(id) => run(() => api.removeProfile(id))}
        />
      )}
      {notice && <button className="toast" onClick={() => setNotice(null)}>{notice}</button>}
    </div>
  );
}

function DeviceButton({ device, active, tick, onClick }: { device: Device; active: boolean; tick: Tick | null; onClick: () => void }) {
  const color = device.zones[0]?.color ?? "#3a4148";
  const temp = device.sensors.find((sensor) => sensor.kind === "temperature");
  const liveTemp = temp ? tick?.sensors.find((sensor) => sensor.id === temp.id)?.value ?? temp.value : null;
  const fan = device.fans[0];
  const liveFan = fan ? tick?.fans.find((item) => item.deviceId === device.id && item.id === fan.id) : undefined;
  const side = liveTemp != null ? `${formatNumber(liveTemp, 0)}°` : liveFan?.rpmKnown ? `${formatNumber(liveFan.rpm, 0)}` : device.hardware === "live" ? "●" : "";
  return (
    <button className={active ? "device active" : "device"} onClick={onClick}>
      <span className="kind-icon" aria-hidden><KindIcon kind={device.kind} /></span>
      <i className="swatch" style={{ background: color }} />
      <span><b>{device.name}</b><small>{kindLabel[device.kind] ?? device.kind}</small></span>
      <em>{side}</em>
    </button>
  );
}

function KindIcon({ kind }: { kind: string }) {
  const stroke = { fill: "none", stroke: "currentColor", strokeWidth: 1.6, strokeLinecap: "round" as const, strokeLinejoin: "round" as const };
  switch (kind) {
    case "cpu":
      return (
        <svg viewBox="0 0 24 24">
          <rect x="7" y="7" width="10" height="10" rx="1.5" {...stroke} />
          <path d="M9 4v3M12 4v3M15 4v3M9 17v3M12 17v3M15 17v3M4 9h3M4 12h3M4 15h3M17 9h3M17 12h3M17 15h3" {...stroke} />
        </svg>
      );
    case "gpu":
      return (
        <svg viewBox="0 0 24 24">
          <rect x="3" y="7" width="18" height="11" rx="2" {...stroke} />
          <circle cx="8.5" cy="12.5" r="2" {...stroke} />
          <path d="M13 10h5M13 13h3M6 7V5M10 7V5" {...stroke} />
        </svg>
      );
    case "motherboard":
      return (
        <svg viewBox="0 0 24 24">
          <rect x="4" y="4" width="16" height="16" rx="2" {...stroke} />
          <rect x="7" y="7" width="6" height="6" rx="1" {...stroke} />
          <path d="M16 8h1M16 11h1M8 16h8M8 19v-2" {...stroke} />
        </svg>
      );
    case "cooler":
      return (
        <svg viewBox="0 0 24 24">
          <circle cx="12" cy="12" r="3" {...stroke} />
          <path d="M12 5v4M12 15v4M5 12h4M15 12h4M7.2 7.2l2.8 2.8M14 14l2.8 2.8M16.8 7.2 14 10M10 14l-2.8 2.8" {...stroke} />
        </svg>
      );
    case "fan_controller":
      return (
        <svg viewBox="0 0 24 24">
          <circle cx="12" cy="12" r="8" {...stroke} />
          <path d="M12 12 16 8.5M12 12l-1.5 6M12 12 6.5 10" {...stroke} />
        </svg>
      );
    case "memory":
      return (
        <svg viewBox="0 0 24 24">
          <rect x="3" y="8" width="18" height="8" rx="1.5" {...stroke} />
          <path d="M6 8V6M9 8V6M12 8V6M15 8V6M18 8V6M6 16v2M18 16v2" {...stroke} />
        </svg>
      );
    case "storage":
      return (
        <svg viewBox="0 0 24 24">
          <rect x="4" y="6" width="16" height="12" rx="2" {...stroke} />
          <path d="M8 10h.01M8 14h8" {...stroke} />
        </svg>
      );
    default:
      return (
        <svg viewBox="0 0 24 24">
          <rect x="5" y="7" width="14" height="10" rx="2" {...stroke} />
          <path d="M9 17v2M15 17v2" {...stroke} />
        </svg>
      );
  }
}

function ScanScreen({ state, onEnter }: { state: AppState; onEnter: () => void }) {
  const done = state.steps.filter((step) => step.status === "ok").length;
  const width = state.steps.length === 0 ? 8 : Math.round((state.steps.length / 6) * 100);
  return (
    <div className="scan">
      <div className="scan-copy">
        <div className="kicker">Первый обзор</div>
        <h1>Helios</h1>
        <p>{state.platform.detail}</p>
        <div className="progress"><i style={{ width: `${Math.min(100, width)}%` }} /></div>
        <button className="btn primary" disabled={state.scanning && state.steps.length < 3} onClick={onEnter}>
          {state.scanning ? "Ещё смотрю…" : "Открыть устройства"}
        </button>
        <p className="faint">{done} шагов без замечаний</p>
      </div>
      <div className="scan-steps">
        {state.steps.map((step) => (
          <div className="step" key={step.id}>
            <i className={`dot ${step.status}`} />
            <div><b>{step.title}</b><span>{step.detail}</span></div>
          </div>
        ))}
        {state.scanning && state.steps.length === 0 && <p>Читаю таблицы и шины…</p>}
      </div>
    </div>
  );
}

function ProfileDialog({
  profiles,
  onClose,
  onSave,
  onDelete,
}: {
  profiles: AppState["profiles"];
  onClose: () => void;
  onSave: (name: string) => void;
  onDelete: (id: string) => void;
}) {
  const [name, setName] = useState("");
  return (
    <div className="dialog-back" onClick={onClose}>
      <form
        className="dialog"
        onClick={(event) => event.stopPropagation()}
        onSubmit={(event) => {
          event.preventDefault();
          if (name.trim()) onSave(name.trim());
        }}
      >
        <h2>Сохранить профиль</h2>
        <p className="note">Запоминаются цвета зон и кривые крыльчаток, которые сейчас на экране.</p>
        <input className="field" autoFocus value={name} placeholder="Вечер" onChange={(event) => setName(event.target.value)} />
        <div className="cluster" style={{ marginTop: 14 }}>
          <button className="btn ghost" type="button" onClick={onClose}>Отмена</button>
          <button className="btn primary" type="submit">Сохранить</button>
        </div>
        {profiles.length > 0 && <div className="group-label">Уже есть</div>}
        {profiles.map((profile) => (
          <div className="spread" key={profile.id} style={{ padding: "8px 0" }}>
            <span>{profile.name}</span>
            <button className="btn ghost" type="button" onClick={() => onDelete(profile.id)}>Удалить</button>
          </div>
        ))}
      </form>
    </div>
  );
}

function Mark() {
  return (
    <svg className="mark" viewBox="0 0 32 32" aria-hidden>
      <circle cx="16" cy="16" r="10" fill="none" stroke="#e3914a" strokeWidth="1.6" />
      <circle cx="16" cy="16" r="3" fill="#7eb8b2" />
      <path d="M16 4v4M16 24v4M4 16h4M24 16h4" stroke="#ebe6dc" strokeWidth="1.4" />
    </svg>
  );
}

function appendHistory(current: History | null, tick: Tick): History | null {
  if (!current || tick.t <= 0) return current;
  const series = current.series.map((item) => ({ ...item, points: item.points.slice() }));
  const push = (id: string, value: number) => {
    const found = series.find((item) => item.id === id);
    if (!found) return;
    const last = found.points[found.points.length - 1];
    if (last && last[0] >= tick.t) return;
    found.points.push([tick.t, value]);
    if (found.points.length > 360) found.points.splice(0, found.points.length - 360);
  };
  for (const sensor of tick.sensors) push(sensor.id, sensor.value);
  for (const fan of tick.fans) {
    if (fan.rpmKnown) push(`fan:${fan.deviceId}/${fan.id}`, fan.rpm);
  }
  return { ...current, series };
}
