# Soundtrack — mokafari

Drop track files here, then wire them up in [`web/main.js`](../../main.js) in the
`TRACKS` array:

```js
const TRACKS = [
  { title: 'MACH 64 — Main Theme', dur: '3:48', src: 'assets/music/mach64.mp3', available: true },
  // ...
];
```

For each track: set `src` to the file path, set the real `dur`, and flip
`available: true`. The track then becomes clickable in the on-page player and
the visualizer reacts to it live (WebAudio FFT).

Format: `.mp3` or `.ogg` (both stream fine in browsers). Keep filenames
lowercase-with-dashes. Suggested starter set already stubbed in `TRACKS`:
`mach64.mp3`, `void-collapse.mp3`, `bullet-time.mp3`, `slipstream.mp3`.
