#!/usr/bin/env bash
set -euo pipefail

main() {
fail() { printf 'WebCLI: %s\n' "$*" >&2; exit 1; }
current_version=''
if [[ $# -gt 0 ]]; then
  [[ $# -eq 2 && "$1" == --current-version && "$2" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail 'Invalid installer arguments.'
  current_version=$2
fi
for tool in curl unzip mktemp awk grep; do
  command -v "$tool" >/dev/null 2>&1 || fail "Install $tool first."
done

case "$(uname -s):$(uname -m)" in
  Linux:x86_64) platform=linux-x64 ;;
  Darwin:arm64) platform=macos-arm64 ;;
  *) fail "Supported platforms: Linux x64 and macOS Apple Silicon. Use install.ps1 on Windows." ;;
esac
if command -v sha256sum >/dev/null 2>&1; then
  checksum() { sha256sum "$1" | awk '{print $1}'; }
elif command -v shasum >/dev/null 2>&1; then
  checksum() { shasum -a 256 "$1" | awk '{print $1}'; }
else
  fail 'Install sha256sum or shasum first.'
fi

repository=https://github.com/krai1101/webcli
latest=$(curl --fail --silent --show-error --location --proto '=https' \
  --proto-redir '=https' --output /dev/null --write-out '%{url_effective}' "$repository/releases/latest")
tag=${latest##*/}
[[ "$tag" =~ ^v[0-9]+\.[0-9]+\.[0-9]+$ ]] || fail 'No public stable release found.'
if [[ -n "$current_version" ]] && awk -v current="$current_version" -v latest="${tag#v}" 'BEGIN {
  split(current,c,"."); split(latest,l,".");
  for(i=1;i<=3;i++){if(c[i]+0>l[i]+0)exit 0;if(c[i]+0<l[i]+0)exit 1} exit 0
}'; then
  printf 'WebCLI %s is already up to date.\n' "$current_version"
  exit 0
fi
asset="webcli-$tag-$platform.zip"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
printf 'Downloading WebCLI %s (%s)...\n' "$tag" "$platform"
for name in "$asset" "$asset.sha256"; do
  curl --fail --silent --show-error --location --proto '=https' --proto-redir '=https' \
    --output "$work/$name" "$repository/releases/download/$tag/$name"
done
expected=$(awk 'NR == 1 {print $1}' "$work/$asset.sha256")
[[ "$expected" =~ ^[0-9a-f]{64}$ ]] || fail 'Invalid release checksum.'
[[ "$(checksum "$work/$asset")" == "$expected" ]] || fail 'Release checksum does not match.'
unzip -q "$work/$asset" -d "$work/unpacked"
folder="${asset%.zip}"
source="$work/unpacked/$folder"
[[ -f "$source/webcli" ]] || fail 'Release executable is missing.'
chmod +x "$source/webcli"
"$source/webcli" --version || fail 'The release cannot run on this system; check its runtime requirements.'

# Immutable version folders keep running processes and their libraries intact.
root="$HOME/.local/share/webcli"
destination="$root/$tag-$platform-${expected:0:12}"
mkdir -p "$root" "$HOME/.local/bin"
if [[ ! -d "$destination" ]]; then
  staging=$(mktemp -d "$root/.install-XXXXXXXX")
  cp -R "$source/." "$staging/"
  mv "$staging" "$destination"
fi
link="$HOME/.local/bin/webcli"
if [[ -e "$link" && ! -L "$link" ]]; then
  fail "$link already exists and is not a symlink; move it before installing."
fi
ln -sfn "$destination/webcli" "$link"

path_line='export PATH="$HOME/.local/bin:$PATH"'
add_path() {
  touch "$1"
  if ! grep -Fqx "$path_line" "$1"; then
    printf '\n# WebCLI\n%s\n' "$path_line" >> "$1"
  fi
}
shell=${SHELL:-sh}
case "${shell##*/}" in
  bash) add_path "$HOME/.bashrc"; add_path "$HOME/.bash_profile" ;;
  zsh) add_path "$HOME/.zshrc"; add_path "$HOME/.zprofile" ;;
  fish)
    mkdir -p "$HOME/.config/fish/conf.d"
    printf 'fish_add_path --path "$HOME/.local/bin"\n' > "$HOME/.config/fish/conf.d/webcli.fish"
    ;;
  *) add_path "$HOME/.profile" ;;
esac
printf '\nInstalled: %s\nOpen a new terminal, then run: webcli\n' "$link"
}

main "$@"
