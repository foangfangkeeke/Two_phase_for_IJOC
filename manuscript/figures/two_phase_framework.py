"""Generate the two-phase solution framework for Chapter 4."""

from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import FancyArrowPatch, FancyBboxPatch, Rectangle


plt.rcParams.update({
    "font.family": "Times New Roman",
    "mathtext.fontset": "custom",
    "mathtext.rm": "Times New Roman",
    "mathtext.it": "Times New Roman:italic",
    "mathtext.bf": "Times New Roman:bold",
    "mathtext.cal": "cmsy10",
    "mathtext.fallback": "stix",
    "svg.fonttype": "none",
    "font.size": 10,
})

INK = "#20262D"
BLUE = "#365C79"
GRAY = "#626B73"
BORDER = "#A9B2BA"

fig, ax = plt.subplots(figsize=(10, 6.4))
ax.set_xlim(0, 12)
ax.set_ylim(0, 8.1)
ax.axis("off")


def label(x, y, text, size=10, color=INK, weight="normal", **kwargs):
    return ax.text(x, y, text, ha="center", va="center", fontsize=size,
                   color=color, fontweight=weight, **kwargs)


def frame(x, y, width, height, fill="white", edge=BORDER, dashed=False):
    patch = FancyBboxPatch(
        (x, y), width, height, boxstyle="round,pad=0,rounding_size=0.11",
        linewidth=0.9, edgecolor=edge, facecolor=fill,
        linestyle=(0, (4, 3)) if dashed else "solid", zorder=1,
    )
    ax.add_patch(patch)
    return patch


def node(cx, y, title, formula, width=3.5, height=0.9, highlighted=False):
    x = cx - width / 2
    ax.add_patch(Rectangle((x, y), width, height,
                          linewidth=0.85, edgecolor=INK,
                          facecolor="#F1F5F8" if highlighted else "white", zorder=3))
    label(cx, y + height * 0.72, title, size=10.3, weight="bold", zorder=4)
    label(cx, y + height * 0.28, formula, size=11, zorder=4)


def arrow(points, dashed=False, color=INK, width=0.9):
    for index, (start, end) in enumerate(zip(points, points[1:])):
        ax.add_patch(FancyArrowPatch(
            start, end, arrowstyle="-|>" if index == len(points) - 2 else "-",
            mutation_scale=10, linewidth=width, color=color,
            linestyle=(0, (3, 2)) if dashed else "solid",
            shrinkA=0, shrinkB=0, zorder=2,
        ))


# Equal phase widths and aligned master/scenario levels.
for left, cx, phase, subtitle, master_variables in [
    (0.15, 2.7, "Phase 1", "Coupling relaxation and route decomposition",
     r"$w,\ z_\omega,\ \widehat{\theta}_{\omega,r}$"),
    (6.75, 9.3, "Phase 2", "Scenario verification",
     r"$w,\ z_\omega,\ \theta_\omega,\ \widehat{\theta}_{\omega,r}$"),
]:
    frame(left, 0.65, 5.1, 7.25, edge=BLUE, dashed=True)
    label(cx, 7.56, phase, size=14, color=BLUE, weight="bold")
    label(cx, 7.20, subtitle, size=10, color=GRAY)
    master_title = "Master" if phase == "Phase 1" else "Extended master"
    node(cx, 5.97, master_title, master_variables, width=3.8, height=0.92)
    arrow([(cx, 5.97), (cx, 4.93)])
    label(cx + 0.68, 5.48, r"Fix $w,z_\omega$", size=9, color=GRAY)
    frame(cx - 1.98, 1.03, 3.96, 3.90)
    label(cx, 4.60, r"Scenario $\omega$", size=10.5, weight="bold")

# Phase 1: compact route blocks, with the route index visible in y.
node(2.7, 3.27, r"Route LP 1: $\widehat{Q}_{\omega,1}$", r"$y_{\omega,1}$", width=2.95, height=0.87)
node(2.7, 2.16, r"Route LP 2: $\widehat{Q}_{\omega,2}$", r"$y_{\omega,2}$", width=2.95, height=0.87)
label(2.7, 1.86, r"$\vdots$", size=13)
label(2.7, 1.42, "Shared capacity removed", size=9.2, color=GRAY)
label(2.7, 0.84, r"Occupancy variables $x$ eliminated", size=9.1, color=GRAY)
arrow([(1.225, 3.70), (0.47, 3.70), (0.47, 6.43), (0.80, 6.43)], dashed=True)
ax.plot([1.225, 0.47, 0.47], [2.595, 2.595, 3.70],
        color=INK, linewidth=0.9, linestyle=(0, (3, 2)), zorder=2)
label(0.65, 5.15, "Route cuts", size=8.7, color=GRAY, rotation=90)

# Phase 2: the LP relaxation and exact evaluation belong to the same scenario.
node(9.3, 3.27, r"Scenario LP (RSP): $Q_\omega^{\mathrm{LP}}$", r"$0\leq x_\omega\leq1,\ y_\omega$",
     width=2.95, height=0.87, highlighted=True)
node(9.3, 1.96, r"Exact SP: $Q_\omega$", r"$x_\omega\in\{0,1\},\ y_\omega$",
     width=2.95, height=0.87)
arrow([(9.3, 3.27), (9.3, 2.83)])
label(10.15, 3.05, "If none proves infeasible", size=8.0, color=GRAY)
label(9.3, 1.42, "Shared capacity retained", size=9.2, color=GRAY)
label(9.3, 0.84, "Exact lifted formulation", size=9.1, color=GRAY)
arrow([(7.825, 3.70), (7.07, 3.70), (7.07, 6.43), (7.40, 6.43)], dashed=True)
ax.plot([7.825, 7.07, 7.07], [2.395, 2.395, 3.70],
        color=INK, linewidth=0.9, linestyle=(0, (3, 2)), zorder=2)
label(7.25, 5.15, "Scenario cuts", size=8.7, color=GRAY, rotation=90)

# Information carried between phases.
arrow([(5.30, 5.32), (6.70, 5.32)], color=BLUE, width=1.2)
label(6.0, 5.69, r"$LB^R$", size=11, color=BLUE)
label(6.0, 4.98, "Route cuts", size=9.0, color=BLUE)

label(6.0, 0.25, "RSP infeasible: return to the master.",
      size=8.9, color=GRAY)

fig.subplots_adjust(left=0.01, right=0.99, bottom=0.01, top=0.99)
out = Path(__file__).with_name("two_phase_framework")
fig.savefig(out.with_suffix(".pdf"), facecolor="white")
fig.savefig(out.with_suffix(".svg"), facecolor="white")
fig.savefig(out.with_suffix(".png"), dpi=190, facecolor="white")
print(out.with_suffix(".png"))
