#!/bin/bash
set -e

EXE="./out/Release/wireless.exe"

# algorithm config: name,problem_type
ALGS=(
  "full:wireless_reform"
  "lifted-two-phase:wireless_reform"
  "lifted-ils:wireless_reform"
  "natural-ils:wireless"
  "solver:wireless"
)

# Experiment 1: fixed scenario count, increasing network scale.
EXP1_DS=("tower_small" "tower_medium" "tower")

# Experiment 2: fixed network, increasing scenario count.
EXP2_DS=("tower_5" "tower_10" "tower_20" "tower_40")

declare -A EXP1_STOPPED
declare -A EXP1_TOWER_DONE
declare -A EXP2_STOPPED

run_one() {
  local alg="$1" pb="$2" ds="$3"

  echo "[$(date '+%H:%M:%S')] RUN $alg --pb $pb --data $ds"
  "$EXE" --alg "$alg" --pb "$pb" --data "$ds"
  local exit_code=$?

  if [ "$exit_code" -eq 0 ]; then
    echo "[$(date '+%H:%M:%S')] DONE $alg --data $ds"
    return 0
  fi

  echo "[$(date '+%H:%M:%S')] STOP $alg --data $ds exit_code=$exit_code"
  return "$exit_code"
}

echo "========== Experiment Start: $(date) =========="
echo "Config: see solverCfg.txt; detailed logs are written under logs/<data>/"

echo ""
echo "===== Experiment 1: ${#ALGS[@]} algs x ${#EXP1_DS[@]} datasets ====="
for alg_entry in "${ALGS[@]}"; do
  alg="${alg_entry%%:*}"
  pb="${alg_entry##*:}"

  for ds in "${EXP1_DS[@]}"; do
    if [ "${EXP1_STOPPED[$alg]}" = "1" ]; then
      echo "[$(date '+%H:%M:%S')] SKIP $alg on $ds (previous run stopped)"
      continue
    fi
    if run_one "$alg" "$pb" "$ds"; then
      if [ "$ds" = "tower" ]; then
        EXP1_TOWER_DONE[$alg]=1
      fi
    else
      EXP1_STOPPED[$alg]=1
    fi
  done
done

echo ""
echo "===== Experiment 2: ${#ALGS[@]} algs x ${#EXP2_DS[@]} datasets ====="
for alg_entry in "${ALGS[@]}"; do
  alg="${alg_entry%%:*}"
  pb="${alg_entry##*:}"

  if [ "${EXP1_TOWER_DONE[$alg]}" != "1" ]; then
    echo "[$(date '+%H:%M:%S')] SKIP $alg in Experiment 2 (tower was not completed in Experiment 1)"
    continue
  fi

  for ds in "${EXP2_DS[@]}"; do
    if [ "${EXP2_STOPPED[$alg]}" = "1" ]; then
      echo "[$(date '+%H:%M:%S')] SKIP $alg on $ds (previous run stopped)"
      continue
    fi
    if ! run_one "$alg" "$pb" "$ds"; then
      EXP2_STOPPED[$alg]=1
    fi
  done
done

echo ""
echo "========== Experiment End: $(date) =========="
