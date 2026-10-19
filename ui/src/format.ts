import type { CurvePoint, Effect, FanMode } from "./types";

export const kindLabel: Record<string, string> = {
  motherboard: "Плата",
  cpu: "Процессор",
  gpu: "Видеокарта",
  cooler: "Охлаждение",
  memory: "Память",
  fan_controller: "Вентиляторы",
  storage: "Накопитель",
  peripheral: "Периферия",
  unknown: "Устройство",
};

export const hardwareLabel: Record<string, string> = {
  live: "Управление",
  profile: "В профиле",
  monitor: "Наблюдение",
};

export const effectLabel: Record<string, string> = {
  static: "Статичный",
  breathe: "Дыхание",
  spectrum: "Спектр",
  rainbow: "Радуга",
  wave: "Волна",
  off: "Выкл",
};

export const modeLabel: Record<FanMode, string> = {
  curve: "Кривая",
  manual: "Ручной",
  auto: "Авто",
};

export const driverLabel: Record<string, string> = {
  ready: "Готов",
  missing: "Нет",
  skipped: "Не нужен",
  unavailable: "Недоступен",
};

export function formatNumber(value: number, digits = 0) {
  return new Intl.NumberFormat("ru-RU", { maximumFractionDigits: digits, minimumFractionDigits: digits }).format(value);
}

export function rgbOf(hex: string) {
  const clean = hex.replace("#", "");
  return {
    r: parseInt(clean.slice(0, 2), 16) || 0,
    g: parseInt(clean.slice(2, 4), 16) || 0,
    b: parseInt(clean.slice(4, 6), 16) || 0,
  };
}

export function hexOf(r: number, g: number, b: number) {
  const byte = (value: number) => Math.max(0, Math.min(255, Math.round(value))).toString(16).padStart(2, "0");
  return `#${byte(r)}${byte(g)}${byte(b)}`;
}

export function rgbToHsv(r: number, g: number, b: number) {
  const red = r / 255;
  const green = g / 255;
  const blue = b / 255;
  const max = Math.max(red, green, blue);
  const min = Math.min(red, green, blue);
  const delta = max - min;
  let hue = 0;
  if (delta !== 0) {
    if (max === red) hue = ((green - blue) / delta) % 6;
    else if (max === green) hue = (blue - red) / delta + 2;
    else hue = (red - green) / delta + 4;
    hue *= 60;
    if (hue < 0) hue += 360;
  }
  return { h: hue, s: max === 0 ? 0 : delta / max, v: max };
}

export function hsvToRgb(h: number, s: number, v: number) {
  const c = v * s;
  const x = c * (1 - Math.abs(((h / 60) % 2) - 1));
  const m = v - c;
  let r = 0;
  let g = 0;
  let b = 0;
  if (h < 60) [r, g, b] = [c, x, 0];
  else if (h < 120) [r, g, b] = [x, c, 0];
  else if (h < 180) [r, g, b] = [0, c, x];
  else if (h < 240) [r, g, b] = [0, x, c];
  else if (h < 300) [r, g, b] = [x, 0, c];
  else [r, g, b] = [c, 0, x];
  return { r: Math.round((r + m) * 255), g: Math.round((g + m) * 255), b: Math.round((b + m) * 255) };
}

export const swatches = ["#f4f1ea", "#e3914a", "#d4655d", "#7eb8b2", "#d6c07a", "#1a1e24"];

export const curvePresets: { id: string; label: string; points: CurvePoint[] }[] = [
  { id: "quiet", label: "Тихо", points: [{ temp: 35, duty: 22 }, { temp: 55, duty: 30 }, { temp: 68, duty: 42 }, { temp: 80, duty: 68 }, { temp: 90, duty: 100 }] },
  { id: "balanced", label: "Ровно", points: [{ temp: 30, duty: 28 }, { temp: 48, duty: 38 }, { temp: 62, duty: 55 }, { temp: 74, duty: 74 }, { temp: 86, duty: 100 }] },
  { id: "boost", label: "Запас", points: [{ temp: 28, duty: 40 }, { temp: 45, duty: 58 }, { temp: 60, duty: 78 }, { temp: 72, duty: 100 }] },
];

export function isEffect(value: string): value is Effect {
  return value in effectLabel;
}
