import { fileURLToPath, URL } from "node:url";
import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";
import vueJsx from "@vitejs/plugin-vue-jsx";
import vueDevTools from "vite-plugin-vue-devtools";
import { analyzer } from "vite-bundle-analyzer";
import tailwindcss from "@tailwindcss/vite";
import Components from "unplugin-vue-components/vite";

// https://vite.dev/config/
export default defineConfig({
  plugins: [
    vue(),
    vueJsx(),
    vueDevTools(),
    analyzer({ analyzerMode: "static", openAnalyzer: false }),
    Components({
      resolvers: [
        (componentName) => {
          // Custom resolver for Heroicons: WeightNameIcon -> NameIcon
          const match = componentName.match(/^(Outline|Solid|Solid20|Solid16)(.+Icon)$/);
          if (match) {
            const [, weight, name] = match;
            let from;
            if (weight === "Outline") {
              from = "@heroicons/vue/24/outline";
            } else if (weight === "Solid") {
              from = "@heroicons/vue/24/solid";
            } else if (weight === "Solid20") {
              from = "@heroicons/vue/20/solid";
            } else if (weight === "Solid16") {
              from = "@heroicons/vue/16/solid";
            }
            if (from) {
              return { name: name, from };
            }
          }
        },
      ],
    }),
    tailwindcss(),
  ],
  resolve: {
    alias: {
      "@": fileURLToPath(new URL("./src", import.meta.url)),
    },
  },
  build: {
    rollupOptions: {
      output: {
        manualChunks: {
          vue: ["vue"],
          mapbox: ["mapbox-gl", "@studiometa/vue-mapbox-gl"],
          chart: ["chart.js", "vue-chartjs"],
        },
      },
    },
  },
});
