export type Effect = "static" | "breathe" | "spectrum" | "rainbow" | "wave" | "off";
export type FanMode = "curve" | "manual" | "auto";
export type Hardware = "live" | "profile" | "monitor";
export type SeriesKind = "temperature" | "load" | "power" | "clock" | "rpm";

export interface Zone {
  id: string;
  name: string;
  leds: number;
  color: string;
  effect: string;
  speed: number;
  brightness: number;
  controllable: boolean;
  effects: string[];
}

export interface CurvePoint {
  temp: number;
  duty: number;
}

export interface Fan {
  id: string;
  name: string;
  rpm: number;
  duty: number;
  minDuty: number;
  controllable: boolean;
  rpmKnown: boolean;
  dutyKnown: boolean;
  allowStop: boolean;
  isPump: boolean;
  mode: FanMode;
  source: string;
  curve: CurvePoint[];
}

export interface Sensor {
  id: string;
  label: string;
  unit: string;
  kind: SeriesKind | string;
  value: number;
}

export interface Device {
  id: string;
  name: string;
  vendor: string;
  kind: string;
  bus: string;
  connection: string;
  hardware: Hardware;
  backend: string;
  note: string;
  status: string;
  zones: Zone[];
  fans: Fan[];
  sensors: Sensor[];
}

export interface ScanStep {
  id: string;
  title: string;
  detail: string;
  status: "ok" | "warn" | "info";
}

export interface DriverInfo {
  id: string;
  name: string;
  state: "ready" | "missing" | "skipped" | "unavailable";
  detail: string;
  action?: string;
}

export interface Profile {
  id: string;
  name: string;
  createdAt: number;
  zones: number;
  fans: number;
}

export interface BusDevice {
  name: string;
  vendor: string;
  vid: number;
  pid: number;
  promoted: boolean;
}

export interface AppState {
  generation: number;
  scanning: boolean;
  platform: { mode: "simulated" | "windows"; computer: string; detail: string };
  steps: ScanStep[];
  drivers: DriverInfo[];
  devices: Device[];
  profiles: Profile[];
  bus: BusDevice[];
}

export interface Tick {
  t: number;
  generation: number;
  metrics: number;
  scanning: boolean;
  sensors: { id: string; value: number }[];
  fans: { deviceId: string; id: string; rpm: number; duty: number; rpmKnown: boolean }[];
}

export interface Series {
  id: string;
  label: string;
  unit: string;
  kind: string;
  points: [number, number][];
}

export interface History {
  seconds: number;
  series: Series[];
}
