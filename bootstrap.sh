#!/usr/bin/env bash

# Simple bootstrappping script designed to emulate cpk's dependency installer behaviour.
# Requires wget, git and a C compiler.

# It will temporarily install dependencies and produce a working executable of cpk, 
# which you can use to set up dependencies properly and subsequently compile and use cpk 
# using the more normal procedure.

set -euo pipefail

TOML_FILE="cpk.toml"                  # TOML source for dependencies
BOOT_DIR="${BOOT_DIR:-.cpkbootstrap}" # Temporary directory for dependencies

if [ -n "${CC:-}" ]; then
  :
elif command -v clang >/dev/null 2>&1; then
  CC=clang
elif command -v gcc >/dev/null 2>&1; then
  CC=gcc
elif command -v cc >/dev/null 2>&1; then
  CC=cc
else
  read -r -p "No C compiler command found (clang/gcc/cc). Enter compiler command or path (e.g., cc or clang): " CC
fi

mkdir -p "$BOOT_DIR"

#!/usr/bin/env bash

# Enter [dependencies] section and collect lines like: name = "spec"
in_deps=0
> /tmp/cpk_deps.$$   # temp file for parsed deps (bash-only logic)

while IFS= read -r line; do
  # Strip leading whitespace
  s="${line#"${line%%[![:space:]]*}"}"
  # Section start
  if [[ "$s" == "[dependencies]" ]]; then
    in_deps=1
    continue
  fi
  # Section end
  if [[ $in_deps -eq 1 && "$s" == \[*\] ]]; then
    break
  fi
  # Parse dependency assignments inside [dependencies]
  # Expected: name = "SPEC"
  if [[ $in_deps -eq 1 && "$s" == *"="* ]]; then
    # remove comments (simple): cut at first #
    s="${s%%#*}"
    # remove spaces around '=' loosely by pattern matching
    # We'll extract: NAME (left of =) and SPEC (inside quotes right of =)
    if [[ "$s" =~ ^([A-Za-z0-9_.-]+)[[:space:]]*=[[:space:]]*\"([^\"]+)\"[[:space:]]*$ ]]; then
      name="${BASH_REMATCH[1]}"
      spec="${BASH_REMATCH[2]}"
      printf '%s\t%s\n' "$name" "$spec" >> /tmp/cpk_deps.$$
    fi
  fi
done < "$TOML_FILE"
# Install each dependency
while IFS=$'\t' read -r NAME SPEC; do
  DEP_PATH="$BOOT_DIR/$NAME"
  echo "Temporarily installing $NAME"
  case "$SPEC" in
    git:*::*)
      # git:<url>::<commit>
      URL="${SPEC#git:}"
      URL="${URL%%::*}"
      REST="${SPEC#git:}"       # "<url>::<commit>"
      URL="${REST%%::*}"        # "<url>"
      COMMIT="${REST#*::}"      # "<commit>"
      rm -rf "$DEP_PATH"
      git clone --quiet "$URL" "$DEP_PATH"
      (cd "$DEP_PATH" && git checkout --quiet "$COMMIT")
      ;;

    git:*)
      # git:<url> (no pin)
      URL="${SPEC#git:}"
      rm -rf "$DEP_PATH"
      git clone --quiet "$URL" "$DEP_PATH"
      ;;

    web:*)
      URL="${SPEC#web:}"
      mkdir -p "$DEP_PATH"
      FILE="$(basename "$URL")"
      wget -q -O "$DEP_PATH/$FILE" "$URL"
      ;;

    zip:*)
      URL="${SPEC#zip:}"
      rm -rf "$DEP_PATH"
      mkdir -p "$DEP_PATH"
      ZIP_FILE="$DEP_PATH/$NAME.zip"
      wget -q -O "$ZIP_FILE" "$URL"
      unzip -q "$ZIP_FILE" -d "$DEP_PATH"
      rm -f "$ZIP_FILE"
      ;;

    file:*)
      TARGET="${SPEC#file:}"
      rm -rf "$DEP_PATH"
      ln -s "$TARGET" "$DEP_PATH"
      ;;

    *)
      echo "Unknown dependency spec for '$NAME': '$SPEC'"
      exit 1
      ;;
  esac
done < /tmp/cpk_deps.$$

rm -f /tmp/cpk_deps.$$

echo "Compiling cpk to ./cpk-bootstrapped"
$CC src/main.c -w -o cpk-bootstrapped -I"$BOOT_DIR"
echo "Done. Cleaning up $BOOT_DIR"
rm -rf "$BOOT_DIR"