import matplotlib.pyplot as plt
from matplotlib.patches import Rectangle, FancyArrowPatch
from pathlib import Path

plt.rcParams.update({
    "font.family": "Times New Roman",
    "mathtext.fontset": "custom",
    "mathtext.rm": "Times New Roman",
    "mathtext.it": "Times New Roman:italic",
    "mathtext.bf": "Times New Roman:bold",
    "mathtext.cal": "cmsy10",
    "mathtext.fallback": "stix",
    "svg.fonttype": "none",
    "pdf.fonttype": 42,
})
fig, ax = plt.subplots(figsize=(12, 4.7))
ax.set_xlim(0, 12)
ax.set_ylim(0, 4.7)
ax.axis("off")

def box(x, y, width, height, dashed=False):
    ax.add_patch(Rectangle((x, y), width, height, facecolor="white", edgecolor="black", linewidth=0.85, linestyle=(0, (4, 3)) if dashed else "solid"))

def line(points, arrow=False):
    for start, end in zip(points, points[1:]):
        ax.add_patch(FancyArrowPatch(start, end, arrowstyle="-|>" if arrow and end == points[-1] else "-", mutation_scale=10, color="black", linewidth=0.85, shrinkA=0, shrinkB=0))

ax.text(1.25, 4.42, "First-stage decision", ha="center", fontsize=11, fontweight="bold")
ax.text(5.05, 4.42, "Possible service plans", ha="center", fontsize=11, fontweight="bold")
ax.text(9.45, 4.42, "Second-stage decisions", ha="center", fontsize=11, fontweight="bold")
ax.text(1.25, 4.13, "Before the service plan is known", ha="center", fontsize=8.5)
ax.text(9.45, 4.13, "Conditional on the realized scenario", ha="center", fontsize=8.5)

box(0.05, 1.83, 2.40, 1.06)
ax.text(1.25, 2.48, "DWC installation", ha="center", fontsize=10.5, fontweight="bold")
ax.text(1.25, 2.10, "Facility layout", ha="center", fontsize=10)

ys = [3.30, 2.18, 0.83]
labels = ["Service plan 1", "Service plan 2", "Other service plans"]
for y, label in zip(ys, labels):
    box(3.95, y - .34, 2.20, .68, dashed=True)
    ax.text(5.05, y, label, ha="center", va="center", fontsize=10)
    box(7.70, y - .37, 3.50, .74)
    first = "Separate battery and charging plans" if label == "Other service plans" else "Route-level battery configuration"
    second = "for each service plan" if label == "Other service plans" else "Representative-day charging plan"
    ax.text(9.45, y + .09, first, ha="center", fontsize=9.4)
    ax.text(9.45, y - .21, second, ha="center", fontsize=9.4)
    line([(6.15, y), (7.70, y)], arrow=True)

line([(1.25, 1.83), (1.25, 0.23), (11.60, 0.23), (11.60, 3.30)])
for y in ys:
    line([(11.60, y), (11.20, y)], arrow=True)
ax.text(6.35, .30, "Same facility layout in every scenario", ha="center", fontsize=8.6, bbox={"facecolor":"white", "edgecolor":"none", "pad":1})
ax.text(5.05, 3.91, "Alternatives for one service planning period", ha="center", fontsize=8.5)
fig.subplots_adjust(left=.025, right=.985, top=.98, bottom=.06)
out = Path(__file__).with_suffix("")
fig.savefig(out.with_suffix(".svg"), facecolor="white")
fig.savefig(out.with_suffix(".pdf"), facecolor="white")
