// Chaikin corner-cutting smoothing. Each iteration inserts two points per edge
// at 1/4 and 3/4, producing a rounded curve that converges to a B-spline.
// Endpoints are preserved so the start and end of the trace stay put.
export function chaikin(
  coords: [number, number][],
  iterations = 3,
): [number, number][] {
  if (coords.length < 3) return coords.slice();
  let pts = coords;
  for (let i = 0; i < iterations; i++) {
    const next: [number, number][] = [pts[0]];
    for (let j = 0; j < pts.length - 1; j++) {
      const [x0, y0] = pts[j];
      const [x1, y1] = pts[j + 1];
      next.push([x0 * 0.75 + x1 * 0.25, y0 * 0.75 + y1 * 0.25]);
      next.push([x0 * 0.25 + x1 * 0.75, y0 * 0.25 + y1 * 0.75]);
    }
    next.push(pts[pts.length - 1]);
    pts = next;
  }
  return pts;
}
