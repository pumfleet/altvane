import { defineConfig } from "astro/config";

export default defineConfig({
  server: { host: true, port: Number(process.env.BERTH_PORT ?? 4321) },
  devToolbar: { enabled: false },
});
