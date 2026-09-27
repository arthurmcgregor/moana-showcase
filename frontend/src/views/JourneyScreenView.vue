<script setup lang="ts">
import { ref, computed, provide, watch } from "vue";
import { useRoute, useRouter } from "vue-router";
import type { TravelMode } from "@/api/types";
import { useAuthStore } from "@/stores";
import {
  JOURNEY_SCOPE,
  buildTimelineSegments,
  groupSegments,
  peakSpeedTo,
  quantiseReadoutMs,
  readoutWindowMs,
  sampleAtMs,
  scopeWindow,
  segmentAtMs,
  statsForWindow,
  useTimelineCursor,
  weatherAtMs,
  weatherForWindow,
  type TimelineCursorState,
} from "@/engine";
import { useJourneyDetail } from "@/composables/useJourneyDetail";
import { useJourneySelection } from "@/composables/useJourneySelection";
import { useLegEditing, type UseLegEditingReturn } from "@/composables/useLegEditing";
import { useJourneyPhotosQuery } from "@/composables/useQueries";
import JourneyDeck from "@/components/journey/JourneyDeck.vue";
import JourneyRailHead from "@/components/journey/JourneyRailHead.vue";
import JourneyTree from "@/components/journey/JourneyTree.vue";
import MapReadout from "@/components/journey/MapReadout.vue";
import { isTraceOnlyJourney } from "@/utils/listHelpers";
import { formatDateRange } from "@/utils/formatters";
import type { JourneyPhoto } from "@/api/web";

// One screen rather than seven tabs: map, deck, and the leg/segment tree are all
// visible at once, so a mode segment has a place on the water. Polar, Photos and
// Details keep their tabs; everything else folds into the Journey tab.

const authStore = useAuthStore();
const route = useRoute();
const router = useRouter();

const validTabs = ["journey", "polar", "photos", "details"] as const;
type Tab = typeof validTabs[number];
const GPS_ONLY_TABS: readonly Tab[] = ["polar"];
const travelMode = ref<TravelMode>("sailing");

const {
  isPending,
  isError,
  error,
  isTracksLoading,
  journey,
  journeyFull,
  journeyGeoJSON,
  allModes,
  allAnnotations,
  timelineData,
  weatherStations,
  selectedStationId,
  weatherSummary,
  isEditMode,
  enterEditMode,
  exitEditMode,
  deleteJourney: doDeleteJourney,
  updateJourney: doUpdateJourney,
} = useJourneyDetail(() => route.params.id as string, {
  onDeleted: () => router.push("/journeys"),
});

const isTraceOnly = computed(() => !!journey.value && isTraceOnlyJourney(journey.value));

const dateLabel = computed(() =>
  journey.value ? formatDateRange(journey.value.started_at, journey.value.ended_at) : "",
);

const activeTab = computed<Tab>({
  get: () => {
    const t = route.query.tab as string;
    if (!validTabs.includes(t as Tab)) return "journey";
    if (isTraceOnly.value && GPS_ONLY_TABS.includes(t as Tab)) return "journey";
    return t as Tab;
  },
  set: (tab: Tab) => {
    router.replace({ query: { ...route.query, tab } });
  },
});

const visibleTabs = computed(() => {
  const all = [
    { key: "journey", label: "Journey" },
    { key: "polar", label: "Polar" },
    { key: "photos", label: "Photos" },
    { key: "details", label: "Details" },
  ] as const;
  return all.filter((t) => !(isTraceOnly.value && GPS_ONLY_TABS.includes(t.key)));
});

// The tree the whole screen reads from — the left column renders it, and the
// map, stats and deck take their time window from whatever is selected in it.
//
// Segments and grouping are two steps rather than one `buildTimelineTree` call
// because the leg-edit override is itself derived from the segments: a leg being
// edited shows its pending membership, so the whole screen previews the change
// before Save. One combined call here would be circular.
const segments = computed(() => buildTimelineSegments(allModes.value, allAnnotations.value));
const legs = computed(() => journey.value?.legs ?? []);

const legEditing = useLegEditing(
  () => segments.value,
  () => legs.value,
  () => journey.value?.id ?? 0,
);
provide<UseLegEditingReturn>("legEditing", legEditing);

// Editing a leg turns the tree into a form — a name, a description and a run of
// segments to tick through — and the pinned stats then describe a selection the
// tree is no longer showing. Yielding the column to the form is worth more than
// a readout of what was scoped before it opened.
// `/journeys/:id` is one route and one component, so navigating between journeys
// reuses this instance. An open edit would otherwise carry a leg id from the
// journey you left into the tree of the one you arrived at.
watch(
  () => journey.value?.id,
  () => legEditing.cancel(),
);

const tree = computed(() =>
  groupSegments(segments.value, legs.value, legEditing.override.value),
);

const { scope, select, selectOnly } = useJourneySelection();

// ── Scoped stats ──

const journeyBounds = computed(() => ({
  started_at: journey.value?.started_at ?? new Date(0).toISOString(),
  ended_at: journey.value?.ended_at ?? null,
}));

const activeWindow = computed(() => scopeWindow(scope.value, tree.value, journeyBounds.value));
const journeyWindow = computed(() =>
  scopeWindow(JOURNEY_SCOPE, tree.value, journeyBounds.value),
);

// The deck needs a finite axis. An unfinished journey has no end, so the last
// fix stands in for one.
const deckEndMs = computed(() => {
  const end = journeyWindow.value.endMs;
  if (Number.isFinite(end)) return end;
  const features = journeyGeoJSON.value.features;
  if (!features.length) return journeyWindow.value.startMs + 1;
  return new Date(features[features.length - 1].properties.timestamp).getTime();
});

const weatherSeries = computed(() => weatherSummary.value?.series ?? []);
const dayGustMax = computed(() => weatherSummary.value?.gust.max ?? null);

const cursor = useTimelineCursor(timelineData);
provide<TimelineCursorState>("timelineCursor", cursor);

const cursorPosition = cursor.cursorPosition;
const cursorMs = computed(() =>
  cursor.cursorTime.value ? new Date(cursor.cursorTime.value).getTime() : null,
);

// Replay lives in the deck; the map needs it to know when to follow the boat,
// and the readout needs the multiplier to know how much journey time one of its
// readings is allowed to cover.
const isReplaying = ref(false);
const replaySpeed = ref(1);

// What the playhead is passing through right now — "live": the row the rail
// lights, the segment whose figures it opens, and the mode named on the map.
const liveSegment = computed(() => {
  const at = cursorMs.value;
  if (at === null) return null;
  return segmentAtMs(tree.value.segments, at);
});

const liveMode = computed(() => {
  const segment = liveSegment.value;
  return segment ? { label: segment.label, color: segment.color } : null;
});

/**
 * While replay runs inside a selection, the highlight follows the boat instead
 * of staying pinned to what was selected — otherwise playback spends most of its
 * time crossing a dimmed speed trace and a dimmed track, which is exactly the
 * part you were watching.
 *
 * The followed segment is held rather than derived from `isReplaying`, because
 * where playback left you is where you want to be looking when it stops. Pausing
 * keeps the highlight on the segment the boat is in; only choosing something
 * else ends the follow. Deriving it from the playing flag instead threw the view
 * back to the old selection the moment you hit pause.
 *
 * It still never writes `scope`. Following is a consequence of time passing;
 * selecting is a deliberate act that fits the camera and writes the URL. Keeping
 * them apart is what lets the camera stay put across both replay and pause.
 *
 * With nothing selected there is nothing dimmed and so nothing to follow.
 */
const followedSegmentId = ref<number | null>(null);

watch([isReplaying, liveSegment], ([playing, segment]) => {
  if (!playing) return;
  // Clearing the selection mid-replay ends the follow rather than freezing it:
  // holding the last followed segment would leave the stats, the label and the
  // deck's window describing a stretch the boat has already left, while the map
  // — which takes its dimming from the selection — showed the whole day.
  followedSegmentId.value = scope.value.kind === "journey" ? null : (segment?.id ?? null);
});

// Choosing something else ends the follow. While playing it does not: the bars
// scope as you click along them, and playback is still the thing in charge.
watch(scope, () => {
  if (!isReplaying.value) followedSegmentId.value = null;
});

const followSegment = computed(() => {
  const id = followedSegmentId.value;
  if (id === null) return null;
  return tree.value.segments.find((s) => s.id === id) ?? null;
});

const isFollowing = computed(() => followSegment.value !== null);

const highlightWindow = computed(() => {
  const segment = followSegment.value;
  if (!segment) return activeWindow.value;
  return { startMs: segment.startMs, endMs: segment.endMs ?? Infinity };
});

// Everything scoped reads the highlight, not the selection, so a followed segment
// is described the way a chosen one would be — stats, weather and the panel's
// label all move together. Following is the selection moving; it just does not
// fit the camera or write the URL.
const scopeStats = computed(() =>
  statsForWindow(journeyGeoJSON.value.features, highlightWindow.value, allAnnotations.value),
);

const scopeWeather = computed(() => weatherForWindow(weatherSeries.value, highlightWindow.value));

// The whole day's figures, which head the rail and stay there whatever is
// selected — the one total on the screen that never moves. The server's are
// preferred where it has them, as everywhere else; the tacks and gybes are
// counted from the annotations, which only the fixes know about.
const journeyStats = computed(() =>
  statsForWindow(journeyGeoJSON.value.features, journeyWindow.value, allAnnotations.value),
);

/**
 * How much journey time one reading of the panel stands for — nothing while the
 * playhead is parked, and the replay multiplier's worth of it while it runs.
 *
 * A scrub is a deliberate act of pointing at one moment and gets the exact fix;
 * a replay is a thing being watched, and what it needs is a figure that holds
 * still long enough to be read. See `readoutWindowMs`.
 */
const readoutWindow = computed(() =>
  isReplaying.value ? readoutWindowMs(replaySpeed.value) : 0,
);

/**
 * The moment every live figure is read at — the tide's height, the speed, the
 * wind, the temperature. Wherever the playhead is standing, whether or not it is
 * moving: parking it mid-leg and having the tide answer for the end of that leg
 * instead was the whole complaint, and the same argument now covers the rest of
 * the panel, which used to answer with averages while the tide answered with a
 * height.
 *
 * The playhead has to be inside what is scoped for that to hold. Selecting a leg
 * the playhead is not in would otherwise mark a moment outside the stretch the
 * panel is describing; the end of the scope is the honest answer there.
 */
const readoutMs = computed(() => {
  const at = cursorMs.value;
  const window = highlightWindow.value;
  const endMs = Number.isFinite(window.endMs) ? window.endMs : deckEndMs.value;
  const raw = at !== null && at >= window.startMs && at <= endMs ? at : endMs;
  if (!readoutWindow.value) return raw;
  // Snapped to its window, so the whole panel — the clock included — changes
  // once per reading rather than once per frame. Clamped back inside the scope,
  // since the window the moment falls in can start before it or end after it.
  const snapped = quantiseReadoutMs(raw, journeyWindow.value.startMs, readoutWindow.value);
  return Math.min(Math.max(snapped, window.startMs), endMs);
});

// What the instruments read at that moment, beside what the scope averaged. Both
// are gap-aware: a playhead in a recording silence reads nothing rather than
// borrowing the last number the instruments happened to send.
const liveStats = computed(() =>
  sampleAtMs(journeyGeoJSON.value.features, readoutMs.value, { windowMs: readoutWindow.value }),
);

const liveWeather = computed(() => weatherAtMs(weatherSeries.value, readoutMs.value));

/**
 * The dial's high-water mark: the fastest the boat had gone by the moment being
 * read, over the stretch that is scoped. Not smoothed — a peak is the peak, and
 * averaging it away would make the mark answer a different question from the one
 * it is drawn to answer.
 */
const peakSpeed = computed(() =>
  peakSpeedTo(journeyGeoJSON.value.features, highlightWindow.value.startMs, readoutMs.value),
);

const liveLegId = computed(() => {
  const segment = liveSegment.value;
  if (!segment) return null;
  const owner = tree.value.entries.find(
    (e) => e.kind === "leg" && e.segments.some((s) => s.id === segment.id),
  );
  return owner && owner.kind === "leg" ? owner.leg.id : null;
});

// The deck's playhead is permanent, so leaving edit mode must not clear it the
// way the old screen did — it keeps the moment you were looking at.
function toggleEdit() {
  if (!isEditMode.value) {
    enterEditMode(cursor);
    return;
  }
  const keep = cursor.cursorTime.value;
  exitEditMode(cursor);
  if (keep) cursor.setCursorTime(keep);
}

// The playhead exists from the moment the track does.
watch(
  () => journeyGeoJSON.value.features.length,
  (count) => {
    if (count > 0 && cursor.cursorTime.value === null) cursor.resetCursor();
  },
  { immediate: true },
);

/**
 * Selecting scopes time as well as space, so the playhead follows the selection
 * into it — Play then runs the thing you just chose rather than resuming from
 * wherever you happened to leave off. A playhead already inside the selection is
 * left alone: it is the moment you were looking at, and moving it would throw
 * away the position that made you select this in the first place.
 *
 * This watches the scope rather than the window, so a leg's pending membership
 * changing under edit does not drag the playhead about on every click.
 *
 * The window is treated as half-open here, unlike `featuresInWindow`. Adjacent
 * segments share the fix on their boundary — deliberately, so distances sum back
 * to the journey's — but that makes an inclusive end read the last moment of a
 * selection as also being the first moment of the next one. Selecting backwards
 * from the start of segment 2 to segment 1 would then leave the playhead pinned
 * to segment 1's very end, which looks like the selection did nothing.
 *
 * The journey's own end is the exception: nothing starts there, so the last
 * instant belongs to the selection that ends on it and a half-open test would
 * throw the playhead back to that selection's start.
 */
watch(scope, (current) => {
  if (current.kind === "journey") return;
  const window = activeWindow.value;
  if (!Number.isFinite(window.startMs)) return;
  const at = cursorMs.value;
  const endsTheDay = window.endMs >= journeyWindow.value.endMs;
  const inside =
    at !== null && at >= window.startMs && (at < window.endMs || (endsTheDay && at <= window.endMs));
  if (inside) return;
  const snapped = cursor.snapMsAtOrAfter(window.startMs);
  cursor.setCursorTime(new Date(snapped ?? window.startMs).toISOString());
});

function onMapClick(lngLat: [number, number]) {
  if (isTraceOnly.value || journeyGeoJSON.value.features.length === 0) return;
  cursor.setCursorFromCoordinate(lngLat);
}

// ── Photos ──

const { state: photosState } = useJourneyPhotosQuery(() => route.params.id as string);
const allPhotos = computed<JourneyPhoto[]>(() =>
  photosState.value.status === "success" ? photosState.value.data.photos : [],
);

const clusterFilter = ref<Set<string> | null>(null);
const lightboxPhotos = computed<JourneyPhoto[]>(() => {
  if (!clusterFilter.value) return allPhotos.value;
  return allPhotos.value.filter((p) => clusterFilter.value!.has(p.id));
});

const activeIndex = computed(() => {
  const asset = route.query.asset;
  if (typeof asset !== "string") return -1;
  return lightboxPhotos.value.findIndex((p) => p.id === asset);
});

function setAsset(id: string | null) {
  const query = { ...route.query };
  if (id) query.asset = id;
  else delete query.asset;
  router.replace({ query });
  if (!id) clusterFilter.value = null;
}

function onPhotoCluster(assetIds: string[]) {
  if (assetIds.length === 0) return;
  clusterFilter.value = new Set(assetIds);
  setAsset(assetIds[0]);
}
</script>

<template>
  <div
    v-if="isPending"
    class="flex flex-1 items-center justify-center"
  >
    <div class="loading loading-xl loading-spinner" />
  </div>

  <div
    v-else-if="isError"
    role="alert"
    class="alert alert-error"
  >
    <OutlineXCircleIcon class="size-6 shrink-0 stroke-current" />
    <span>Error loading journey: {{ error }}</span>
  </div>

  <div
    v-else
    class="flex min-h-0 flex-1 flex-col"
  >
    <!-- Delete modal -->
    <dialog
      id="delete_journey"
      class="modal"
    >
      <div class="modal-box w-[calc(100vw-1rem)] max-h-[calc(100dvh-2rem)] overflow-y-auto">
        <h3 class="text-lg font-bold">
          Delete Journey: {{ journey?.name }}
        </h3>
        <p class="py-4">
          Are you sure you want to delete this journey? Cannot be undone.
        </p>
        <div class="modal-action flex-wrap gap-2">
          <form method="dialog">
            <button class="btn">
              Cancel
            </button>
            <button
              class="btn btn-error"
              @click="doDeleteJourney()"
            >
              Delete
            </button>
          </form>
        </div>
      </div>
    </dialog>

    <!-- One grid, two columns, on every tab. The rail down the left is about
         the journey; the column on the right is about the view of it, and is
         the only half a tab changes. That is why the rail sits outside the tab
         switch: a polar plot or a photo grid still says whose it is, and the
         name and the day's figures don't reload themselves when you come back.

         Below md it unfolds into one page-long scroll, and `contents` is what
         lets the rail's two halves take their own places in it — the journey's
         name and figures first, the map and deck next, the tree last, which is
         the order you'd read them on a phone. On md and up the same `contents`
         collapses the wrappers away so the head and the tree are direct
         children of the rail's scroller, which is what lets the name bar stick
         to it.

         `relative` is load-bearing on the mobile path: the tree's rows carry
         `sr-only` labels, and an absolutely positioned box with no positioned
         ancestor inside the scroller is measured against the page instead. A
         long tree then handed the window a scrollbar as tall as the whole list
         (#163) — the one thing this screen is built not to have. -->
    <div class="relative flex min-h-0 flex-1 flex-col overflow-y-auto md:grid md:grid-cols-[280px_1fr] md:gap-x-4 md:overflow-hidden">
      <aside
        class="max-md:contents md:relative md:flex md:min-h-0 md:flex-col md:overflow-y-auto md:border-r md:border-base-300 md:px-4 md:pb-4"
      >
        <!-- No top padding on the scroller: the name bar supplies its own, so
             `sticky top-0` lands flush on the scroll edge. With padding here
             the bar stuck 16px down and a mode row showed through the gap. -->
        <div class="max-md:order-1 max-md:px-4 max-md:pt-3 md:contents">
          <JourneyRailHead
            :name="journey!.name"
            :date-label="dateLabel"
            :description="journey!.description ?? null"
            :journey-stats="journeyStats"
            :journey-api-stats="journey?.statistics ?? null"
            :route-distance-nm="isTraceOnly ? journey!.estimated_distance_nm : null"
          >
            <template #actions>
              <ActionGroup v-if="authStore.isAdmin">
                <ActionBtn
                  :active="isEditMode"
                  :title="isEditMode ? 'Exit edit mode' : 'Edit journey'"
                  @click="toggleEdit"
                >
                  <OutlinePencilSquareIcon class="size-5" />
                  <span class="sr-only">{{ isEditMode ? 'Editing' : 'Edit' }}</span>
                </ActionBtn>
                <ActionBtn
                  variant="danger"
                  title="Delete journey"
                  onclick="delete_journey.showModal()"
                >
                  <OutlineTrashIcon class="size-5" />
                  <span class="sr-only">Delete</span>
                </ActionBtn>
              </ActionGroup>
            </template>
          </JourneyRailHead>
        </div>

        <!-- The tree is the one part of the rail that belongs to a single tab:
             a run of modes is an index to the map and the deck, and there is
             nothing for it to index on Polar, Photos or Details — nor on a
             drawn journey, whose one mode is the placeholder the server opens
             every journey with. -->
        <div
          v-if="activeTab === 'journey' && !isTraceOnly"
          class="max-md:order-4 max-md:px-4 md:contents"
        >
          <JourneyTree
            :tree="tree"
            :scope="scope"
            :features="journeyGeoJSON.features"
            :journey-id="journey!.id"
            :vessel-id="journey!.vessel"
            :journey-started-at="journey!.started_at"
            :journey-ended-at="journey!.ended_at"
            :live-segment-id="liveSegment?.id ?? null"
            :live-leg-id="liveLegId"
            :follow-segment-id="followSegment?.id ?? null"
            :editable="isEditMode && authStore.isAdmin"
            @select="select"
            @select-only="selectOnly"
          />
        </div>
      </aside>

      <!-- `bg-base-100` because the panes expect a ground of their own: the
           rail's darker one is what makes the segment list read as a list, and
           a polar plot or a photo grid drawn on it would look unfinished. -->
      <div class="flex min-w-0 flex-col bg-base-100 max-md:order-2 md:col-start-2 md:min-h-0">
        <!-- The tabs sit over the pane they switch, not across the whole
             screen: the rail is the same on all four, so a row spanning both
             columns would be charging the rail for a control that never
             changes it. -->
        <nav
          role="tablist"
          class="tab-scroll flex shrink-0 gap-1 border-b border-base-300 px-1"
        >
          <button
            v-for="tab in visibleTabs"
            :key="tab.key"
            role="tab"
            :aria-selected="activeTab === tab.key"
            class="relative shrink-0 whitespace-nowrap px-3 py-2 font-mono text-xs uppercase tracking-wider transition-colors"
            :class="activeTab === tab.key
              ? 'text-base-content'
              : 'text-base-content/40 hover:text-base-content/70'"
            @click="activeTab = tab.key"
          >
            {{ tab.label }}
            <span
              v-if="activeTab === tab.key"
              class="absolute inset-x-0 -bottom-px h-0.5 bg-accent"
            />
          </button>
        </nav>

        <!-- Journey tab: the map, and the deck under it. Everything the weather
             station measured — the tide, the temperature, the wind's dial —
             reads off the map itself, where the water is; the scope's figures
             ride at the right-hand end of the deck's own axis. So this column
             is only ever the map or the time axis. -->
        <div
          v-show="activeTab === 'journey'"
          class="flex min-h-0 flex-1 flex-col"
        >
          <div class="relative h-[55vh] shrink-0 md:h-auto md:min-h-0 md:flex-1">
            <TheMap
              :markers="journeyGeoJSON"
              :travel-mode="travelMode"
              :cursor-position="!isTraceOnly ? cursorPosition : null"
              :annotations="allAnnotations"
              :modes="allModes"
              :photos="allPhotos"
              :scope-window="scope.kind === 'journey' ? null : highlightWindow"
              :playhead-ms="cursorMs"
              :is-playing="isReplaying"
              :following="isFollowing"
              has-playhead
              @map-click="onMapClick"
              @photo-click="setAsset"
              @photo-cluster="onPhotoCluster"
            />
            <div
              v-if="isTracksLoading"
              class="absolute inset-0 flex items-center justify-center bg-base-100/50"
            >
              <div class="loading loading-lg loading-spinner" />
            </div>

            <!-- Top right is the one corner the map's own controls leave
                 alone: zoom, locate, fullscreen and the scale bar all sit left,
                 on the edge the rail is nowhere near. -->
            <MapReadout
              class="absolute right-3 top-3 z-10"
              :series="weatherSeries"
              :marker-ms="readoutMs"
              :weather="scopeWeather"
              :live-weather="liveWeather"
              :day-gust-max="dayGustMax"
              :live-stats="liveStats"
              :mode="liveMode"
              :has-heel="scopeStats.heel !== null"
              :scope-speed-max="scopeStats.speed?.max ?? null"
              :peak-speed="peakSpeed"
            />

            <!-- Where the deck teleports its transport to. Bottom centre is the
                 one edge of the map nothing else claims — zoom, locate and the
                 scale bar sit left, the readout top right — and the controls
                 drive the playhead, which is a thing on the map.
                 `empty:hidden` so a journey with no deck (a bare trace) is left
                 with an empty pill floating over the water. -->
            <div
              id="journey-playhead-controls"
              class="absolute bottom-3 left-1/2 z-10 -translate-x-1/2 rounded-lg border border-base-300/60 bg-base-100/80 px-1.5 py-1 shadow-lg backdrop-blur empty:hidden"
            />
          </div>

          <!-- The deck is permanent, not an edit-mode affordance; edit mode
               layers add-chips and boundary handles onto the same bar. As wide
               as the map, so the day's modes and the speed lane are read at the
               same scale as the water above them. -->
          <div class="shrink-0">
            <JourneyDeck
              v-if="!isTraceOnly && journeyGeoJSON.features.length > 0"
              :tree="tree"
              :features="journeyGeoJSON.features"
              :start-ms="journeyWindow.startMs"
              :end-ms="deckEndMs"
              :window="highlightWindow"
              :nothing-selected="scope.kind === 'journey'"
              :editable="isEditMode && authStore.isAdmin"
              :journey-full="journeyFull ?? null"
              @update:playing="isReplaying = $event"
              @update:speed="replaySpeed = $event"
              @select-segment="selectOnly({ kind: 'segments', ids: [$event] })"
            />
          </div>
        </div>

        <!-- Polar tab -->
        <div
          v-if="activeTab === 'polar'"
          class="min-h-0 flex-1 p-4"
        >
          <WindAngleGraph
            v-if="journeyFull && journeyGeoJSON"
            :journey-full="journeyFull"
            :journey-tracks="journeyGeoJSON"
          />
        </div>

        <!-- Photos tab -->
        <div
          v-else-if="activeTab === 'photos'"
          class="flex min-h-0 flex-1 flex-col"
        >
          <JourneyPhotosTab :journey-id="journey!.id" />
        </div>

        <!-- Details tab -->
        <div
          v-else-if="activeTab === 'details'"
          class="min-h-0 flex-1 overflow-auto p-6"
        >
          <!-- Set once and then true for the rest of the journey's life — the
               station is a property of the day, not a view of it, so it reads
               as a setting here rather than riding the tab row on every screen
               that has nothing to do with weather. -->
          <div class="mb-6 border-b border-base-300 pb-6">
            <AppField label="Weather station">
              <AppSelect v-model="selectedStationId">
                <option :value="null">
                  No weather
                </option>
                <option
                  v-for="station in weatherStations"
                  :key="station.id"
                  :value="station.id"
                >
                  {{ station.name }}
                </option>
              </AppSelect>
            </AppField>
          </div>

          <JourneySummary
            :journey="journey!"
            :annotations="allAnnotations"
            :notes="journey?.notes"
            :is-editing="isEditMode"
            @save="doUpdateJourney($event)"
          />
        </div>
      </div>
    </div>

    <PhotoLightbox
      v-if="activeIndex >= 0"
      :photos="lightboxPhotos"
      :start-index="activeIndex"
      @close="setAsset(null)"
      @navigate="setAsset"
    />
  </div>
</template>
