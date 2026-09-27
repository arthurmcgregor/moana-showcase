export type NavChild = {
  label: string;
  path: string;
  adminOnly?: boolean;
};

export type NavLink = {
  label: string;
  path?: string;
  children?: NavChild[];
  adminOnly?: boolean;
};

export const navLinks: NavLink[] = [
  { label: "Journeys", path: "/journeys" },
  { label: "Tracks", path: "/tracks/web" },
  { label: "People", path: "/people" },
  { label: "Gear", path: "/gear" },
  { label: "Wind Rose", path: "/wind-rose" },
  {
    label: "Add",
    adminOnly: true,
    children: [
      { label: "Track from device", path: "/tracks/device", adminOnly: true },
      { label: "Track from file", path: "/tracks/local", adminOnly: true },
      { label: "Trace path", path: "/tracks/trace", adminOnly: true },
      { label: "Weather CSV", path: "/weather/upload", adminOnly: true },
    ],
  },
];

export function filterNavLinks(links: NavLink[], isAdmin: boolean): NavLink[] {
  if (isAdmin) return links;
  const result: NavLink[] = [];
  for (const link of links) {
    if (link.adminOnly) continue;
    if (!link.children) {
      result.push(link);
      continue;
    }
    const children = link.children.filter((c) => !c.adminOnly);
    if (children.length > 0) result.push({ ...link, children });
  }
  return result;
}
