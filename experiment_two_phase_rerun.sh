#!/bin/bash
set -uo pipefail

EXE="./out/wireless.exe"

# Re-run only the algorithms affected by the Phase 1 route-theta to Phase 2
# scenario-theta linking change.
TABLE_ALGS=(
  "full:wireless_reform:Full"
)

TABLE_A_DATA=(
  "tower_small:S-3"
  "tower_medium:M-3"
  "tower:B-3"
  "tower_large:L-3"
)

TABLE_B_DATA=(
  "tower_5:B-5"
  "tower_10:B-10"
  "tower_20:B-20"
  "tower_40:B-40"
)

CAPACITY_ALGS=(
  "full:wireless_reform:Full"
)

CAPACITY_DATA=(
  "tower_10_cap6:B-10-C6"
  "tower_10_cap5:B-10-C5"
  "tower_10_cap4:B-10-C4"
  "tower_10_cap3:B-10-C3"
  "tower_10_cap2:B-10-C2"
  "tower_10_cap1:B-10-C1"
)

latest_log() {
  local alg="$1"
  local pb="$2"
  local data_name="$3"
  ls -t "logs/${data_name}/${alg}_${pb}_"*.log 2>/dev/null | head -n 1
}

log_has_success() {
  local log_path="$1"
  grep -Eq "converged\.|final elapsed time|Optimal solution found" "$log_path"
}

log_has_phase2_incumbent() {
  local log_path="$1"
  grep -Eq "Phase 2.*UB=-?[0-9]|Phase 2 converged\.|Active master variables|Q\\(z\\):" "$log_path"
}

run_one() {
  local group_name="$1"
  local alg="$2"
  local pb="$3"
  local data_name="$4"
  local instance_name="$5"
  local accept_phase2_incumbent="$6"
  local log_before
  local log_after
  local exit_code

  echo "[$(date '+%H:%M:%S')] RUN ${group_name} ${instance_name} ${alg} --pb ${pb} --data ${data_name}"
  log_before="$(latest_log "$alg" "$pb" "$data_name")"

  "$EXE" --alg "$alg" --pb "$pb" --data "$data_name"
  exit_code=$?
  log_after="$(latest_log "$alg" "$pb" "$data_name")"

  if [ -n "$log_after" ] && [ "$log_after" != "$log_before" ] && log_has_success "$log_after"; then
    echo "[$(date '+%H:%M:%S')] DONE ${group_name} ${instance_name} ${alg} (log completed: ${log_after}, exit_code=${exit_code})"
    return 0
  fi

  if [ "$accept_phase2_incumbent" = "1" ] && [ -n "$log_after" ] && [ "$log_after" != "$log_before" ] && log_has_phase2_incumbent "$log_after"; then
    echo "[$(date '+%H:%M:%S')] ACCEPT ${group_name} ${instance_name} ${alg} (Phase 2 incumbent found: ${log_after}, exit_code=${exit_code})"
    return 0
  fi

  echo "[$(date '+%H:%M:%S')] STOP ${group_name} ${instance_name} ${alg} exit_code=${exit_code}"
  return "$exit_code"
}

run_table() {
  local group_name="$1"
  shift
  local data_entries=("$@")
  local alg_entry
  local alg
  local pb
  local method_name
  local data_entry
  local data_name
  local instance_name
  local stopped

  echo ""
  echo "===== ${group_name}: affected two-phase algorithms ====="

  for alg_entry in "${TABLE_ALGS[@]}"; do
    IFS=':' read -r alg pb method_name <<< "$alg_entry"
    stopped=0

    echo ""
    echo "--- ${group_name} method: ${method_name} (${alg}, ${pb}) ---"

    for data_entry in "${data_entries[@]}"; do
      IFS=':' read -r data_name instance_name <<< "$data_entry"

      if [ "$stopped" = "1" ]; then
        echo "[$(date '+%H:%M:%S')] SKIP ${group_name} ${instance_name} ${alg} (previous smaller case failed)"
        continue
      fi

      if ! run_one "$group_name" "$alg" "$pb" "$data_name" "$instance_name" "0"; then
        stopped=1
      fi
    done
  done
}

run_capacity() {
  local alg_entry
  local alg
  local pb
  local method_name
  local data_entry
  local data_name
  local instance_name
  local stopped

  echo ""
  echo "===== Capacity: full only ====="

  for alg_entry in "${CAPACITY_ALGS[@]}"; do
    IFS=':' read -r alg pb method_name <<< "$alg_entry"
    stopped=0

    echo ""
    echo "--- Capacity method: ${method_name} (${alg}, ${pb}) ---"

    for data_entry in "${CAPACITY_DATA[@]}"; do
      IFS=':' read -r data_name instance_name <<< "$data_entry"

      if [ "$stopped" = "1" ]; then
        echo "[$(date '+%H:%M:%S')] SKIP Capacity ${instance_name} ${alg} (previous looser capacity failed)"
        continue
      fi

      if ! run_one "Capacity" "$alg" "$pb" "$data_name" "$instance_name" "1"; then
        stopped=1
      fi
    done
  done
}

echo "========== Two-Phase Rerun Start: $(date) =========="
echo "Executable: ${EXE}"
echo "Outer timeout: none; solver internal time limit controls each run."
echo "Config: see solverCfg.txt; detailed logs are written under logs/<data>/"

# run_table "TableA" "${TABLE_A_DATA[@]}"
# run_table "TableB" "${TABLE_B_DATA[@]}"
# run_capacity
run_one "Capacity" "full" "wireless_reform" "tower_10_cap2" "B-10-C2" "1"
run_one "Extra" "lifted-two-phase" "wireless_reform" "tower_5" "B-5" "0"
run_one "Extra2" "full" "wireless_reform" "tower_large" "L-3" "0"

echo ""
echo "========== Two-Phase Rerun End: $(date) =========="
