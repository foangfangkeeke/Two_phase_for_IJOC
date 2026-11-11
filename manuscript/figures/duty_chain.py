"""Illustrate a continuous bus duty and its charging opportunities."""

from pathlib import Path

import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, Rectangle


plt.rcParams.update({
    "font.family": "Times New Roman",
    "mathtext.fontset": "custom",
    "mathtext.rm": "Times New Roman",
    "mathtext.it": "Times New Roman:italic",
    "mathtext.bf": "Times New Roman:bold",
    "mathtext.cal": "cmsy10",
    "mathtext.fallback": "stix",
    "pdf.fonttype": 42,
})
fig, ax = plt.subplots(figsize=(12, 3.9))
ax.set_xlim(0, 12)
ax.set_ylim(0, 3.9)
ax.axis("off")

ink = "#242a31"
accent = "#31546a"
muted = "#66727b"
shared = "#b14545"
y = 2.05
stations = {"1": 1.95, "2": 2.85, "3": 3.75, "4": 5.15, "5": 6.05, "6": 6.95, "7": 8.85}


def arrow(x1, x2, color=ink, linewidth=1.3):
    ax.add_patch(FancyArrowPatch((x1, y), (x2, y), arrowstyle="-|>",
                                 mutation_scale=11, linewidth=linewidth,
                                 color=color, shrinkA=0, shrinkB=0, zorder=2))


def bracket(x1, x2, label):
    top = 3.18
    ax.plot([x1, x1, x2, x2], [top - .12, top, top, top - .12],
            color=muted, lw=0.9)
    ax.text((x1 + x2) / 2, top + .12, label, ha="center", va="bottom",
            fontsize=11, color=ink)


for x in (0.13, 9.92):
    ax.add_patch(Rectangle((x, y - .31), 1.12, .62, facecolor="white",
                           edgecolor=ink, linewidth=1.0))
    ax.text(x + .56, y, "Depot", ha="center", va="center", fontsize=10.5, color=ink)

positions = [1.25, *stations.values(), 9.92]
for left, right in zip(positions, positions[1:]):
    if left == stations["6"]:
        continue
    arrow(left + .11, right - .11, ink, 1.15)
arrow(stations["6"] + .11, 7.49)
ax.text(7.72, y, r"$\cdots$", fontsize=17, ha="center", va="center", color=ink)
arrow(7.96, stations["7"] - .11)

for label, x in stations.items():
    ax.add_patch(Rectangle((x - .075, y - .075), .15, .15,
                           facecolor="white", edgecolor=shared if label == "4" else ink,
                           linewidth=1.2, zorder=3))
    ax.text(x, y + .18, label, ha="center", va="bottom", fontsize=11,
            color=shared if label == "4" else ink)

bracket(stations["1"], stations["3"], "Trip 1")
bracket(stations["4"], stations["6"], "Trip 2")
ax.text(7.80, 3.29, "Further trips", ha="center", va="bottom", fontsize=9.5, color=muted)
ax.text(.15, 2.61, "Bus 1 duty", ha="left", va="center", fontsize=10, color=ink)
ax.text(4.45, 2.43, "Transfer link", ha="center", va="bottom", fontsize=9.5, color=ink)
ax.text(1.59, 1.60, "Departure", ha="center", va="top", fontsize=9, color=muted)
ax.text(9.38, 1.75, "Return", ha="center", va="top", fontsize=9, color=muted)
ax.text(10.48, 1.58, "Overnight charging", ha="center", va="top", fontsize=9, color=muted)

# Highlight one complete eligible physical link; a facility decision determines use.
ax.plot([2.96, 3.64], [1.925, 1.925], color=accent, lw=3.1, solid_capstyle="butt")
ax.text(3.30, 1.80, "Selected DWC link", ha="center", va="top",
        fontsize=9.4, color=accent)

# A second duty visits the same physical station 4 and accesses its chargers.
other_y = .60
ax.text(2.82, other_y, "Bus 2 duty", ha="center", va="center", fontsize=10, color=ink)
for start, end in [((3.55, other_y), (5.04, other_y)),
                   ((5.26, other_y), (6.25, other_y))]:
    ax.add_patch(FancyArrowPatch(start, end, arrowstyle="-|>", mutation_scale=11,
                                 color=ink, linewidth=1.15))
ax.add_patch(Rectangle((5.075, other_y - .075), .15, .15, facecolor="white",
                       edgecolor=shared, linewidth=1.2, zorder=3))
ax.text(5.15, other_y - .16, "4", ha="center", va="top", fontsize=11, color=shared)
ax.add_patch(Rectangle((4.05, 1.03), 2.20, .59, facecolor="white",
                       edgecolor=ink, linewidth=.9))
ax.text(5.15, 1.325, "Plug-in chargers", ha="center", va="center",
        fontsize=9.2, color=ink)
for start, end in [((5.15, 1.96), (5.15, 1.63)),
                   ((5.15, .69), (5.15, 1.02))]:
    ax.add_patch(FancyArrowPatch(start, end, arrowstyle="-", color=muted,
                                 linewidth=.9, linestyle=(0, (2, 2))))

fig.subplots_adjust(left=.02, right=.98, top=.97, bottom=.04)
out = Path(__file__).with_suffix("")
fig.savefig(out.with_suffix(".pdf"), facecolor="white")
fig.savefig(out.with_suffix(".png"), dpi=200, facecolor="white")
