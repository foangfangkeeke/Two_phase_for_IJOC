"""Select the best logged costed incumbent and write a strict plan-audit input.

Usage: python scripts/analysis/prepare_fixed_plan.py solve.log fixed.plan
"""

import hashlib
import json
import re
import sys
from pathlib import Path


def field(line: str, name: str) -> str:
    match = re.search(rf"(?:^|[, ]){re.escape(name)}=([^, ]+)", line)
    if match is None:
        raise ValueError(f"Missing {name}: {line[:160]}")
    return match.group(1)


def main() -> None:
    source = Path(sys.argv[1])
    destination = Path(sys.argv[2])
    content = source.read_bytes()
    lines = content.decode("utf-8", errors="replace").splitlines()
    runtime = next(line for line in lines if line.startswith("Runtime options: "))
    instance = next(line for line in lines if line.startswith("Instance: "))
    master = None
    repair = None
    best = None
    for line in lines:
        if line.startswith("Search master: "):
            record = json.loads(line.split(": ", 1)[1])
            if record.get("has_solution"):
                master = record["design"]
        elif line.startswith("Search incumbent: "):
            repair = json.loads(line.split(": ", 1)[1])["design"]
        elif line.startswith("Incumbent cost distribution: "):
            source_type = field(line, "source")
            design = master if source_type == "master" else repair
            if design is None:
                raise ValueError("Cost distribution has no preceding design")
            total = float(field(line, "total"))
            if best is None or total < best[0]:
                best = (total, design)
    if best is None:
        raise ValueError("No costed incumbent in source log")
    total, design = best
    termination = [line for line in lines if line.startswith("Algorithm termination: ")]
    if len(termination) != 1 or abs(total - float(field(termination[0], "UB"))) > 0.001:
        raise ValueError("Costed incumbent does not reconcile with final original UB")
    batteries = design["batteries"]
    count = int(field(runtime, "scenario_count"))
    if len(batteries) != count or any(len(row) != len(batteries[0]) for row in batteries):
        raise ValueError("Incomplete scenario battery design")
    if any(capacity is None for row in batteries for capacity in row):
        raise ValueError("Null battery in incumbent design")
    output = [
        f"source_log_sha256 {hashlib.sha256(content).hexdigest()}",
        f"source_ub {total:.6f}",
        f"data {field(instance, 'data')}",
        f"scenarios {count}",
        f"capacity {field(runtime, 'cap')}",
        f"candidate_count {field(runtime, 'dwc_top_n')}",
        f"battery_step {field(runtime, 'battery_step')}",
        f"wireless {field(runtime, 'wireless_option')}",
    ]
    output.extend(f"facility {name}" for name in design["facilities"])
    output.extend(
        f"battery {scenario} {route} {capacity}"
        for scenario, row in enumerate(batteries)
        for route, capacity in enumerate(row)
    )
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(output) + "\n", encoding="utf-8")
    print(f"source_ub={total:.6f} facilities={len(design['facilities'])} "
          f"batteries={sum(map(len, batteries))} plan={destination}")


if __name__ == "__main__":
    main()
