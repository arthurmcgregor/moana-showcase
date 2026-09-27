import { globalIgnores } from "eslint/config";
import { defineConfigWithVueTs, vueTsConfigs } from "@vue/eslint-config-typescript";
import pluginVue from "eslint-plugin-vue";
import { importX } from "eslint-plugin-import-x";

// To allow more languages other than `ts` in `.vue` files, uncomment the following lines:
// import { configureVueProject } from '@vue/eslint-config-typescript'
// configureVueProject({ scriptLangs: ['ts', 'tsx'] })
// More info at https://github.com/vuejs/eslint-config-typescript/#advanced-setup

export default defineConfigWithVueTs(
  {
    name: "app/files-to-lint",
    files: ["**/*.{ts,mts,tsx,vue}"],
  },

  globalIgnores(["**/dist/**", "**/dist-ssr/**", "**/coverage/**"]),

  pluginVue.configs["flat/recommended"],
  vueTsConfigs.recommended,
  importX.flatConfigs.recommended,
  importX.flatConfigs.typescript,
  {
    name: "import-x/overrides",
    rules: {
      "import-x/no-unresolved": "off",
    },
  },

  // {
  //   name: "better-tailwindcss",
  //   files: ["**/*.vue"],
  //   extends: [eslintPluginBetterTailwindcss.configs.recommended],
  //   languageOptions: {
  //     parser: eslintParserVue,
  //   },
  //   settings: {
  //     "better-tailwindcss": {
  //       entryPoint: "src/app.css",
  //     },
  //   },
  // },
);
