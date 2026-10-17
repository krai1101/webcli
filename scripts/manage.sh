#!/usr/bin/env bash
set -euo pipefail
operation=$1
executable=$2
root="$HOME/.local/share/webcli"
directory=${executable%/*}
version=${directory##*/}
[[ "$directory" == "$root/"* && "${directory%/*}" == "$root" &&
   "$version" =~ ^v[0-9]+\.[0-9]+\.[0-9]+-(linux-x64|macos-arm64)-[0-9a-f]{12}$ &&
   "${executable##*/}" == webcli && ! -L "$root" && ! -L "$directory" ]] || {
  printf '%s\n' 'This is not a script-managed installation. Use the installation command or remove your extracted ZIP manually.' >&2
  exit 1
}
if [[ "$operation" == update ]]; then
  version=$("$executable" --version)
  exec /bin/bash "${BASH_SOURCE[0]%/*}/install.sh" --current-version "${version#WebCLI }"
fi
[[ "$operation" == uninstall ]] || exit 1
# Inspect every deletion target first; never remove unrelated files or links.
shopt -s nullglob dotglob
targets=("$root"/*)
for target in "${targets[@]}"; do
  name=${target##*/}
  [[ -d "$target" && ! -L "$target" &&
     "$name" =~ ^v[0-9]+\.[0-9]+\.[0-9]+-(linux-x64|macos-arm64)-[0-9a-f]{12}$ ]] || {
    printf '%s\n' 'Unexpected content in installation directory; removal stopped.' >&2
    exit 1
  }
done
link="$HOME/.local/bin/webcli"
if [[ -L "$link" ]]; then
  destination=$(readlink "$link")
  if [[ "$destination" == "$root/"*/webcli ]]; then rm -- "$link"; fi
fi
for target in "${targets[@]}"; do rm -rf -- "$target"; done
rmdir -- "$root"
# ~/.local/bin is shared with other tools; leave its PATH configuration intact.
printf '%s\n' 'WebCLI uninstalled. Saved browsing data is kept.'
