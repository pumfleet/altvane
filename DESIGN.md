---
name: Altvane
description: Audio plug-ins, nothing approximate. A white concert-series sheet where each product is announced by a figure computed from its own mechanism.
colors:
  paper: "#f4f5f4"
  ink: "#111213"
  ink-2: "#4c4e51"
  ink-3: "#6b6d70"
  rule: "#d5d7d6"
  rule-strong: "#b9bbba"
  eq: "#2a3fd4"
  dialogue: "#e0502a"
  compressor: "#0f7a5c"
  limiter: "#a8740a"
  deesser: "#c0287a"
  leveler: "#1683b8"
typography:
  display:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "clamp(3rem, 7.4vw, 6rem)"
    fontWeight: 330
    lineHeight: 0.94
    letterSpacing: "-0.035em"
    fontVariation: "\"wdth\" 125"
  headline:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "clamp(2rem, 3.9vw, 3.5rem)"
    fontWeight: 360
    lineHeight: 1.02
    letterSpacing: "-0.028em"
    fontVariation: "\"wdth\" 112"
  title:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "clamp(1.375rem, 1.9vw, 1.75rem)"
    fontWeight: 420
    lineHeight: 1.15
    letterSpacing: "-0.015em"
    fontVariation: "\"wdth\" 112"
  name:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "1.1875rem"
    fontWeight: 450
    lineHeight: 1.3
    letterSpacing: "-0.012em"
    fontVariation: "\"wdth\" 112"
  lead:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "clamp(1.125rem, 1.35vw, 1.3125rem)"
    fontWeight: 400
    lineHeight: 1.45
    letterSpacing: "-0.006em"
  body:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "1.0625rem"
    fontWeight: 400
    lineHeight: 1.55
  action:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "1.0625rem"
    fontWeight: 480
    letterSpacing: "-0.005em"
    fontVariation: "\"wdth\" 112"
  label:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "0.8125rem"
    fontWeight: 520
    lineHeight: 1.3
    letterSpacing: "0.01em"
    fontVariation: "\"wdth\" 108"
  small:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "0.8125rem"
    fontWeight: 400
    lineHeight: 1.45
    letterSpacing: "0.004em"
  reading:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "1.625rem"
    fontWeight: 380
    lineHeight: 1.1
    letterSpacing: "-0.025em"
    fontFeature: "\"tnum\", \"lnum\""
    fontVariation: "\"wdth\" 115"
  reading-large:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "clamp(2.25rem, 3.4vw, 3.5rem)"
    fontWeight: 300
    lineHeight: 1.1
    letterSpacing: "-0.04em"
    fontFeature: "\"tnum\", \"lnum\""
    fontVariation: "\"wdth\" 115"
  wordmark:
    fontFamily: "Archivo Variable, Helvetica Neue, Helvetica, Arial, sans-serif"
    fontSize: "1.5rem"
    fontWeight: 540
    lineHeight: 1
    letterSpacing: "-0.035em"
    fontVariation: "\"wdth\" 118"
rounded:
  none: "0px"
  key: "3px"
  plate: "6px"
  full: "50%"
spacing:
  s1: "4px"
  s2: "8px"
  s3: "12px"
  s4: "16px"
  s5: "24px"
  s6: "32px"
  s7: "48px"
  s8: "72px"
  s9: "clamp(80px, 9vw, 136px)"
  s10: "clamp(112px, 13vw, 200px)"
  margin: "clamp(20px, 3.4vw, 56px)"
  gutter: "clamp(16px, 1.8vw, 28px)"
components:
  action:
    textColor: "{colors.ink}"
    typography: "{typography.action}"
    rounded: "{rounded.none}"
    padding: "12px 0"
  action-quiet:
    textColor: "{colors.ink-2}"
    typography: "{typography.action}"
    rounded: "{rounded.none}"
    padding: "12px 0"
  nav-link:
    textColor: "{colors.ink-2}"
    padding: "8px 0"
  nav-link-active:
    textColor: "{colors.ink}"
    padding: "8px 0"
  programme-row:
    textColor: "{colors.ink}"
    typography: "{typography.name}"
    padding: "12px 0 13px"
  reading:
    textColor: "{colors.ink}"
    typography: "{typography.reading}"
    padding: "16px 16px 16px 0"
  control-thumb:
    backgroundColor: "{colors.ink}"
    rounded: "{rounded.none}"
    size: "14px"
  format-dot:
    backgroundColor: "{colors.ink}"
    rounded: "{rounded.full}"
    size: "12px"
  format-dot-off:
    backgroundColor: "{colors.paper}"
    rounded: "{rounded.full}"
    size: "12px"
  plate:
    rounded: "{rounded.plate}"
    padding: "clamp(12px, 1.6vw, 24px)"
  key-cap:
    textColor: "{colors.ink}"
    rounded: "{rounded.key}"
    padding: "0 0.35em"
---

# Design System: Altvane

Scope: this system governs the website (`website/`) and the Altvane wordmark. The plug-in editors (C++/JUCE, `libs/ui`) keep their own dark design system and are not documented here.

## Overview

**Creative North Star: "The Musica Viva Sheet"**

Every Altvane plug-in is announced the way a concert in a Swiss poster series is: one cool white sheet, lowercase grotesk set flush left on a strict twelve-column grid, and one large geometric figure. The figure is not illustration. It is computed from the product's real mechanism (EQ's filter response as concentric arcs, Dialogue's stems summing to the input, the Compressor's gain computer as columns under a knee, the Limiter's lookahead holding a waveform under its ceiling, the De-Esser's shelf biting the top off an ess, the Leveler's moves gathering a vocal's phrases on its target), it draws in once from the product's neutral state, and it answers the pointer and the keyboard. The sheet carries the precision claim; the figure proves it.

The page is quiet and dense in the way a printed programme is dense: near-black ink, hairline rules, registration ticks on every column line, small tabular readings with the condition they were measured under. Hierarchy comes from Archivo's width axis and from scale, not from colour, boxes or weight. Each product owns exactly one printed spot ink, and that ink appears only in its figure, its own rules and the links that name it. Actions are set type standing on a rule with a drawn arrow; nothing on the sheet is a filled button.

The world rejects the plug-in category's defaults: a dark hero, a GUI screenshot used as the hook, feature cards and format badges. Screenshots do appear, but only further down a product page, framed as a plate.

**Key Characteristics:**
- Cool white stock, near-black ink, one spot ink per product.
- Archivo Variable across its width axis: expanded and light for names, normal for text, tabular figures for readings.
- Twelve-column sheet with registration ticks; figures crop off the right edge of the sheet.
- Hairline rules everywhere; a 1.5px rule heads every list, table and product section.
- No boxes: actions are type on a rule; lists and tables are ruled, never carded.
- One authored motion: the figure's draw-in, plus underline wipes in the spot ink.

## Colors

A paper-and-ink palette with six printed spot inks, one per product, that never meet on the same product sheet.

### Primary
- **Programme Ink** (#111213): headings, names, labels, the primary action and its rule, the 1.5px rules that head lists and tables, the format dots, the control thumb, the focus ring and the text selection. It is the system's real primary; the spot inks are secondary to it.

### Secondary
The spot inks. A page or section takes its product's ink through `data-product`, which sets `--spot`; with no product, `--spot` falls back to Programme Ink.
- **EQ Cobalt** (#2a3fd4): Altvane EQ's arcs, its section rule, its hover wipes.
- **Dialogue Vermilion** (#e0502a): Altvane Dialogue's voice and reverb segments, its section rule, its hover wipes.
- **Compressor Viridian** (#0f7a5c): Altvane Compressor's over-threshold columns and gain-reduction outlines, its section rule, its hover wipes.
- **Limiter Ochre** (#a8740a): Altvane Limiter's gain reduction and held-back outlines, its section rule, its hover wipes. 3.72:1 on the stock: a graphic ink, never a text colour.
- **De-Esser Rhodamine** (#c0287a): what Altvane De-Esser's shelf takes from each column, its section rule, its hover wipes. 5.02:1 on the stock; still a graphic ink only.
- **Leveler Cerulean** (#1683b8): Altvane Leveler's input outlines and the rider's moves, its section rule, its hover wipes. 3.86:1 on the stock: a graphic ink, never a text colour. It sits well apart from EQ Cobalt in lightness and chroma (oklch 57.9% 0.121 against 46.3% 0.226).

### Neutral
- **Cool Stock** (#f4f5f4): the sheet. Every page background, the browser theme colour, and the knock-out fill behind captions that sit over a figure.
- **Second Ink** (#4c4e51): lead paragraphs, body prose, nav links at rest, quiet actions, table cells.
- **Third Ink** (#6b6d70): small notes, captions, reading conditions, table heads, the "Altvane" prefix in product titles. The lightest colour allowed to set text (4.75:1 on the stock).
- **Hairline** (#d5d7d6): the 1px rules between rows, sections and the header's top line.
- **Strong Hairline** (#b9bbba): registration ticks, link underlines at rest, the quiet action's rule, the range track, empty format dots, the dotted 0 dB and unity references in figures.

### Named Rules
**The One Spot Ink Rule.** Each product owns one spot ink, used for that product's figure, its 1.5px section rule and its hover wipes, and nothing else. A spot ink only ever marks its own product: where another product is named (a header nav link, a programme row, a "Next" action), that element wipes in the named product's ink, never the page's.

**The Spot Never Sets Text Rule.** Spot inks are for marks, rules and fills, never for type. Dialogue Vermilion is 3.59:1 on the stock: enough for a graphic, not for words.

**The Three Inks Rule.** Text is set in Programme Ink, Second Ink or Third Ink, and nothing lighter. Hairlines are for rules only.

## Typography

**Display Font:** Archivo Variable (with Helvetica Neue, Helvetica, Arial)
**Body Font:** Archivo Variable (same stack)

**Character:** One grotesk doing every job by moving along its width axis: wide and light at poster scale, normal width for reading, tabular figures for numbers. Large type goes lighter (250 to 360); small type goes heavier (450 to 540).

### Hierarchy
- **Display** (330, clamp(3rem, 7.4vw, 6rem), 0.94, width 125%, lowercase): product names on the home series and product page titles. Page compositions resize it locally (5rem cap in the home series, 8.4vw on product pages) without changing its width or weight.
- **Headline** (360, clamp(2rem, 3.9vw, 3.5rem), 1.02, width 112%, lowercase): closing statements, such as "free, with every line of source open." and each product page's closing download line.
- **Title** (420, clamp(1.375rem, 1.9vw, 1.75rem), 1.15, width 112%, sentence case): section heads on product pages ("What it does", "Specifications").
- **Name** (450, 1.1875rem, 1.3, width 112%): the first item in a ruled row: programme entries, feature leads, platform names in the release matrix.
- **Lead** (400, clamp(1.125rem, 1.35vw, 1.3125rem), 1.45, Second Ink, max 38ch): the one paragraph under a title or product name.
- **Body** (400, 1.0625rem, 1.55, Second Ink, max 66ch): prose.
- **Label** (520, 0.8125rem, 1.3, +0.01em, width 108%): the product kind line set directly under a product's name.
- **Small** (400, 0.8125rem, 1.45, Third Ink): captions, notes, version lines, column heads.
- **Reading** (380, 1.625rem, width 115%, tabular lining figures) and **Reading Large** (300, clamp(2.25rem, 3.4vw, 3.5rem)): measured values, each with its condition in Small beneath.
- **Wordmark** (540, 1.5rem, width 118%, -0.035em, lowercase "altvane"): the brand, set in type alone in the header. The footer repeats it at poster scale (300, width 125%, up to 30rem), cropped by the sheet's bottom edge.

### Named Rules
**The Width Axis Rule.** Hierarchy is carried by width before weight: 125% for display, 112% to 118% for names, titles, actions and the wordmark, 100% for reading text. Never use bold weights to make something louder; make it wider and larger.

**The Lowercase Poster Rule.** Display and headline sizes are set lowercase via text-transform; title size and below keep sentence case so product names stay readable.

**The Reading Rule.** Every number on the sheet is set in tabular lining figures and carries the condition it was measured under. A value with an adjective instead of a condition does not belong in a reading.

## Layout

The page is a twelve-column sheet: columns between margins of clamp(20px, 3.4vw, 56px) with gutters of clamp(16px, 1.8vw, 28px). A row of 7px registration ticks under the header marks every column line, and every section places its content on those columns.

Recurring placements: copy in columns 1 to 5 and the figure in 6 to 12 (the home hero, the EQ, Compressor and De-Esser series); the figure in 1 to 7 and copy in 9 to 12 (the Dialogue, Limiter and Leveler series, mirrored for rhythm); section heads in 1 to 4 and content in 5 to 12 (product page sections). Figures bleed past the right margin and are cropped by the sheet's edge rather than contained in a frame.

Spacing follows one ten-step rhythm: 4, 8, 12, 16, 24, 32, 48, 72px, then fluid clamp(80px, 9vw, 136px) between product page sections and clamp(112px, 13vw, 200px) between home series and before the footer. The home hero fills at least the first viewport (clamp(640px, 100svh minus 84px, 940px)) and grows when its programme of six needs more.

Responsive behaviour: at 900px and below, every split collapses to full width and the figure moves after the copy (the home hero reads title, claim, figure, then downloads). At 1000px the header breaks into two rows with the nav below, which wraps on a phone. At 640px large readings drop to two columns, figure controls stack, and the release matrix becomes a ruled list.

### Named Rules
**The Twelve Columns Rule.** Every element starts and ends on a column line. If a placement cannot be written as `grid-column: a / span b` on the sheet, it is wrong.

**The Crop Rule.** Figures are large enough to be cut by the sheet's edge. They do not sit centred in a box with margin around them.

## Elevation & Depth

The sheet is flat, like print. Depth comes from rules, ink density and overprint (a figure's caption knocks out the arcs behind it with a Cool Stock fill), not from shadow. The single exception is the screenshot plate, which shows a real editor window and so lifts it slightly off the sheet.

### Shadow Vocabulary
- **Plate lift** (`box-shadow: 0 1px 2px rgb(17 18 19 / 0.08), 0 18px 48px -20px rgb(17 18 19 / 0.35)`): screenshots of the plug-in editors inside a plate, and nowhere else.

### Named Rules
**The Printed Sheet Rule.** Nothing on the sheet casts a shadow except a screenshot of a real window. No cards, panels or sticky bars float.

## Shapes

The form language is square and drawn. Corners are 0 everywhere: actions, rules, the range thumb, sections. Circles appear only where the geometry is a circle: the format dots (12px), and the EQ figure's band node. Two small radii exist for objects that are not part of the sheet: key caps in gesture tables (3px), and editor screenshots (6px) that echo the window they show.

Plates carry four 14px registration corners in 1px Programme Ink instead of a border. Rules come in two weights: a 1px hairline between items, and a 1.5px rule (ink, or the product's spot ink) that heads a list, table or product section. Icons are drawn arrows on an 18-unit grid with 1.5px square-capped strokes: right (moves within the site), out (leaves it), down (downloads).

### Named Rules
**The Square Corner Rule.** Corners are square unless the object is a circle by geometry or a picture of something outside the sheet.

## Components

### Actions
Set type standing on a rule, with a drawn arrow. Never a box.
- **Shape:** no background, no border except a bottom rule; square corners. Minimum width min(100%, 22rem), label and arrow pushed to opposite ends.
- **Primary:** Programme Ink type in the action style (480, width 112%), 12px vertical padding, on a 1.5px Programme Ink rule.
- **Hover / Focus:** the product's spot ink wipes along the rule from the left over 0.6s; the arrow nudges 3px right (2px up-right for "out"). Focus also draws the 2px ink focus ring.
- **Quiet:** Second Ink at 420 on a 1px Strong Hairline rule; it turns Programme Ink on hover. Used for the secondary action beside a primary one (download beside "About", "Next" beside "Download").

### Navigation
- **Style:** a single top row on the sheet: the wordmark in columns 1 to 3, the six product links in 5 to 10, "Source" right-aligned in 11 to 12, a 1px hairline beneath, then the registration ticks.
- **Links:** 0.9375rem, 450, width 106%, Second Ink, no underline at rest.
- **Hover / Current:** Programme Ink with a 1.5px underline in that product's spot ink wiping in from the left over 0.5s; the current page keeps the underline. "Source" wipes in Programme Ink.
- **Mobile:** at 1000px the product links drop to a second row; on a phone they wrap onto two lines.

### Programme
The download list, set like a concert programme.
- **Structure:** a 1.5px Programme Ink rule on top, then one row per plug-in: its name (Name style), its version and platform note (Small, tabular), and a "Download" link with the down arrow. A plug-in with no release yet reads "From source" and links "Build" with the out arrow to the build instructions in its README.
- **Rows:** 12px vertical padding with 1px hairlines between.
- **Hover:** the product's spot ink draws across the row's top edge over 0.6s.

### Readings
- **Style:** a grid of measured values on hairlines, two columns (three for large), each value in the Reading style with its condition in Small Third Ink (max 18ch) beneath. A multiplication sign is drawn as an SVG cross, not taken from the font.
- **Odd counts:** the last reading spans the row so no rule stops halfway.

### Ruled Lists and Tables
Features, gesture tables and the release matrix share one form: a 1.5px ink rule on top (or under the column heads), 1px hairlines between rows, row heads in the Name style, cells in Second Ink, column heads in Small Third Ink. Formats are marked with 12px dots, filled Programme Ink for yes and a Strong Hairline outline for no. Key caps are bordered in Strong Hairline with a 3px radius.

### Figure Controls
- **Style:** label (0.9375rem, 450, width 108%) and its value in tabular figures on one line, a range below drawn as a 1px Strong Hairline track.
- **Thumb:** a 14px square of Programme Ink. On hover or drag it turns the product's spot ink and rotates 45° into a diamond.
- **Focus:** the 2px ink focus ring, offset 2px.
- **Choices:** a few named settings (the De-Esser's Sound and Mode, the Leveler's Lookahead) are radio buttons or a checkbox set as type under the control's label: Second Ink at rest, Programme Ink on a 1.5px ink rule when chosen, the spot ink's rule on hover. A checkbox draws a 10px square, filled with ink when on.

### Plate
A screenshot set on the sheet like a printed plate: four registration corners, padding clamp(12px, 1.6vw, 24px), the image with a 6px radius and the plate lift shadow, and a Small caption flush left beneath (max 60ch).

### Poster Figures (signature)
The defining component. Each figure is an SVG computed at build time from the product's real DSP code and redrawn in the browser with the same functions, so it is complete without JavaScript.
- **EQ:** 32 concentric arcs in EQ Cobalt, one per frequency from 20 Hz at the centre to 20 kHz at the edge; each arc's sweep is the response at that frequency. A dotted Strong Hairline ray marks 0 dB. The live band's node is a Cool Stock circle ringed in 1.5px ink with an ink dot.
- **Dialogue:** one bar per spectrum band, split into voice (solid spot), reverb (spot hatching) and ambience (Programme Ink), inside a 1px ink outline of the input. At 0 dB on every stem the segments end exactly at the outline.
- **Compressor:** 30 columns from -60 to 0 dB input, rising to the output level; columns over the knee fill with spot ink and show a 1px spot outline up to where the input would have been. A 1px ink threshold line and a dotted unity line.
- **Limiter:** three seconds of a synthetic programme through a lookahead peak limiter, as 60 columns mirrored about a centre line. Each column reaches the output's peak in its slice of time in Programme Ink, with a 1px spot outline out to where the gained input would have gone; the gain reduction hangs from a 1px ink top rule as spot bars. Two 1px ink ceiling lines and dotted full-scale lines. The ramp and release only ever lower the gain below what each point needs, so no column passes the ceiling. On its product page, dragging the waveform up or down sets the gain, as in the editor.
- **De-Esser:** one moment of a voice as a spectrum, 40 columns from 120 Hz to 20 kHz, through the plug-in's detection band (fourth-order high-pass), Vocal weighting (band against body, a smoothstep from −12 to 0 dB), 6 dB soft knee and dynamic high shelf. Each column stands up to the level heard in Programme Ink, capped by what the shelf took in the spot ink; a 1.5px ink rule from the band's low edge to the sheet's edge marks the detection band, with a dotted edge. On its product page, dragging sideways moves the band, and an ess can be swapped for a bright vowel to show Vocal leaving the vowel alone.
- **Leveler:** eight seconds of a synthetic vocal through the rider (5 ms hops, hold, guard, onset catch-up, lookahead), one column per 100 ms. A spot outline marks each column's level in, an ink mark the level heard, a 1.5px spot line joins them, and a 1px ink rule is the target. Breaths are hollow ink marks. On its product page, dragging up or down moves the target, as in the editor.
- **Motion:** one draw-in per figure, from the product's neutral state to its setting, with an exponential ease-out over 1.6s, staggered once, triggered when a third of the figure is in view. Under reduced motion, or without JavaScript, the figure shows its finished state and nothing moves.
- **Interaction:** the pointer and arrow keys tune the figure; readouts update in tabular figures.

### Wordmark
The brand is the lowercase word "altvane" in Archivo, with no symbol. It is set in the Wordmark style in the header and at poster scale at the foot of every page. The favicon is the wordmark's "a", outlined from Archivo SemiBold, in Programme Ink, flipping to Cool Stock in dark mode.

## Do's and Don'ts

### Do:
- **Do** give every product surface its spot ink through `data-product`, and use it only for the figure, the 1.5px section rule and hover wipes.
- **Do** set actions as type on a rule with a drawn arrow: 1.5px Programme Ink for primary, 1px Strong Hairline for quiet.
- **Do** carry hierarchy on Archivo's width axis (125% display, 112% names and titles, 100% text), going lighter as type gets larger.
- **Do** place every element on the twelve-column sheet and let figures crop at the sheet's edge.
- **Do** head every list, table and section with a 1.5px rule and separate rows with 1px hairlines.
- **Do** compute every figure from the product's real mechanism, and keep it complete and static without JavaScript or under reduced motion.
- **Do** set every number in tabular lining figures with the condition it was measured under.

### Don't:
- **Don't** fill a button or draw a box around an action.
- **Don't** set text in a spot ink, or in any colour lighter than Third Ink (#6b6d70).
- **Don't** use a product's spot ink for anything that is not that product's figure, rule or link.
- **Don't** use cards, panels, format badges or a dark section; group content with rules instead.
- **Don't** add shadows or rounded corners to anything except a screenshot plate (6px) or a key cap (3px).
- **Don't** lead a page with a GUI screenshot; the figure is the hook, and screenshots sit lower on product pages as plates.
- **Don't** add motion beyond the figure's single draw-in and the underline and arrow responses to hover.
- **Don't** add a symbol or logo mark beside the wordmark, and don't use the `libs/ui` weather-vane mark or the plug-ins' Inter typeface on the website.
