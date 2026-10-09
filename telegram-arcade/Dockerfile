# syntax=docker/dockerfile:1.7
#
# Telegram Arcade: one image with the server, the Mini App bundle and the
# WebAssembly emulator cores (built from pinned upstream sources).
#
#   docker build -t telegram-arcade .
#
# No ROMs, BIOS files or other proprietary content are fetched or included.
# Behind a TLS-inspecting proxy, pass its CA bundle as a build secret:
#   docker build --secret id=ca,src=/path/to/ca.pem ...

ARG NODE_IMAGE=node:22.22.0-bookworm-slim
ARG EMSDK_IMAGE=emscripten/emsdk:6.0.12

# ----------------------------------------------------------------- cores
FROM ${EMSDK_IMAGE} AS cores
WORKDIR /build
COPY native/build-cores.sh native/
COPY native/frontend native/frontend
COPY native/patches native/patches
RUN --mount=type=secret,id=ca \
    if [ -s /run/secrets/ca ]; then export GIT_SSL_CAINFO=/run/secrets/ca; fi; \
    SRC_DIR=/tmp/src OUT_DIR=/out native/build-cores.sh \
 && rm -rf /out/obj /tmp/src

# ----------------------------------------------------------------- build
FROM ${NODE_IMAGE} AS build
WORKDIR /app
COPY package.json package-lock.json ./
RUN --mount=type=secret,id=ca \
    if [ -s /run/secrets/ca ]; then export NODE_EXTRA_CA_CERTS=/run/secrets/ca; fi; \
    npm ci --no-audit --no-fund
COPY tsconfig.json ./
COPY shared shared
COPY server server
COPY client client
COPY scripts scripts
COPY test test
COPY --from=cores /out native/build
RUN npm run typecheck \
 && node scripts/build-client.ts \
 && node scripts/build-catalog.ts native/build \
 && npm prune --omit=dev

# ----------------------------------------------------------------- runtime
FROM ${NODE_IMAGE} AS runtime
ENV NODE_ENV=production \
    DATA_DIR=/data \
    PORT=8080 \
    HOST=0.0.0.0
WORKDIR /app
COPY --from=build /app/package.json ./
COPY --from=build /app/node_modules node_modules
COPY --from=build /app/shared shared
COPY --from=build /app/server server
COPY --from=build /app/client/dist client/dist
COPY --from=build /app/native/build native/build
COPY scripts/backup.ts scripts/restore.ts scripts/
RUN mkdir -p /data && chown node:node /data
USER node
VOLUME ["/data"]
EXPOSE 8080
HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
  CMD ["node", "-e", "fetch('http://127.0.0.1:'+(process.env.PORT||8080)+'/healthz').then(r=>process.exit(r.ok?0:1),()=>process.exit(1))"]
# SIGTERM checkpoints every running room before exit (see server/main.ts)
STOPSIGNAL SIGTERM
CMD ["node", "--disable-warning=ExperimentalWarning", "server/main.ts"]
