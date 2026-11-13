# EARRECTION

Effekt-Plugin für Ableton (VST3, auf dem Mac zusätzlich AU). Es funktioniert wie ein Kaoss Pad mit 8×8 Feldern. Jede Zeile ist ein Effekt, jede Spalte eine Variante davon. Du kannst bis zu 4 Effekte gleichzeitig halten, aber nur einen pro Zeile.

## Die Matrix

| Zeile | Effekt | Spalten 1–8 |
| --- | --- | --- |
| 1 | Stutter | 1/4 · 1/8 · 1/8T · 1/16 · 1/16T · 1/32 · 1/64 · 1/128 |
| 2 | Repeat | 2 Takte · 1 Takt · 1/2 · 1/4 · 1/8 · 1/16 · 1/32 · 1/64 |
| 3 | Reverse | 1/16 · 1/8 · 1/4 · 1/2 · 1 Takt · 2 Takte · ½× (langsam) · 2× (schnell) |
| 4 | Tempo | Halftime · Doubletime · Tape-Stop 2 / 1 / ½ / ¼ Beats · Backspin · harter Stop |
| 5 | Echo | 1/2 · 1/4. · 1/4 · 1/8. · 1/8 · 1/8T · 1/16 · ∞ (Endlosschleife) |
| 6 | Gate | 1/4 · 1/8 · 1/8T · 1/16 · 1/16T · 1/32 · Swing · Zufall |
| 7 | Filter | Lowpass 1–4 (immer dunkler) · Highpass 1–4 (immer dünner) |
| 8 | Klang | Bitcrush 1–3 · Downsample 1–2 · Granular · Freeze · Chaos |

Die Effekte sind von oben nach unten hintereinandergeschaltet. Alles läuft synchron zum Ableton-Tempo.

## Regler

| Regler | Funktion |
| --- | --- |
| Mix | Anteil des Effektsignals |
| Quant | Wann ein Pad einsetzt: sofort, 1/32, 1/16, 1/8, 1/4, 1/2 oder 1 Takt |
| Release | Wie lange der Effekt nach dem Loslassen ausklingt (Echos klingen natürlich aus) |
| Glide | Wie weich ein Effekt einsetzt und zwischen Spalten überblendet |
| Drive | Sättigung des Effektsignals |
| Tone | Effektsignal dunkler (links) oder heller (rechts) |
| Pitch | Tonhöhe von Stutter, Repeat, Reverse und Granular (±12 Halbtöne) |
| Out | Ausgangslautstärke |

Dazu kommen drei Schalter:
- **LATCH**: Pads bleiben an, bis du sie nochmal drückst.
- **VELOCITY**: Die Anschlagstärke steuert, wie stark der Effekt reinkommt.
- **CLEAR**: Schaltet alle Pads aus.

Mit der Maus gilt: Klicken und Halten aktiviert ein Pad, beim Loslassen ist es wieder aus. Ziehen wechselt das Pad. **Shift-Klick** hält ein Pad fest.

## Installieren

1. Öffne [Releases → Earrection – neuester Build](https://github.com/Brucequayne/AudioPlugins/releases/tag/earrection-latest) und lade die ZIP für dein System herunter.
2. Entpacke die ZIP.

**Windows:** Kopiere den Ordner `Earrection.vst3` nach `C:\Program Files\Common Files\VST3`.

**Mac:**
- `Earrection.vst3` kommt nach `~/Library/Audio/Plug-Ins/VST3`.
- `Earrection.component` kommt nach `~/Library/Audio/Plug-Ins/Components`.

Weil das Plugin nicht bei Apple registriert ist, musst du es einmal freigeben. Öffne dazu das Terminal und gib ein:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Earrection.vst3 ~/Library/Audio/Plug-Ins/Components/Earrection.component
```

Danach in Ableton unter **Einstellungen → Plug-ins** auf „Neu scannen“ klicken.

## In Ableton benutzen

1. Ziehe **Earrection** auf die Spur, die du bearbeiten willst, zum Beispiel Drums, eine Gruppe, den Master oder eine Return-Spur.
2. Zum Spielen mit der Maus öffnest du das Plugin-Fenster. Mehr ist nicht nötig.

### Mit Push 1 oder Push 2

1. Öffne die Einstellungen unter **Link, Tempo & MIDI**. Setze beim Eingang **„Push User Port“** (Push 1: „Ableton Push (User Port)“) den Haken bei **Track**.
2. Lege eine leere **MIDI-Spur** an:
   - **MIDI From:** Push User Port
   - **Monitor:** In
   - **MIDI To:** die Spur mit Earrection, darunter „Earrection“ auswählen
3. Drücke am Push die Taste **User**.

Danach entspricht jedes Pad dem gleichen Feld im Plugin, unten links ist unten links. Die 8 Drehregler über den Pads steuern im User Mode die 8 Regler des Plugins.

**Tipp:** Wenn du auf der MIDI-Spur aufnimmst, landet dein Pad-Spiel als MIDI-Clip im Projekt. So kannst du es wiederholen und nachträglich editieren.

## Bekannte Grenzen (Version 1)

- Im User Mode leuchten die Push-Pads nicht.
- Die Felder haben eine feste Belegung.
