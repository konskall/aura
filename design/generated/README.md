# Generated visual source assets

These project-bound raster sources were created with the built-in OpenAI image-generation tool on 2026-10-18. The supplied visual board was used as human-inspected design guidance; it was not embedded, cropped or repainted into the runtime UI.

Final prompt set:

- `aura-home-orb-source.png`: a single centered translucent glasslike AI sphere on uniform `#000105`, cyan/blue lower illumination, ultraviolet/magenta highlights, internal particles and filaments; no UI, text, logo, bezel or watermark.
- `aura-listening-source.png`: a symmetric horizontal fluid voice membrane with cyan/magenta waveform wings and sparse bars on uniform black; explicitly no sphere, controls, text or screen mockup.
- `aura-thinking-source.png`: a hollow blue-violet-magenta particle vortex with a dark center and controlled star dust on uniform black; no loading widget, UI or text.
- `aura-speaking-source.png`: a glasslike violet-blue orb wrapped by one cyan-to-magenta luminous ribbon with black space below for native controls; no equalizer, UI or text.
- `aura-coast-source.png`: a fictional Mediterranean twilight bay with deep blue water, violet/pink sky and warm town lights; no people, text, logos or UI.
- `aura-map-source.png`: a fictional dark top-down city map with roads, blocks and muted park space; no labels, route, pins, real geography, logos or UI.

`scripts/generate-visual-assets.py` deterministically crops, scales and converts these sources into the compiled RGB565 descriptors in `shared/assets/images/`. Runtime copy, state, controls and hit targets remain native LVGL objects.
