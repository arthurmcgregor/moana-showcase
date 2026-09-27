# Stage 1: Build
FROM node:24-alpine AS build

WORKDIR /app

COPY package.json package-lock.json ./
RUN npm ci

COPY . .

# Build args for Vite env variables (baked in at build time)
ARG VITE_DEVICE_SERVER_BASE_URL
ARG VITE_WEB_SERVER_BASE_URL
ARG VITE_MAPBOX_ACCESS_TOKEN
ARG VITE_APP_VERSION

RUN npm run build-only

# Stage 2: Serve
FROM nginx:stable-alpine

COPY --from=build /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80

CMD ["nginx", "-g", "daemon off;"]
