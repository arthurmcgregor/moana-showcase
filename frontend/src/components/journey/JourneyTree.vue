<script setup lang="ts">
import { computed, inject, ref } from "vue";
import type { AboardRecord, GeoJSONFeature, Leg } from "@/api/web";
import type { Scope, ScopeStats, TimelineTree, TreeLeg } from "@/engine";
import {
  blockedReason,
  claimedSegmentIds,
  selectedSegmentIds,
  statsForWindow,
  toggleContiguous,
} from "@/engine";
import type { UseLegEditingReturn } from "@/composables/useLegEditing";
import { useJourneyAboardQuery, usePeopleQuery } from "@/composables/useQueries";
import { useAboardMutations } from "@/composables/useAboardMutations";
import {
  planAdd,
  planRemove,
  computeCrewForRange,
  isSubset,
  type Bounds,
} from "@/composables/aboardOps";
import { useAuthStore } from "@/stores/auth";
import {
  formatDistanceNm,
  formatDurationSeconds,
  formatSpanDuration,
  formatSpanRange,
} from "@/utils/formatters";
import CrewBar from "@/components/crew/CrewBar.vue";
import JourneyTreeSegment from "./JourneyTreeSegment.vue";

// Legs and the mode breakdown as one tree, deliberately both rather than either.
// Selecting a row scopes the rest of the screen to it; selecting it again is
// what returns to the whole journey, so there is no separate clear button.
//
// A row means one of two things depending on whether a leg is open for editing.
// Normally a click scopes the screen and shift-click gathers a run. With a leg
// open, a click moves that segment into or out of the leg instead — the leg is
// the thing being edited, so the rows are its membership list.

const props = defineProps<{
  tree: TimelineTree;
  scope: Scope;
  /** The day's fixes — what the selected segment's figures are measured from. */
  features: GeoJSONFeature[];
  journeyId: number;
  vesselId: number;
  journeyStartedAt: string;
  journeyEndedAt: string | null;
  /** Where the playhead is now — lit like a selection, and given its figures. */
  liveSegmentId?: number | null;
  liveLegId?: number | null;
  /** The segment replay is following: what is lit instead of the selection. */
  followSegmentId?: number | null;
  /** Edit mode adds leg creation, and opens a leg for editing when selected. */
  editable?: boolean;
}>();

const emit = defineEmits<{
  select: [Scope];
  /** Set the scope without the toggle-off rule, for gestures that build a run. */
  selectOnly: [Scope];
}>();

const legEdit = inject<UseLegEditingReturn>("legEditing")!;
const {
  editingLegId,
  name: legName,
  notes: legNotes,
  runIds,
  claimedIds,
  isSaving,
  error: legError,
} = legEdit;

const isEditingLeg = computed(() => editingLegId.value !== null);

// ── Rows ──

// The date sits above the first entry of each day, not on every row — and not
// at all on a journey that never leaves one day, which is most of them. There
// the header is a heading over the whole list with nothing to separate it from,
// saying again what the page's own subtitle says under the journey's name.
const rows = computed(() => {
  const keys = props.tree.entries.map((entry) => new Date(entry.startMs).toDateString());
  const spansDays = new Set(keys).size > 1;

  let prevKey: string | null = null;
  return props.tree.entries.map((entry, i) => {
    const key = keys[i];
    const dateHeader = spansDays && key !== prevKey
      ? new Date(entry.startMs).toLocaleDateString(undefined, {
          day: "numeric",
          month: "short",
          year: "numeric",
        })
      : null;
    prevKey = key;
    return { entry, dateHeader };
  });
});

function isLegSelected(leg: TreeLeg): boolean {
  return props.scope.kind === "leg" && props.scope.id === leg.leg.id;
}

// While a leg is open its pending membership is what the rows show as selected;
// otherwise the screen's own segment run is.
const selectedIds = computed<ReadonlySet<number>>(() =>
  isEditingLeg.value ? runIds.value : selectedSegmentIds(props.scope),
);

/**
 * The one segment worth spelling out: the one the playhead is standing in. A row
 * carries its mode and how long it lasted; what it does not carry is how far the
 * boat got and how fast, and putting those on every row would be four figures a
 * row down the whole rail.
 *
 * The playhead rather than the selection, so the line follows where you are
 * looking on the map and the deck instead of what you last clicked — scrubbing
 * the deck walks it down the rail. Not while a leg is open for editing: the rows
 * are that leg's membership list then, and figures are not what a click means.
 *
 * `statsForWindow` rather than anything of its own, so the line and the deck
 * agree to the decimal about the same stretch of water.
 */
const activeSegmentId = computed(() =>
  isEditingLeg.value ? null : props.liveSegmentId ?? null,
);

const activeStats = computed(() => {
  const segment = props.tree.segments.find((s) => s.id === activeSegmentId.value);
  if (!segment) return null;
  return statsForWindow(props.features, {
    startMs: segment.startMs,
    endMs: segment.endMs ?? Infinity,
  });
});

function statsFor(id: number): ScopeStats | null {
  return id === activeSegmentId.value ? activeStats.value : null;
}

/**
 * One mark on a row, not two. A row used to carry a ring for being selected and
 * an accent edge for holding the playhead, which in normal use are the same row:
 * clicking scopes the screen and lands the playhead inside what you clicked.
 *
 * So the rail lights what the screen is looking at, whether you got there by
 * choosing it or by playing into it. While replay is following, that is the
 * segment the boat is in *instead of* the one you last clicked — the follow
 * replaces the selection here rather than joining it, so a stale ring is never
 * left burning behind a moving playhead.
 *
 * A run of segments still lights all of them, and which one the boat is in is
 * the row that has opened its figures. That is what the accent edge was for.
 *
 * Not while a leg is open: the rows are that leg's membership list then, and a
 * lit row has to mean "in the leg" and nothing else.
 */
function isSegmentSelected(id: number): boolean {
  if (isEditingLeg.value) return selectedIds.value.has(id);
  if (props.followSegmentId != null) return props.followSegmentId === id;
  return selectedIds.value.has(id) || props.liveSegmentId === id;
}

/**
 * Only meaningful while a leg is open, because that is the only time a click is
 * about membership and can therefore be refused.
 */
function segmentBlocked(id: number): "claimed" | "unreachable" | null {
  if (!isEditingLeg.value) return null;
  return blockedReason({
    orderedIds: props.tree.segments.map((s) => s.id),
    selected: runIds.value,
    clickedId: id,
    claimed: claimedIds.value,
  });
}

// Legs do not collapse. A segment outside a leg has no way to be hidden, so a
// leg that could fold its own segments away made the rail two lists with two
// different rules — and it needed a watch on the selection and another on the
// edit target to reopen itself whenever something inside it was wanted, which
// is most of the times it mattered.

// ── Crew / aboard wiring ──
//
// Leg-level only. The journey's own crew bar lives in the Details tab
// (`JourneyCrewBar`); the journey crew is still computed here because a leg only
// shows a crew row when its crew differs from the journey's.

const authStore = useAuthStore();
const aboardQuery = useJourneyAboardQuery(() => props.journeyId);
const peopleQuery = usePeopleQuery();

const aboardRecords = computed<AboardRecord[]>(() => aboardQuery.data.value ?? []);
const people = computed(() => peopleQuery.data.value ?? []);

const journeyBounds = computed<Bounds>(() => ({
  started_at: props.journeyStartedAt,
  ended_at: props.journeyEndedAt,
}));

const journeyCrew = computed(() => computeCrewForRange(aboardRecords.value, journeyBounds.value));
const journeyCrewPersonIds = computed(() => journeyCrew.value.map((r) => r.person));

const { runOps, isRunningOps, createNewPerson, isCreatingPerson } = useAboardMutations(
  () => props.journeyId,
  () => props.vesselId,
);
const isCrewBusy = computed(() => isRunningOps.value || isCreatingPerson.value);

type AddPayload =
  | { kind: "existing"; personId: number }
  | { kind: "new"; firstName: string; lastName: string };

function applyAdd(personId: number, bounds: Bounds) {
  const ops = planAdd(aboardRecords.value, personId, bounds);
  if (ops.length) runOps(ops);
}

async function handleCrewAdd(payload: AddPayload, bounds: Bounds) {
  if (payload.kind === "existing") {
    applyAdd(payload.personId, bounds);
  } else {
    const person = await createNewPerson({
      first_name: payload.firstName,
      last_name: payload.lastName,
    });
    applyAdd(person.id, bounds);
  }
}

function handleCrewRemove(record: AboardRecord, bounds: Bounds) {
  const ops = planRemove(aboardRecords.value, record.person, bounds);
  if (ops.length) runOps(ops);
}

function legBounds(leg: Leg): Bounds {
  return { started_at: leg.started_at, ended_at: leg.ended_at ?? null };
}

function legCrew(leg: Leg): AboardRecord[] {
  return computeCrewForRange(aboardRecords.value, legBounds(leg));
}

// A leg whose crew is exactly the journey's says nothing worth a second row.
function shouldShowLegCrew(leg: Leg): boolean {
  const crew = legCrew(leg);
  if (crew.length === 0 && journeyCrew.value.length === 0) return false;
  const legIds = crew.map((r) => r.person);
  return !(isSubset(legIds, journeyCrewPersonIds.value) && isSubset(journeyCrewPersonIds.value, legIds));
}

function legDurationMs(leg: TreeLeg): number | null {
  return leg.endMs === null ? null : leg.endMs - leg.startMs;
}

const legs = computed<Leg[]>(() =>
  props.tree.entries.filter((e): e is TreeLeg => e.kind === "leg").map((e) => e.leg),
);

// ── Row activation ──

const EMPTY = new Set<number>();

/** Selected ids in time order, so a run's bounds read off its ends directly. */
function orderedIds(ids: ReadonlySet<number>): number[] {
  return props.tree.segments.filter((s) => ids.has(s.id)).map((s) => s.id);
}

function onSegmentSelect(id: number) {
  // A leg is open: the click is about that leg's membership, not the scope.
  if (isEditingLeg.value) {
    legEdit.toggleMembership(id);
    return;
  }
  emit("select", { kind: "segments", ids: [id] });
}

/**
 * Shift-click extends the run rather than replacing it. Leg ownership does not
 * block this — scoping the screen to a stretch of the day is a way of looking at
 * it, and only leg membership needs the ownership rule.
 */
function onSegmentRangeSelect(id: number) {
  if (isEditingLeg.value) {
    legEdit.toggleMembership(id);
    return;
  }
  const next = toggleContiguous({
    orderedIds: props.tree.segments.map((s) => s.id),
    selected: selectedIds.value,
    clickedId: id,
    claimed: EMPTY,
  });
  // Set-only: retreating to an empty run already means "the whole journey", so
  // the toggle rule would fire a second time on the same click.
  emit("selectOnly", { kind: "segments", ids: orderedIds(next) });
}

function onLegSelect(leg: Leg) {
  // Selecting a leg in edit mode is how you edit it.
  if (props.editable && editingLegId.value === null) {
    legEdit.begin(leg);
    emit("selectOnly", { kind: "leg", id: leg.id });
    return;
  }
  if (editingLegId.value !== null) return;
  emit("select", { kind: "leg", id: leg.id });
}

// ── Leg creation ──
//
// Only over segments no leg owns: a new leg cannot quietly take a stretch from
// an existing one.

const ownedByAnyLeg = computed(() => claimedSegmentIds(props.tree.segments, legs.value, null));

const showLegModal = ref(false);

const selectionOwnedCount = computed(
  () => orderedIds(selectedIds.value).filter((id) => ownedByAnyLeg.value.has(id)).length,
);

const canCreateLeg = computed(
  () => !isEditingLeg.value && selectedIds.value.size > 0 && selectionOwnedCount.value === 0,
);

const showSelectionBar = computed(
  () => !!props.editable && !isEditingLeg.value && selectedIds.value.size > 0,
);

// Each of these closes only on a write that landed: a failed one leaves the bar
// and the selection where they were, showing `legEdit.error` instead.
async function onCreateLeg(name: string, notes: string) {
  if (!(await legEdit.create(name, orderedIds(selectedIds.value), notes))) return;
  showLegModal.value = false;
  emit("selectOnly", { kind: "journey" });
}

const selectionBounds = computed(() => {
  const inRun = props.tree.segments.filter((s) => selectedIds.value.has(s.id));
  if (!inRun.length) return null;
  const last = inRun[inRun.length - 1];
  return { start: inRun[0].startedAt, end: last.endedAt ?? last.startedAt };
});

function onCancelEdit() {
  legEdit.cancel();
  emit("selectOnly", { kind: "journey" });
}

const showDeleteLegConfirm = ref(false);

async function onDeleteLeg() {
  showDeleteLegConfirm.value = false;
  if (!(await legEdit.remove())) return;
  emit("selectOnly", { kind: "journey" });
}
</script>

<template>
  <div class="space-y-1 pt-3">
    <template
      v-for="{ entry, dateHeader } in rows"
      :key="entry.kind === 'mode' ? `m-${entry.id}` : `l-${entry.leg.id}`"
    >
      <div
        v-if="dateHeader"
        class="pt-3 pb-1 font-mono text-[11px] uppercase tracking-wider text-base-content/50"
      >
        {{ dateHeader }}
      </div>

      <!-- A segment belonging to no leg, in its place in the day -->
      <JourneyTreeSegment
        v-if="entry.kind === 'mode'"
        :segment="entry"
        :stats="statsFor(entry.id)"
        :selected="isSegmentSelected(entry.id)"
        :blocked="segmentBlocked(entry.id)"
        :max-duration-ms="tree.maxDurationMs"
        @select="onSegmentSelect(entry.id)"
        @range-select="onSegmentRangeSelect(entry.id)"
      />

      <!-- A leg, with its segments nested inside it -->
      <div
        v-else
        class="-mx-4 rounded-lg border transition-colors"
        :class="[
          isLegSelected(entry) ? 'border-primary/60 bg-primary/5' : 'border-base-300',
          entry.isEditing ? 'ring-2 ring-primary/40' : '',
          liveLegId === entry.leg.id ? 'border-l-2 border-l-accent' : '',
        ]"
      >
        <!-- 15px, not 16: the card reaches a whole `p-4` further out than the
             rows around it, so its own inset has to give back that 16 less the
             1px border it just added. A segment inside a leg is then exactly as
             wide as one outside, and the leg reads as a band drawn around them
             rather than a column of its own. -->
        <div class="flex items-start gap-2 px-[15px] pt-4 pb-3">
          <!-- Editing the leg replaces the header text in place. The range comes
               back while it is open, which is the one time it earns the line: a
               leg being edited is a leg whose bounds are being dragged, and the
               clock times are the only readout of where they have got to. -->
          <div
            v-if="entry.isEditing"
            class="flex min-w-0 flex-1 flex-col gap-2"
          >
            <input
              v-model="legName"
              class="ui-input font-display text-lg tracking-tight"
              placeholder="Leg name"
              aria-label="Leg name"
            >
            <div class="font-mono text-[11px] text-base-content/40">
              {{ formatSpanRange(entry.startedAt, entry.endedAt) }}
            </div>
            <textarea
              v-model="legNotes"
              class="ui-input ui-input--area"
              placeholder="Description (optional)"
              rows="2"
              aria-label="Leg description"
            />
          </div>

          <!-- Name and range select the leg; the notes below them do not, so the
               text stays selectable and the click target stays the identity of
               the leg rather than the whole block. -->
          <div
            v-else
            class="min-w-0 flex-1"
          >
            <button
              type="button"
              class="w-full text-left"
              :aria-pressed="isLegSelected(entry)"
              @click="onLegSelect(entry.leg)"
            >
              <div class="flex items-baseline justify-between gap-3">
                <span class="truncate font-display text-lg tracking-tight">{{ entry.leg.name }}</span>
                <span
                  v-if="!entry.leg.ended_at"
                  class="shrink-0 font-mono text-[11px] uppercase tracking-wider text-info"
                >Ongoing</span>
              </div>
            </button>

            <p
              v-if="entry.leg.notes"
              class="mt-2 text-sm text-base-content/60"
            >
              {{ entry.leg.notes }}
            </p>

            <div class="mt-2 flex flex-wrap gap-x-5 gap-y-1 font-mono text-[11px] text-base-content/50">
              <span
                v-if="entry.leg.statistics"
                class="tabular-nums"
              >{{ formatDistanceNm(entry.leg.statistics.total_distance_nm) }}</span>
              <span class="tabular-nums">
                {{ entry.leg.statistics
                  ? formatDurationSeconds(entry.leg.statistics.total_duration_seconds)
                  : formatSpanDuration(legDurationMs(entry)) }}
              </span>
              <span
                v-if="entry.leg.statistics"
                class="tabular-nums"
              >{{ entry.leg.statistics.avg_speed_knots.toFixed(1) }} avg ·
                {{ entry.leg.statistics.max_speed_knots.toFixed(1) }} max kts</span>
            </div>
          </div>
        </div>

        <div class="px-[15px] pb-4">
          <div
            v-if="shouldShowLegCrew(entry.leg)"
            class="mb-3"
          >
            <CrewBar
              label="On board"
              :records="legCrew(entry.leg)"
              :people="people"
              :can-edit="authStore.isAdmin"
              :journey-person-ids="journeyCrewPersonIds"
              :busy="isCrewBusy"
              @add="(payload) => handleCrewAdd(payload, legBounds(entry.leg))"
              @remove="(record) => handleCrewRemove(record, legBounds(entry.leg))"
            />
          </div>

          <div
            v-if="entry.segments.length"
            class="space-y-1"
          >
            <JourneyTreeSegment
              v-for="segment in entry.segments"
              :key="segment.id"
              :segment="segment"
              :stats="statsFor(segment.id)"
              :selected="isSegmentSelected(segment.id)"
              :blocked="segmentBlocked(segment.id)"
              :max-duration-ms="tree.maxDurationMs"
              @select="onSegmentSelect(segment.id)"
              @range-select="onSegmentRangeSelect(segment.id)"
            />
          </div>
        </div>
      </div>
    </template>

    <!-- Editing a leg: the bar carries the leg's own actions -->
    <div
      v-if="isEditingLeg"
      class="sticky bottom-0 z-10 -mx-2 mt-2 flex flex-wrap items-center justify-between gap-2 rounded-lg border border-primary/40 bg-base-100/95 px-4 py-3 shadow-lg backdrop-blur"
    >
      <span class="font-mono text-sm">
        <span class="font-semibold">{{ runIds.size }}</span>
        segment{{ runIds.size === 1 ? '' : 's' }}
        <span class="ml-1 text-base-content/60">· editing</span>
      </span>
      <div class="flex gap-2">
        <button
          class="btn btn-error btn-sm"
          :disabled="isSaving"
          @click="showDeleteLegConfirm = true"
        >
          Delete
        </button>
        <button
          class="btn btn-ghost btn-sm"
          :disabled="isSaving"
          @click="onCancelEdit"
        >
          Cancel
        </button>
        <button
          class="btn btn-primary btn-sm"
          :disabled="!legName.trim() || runIds.size === 0 || isSaving"
          :title="runIds.size === 0 ? 'A leg needs at least one segment' : undefined"
          @click="legEdit.save()"
        >
          <span
            v-if="isSaving"
            class="loading loading-spinner loading-xs"
          />
          Save
        </button>
      </div>
      <!-- A write that did not land. Full-width so it pushes the buttons down
           rather than squeezing them, and the edit is still open behind it. -->
      <AppNotice
        v-if="legError"
        variant="error"
        class="basis-full"
      >
        {{ legError }}
      </AppNotice>
    </div>

    <!-- A run of segments selected: what can be done with it -->
    <div
      v-else-if="showSelectionBar"
      class="sticky bottom-0 z-10 -mx-2 mt-2 flex flex-wrap items-center justify-between gap-2 rounded-lg border border-base-300 bg-base-100/95 px-4 py-3 shadow-lg backdrop-blur"
    >
      <span class="font-mono text-sm">
        <span class="font-semibold">{{ selectedIds.size }}</span>
        segment{{ selectedIds.size === 1 ? '' : 's' }} selected
      </span>
      <button
        v-if="canCreateLeg"
        class="btn btn-primary btn-sm"
        :disabled="isSaving"
        @click="showLegModal = true"
      >
        Create Leg
      </button>
      <span
        v-else
        class="font-mono text-[11px] text-base-content/50"
      >
        {{ selectionOwnedCount }} already in a leg — select it to edit instead
      </span>
      <!-- A failed Create leaves the run selected, so the reason belongs here. -->
      <AppNotice
        v-if="legError"
        variant="error"
        class="basis-full"
      >
        {{ legError }}
      </AppNotice>
    </div>

    <LegCreationModal
      :show="showLegModal"
      :start-time="selectionBounds?.start ?? null"
      :cursor-timestamp="selectionBounds ? new Date(selectionBounds.end).getTime() : 0"
      @confirm="onCreateLeg"
      @cancel="showLegModal = false"
    />

    <ConfirmModal
      :show="showDeleteLegConfirm"
      title="Delete leg"
      :body="`Delete “${legName}”? The segments stay; only the leg goes. Cannot be undone.`"
      confirm-label="Delete"
      @confirm="onDeleteLeg"
      @cancel="showDeleteLegConfirm = false"
    />
  </div>
</template>
