import { createRouter, createWebHistory } from "vue-router";
import { useAuthStore } from "@/stores";
import DeviceTracksListView from "../views/DeviceTracksListView.vue";
import WebTracksListView from "../views/WebTracksListView.vue";
import TrackMapView from "../views/TrackMapView.vue";
import JourneysView from "../views/JourneysView.vue";
import JourneyScreenView from "../views/JourneyScreenView.vue";
import LocalTracksUploadView from "../views/LocalTracksUploadView.vue";
import WindRoseView from "../views/WindRoseView.vue";
import WeatherUploadView from "../views/WeatherUploadView.vue";
import TraceView from "../views/TraceView.vue";
import PeopleListView from "../views/PeopleListView.vue";
import PersonDetailView from "../views/PersonDetailView.vue";
import GearChecklistView from "../views/GearChecklistView.vue";
import GearStowageView from "../views/GearStowageView.vue";

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path: "/",
      redirect: "/journeys",
    },
    {
      path: "/journeys",
      name: "journeys",
      component: JourneysView,
    },
    {
      path: "/journeys/:id",
      name: "journey-detail",
      component: JourneyScreenView,
    },
    {
      path: "/tracks/device",
      name: "device-tracks",
      component: DeviceTracksListView,
      meta: { adminOnly: true },
    },
    {
      path: "/tracks/web",
      name: "web-tracks",
      component: WebTracksListView,
    },
    {
      path: "/tracks/local",
      name: "local-tracks",
      component: LocalTracksUploadView,
      meta: { adminOnly: true },
    },
    {
      path: "/tracks/trace",
      name: "trace-path",
      component: TraceView,
      meta: { adminOnly: true },
    },
    {
      path: "/tracks/:id",
      name: "track-view",
      component: TrackMapView,
    },
    {
      path: "/people",
      name: "people",
      component: PeopleListView,
    },
    {
      path: "/people/:id",
      name: "person-detail",
      component: PersonDetailView,
    },
    {
      path: "/wind-rose",
      name: "wind-rose",
      component: WindRoseView,
    },
    {
      path: "/weather/upload",
      name: "weather-upload",
      component: WeatherUploadView,
      meta: { adminOnly: true },
    },
    {
      path: "/gear",
      name: "gear-checklist",
      component: GearChecklistView,
    },
    {
      path: "/gear/stowage",
      name: "gear-stowage",
      component: GearStowageView,
    },
  ],
});

router.beforeEach(async (to) => {
  if (!to.meta.adminOnly) return true;
  const authStore = useAuthStore();
  if (!authStore.isLoaded) {
    await authStore.loadUser();
  }
  if (!authStore.isAdmin) {
    return { name: "journeys" };
  }
  return true;
});

export default router;
