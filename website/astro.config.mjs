import { defineConfig } from "astro/config";
import sitemap from "@astrojs/sitemap";

export default defineConfig({
  site: "https://altvane.co",
  integrations: [sitemap()],
  server: { host: true, port: Number(process.env.BERTH_PORT ?? 4321) },
  devToolbar: { enabled: false },
});
