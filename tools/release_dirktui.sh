#!/bin/sh
# tools/release_dirktui.sh — BUILD, SIGN, NOTARIZE AND SHIP dirktui through the Homebrew tap.
#
# What it does, in order, stopping at the first failure:
#   1. builds a Release, arm64 `dirktui` in build-release-dirktui/ (the shipped binary: no selftest);
#   2. signs it with the Developer ID Application certificate (hardened runtime, timestamped);
#   3. notarizes it with `xcrun notarytool` through an existing Keychain profile, and waits for
#      Apple's answer — a bare binary cannot be stapled, so Gatekeeper checks the ticket online;
#   4. packs `dirktui-<version>-arm64.tar.gz` and records its sha256;
#   5. with --publish: creates the GitHub release `dirktui-v<version>` ON THE TAP REPOSITORY
#      (itscool/homebrew-tap — dirktui rides that tap's releases, not rolltui's own), uploads the
#      tarball, writes Formula/dirktui.rb in the tap checkout with the version and the sha256,
#      commits it there and pushes.
#   Without --publish it stops after step 4 and prints what step 5 would do: a dry run that still
#   proves the build signs and notarizes.
#
# The version is `examples/dirktui.version`, the one place it lives; bump it there first.
# Nothing here reads a password: the notary profile is a Keychain item made once with
#   xcrun notarytool store-credentials Perch --apple-id … --team-id S42F8BV6J2
# and the certificate is in the login keychain.
#
# Environment (all optional):
#   DIRKTUI_SIGN_IDENTITY   the codesign identity   (default: Developer ID Application: Scott Williams (S42F8BV6J2))
#   DIRKTUI_NOTARY_PROFILE  the notarytool profile  (default: Perch)
#   DIRKTUI_TAP_DIR         the tap checkout        (default: ~/git/homebrew-tap)
#   DIRKTUI_TAP_REPO        the tap on GitHub       (default: itscool/homebrew-tap)
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(head -n 1 "$ROOT/examples/dirktui.version" | tr -d '[:space:]')
IDENTITY=${DIRKTUI_SIGN_IDENTITY:-"Developer ID Application: Scott Williams (S42F8BV6J2)"}
PROFILE=${DIRKTUI_NOTARY_PROFILE:-Perch}
TAP_DIR=${DIRKTUI_TAP_DIR:-"$HOME/git/homebrew-tap"}
TAP_REPO=${DIRKTUI_TAP_REPO:-itscool/homebrew-tap}
BUILD="$ROOT/build-release-dirktui"
OUT="$ROOT/dist"
TARBALL="dirktui-$VERSION-arm64.tar.gz"
TAG="dirktui-v$VERSION"
PUBLISH=0
for a in "$@"; do
  case "$a" in
    --publish) PUBLISH=1 ;;
    -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
    *) echo "unknown argument: $a" >&2; exit 2 ;;
  esac
done
[ -n "$VERSION" ] || { echo "no version in examples/dirktui.version" >&2; exit 2; }

echo "== dirktui $VERSION: build (Release, arm64)"
cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
cmake --build "$BUILD" --target dirktui -j >/dev/null
BIN="$BUILD/dirktui"
[ -x "$BIN" ] || { echo "no binary at $BIN" >&2; exit 1; }
"$BIN" --version | grep -qx "dirktui $VERSION" || { echo "the binary does not report version $VERSION" >&2; exit 1; }

echo "== sign"
codesign --force --sign "$IDENTITY" --options runtime --timestamp "$BIN"
codesign --verify --strict --verbose=2 "$BIN"

echo "== notarize (profile $PROFILE)"
rm -rf "$OUT"; mkdir -p "$OUT"
ditto -c -k --keepParent "$BIN" "$OUT/dirktui-notarize.zip"
xcrun notarytool submit "$OUT/dirktui-notarize.zip" --keychain-profile "$PROFILE" --wait --output-format json > "$OUT/notarize.json"
STATUS=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("status",""))' "$OUT/notarize.json")
[ "$STATUS" = "Accepted" ] || { echo "notarization: $STATUS (see $OUT/notarize.json; inspect with notarytool log)" >&2; exit 1; }
rm -f "$OUT/dirktui-notarize.zip"

echo "== pack $TARBALL"
STAGE="$OUT/stage"; mkdir -p "$STAGE"
cp "$BIN" "$STAGE/dirktui"
( cd "$STAGE" && tar -czf "../$TARBALL" dirktui )
SHA=$(shasum -a 256 "$OUT/$TARBALL" | cut -d' ' -f1)
echo "sha256 $SHA"

FORMULA=$(cat <<RUBY
class Dirktui < Formula
  desc "Column file browser for the shell: dirk cd's, opens, and hands paths to the command line"
  homepage "https://github.com/$TAP_REPO"
  url "https://github.com/$TAP_REPO/releases/download/$TAG/$TARBALL"
  sha256 "$SHA"
  version "$VERSION"

  depends_on arch: :arm64
  depends_on :macos

  def install
    bin.install "dirktui"
    # FISH GETS THE SHELL SIDE FROM THE INSTALL ITSELF: fish sources every file in the vendor
    # config directory, so the \`dirk\` function and Right Arrow are there in the next fish shell
    # with no rc file touched. zsh and bash have no such directory — a formula may not edit a
    # dotfile — so for them the caveat below is the one step left.
    (share/"fish/vendor_conf.d").mkpath
    (share/"fish/vendor_conf.d/dirk.fish").write Utils.safe_popen_read(bin/"dirktui", "init", "fish")
  end

  def caveats
    <<~EOS
      fish: done — \`dirk\` and Right Arrow are there in your next fish shell.
      zsh or bash: put the shell side in your rc file, once:
        dirktui install            # writes the block for \$SHELL
      or by hand:
        eval "\$(dirktui init zsh)"  # bash likewise
    EOS
  end

  test do
    assert_match "dirktui #{version}", shell_output("#{bin}/dirktui --version")
    assert_match "dirk", shell_output("#{bin}/dirktui init zsh")
    assert_predicate share/"fish/vendor_conf.d/dirk.fish", :exist?
  end
end
RUBY
)
printf '%s\n' "$FORMULA" > "$OUT/dirktui.rb"

if [ "$PUBLISH" -ne 1 ]; then
  echo "== dry run: signed, notarized, packed at $OUT/$TARBALL; formula at $OUT/dirktui.rb"
  echo "   --publish would: gh release create $TAG --repo $TAP_REPO $OUT/$TARBALL; write $TAP_DIR/Formula/dirktui.rb; commit and push the tap"
  exit 0
fi

echo "== publish: release $TAG on $TAP_REPO"
[ -d "$TAP_DIR/.git" ] || { echo "no tap checkout at $TAP_DIR" >&2; exit 1; }
gh release create "$TAG" --repo "$TAP_REPO" --title "dirktui $VERSION" --notes "dirktui $VERSION, arm64, signed and notarized." "$OUT/$TARBALL"
mkdir -p "$TAP_DIR/Formula"
cp "$OUT/dirktui.rb" "$TAP_DIR/Formula/dirktui.rb"
( cd "$TAP_DIR" && git add Formula/dirktui.rb && git commit -m "Add dirktui formula $VERSION" -- Formula/dirktui.rb && git push )
echo "== done: brew update && brew upgrade dirktui"
