<script setup lang="ts">
import { computed, inject, onBeforeUnmount, onMounted, ref, watch } from "vue";
import type { AnnotationType, GeoJSONFeature, JourneyFull } from "@/api/web";
import {
  createReplayClock,
  nextReplaySpeed,
  type TimelineCursorState,
  type TimelineTree,
  type TimeWindow,
} from "@/engine";
import { useJourneyMutations } from "@/composables/useJourneyMutations";
import { modeTypeList, annotationTypeList } from "@/constants/journeyTypes";
import {
  FULL_VIEW,
  followPlayhead,
  isFullView,
  panBy,
  zoomAbout,
  zoomFactor,
  type TimeViewport,
} from "@/utils/timeViewport";
import ModeBar from "./ModeBar.vue";
import SpeedLane from "./SpeedLane.vue";

// Controls above, mode bar and speed always visible below, one playhead crossing
// both. The deck is permanent — it is not an edit-mode affordance.

const props = defineProps<{
  tree: TimelineTree;
  features: GeoJSONFeature[];
  startMs: number;
  endMs: number;
  window: TimeWindow;
  nothingSelected: boolean;
  /** Edit mode layers add-chips and boundary handles onto the same bar. */
  editable?: boolean;
  journeyFull?: JourneyFull | null;
}>();

// Replay state is lifted so the map can follow the boat while it runs.
const emit = defineEmits<{
  "update:playing": [boolean];
  /**
   * How fast it is running. The map's readout sizes its averaging window off
   * this: at 900x a per-fix reading is a blur, and only the transport knows the
   * multiplier.
   */
  "update:speed": [number];
  /** Clicking the bars scopes the screen. It only ever sets — never clears. */
  selectSegment: [id: number];
}>();

const cursor = inject<TimelineCursorState>("timelineCursor")!;

const cursorMs = computed(() =>
  cursor.cursorTime.value ? new Date(cursor.cursorTime.value).getTime() : null,
);

const range = computed(() => Math.max(props.endMs - props.startMs, 1));

// ── Zoom ──
//
// A five-hour journey drawn across a few hundred pixels puts every mode change
// within a pixel or two of its neighbours, and the boundaries are the part worth
// looking at. Scrolling over the bars widens them about the pointer.
//
// The wheel rather than a drag, and no overview strip: the bars already answer a
// click by seeking and scoping, so a drag would have to guess which was meant,
// and the deck sits in a column that has no vertical space to give away.
//
// Only the drawing is zoomed. Replay bounds, the seek hit-test and the scope all
// go on using the journey's own start and end — zooming changes what you can
// see, never what is there.
const view = ref<TimeViewport>({ ...FULL_VIEW });

const viewStartMs = computed(() => props.startMs + view.value.from * range.value);
const viewEndMs = computed(() => props.startMs + view.value.to * range.value);
const viewRange = computed(() => Math.max(viewEndMs.value - viewStartMs.value, 1));
const zoomLabel = computed(() => `${zoomFactor(view.value).toFixed(1)}×`);
const isZoomed = computed(() => !isFullView(view.value));

// A different journey is a different thing to be looking at.
watch(() => [props.startMs, props.endMs], () => {
  view.value = { ...FULL_VIEW };
});

/**
 * Vertical intent zooms, horizontal pans; shift makes a one-axis mouse do both.
 *
 * The event is only consumed when it actually moves the viewport, so scrolling
 * down over an unzoomed deck still scrolls the page — on a narrow screen the
 * deck is in the middle of a scrolling column, and swallowing that would trap
 * the page.
 */
function onWheel(event: WheelEvent) {
  const rect = (event.currentTarget as HTMLElement).getBoundingClientRect();
  if (rect.width === 0) return;
  const pointer = (event.clientX - rect.left) / rect.width;

  const horizontal = event.shiftKey || Math.abs(event.deltaX) > Math.abs(event.deltaY);
  const next = horizontal
    ? panBy(view.value, ((event.shiftKey ? event.deltaY : event.deltaX) / rect.width) * 2)
    : zoomAbout(view.value, pointer, Math.exp(event.deltaY * 0.002));

  if (next.from === view.value.from && next.to === view.value.to) return;
  event.preventDefault();
  view.value = next;
}

function resetZoom() {
  view.value = { ...FULL_VIEW };
}

// ── Touch ──
//
// A wheel is a mouse's gesture and a phone has none, so the same two moves are
// offered to fingers: pinch to zoom about the midpoint, drag to pan.
//
// `touch-action: pan-y` on the bars is what makes this liveable — the browser
// keeps vertical scrolling, so the page still scrolls under a finger dragged
// down the deck, while horizontal movement and pinches come here instead of
// scrolling the page sideways or zooming the whole document.
//
// Dragging is for fingers only. A mouse keeps the wheel, because the bars answer
// a click by seeking and scoping and a mouse drag would have to guess which was
// meant; a finger has no such ambiguity once it has moved far enough to say so.
const PAN_THRESHOLD_PX = 8;

const touches = new Map<number, { x: number; y: number }>();
let pinch: { distance: number; view: TimeViewport } | null = null;
let panFrom: { x: number; view: TimeViewport } | null = null;
// A drag that panned must not also seek when it ends.
const panned = ref(false);

function boundsOf(event: PointerEvent): DOMRect {
  return (event.currentTarget as HTMLElement).getBoundingClientRect();
}

function onPointerDown(event: PointerEvent) {
  // Cleared for every gesture, mouse included: a touch drag that ended in a
  // cancel leaves no click to spend the flag on, and it must not be waiting for
  // someone else's.
  panned.value = false;
  if (event.pointerType === "mouse") return;
  touches.set(event.pointerId, { x: event.clientX, y: event.clientY });

  if (touches.size === 2) {
    const [a, b] = [...touches.values()];
    pinch = { distance: Math.hypot(a.x - b.x, a.y - b.y), view: { ...view.value } };
    panFrom = null;
  } else if (touches.size === 1) {
    panFrom = { x: event.clientX, view: { ...view.value } };
  }
}

function onPointerMove(event: PointerEvent) {
  if (event.pointerType === "mouse" || !touches.has(event.pointerId)) return;
  touches.set(event.pointerId, { x: event.clientX, y: event.clientY });
  const rect = boundsOf(event);
  if (rect.width === 0) return;

  if (pinch && touches.size >= 2) {
    const [a, b] = [...touches.values()];
    const distance = Math.hypot(a.x - b.x, a.y - b.y);
    if (distance <= 0) return;
    const midpoint = ((a.x + b.x) / 2 - rect.left) / rect.width;
    event.preventDefault();
    panned.value = true;
    view.value = zoomAbout(pinch.view, midpoint, pinch.distance / distance);
    return;
  }

  if (!panFrom || isFullView(view.value)) return;
  const dx = event.clientX - panFrom.x;
  if (!panned.value && Math.abs(dx) < PAN_THRESHOLD_PX) return;
  event.preventDefault();
  panned.value = true;
  view.value = panBy(panFrom.view, -dx / rect.width);
}

function onPointerUp(event: PointerEvent) {
  touches.delete(event.pointerId);
  if (touches.size < 2) pinch = null;
  if (touches.size === 0) panFrom = null;
}

// A cancelled gesture is followed by no click, so nothing will come to clear the
// flag the way a finished one does.
function onPointerCancel(event: PointerEvent) {
  onPointerUp(event);
  panned.value = false;
}

/**
 * Swallow the click a finished pan or pinch leaves behind, before the bars below
 * can read it as a seek. Capture phase, because they are the ones listening.
 */
function onClickCapture(event: MouseEvent) {
  if (!panned.value) return;
  panned.value = false;
  event.stopPropagation();
  event.preventDefault();
}

const playheadPercent = computed(() => {
  if (cursorMs.value === null) return null;
  const percent = ((cursorMs.value - viewStartMs.value) / viewRange.value) * 100;
  // Outside the view it is not drawn rather than pinned to the edge, which would
  // read as the boat sitting at the start of what you are looking at.
  return percent < 0 || percent > 100 ? null : percent;
});

const scopeEndMs = computed(() => Math.min(props.window.endMs, props.endMs));

function seek(epochMs: number) {
  cursor.setCursorTime(new Date(epochMs).toISOString());
}

/**
 * Clicking the bars moves the scope along as well as the playhead — but only
 * ever sets it, never clears it, so scrubbing within a segment cannot toggle it
 * off underneath you. Transport and replay call plain `seek`: scope must not
 * change every frame.
 *
 * With nothing selected a click only seeks. Creating a selection from nothing
 * fits the camera to one segment, which reads as the map lurching in response to
 * a click that was about time, not place. Once something is selected the camera
 * is already at that scale, so moving between segments is a small adjustment.
 *
 * Adjacent segments share the instant on their boundary, and the hit test is
 * half-open so that instant belongs to the segment that *starts* there. The
 * screen treats a selection the same way when it decides whether to move the
 * playhead into it, so agreeing here is what stops a click on the far edge of a
 * bar from scoping to that bar and then being thrown back to its start.
 */
function seekAndScope(epochMs: number) {
  seek(epochMs);
  if (props.nothingSelected) return;
  const segments = props.tree.segments;
  const last = segments[segments.length - 1];
  const hit =
    segments.find((s) => epochMs >= s.startMs && epochMs < (s.endMs ?? props.endMs))
    // Nothing starts at the end of the day, so the last instant is the last
    // segment's — the one case where the closed end is the right reading, and
    // the only one the screen reads that way too. An instant on any other
    // segment's end either starts the next one, and is already caught above, or
    // sits in a gap between modes and belongs to neither: scoping that to the
    // segment before it would put the playhead outside its own selection, and
    // the screen would throw it back to that segment's start.
    ?? (last && epochMs === (last.endMs ?? props.endMs) ? last : undefined);
  if (hit) emit("selectSegment", hit.id);
}

// ── Annotations ──

const annotationMarks = computed(() =>
  props.tree.segments
    .flatMap((segment) => [...segment.chips, ...segment.comments])
    .map((annotation) => ({
      id: annotation.id,
      label: annotation.label,
      color: annotation.color,
      percent: ((annotation.timeMs - viewStartMs.value) / viewRange.value) * 100,
      timeMs: annotation.timeMs,
    }))
    // Zoomed in, a mark outside the view would otherwise stack against the edge
    // and claim to be somewhere it is not.
    .filter((mark) => mark.percent >= 0 && mark.percent <= 100),
);

// ── Transport ──

const speed = ref<number>(1);
const isPlaying = ref(false);

const clock = createReplayClock({
  now: () => performance.now(),
  schedule: (tick) => requestAnimationFrame(tick),
  cancel: (handle) => cancelAnimationFrame(handle),
  getTime: () => cursorMs.value,
  setTime: (ms) => seek(ms),
  getEndMs: () => props.endMs,
  onStop: () => {
    isPlaying.value = false;
    emit("update:playing", false);
  },
});

function togglePlay() {
  if (isPlaying.value) {
    clock.stop();
    return;
  }
  if (cursorMs.value === null) cursor.resetCursor();
  // Restarting from the end would sit there stopped; go back to the beginning.
  else if (cursorMs.value >= props.endMs) cursor.resetCursor();
  isPlaying.value = true;
  emit("update:playing", true);
  clock.start(speed.value);
}

/**
 * Zoomed in, replay would otherwise run the boat off the edge within seconds and
 * leave you watching an empty stretch of bar. The playhead parks four-fifths of
 * the way across and the journey is drawn past it — see `followPlayhead`.
 *
 * Only while it is playing: scrubbing by hand is a deliberate act, and having
 * the bars slide underneath a click would make the next one land somewhere else.
 */
watch(cursorMs, (at) => {
  if (!isPlaying.value || at === null || isFullView(view.value)) return;
  view.value = followPlayhead(view.value, (at - props.startMs) / range.value);
});

function cycleSpeed() {
  speed.value = nextReplaySpeed(speed.value);
  clock.setMultiplier(speed.value);
  emit("update:speed", speed.value);
}

function step(direction: 1 | -1, event: MouseEvent) {
  cursor.stepPoint(direction, event.shiftKey ? 10 : 1);
}

/** Previous or next segment boundary, which is where the mode bar changes. */
function stepSegment(direction: 1 | -1) {
  cursor.stepModeBoundary(direction);
}

function onKeydown(event: KeyboardEvent) {
  const target = event.target as HTMLElement | null;
  if (target && (target.tagName === "INPUT" || target.tagName === "TEXTAREA" || target.isContentEditable)) {
    return;
  }
  if (event.key === "ArrowLeft") {
    cursor.stepPoint(-1, event.shiftKey ? 10 : 1);
    event.preventDefault();
  } else if (event.key === "ArrowRight") {
    cursor.stepPoint(1, event.shiftKey ? 10 : 1);
    event.preventDefault();
  }
}

onMounted(() => window.addEventListener("keydown", onKeydown));
onBeforeUnmount(() => {
  window.removeEventListener("keydown", onKeydown);
  clock.stop();
});

// ── Editing ──
//
// Everything applies at the playhead, which is the only place that resolves to a
// real timestamp. Every change saves immediately — no save button, no dirty
// state, as on the screen this replaces.

const mutations = useJourneyMutations(
  () => props.journeyFull as JourneyFull,
  cursor,
);

const showNoteModal = ref(false);
const showDeleteModal = ref(false);

const cursorTimestamp = computed(() => cursorMs.value ?? 0);

function onAddMode(mode: string) {
  mutations.addModeAtCursor(mode);
}

function onAddAnnotation(type: AnnotationType) {
  if (type === "NOTE") showNoteModal.value = true;
  else mutations.addAnnotation(type, "");
}

function onNoteConfirm(notes: string) {
  mutations.addAnnotation("NOTE", notes);
  showNoteModal.value = false;
}

/**
 * A mode owns only its start — its end is the next mode's start — so dragging
 * the boundary in front of a segment moves that segment's start. Snapping to a
 * fix keeps every boundary on a moment the track actually recorded.
 */
function onMoveBoundary(segmentId: number, epochMs: number) {
  const snapped = cursor.snapMs(epochMs);
  if (snapped === null) return;
  mutations.moveModeStart({ id: segmentId, started_at: new Date(snapped).toISOString() });
}
</script>

<template>
  <div class="border-t border-base-300 bg-base-100 px-3 py-2">
    <!-- The transport belongs to the playhead, and the playhead is a thing on
         the map, so the controls sit over the map rather than costing the deck a
         row of its own. `defer` because the target is a plain div in the view,
         which mounts in the same pass this one does.

         Teleporting rather than lifting the replay clock into the view: the
         clock, the keyboard handlers and the zoom all live here, and moving the
         markup is a smaller thing than moving the state that drives it. -->
    <Teleport
      to="#journey-playhead-controls"
      defer
    >
      <div class="flex flex-wrap items-center justify-center gap-1">
        <button
          class="btn btn-ghost btn-sm btn-square"
          title="Previous segment"
          @click="stepSegment(-1)"
        >
          <OutlineBackwardIcon class="size-4" />
        </button>
        <button
          class="btn btn-ghost btn-sm btn-square"
          title="Back one fix (shift for ten)"
          @click="step(-1, $event)"
        >
          <OutlineChevronLeftIcon class="size-4" />
        </button>
        <button
          class="btn btn-sm btn-square"
          :class="isPlaying ? 'btn-primary' : 'btn-ghost'"
          :title="isPlaying ? 'Pause' : 'Play'"
          @click="togglePlay"
        >
          <OutlinePauseIcon
            v-if="isPlaying"
            class="size-4"
          />
          <OutlinePlayIcon
            v-else
            class="size-4"
          />
        </button>
        <button
          class="btn btn-ghost btn-sm btn-square"
          title="Forward one fix (shift for ten)"
          @click="step(1, $event)"
        >
          <OutlineChevronRightIcon class="size-4" />
        </button>
        <button
          class="btn btn-ghost btn-sm btn-square"
          title="Next segment"
          @click="stepSegment(1)"
        >
          <OutlineForwardIcon class="size-4" />
        </button>

        <button
          class="btn btn-ghost btn-sm font-mono text-xs tabular-nums"
          title="Replay speed"
          @click="cycleSpeed"
        >
          {{ speed }}×
        </button>

        <!-- Only once zoomed: the way back out, and the only visible sign the
           gesture exists. -->
        <button
          v-if="isZoomed"
          class="btn btn-ghost btn-sm font-mono text-xs tabular-nums"
          title="Zoomed in. Scroll over the bars to zoom and shift-scroll to pan, or pinch and drag them. Click to see the whole journey again."
          @click="resetZoom"
        >
          {{ zoomLabel }}
          <OutlineXMarkIcon class="size-3" />
        </button>
      </div>
    </Teleport>

    <!-- Annotation strip, mode bar and speed lane share one time axis, with a
         single playhead drawn across all three. Scrolling anywhere over them
         zooms; see the zoom block in the script. -->
    <div>
      <div
        class="relative touch-pan-y"
        @wheel="onWheel"
        @pointerdown="onPointerDown"
        @pointermove="onPointerMove"
        @pointerup="onPointerUp"
        @pointercancel="onPointerCancel"
        @click.capture="onClickCapture"
      >
        <div class="relative mb-1 h-3">
          <span
            v-for="mark in annotationMarks"
            :key="mark.id"
            class="absolute top-1 size-2 -translate-x-1/2 rotate-45 cursor-pointer"
            :style="{ left: mark.percent + '%', backgroundColor: mark.color }"
            :title="mark.label"
            @click="seekAndScope(mark.timeMs)"
          />
        </div>

        <ModeBar
          :segments="tree.segments"
          :start-ms="viewStartMs"
          :end-ms="viewEndMs"
          :scope-start-ms="window.startMs"
          :scope-end-ms="scopeEndMs"
          :nothing-selected="nothingSelected"
          :editable="editable && !!journeyFull"
          @seek="seekAndScope"
          @move-boundary="onMoveBoundary"
        />

        <SpeedLane
          :features="features"
          :start-ms="viewStartMs"
          :end-ms="viewEndMs"
          :scope-start-ms="window.startMs"
          :scope-end-ms="scopeEndMs"
          :nothing-selected="nothingSelected"
          @seek="seekAndScope"
        />

        <div
          v-if="playheadPercent !== null"
          class="pointer-events-none absolute inset-y-0 z-20 w-px bg-accent"
          :style="{ left: playheadPercent + '%' }"
        >
          <span class="absolute -top-0.5 -left-1 size-2 rounded-full bg-accent" />
        </div>
      </div>
    </div>

    <!-- Add-chips: click one and it lands at the playhead, saved immediately -->
    <div
      v-if="editable && journeyFull"
      class="mt-2 flex flex-wrap items-center gap-1 border-t border-base-300 pt-2"
    >
      <span class="mr-1 font-mono text-[10px] uppercase tracking-wider text-base-content/40">
        Add at playhead
      </span>
      <button
        v-for="mode in modeTypeList"
        :key="mode.value"
        class="btn btn-ghost btn-xs font-mono text-[11px] uppercase tracking-wider"
        :class="{ 'btn-active': mode.value === cursor.activeMode.value?.mode }"
        :title="`Start ${mode.label} here`"
        @click="onAddMode(mode.value)"
      >
        {{ mode.label }}
      </button>

      <span class="mx-1 h-4 w-px shrink-0 bg-base-300" />

      <button
        v-for="annotation in annotationTypeList"
        :key="annotation.value"
        class="btn btn-ghost btn-xs font-mono text-[11px] uppercase tracking-wider"
        :title="`Mark ${annotation.label} here`"
        @click="onAddAnnotation(annotation.value)"
      >
        {{ annotation.label }}
      </button>

      <div class="ml-auto flex items-center gap-1">
        <button
          class="btn btn-ghost btn-xs font-mono text-[11px] uppercase tracking-wider"
          title="Split the current mode at the playhead"
          @click="mutations.splitModeAtCursor()"
        >
          Split
        </button>
        <button
          class="btn btn-ghost btn-xs text-error"
          title="Delete something at the playhead"
          @click="showDeleteModal = true"
        >
          <OutlineTrashIcon class="size-4" />
          <span class="sr-only">Delete at playhead</span>
        </button>
      </div>
    </div>

    <AnnotationModal
      :show="showNoteModal"
      type="NOTE"
      :cursor-timestamp="cursorTimestamp"
      @confirm="onNoteConfirm"
      @cancel="showNoteModal = false"
    />

    <DeleteAtCursorModal
      :show="showDeleteModal"
      :timestamp="cursorTimestamp"
      :coordinates="cursor.cursorPosition.value"
      :annotations="cursor.nearbyAnnotations.value"
      :mode="cursor.activeMode.value"
      :mode-starts-here="cursor.cursorAtModeStart.value"
      :preceding-mode="cursor.precedingMode.value"
      @delete-annotation="mutations.deleteAnnotation($event); showDeleteModal = false"
      @delete-mode="mutations.deleteMode($event); showDeleteModal = false"
      @delete-point="mutations.deleteSnappedPoint(); showDeleteModal = false"
      @cancel="showDeleteModal = false"
    />
  </div>
</template>
