"""Render numerical results with two-row case/metric headers."""

import datetime
import html
import json
import math
import os

from verify import PACKAGE, load


ALGORITHMS = [("ef", "EF"), ("natural-ils", "Natural"),
              ("lifted-ils", "Lifted"), ("lifted-two-phase", "L2P"),
              ("full", "Full")]


def number(value):
    if value == "":
        return "未记录"
    numeric = float(value)
    if math.isinf(numeric):
        return "∞" if numeric > 0 else "−∞"
    return f"{numeric:.3f}"


def publish(path, content):
    temporary = path.with_name(path.name + ".tmp." + str(os.getpid()))
    temporary.write_text(content, encoding="utf-8")
    os.replace(temporary, path)


def render(plan, state):
    records = []
    for original in plan["records"]:
        record = dict(original)
        job_state = state["jobs"][record["run_id"]]
        if job_state == "COMPLETE":
            result = load(PACKAGE / "runs" / record["run_id"] / "result.json")
            record.update(status=result["status"], ub=result["summary"]["ub"],
                          lb=result["summary"]["lb"],
                          actual_wall_seconds=result["actual_wall_seconds"],
                          bound_source=result["summary"]["bound_source"])
        else:
            status = job_state
            if job_state in ("LAUNCHING", "RUNNING") and state["state"] != "RUNNING":
                status = "INTERRUPTED_EXIT_UNRECORDED"
            record.update(status=status, ub="", lb="", actual_wall_seconds=None,
                          bound_source="UNRECORDED")
        records.append(record)

    def cells(record):
        status = record["status"]
        if record["actual_wall_seconds"] is None:
            text = {"PENDING": "待运行", "RUNNING": "运行中", "LAUNCHING": "启动中"}.get(status, status)
            return f'<td>—</td><td>—</td><td class="status">{html.escape(text)}</td>'
        short = {"TIME_LIMIT": "TL", "MEMORY_LIMIT_EXTERNAL": "ML",
                 "MEMORY_LIMIT_NATIVE": "ML", "OPTIMAL": ""}.get(status, status)
        elapsed = f'{record["actual_wall_seconds"]:.3f}'
        if short:
            elapsed += " (" + short + ")"
        return "".join("<td>" + html.escape(text) + "</td>" for text in
                       [number(record["lb"]), number(record["ub"]), elapsed])

    def matrix(title, cases, rows, group, use_vi=False):
        parts = [f"<h3>{html.escape(title)}</h3><div class=scroll><table><thead><tr>",
                 '<th rowspan="2">算法</th>']
        parts.extend(f'<th colspan="3">{html.escape(case)}</th>' for case in cases)
        parts.append("</tr><tr>" + "<th>LB</th><th>UB</th><th>actualTime(s)</th>" * len(cases)
                     + "</tr></thead><tbody>")
        for key, label in rows:
            parts.append("<tr><th>" + html.escape(label) + "</th>")
            for case in cases:
                matches = [record for record in records if record["group"] == group
                           and record["case_id"] == case
                           and (record["vi"] == key if use_vi else record["algorithm"] == key)]
                if len(matches) != 1:
                    raise ValueError(f"Missing or duplicate report cell: {group}/{case}/{key}")
                parts.append(cells(matches[0]))
            parts.append("</tr>")
        parts.append("</tbody></table></div>")
        return "".join(parts)

    generated = datetime.datetime.now(datetime.timezone.utc).isoformat()
    parts = ['<!doctype html><html lang="zh-CN"><meta charset="utf-8">',
             '<title>最终实验数值</title><style>body{font:15px system-ui;margin:32px;color:#17212e}',
             'table{border-collapse:collapse;min-width:750px}th,td{padding:9px 12px;',
             'border:1px solid #b9c4ce;text-align:right}th{background:#edf2f6}',
             'h2{margin-top:32px}.scroll{overflow-x:auto}.status{color:#6b5369}',
             '</style><h1>最终实验数值</h1>',
             f'<p>更新时间：{generated}；已完成 {state["completed_result_records"]}/65 条结果，',
             f'{len(state["completed_runs"])}/58 次独立运行。</p>',
             '<p>TL：限时；ML：内存限制；未记录：日志没有该数值；—：尚无最终结果。</p>',
             '<h2>1. 主表</h2>']
    for scale in (3, 12, 28):
        cases = [f"R{scale}-N5-F{f}-C4" for f in (10, 20, 50)]
        parts.append(matrix(f"R{scale} / N5 / C4", cases, ALGORITHMS, "main_table"))
    parts.extend(['<h2>2. VI 消融</h2>', matrix("VI", ["R12-N5-F50-C4"],
                  [(1, "Lifted + VI"), (0, "Lifted − VI")], "vi_validation", True),
                  '<h2>3. R28 Full/L2P</h2>'])
    comparison = [record for record in records if record["group"] == "r28_full_l2p_comparison"]
    sections = list(dict.fromkeys(record["result_group"] for record in comparison))
    for section in sections:
        cases = list(dict.fromkeys(record["case_id"] for record in comparison
                                  if record["result_group"] == section))
        parts.append(matrix(" / ".join(cases), cases, ALGORITHMS[-2:],
                            "r28_full_l2p_comparison"))
    parts.append("</html>\n")
    publish(PACKAGE / "results.html", "".join(parts))
    publish(PACKAGE / "results.json", json.dumps({"generated_utc": generated,
            "records": records}, ensure_ascii=False, indent=2) + "\n")


if __name__ == "__main__":
    render(load(PACKAGE / "runs.json"), load(PACKAGE / "status.json"))
