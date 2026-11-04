# Photos from microSD and the simulator

Open **Εφαρμογές → Φωτογραφίες** (English: **Apps → Photos**). The new app is
appended to the existing order; earlier saved arrangements are preserved.

Put JPG/JPEG/PNG files directly in:

- Device FAT32 microSD: `aura/photos` (mounted as `/sdcard/aura/photos`).
- Windows development simulator: `simulator/runtime/companion/photos`.

You can also send photos from a phone or PC on the same trusted Wi-Fi through
**Μεταφορά αρχείων / File transfer** in Photos or Settings, without removing the
card. Enable it on the device, open its QR/address, enter the displayed code and
confirm Send in the browser. Completed photos refresh the gallery automatically.
See [local file transfer](FILE_TRANSFER.md) for access lifetime, other file types
and simulator network setup.

The folder is created automatically when storage is available. Existing files
remain local. Subfolders are not searched. Up to 128 supported
filenames are listed, eight per page. Use **Ανανέωση / Refresh** after changing
the files while the app is open. An absent card, empty folder, failed read or
unsupported image has its own state. Bad images can be skipped with Next.

Tap a filename to open it. Previous/Next and horizontal swipes change images;
tap the image to enter or leave full screen. The play button starts a slideshow.
The settings button selects 5, 10, 30 or 60 seconds. The slideshow keeps the
display awake and stops when another screen opens; it does not restart itself
after reboot.

To delete the current photo, tap the coral trash button beside Play in the viewer,
then confirm the displayed filename. Tap the image first if full screen has hidden
the controls. **Διαγραφή φωτογραφίας / Delete photo** is also fixed at the bottom
of photo settings, visible without scrolling. Cancel preserves the file. A
deleted ambient photo is also removed from the clock preference. The local
browser transfer page can delete both photos and other stored files after login.
Factory reset continues to preserve files.

In the same settings, **Στο ρολόι / On the clock** enables or disables an
ambient photo. **Χρήση αυτής / Use this photo** replaces a previously chosen
background with the currently viewed photo. The timing and ambient selection
survive restart. The ambient image is dimmed behind the existing clock and
retains the existing pixel-shift behavior. It is decoded once, not reread each
frame. Normal display brightness, sleep and face-down behavior still apply.

Each source file is limited to 4 MiB. Baseline JPEGs can be downscaled from up
to 40 megapixels (neither edge above 12,000). PNG and progressive JPEG input
must be at most 1,048,576 pixels; decoding also depends on available memory and
has a 4 MiB allocation budget. Smaller exported copies are suitable for this
480x480 display. Browser uploads automatically prepare larger photos at this
display size; the original on the sending device is preserved. HEIC, RAW, animated images and embedded thumbnails are not
supported. EXIF orientations 1-8 are applied, PNG transparency is placed over
black, and aspect ratio is preserved.

The simulator uses the same decoder, model and LVGL views as firmware. Automated
tests use synthetic DEMO files, not personal photos. Physical card timing,
removal behavior, touch and AMOLED appearance remain NEEDS HARDWARE until the
board arrives. Evidence is recorded in [VERIFICATION.md](VERIFICATION.md).
