"""Draw the illustrative mapping from service events to a physical link."""

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
out = Path(__file__).resolve().parent
fig, ax = plt.subplots(figsize=(8.4, 4.6))
ax.set_xlim(-1.55, 7.7)
ax.set_ylim(-0.85, 4.9)
ax.axis("off")
x1, x2 = 2.0, 5.05
ys = [3.8, 2.85, 1.9]
ax.text(-1.2, 4.6, "Service network", fontsize=12, fontweight="bold")
ax.text(-1.2, 0.7, "Physical network", fontsize=12, fontweight="bold", va="center")
for x in (x1, x2):
    ax.plot([x, x], [0.85, 4.02], color="0.65", lw=1.0,
            linestyle=(0, (3, 3)), zorder=0)
for k, (y, label) in enumerate(zip(ys, ["Bus 1, traversal 1", "Bus 1, traversal 2", "Bus 2, traversal 1"]), 1):
    ax.text(-1.2, y, label, fontsize=10.5, va="center")
    for x, name in ((x1, rf"$i^\prime_{{{k}}}$"), (x2, rf"$j^\prime_{{{k}}}$")):
        ax.add_patch(Rectangle((x - 0.10, y - 0.10), 0.20, 0.20,
                               facecolor="white", edgecolor="black", lw=1.2, zorder=3))
        ax.text(x, y + 0.16, name, fontsize=11, ha="center", va="bottom")
    ax.add_patch(FancyArrowPatch((x1 + 0.14, y), (x2 - 0.14, y),
                                arrowstyle="-|>", mutation_scale=12, lw=1.2, color="black"))
    ax.text((x1 + x2) / 2, y + 0.12, rf"$a_{k}$", fontsize=11, ha="center")
ax.text(5.75, 3.12, r"$i^\prime_k,j^\prime_k\in\mathcal{V}$" + "\n" +
        r"$a_k\in\mathcal{A}$", fontsize=10.5, va="center", linespacing=1.8)
for x, name in ((x1, r"$i$"), (x2, r"$j$")):
    ax.add_patch(Rectangle((x - 0.12, 0.58), 0.24, 0.24,
                           facecolor="black", edgecolor="black", zorder=3))
    ax.text(x, 0.48, name, fontsize=12, ha="center", va="top")
ax.add_patch(FancyArrowPatch((x1 + 0.16, 0.70), (x2 - 0.16, 0.70),
                            arrowstyle="-|>", mutation_scale=13, lw=1.8, color="black"))
ax.text((x1 + x2) / 2, 0.86, r"$l=(i,j)\in\mathcal{L}$",
        fontsize=11, ha="center")
ax.text(2.0, -0.54,
        r"$s(i^\prime_k)=i,\quad s(j^\prime_k)=j,\quad \ell(a_k)=l\quad(k=1,2,3)$",
        fontsize=10.5)
fig.tight_layout(pad=0.35)
fig.savefig(out / "physical_service_mapping.pdf", bbox_inches="tight", facecolor="white")
fig.savefig(out / "physical_service_mapping.png", dpi=200, bbox_inches="tight", facecolor="white")
