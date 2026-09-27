<script setup lang="ts">
import { computed } from "vue";
import type { LegStatistics } from "@/api/web";
import type { ScopeStats } from "@/engine";
import {
  formatDistanceNm,
  formatDurationSeconds,
  formatSpanDuration,
} from "@/utils/formatters";

/**
 * The journey itself, at the head of the rail — and the only part of the screen
 * that is about the whole day rather than a piece of it.
 *
 * Two roots, not one wrapper, and deliberately so: the name bar is `sticky`, and
 * a sticky box can only travel inside its own parent's box. Wrapped together
 * with the figures, the bar would unstick the moment that wrapper scrolled past,
 * which is exactly the scroll it exists to survive. As siblings in the rail's
 * scroller, the bar holds and the figures leave.
 *
 * It renders on every tab. The mode tree below it does not — the tree only makes
 * sense beside the map and the deck — so on Polar, Photos and Details this is the
 * whole rail, and it says which journey the pane on the right belongs to.
 */
const props = defineProps<{
  name: string;
  dateLabel?: string;
  description?: string | null;
  /** The day, counted from the fixes — where the tacks and gybes come from. */
  journeyStats: ScopeStats;
  /** The server's figures for the day, which beat the fixes where they exist. */
  journeyApiStats?: LegStatistics | null;
  /**
   * Set when the day was drawn rather than recorded: the length of the drawn
   * route, and the only figure there is. The times are whole days picked from a
   * calendar and there are no fixes, so elapsed and speed would be made up.
   */
  routeDistanceNm?: number | null;
}>();

const totals = computed(() => {
  const api = props.journeyApiStats;
  const stats = props.journeyStats;
  if (props.routeDistanceNm != null) {
    return {
      distance: `${formatDistanceNm(props.routeDistanceNm)} (est.)`,
      duration: null,
      speed: null,
      tacks: 0,
      gybes: 0,
    };
  }
  return {
    distance: formatDistanceNm(api ? api.total_distance_nm : stats.distanceNm),
    duration: api
      ? formatDurationSeconds(api.total_duration_seconds)
      : formatSpanDuration(stats.elapsedMs),
    speed: api
      ? `${api.avg_speed_knots.toFixed(1)} · ${api.max_speed_knots.toFixed(1)} kts`
      : stats.speed
        ? `${stats.speed.avg.toFixed(1)} · ${stats.speed.max.toFixed(1)} kts`
        : null,
    tacks: stats.tacks,
    gybes: stats.gybes,
  };
});

// "1 tack · 2 gybes" — both counts are usually 0 or 1, and "1 tacks" reads as a
// bug in the counter rather than a count of one.
const manoeuvres = computed(() => {
  const { tacks, gybes } = totals.value;
  if (!tacks && !gybes) return null;
  const parts: string[] = [];
  if (tacks) parts.push(`${tacks} tack${tacks === 1 ? "" : "s"}`);
  if (gybes) parts.push(`${gybes} gybe${gybes === 1 ? "" : "s"}`);
  return parts.join(" · ");
});
</script>

<template>
  <!-- The name and the two buttons that act on it: the only things in the rail
       that hold their place while the rest of it scrolls. `-mx-4 px-4` so the
       background covers the scroller's side padding rather than letting a row
       show through beside it; the top padding is the bar's own, so `top-0` is
       flush with the scroll edge and nothing shows above it either.
       Static below md, where the column is a stretch of one page-long scroll
       rather than a scroller of its own and a bar pinned to the viewport would
       follow you all the way down the deck. -->
  <div class="sticky top-0 z-20 -mx-4 flex items-start gap-2 border-b border-base-300 bg-base-100 px-4 pt-4 pb-3 max-md:static">
    <h2 class="min-w-0 flex-1 line-clamp-2 font-display text-lg leading-tight tracking-tight text-base-content">
      {{ name }}
    </h2>
    <slot name="actions" />
  </div>

  <!-- The day's own figures. Below the bar rather than in it, so they scroll
       away: they are read once when the screen opens, and the tree wants the
       height for the rest of the visit. A grid rather than a run of text
       because the rail is 280px wide and four labelled figures fit two-up
       without any of them breaking across a line.

       Same ground as the name bar, and the same `-mx-4 px-4` reach to the
       rail's edges, so the two read as one panel about the journey — and the
       segments below, which sit on the rail's own darker ground, read as a
       separate list rather than more of the same block. -->
  <div class="-mx-4 flex flex-col gap-2 border-b border-base-300 bg-base-100 px-4 py-3">
    <div
      v-if="dateLabel"
      class="font-mono text-[10px] uppercase tracking-wider text-base-content/40"
    >
      {{ dateLabel }}
    </div>

    <p
      v-if="description"
      class="line-clamp-3 text-xs leading-snug text-base-content/60"
    >
      {{ description }}
    </p>

    <dl class="grid grid-cols-2 gap-x-3 gap-y-2">
      <div>
        <dt class="font-mono text-[9px] uppercase tracking-wider text-base-content/35">
          Distance
        </dt>
        <dd class="font-display text-lg leading-none tracking-tight text-base-content">
          {{ totals.distance }}
        </dd>
      </div>
      <div v-if="totals.duration">
        <dt class="font-mono text-[9px] uppercase tracking-wider text-base-content/35">
          Elapsed
        </dt>
        <dd class="font-mono text-[11px] tabular-nums text-base-content/70">
          {{ totals.duration }}
        </dd>
      </div>
      <div v-if="totals.speed">
        <dt class="font-mono text-[9px] uppercase tracking-wider text-base-content/35">
          Avg · max
        </dt>
        <dd class="font-mono text-[11px] tabular-nums text-base-content/70">
          {{ totals.speed }}
        </dd>
      </div>
      <div v-if="manoeuvres">
        <dt class="font-mono text-[9px] uppercase tracking-wider text-base-content/35">
          Manoeuvres
        </dt>
        <dd class="font-mono text-[11px] tabular-nums text-base-content/70">
          {{ manoeuvres }}
        </dd>
      </div>
    </dl>
  </div>
</template>
