import { createApp } from "vue";
import { createPinia } from "pinia";
import { PiniaColada } from "@pinia/colada";
import router from "./router";
import App from "./App.vue";
import "./app.css";
import "mapbox-gl/dist/mapbox-gl.css";

const pinia = createPinia();
const app = createApp(App);

app.use(pinia);
app.use(PiniaColada);
app.use(router);
app.mount("#app");
