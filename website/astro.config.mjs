import { defineConfig } from "astro/config";

export default defineConfig({
  site: "https://altvane.co",
  server: { host: true, port: Number(process.env.BERTH_PORT ?? 4321) },
  devToolbar: { enabled: false },
});
