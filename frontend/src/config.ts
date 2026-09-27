export const config = {
  deviceServerBaseUrl: import.meta.env.VITE_DEVICE_SERVER_BASE_URL || "http://moana.local",
  webServerBaseUrl: import.meta.env.VITE_WEB_SERVER_BASE_URL || "http://localhost:8000",
} as const;
