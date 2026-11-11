# Firmware releases

AURA devices update over the air from this repository's GitHub releases. A
device with no source of its own reads the newest release's manifest,
`https://github.com/konskall/aura/releases/latest/download/aura-manifest.json`,
and installs the image it names only when the owner's signature checks out
and the version is newer than its own (`docs/SECURITY_PRIVACY.md`).

The subfolders here (`v<version>/`) are prepared releases. They are uploaded
to GitHub, never committed (`.gitignore`).

## Publishing a release

1. Set `firmware/version.txt` to the new version, newer than the devices'
   (for example `13.1.0`), and build the firmware.
2. Prepare the release with the private key, which lives outside the
   repository:

   ```
   node scripts/update-signing.mjs release --version 13.1.0 --key <private key file> --notes "What changed"
   ```

   It refuses an image that carries another version, and an existing
   folder. It writes `releases/v13.1.0/aura_firmware.bin` and
   `releases/v13.1.0/aura-manifest.json`.
3. At `https://github.com/konskall/aura/releases/new`, create the tag
   `v13.1.0`, attach both files and publish it as a full release, not a
   draft or a pre-release: devices read the newest full release.
4. On the device: Settings > Software update > Check for updates.

## Before the first release

- The signing key is made once, when the board is here:
  `node scripts/update-signing.mjs keygen <file outside the repository>`. Its
  public half goes into `firmware/sdkconfig.defaults`
  (`CONFIG_AURA_OTA_PUBLIC_KEY`). Without it a device installs nothing. Keep
  an offline copy of the private key: devices built with its public half
  accept updates signed with it only.
- The first firmware goes onto the board over USB. Updates work from then
  on.
