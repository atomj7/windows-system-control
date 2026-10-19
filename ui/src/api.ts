import type { AppState, History } from "./types";

async function read(response: Response) {
  const data = await response.json();
  if (!response.ok || data.ok === false) throw new Error(data.error || "Запрос не прошёл");
  return data;
}

export function loadState(): Promise<AppState> {
  return fetch("/api/state").then(read);
}

export function loadHistory(seconds: number): Promise<History> {
  return fetch(`/api/history?seconds=${seconds}`).then(read);
}

export function rescan() {
  return fetch("/api/scan", { method: "POST" }).then(read);
}

export function elevate() {
  return fetch("/api/elevate", { method: "POST" }).then(read);
}

export function patchZone(deviceId: string, zoneId: string, body: object) {
  return fetch(`/api/devices/${deviceId}/zones/${zoneId}`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  }).then(read);
}

export function patchFan(deviceId: string, fanId: string, body: object) {
  return fetch(`/api/devices/${deviceId}/fans/${fanId}`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  }).then(read);
}

export function paintDevice(deviceId: string, body: object) {
  return fetch(`/api/devices/${deviceId}/lighting`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  }).then(read);
}

export function paintAll(body: object) {
  return fetch("/api/lighting", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  }).then(read);
}

export function saveProfile(name: string) {
  return fetch("/api/profiles", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ name }),
  }).then(read);
}

export function applyProfile(id: string) {
  return fetch(`/api/profiles/${id}/apply`, { method: "POST" }).then(read);
}

export function removeProfile(id: string) {
  return fetch(`/api/profiles/${id}`, { method: "DELETE" }).then(read);
}
