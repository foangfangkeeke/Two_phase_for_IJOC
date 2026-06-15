"""Trim tower_data scenarios to keep only the first N EBs."""
import sys
import os
import shutil

def trim_comma_line(line: str, n: int) -> str:
    """Trim a comma-separated line to first n values."""
    line = line.rstrip('\n\r')
    values = [v.strip() for v in line.split(',')]
    return ','.join(values[:n])

def trim_scenario_file(src_path: str, dst_path: str, n: int):
    """Read a scenarioMiscCfg.txt, trim all per-EB arrays to n, write back."""
    with open(src_path, 'r', encoding='utf-8') as f:
        lines = f.readlines()

    # Find the original numEB
    old_n = None
    for i, line in enumerate(lines):
        if line.strip() == '# numEB':
            old_n = int(lines[i + 1].strip())
            break

    if old_n is None:
        print(f"  ERROR: could not find numEB in {src_path}")
        return False

    print(f"  original numEB={old_n}, trimming to {n}")

    # Build new file line by line
    new_lines = []
    i = 0
    trip_ebs_start = None
    trip_ebs_end = None
    in_trip_ebs = False

    while i < len(lines):
        line = lines[i]

        # NetworkCfg section
        if line.strip() == '# startDistanceEBs':
            new_lines.append(line)
            new_lines.append(trim_comma_line(lines[i + 1], n) + '\n')
            i += 2
            continue
        elif line.strip() == '# endDistanceEBs':
            new_lines.append(line)
            new_lines.append(trim_comma_line(lines[i + 1], n) + '\n')
            i += 2
            continue

        # SchedulingCfg section
        elif line.strip() == '# numEB':
            new_lines.append(line)
            new_lines.append(f'{n}\n')
            i += 2
            continue
        elif line.strip() == '# EBs':
            new_lines.append(line)
            new_lines.append(trim_comma_line(lines[i + 1], n) + '\n')
            i += 2
            continue
        elif line.strip() == '# tripNumEBs':
            new_lines.append(line)
            new_lines.append(trim_comma_line(lines[i + 1], n) + '\n')
            i += 2
            continue
        elif line.strip() == '# tripEBs':
            new_lines.append(line)
            i += 1
            # Keep first n lines of tripEBs
            for j in range(n):
                new_lines.append(lines[i + j])
            i += old_n  # skip old_n trip lines
            continue

        # RouteCfg section
        elif line.strip() == '# routeIdByEBs':
            new_lines.append(line)
            new_lines.append(trim_comma_line(lines[i + 1], n) + '\n')
            i += 2
            continue

        new_lines.append(line)
        i += 1

    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, 'w', encoding='utf-8') as f:
        f.writelines(new_lines)
    return True


def main():
    n = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    src_root = os.path.join(os.path.dirname(__file__), '..', 'data', 'tower_data')
    dst_root = os.path.join(os.path.dirname(__file__), '..', 'data', f'tower{n}_data')

    print(f"Creating tower{n}_data with {n} EBs per scenario...")

    # Clear destination
    if os.path.exists(dst_root):
        shutil.rmtree(dst_root)

    # Copy everything
    shutil.copytree(src_root, dst_root)
    print(f"  copied {src_root} -> {dst_root}")

    # Also copy scenario config
    shutil.copy2(
        os.path.join(src_root, 'scenarioCfg.txt'),
        os.path.join(dst_root, 'scenarioCfg.txt'))

    # Trim each scenario directory
    for entry in os.listdir(dst_root):
        entry_path = os.path.join(dst_root, entry)
        if os.path.isdir(entry_path):
            misc_file = os.path.join(entry_path, 'scenarioMiscCfg.txt')
            if os.path.exists(misc_file):
                print(f"  trimming {entry}...")
                if not trim_scenario_file(misc_file, misc_file, n):
                    print(f"  FAILED to trim {entry}")
                    return 1

    print(f"Done! Created {dst_root}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
