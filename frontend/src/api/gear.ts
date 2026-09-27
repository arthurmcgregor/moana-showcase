import { config } from "../config";
import type { PaginatedResponse } from "./web";

function getCsrfToken(): string | undefined {
  return document.cookie
    .split("; ")
    .find((row) => row.startsWith("csrftoken="))
    ?.split("=")[1];
}

// If the csrftoken cookie is missing (fresh session, cleared cookies,
// SECRET_KEY rotation invalidating the old one), unsafe requests will 403.
// Hit /api/me/ — decorated with @ensure_csrf_cookie on the backend — to set
// a fresh cookie before retrying.
async function ensureCsrfCookie(): Promise<void> {
  if (getCsrfToken()) return;
  try {
    await fetch(`${config.webServerBaseUrl}/api/me/`, { credentials: "include" });
  } catch {
    // Fall through; the unsafe request will surface the error itself.
  }
}

async function apiFetch(url: string | URL, init?: RequestInit): Promise<Response> {
  const method = init?.method?.toUpperCase();
  const isUnsafe = !!method && ["POST", "PATCH", "PUT", "DELETE"].includes(method);
  if (isUnsafe) await ensureCsrfCookie();
  const headers = new Headers(init?.headers);
  if (isUnsafe) {
    const csrf = getCsrfToken();
    if (csrf) headers.set("X-CSRFToken", csrf);
  }
  return fetch(url, { credentials: "include", ...init, headers });
}

export interface Tag {
  id: number;
  name: string;
  parent: number | null;
}

export interface Item {
  id: number;
  name: string;
  done: boolean;
  tags: number[];
  created_at: string;
  updated_at: string;
}

export interface ChecklistState {
  selected_tags: number[];
  updated_at: string;
}

const BASE = () => `${config.webServerBaseUrl}/api/gear`;

async function fetchAllPaginated<T>(url: string): Promise<T[]> {
  const results: T[] = [];
  let next: string | null = url;
  while (next) {
    const response = await apiFetch(next);
    if (!response.ok) throw new Error(`Failed to fetch ${url}: ${response.statusText}`);
    const page = (await response.json()) as PaginatedResponse<T>;
    results.push(...page.results);
    next = page.next;
  }
  return results;
}

export async function fetchTags(): Promise<Tag[]> {
  return fetchAllPaginated<Tag>(`${BASE()}/tags/`);
}

export async function createTag(data: { name: string; parent: number | null }): Promise<Tag> {
  const response = await apiFetch(`${BASE()}/tags/`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data),
  });
  if (!response.ok) throw new Error(`Failed to create tag: ${response.statusText}`);
  return await response.json();
}

export async function deleteTag(id: number): Promise<void> {
  const response = await apiFetch(`${BASE()}/tags/${id}/`, { method: "DELETE" });
  if (!response.ok) throw new Error(`Failed to delete tag ${id}: ${response.statusText}`);
}

export async function fetchItems(): Promise<Item[]> {
  return fetchAllPaginated<Item>(`${BASE()}/items/`);
}

export async function createItem(data: { name: string; tags: number[] }): Promise<Item> {
  const response = await apiFetch(`${BASE()}/items/`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data),
  });
  if (!response.ok) throw new Error(`Failed to create item: ${response.statusText}`);
  return await response.json();
}

export async function updateItem(id: number, data: Partial<Item>): Promise<Item> {
  const response = await apiFetch(`${BASE()}/items/${id}/`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data),
  });
  if (!response.ok) throw new Error(`Failed to update item ${id}: ${response.statusText}`);
  return await response.json();
}

export async function deleteItem(id: number): Promise<void> {
  const response = await apiFetch(`${BASE()}/items/${id}/`, { method: "DELETE" });
  if (!response.ok) throw new Error(`Failed to delete item ${id}: ${response.statusText}`);
}

export async function resetAllDone(): Promise<{ updated: number }> {
  const response = await apiFetch(`${BASE()}/items/reset-done/`, { method: "POST" });
  if (!response.ok) throw new Error(`Failed to reset done: ${response.statusText}`);
  return await response.json();
}

export async function fetchChecklistState(): Promise<ChecklistState> {
  const response = await apiFetch(`${BASE()}/checklist-state/`);
  if (!response.ok) throw new Error(`Failed to fetch checklist state: ${response.statusText}`);
  return await response.json();
}

export async function updateChecklistState(
  data: { selected_tags: number[] },
): Promise<ChecklistState> {
  const response = await apiFetch(`${BASE()}/checklist-state/`, {
    method: "PATCH",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(data),
  });
  if (!response.ok) throw new Error(`Failed to update checklist state: ${response.statusText}`);
  return await response.json();
}
