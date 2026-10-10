# Pinned RML / RCSS feature reference

Inventory verified against PlutoGE's RmlUi **6.1** `Factory.cpp` and `StyleSheetSpecification.cpp`. These are core parser registrations, not a claim that every renderer implements every effect. See [the authoring guide](RMLUI_AUTHORING.md) for workflow, scaling, scripting and backend limits. This inventory must be refreshed when the dependency changes.

## Feature and host availability

| Feature family | RmlUi / PlutoGE behavior |
|---|---|
| Generic elements, XML markup, templates, tabsets, handles | Native core; author explicit styles |
| Forms, text/password, radio/checkbox, range, select, textarea, progress | Native core; managed events use current state |
| Cascade, selectors, box layout, flex, tables, media queries | Core; browser CSS Grid is absent in 6.1 |
| dp, px, font-relative, percent, viewport lengths | Core; screen dp uses UISettings, world dp remains 1 |
| Transforms, transition, @keyframes animations | Core; UISettings uses layout rather than a transform |
| Sprites/decorators/font effects | Core parser; renderer capabilities vary |
| Filters, backdrop filters, custom shader decorators | Legacy GL3 advanced renderer; not general RHI support |
| SVG, Lottie, Lua, browser JavaScript | Optional upstream integrations; not a guaranteed engine authoring API |
| Native data models and data-* views | Core C++; no automatic managed-property model registration |
| Managed DOM, observables, drag IDs, pan/zoom, portraits, render textures | Engine-specific; see authoring guide |
| Managed public-property/value/action/localization bindings | RmlViewBinder; explicit IDs, Refresh/Dispose lifetime, data-pluto-binding-* recreation markers; not native data-model registration |
| Visual UI construction | Source hierarchy and ID picking, computed layout, source-preserving widget/text/attribute/style edits with shared undo |
| Camera runtime UI visibility | Camera RenderRuntimeUI controls its viewport; editor camera defaults off and has a saved Inspector override |
| CSS var()/browser custom properties | Not implemented by this pinned RCSS parser; use token generation |

## Registered special elements

Generic `*` elements also support custom names. RML structure/head/template elements are handled by XML parsers, not just the element-instancer list.

### Element instancers

`*`, `img`, `#text`, `handle`, `body`, `form`, `input`, `select`, `label`, `textarea`, `#selection`, `tabset`, `progress`, `progressbar`

### Decorators

`text`, `tiled-horizontal`, `tiled-vertical`, `tiled-box`, `image`, `ninepatch`, `shader`, `gradient`, `horizontal-gradient`, `vertical-gradient`, `linear-gradient`, `repeating-linear-gradient`, `radial-gradient`, `repeating-radial-gradient`, `conic-gradient`, `repeating-conic-gradient`

### Filters

`hue-rotate`, `brightness`, `contrast`, `grayscale`, `invert`, `opacity`, `saturate`, `sepia`, `blur`, `drop-shadow`

### Font effects

`blur`, `glow`, `outline`, `shadow`

## Native data binding inventory

Views: `data-attr`, `data-attrif`, `data-class`, `data-if`, `data-visible`, `data-rml`, `data-style`, `data-text`, `data-value`, `data-checked`, `data-alias`, `data-for`.

Controllers: `data-checked`, `data-event`, `data-value`.

A registered model is required. Interpolation and expressions belong to that model. Engine-reserved attributes such as data-pan-zoom and data-preview-root are independent host extensions.

## Every registered core property

Defaults shown below are native values; an authored stylesheet or the engine can override them. An empty default is shown as `(empty)`. Control-specific properties such as input-generated geometry and font effects have additional instancer parsers.

| Property | Native default | Inherited |
|---|---|---|
| `margin-top` | `0px` | no |
| `margin-right` | `0px` | no |
| `margin-bottom` | `0px` | no |
| `margin-left` | `0px` | no |
| `padding-top` | `0px` | no |
| `padding-right` | `0px` | no |
| `padding-bottom` | `0px` | no |
| `padding-left` | `0px` | no |
| `border-top-width` | `0px` | no |
| `border-right-width` | `0px` | no |
| `border-bottom-width` | `0px` | no |
| `border-left-width` | `0px` | no |
| `border-top-color` | `black` | no |
| `border-right-color` | `black` | no |
| `border-bottom-color` | `black` | no |
| `border-left-color` | `black` | no |
| `border-top-left-radius` | `0px` | no |
| `border-top-right-radius` | `0px` | no |
| `border-bottom-right-radius` | `0px` | no |
| `border-bottom-left-radius` | `0px` | no |
| `display` | `inline` | no |
| `position` | `static` | no |
| `top` | `auto` | no |
| `right` | `auto` | no |
| `bottom` | `auto` | no |
| `left` | `auto` | no |
| `float` | `none` | no |
| `clear` | `none` | no |
| `box-sizing` | `content-box` | no |
| `z-index` | `auto` | no |
| `width` | `auto` | no |
| `min-width` | `0px` | no |
| `max-width` | `none` | no |
| `height` | `auto` | no |
| `min-height` | `0px` | no |
| `max-height` | `none` | no |
| `line-height` | `1.2` | yes |
| `vertical-align` | `baseline` | no |
| `overflow-x` | `visible` | no |
| `overflow-y` | `visible` | no |
| `clip` | `auto` | no |
| `visibility` | `visible` | no |
| `background-color` | `transparent` | no |
| `color` | `white` | yes |
| `caret-color` | `auto` | yes |
| `image-color` | `white` | no |
| `opacity` | `1` | yes |
| `font-family` | `(empty)` | yes |
| `font-style` | `normal` | yes |
| `font-weight` | `normal` | yes |
| `font-size` | `12px` | yes |
| `letter-spacing` | `normal` | yes |
| `text-align` | `left` | yes |
| `text-decoration` | `none` | yes |
| `text-transform` | `none` | yes |
| `white-space` | `normal` | yes |
| `word-break` | `normal` | yes |
| `row-gap` | `0px` | no |
| `column-gap` | `0px` | no |
| `cursor` | `(empty)` | yes |
| `drag` | `none` | no |
| `tab-index` | `none` | no |
| `focus` | `auto` | yes |
| `nav-up` | `none` | no |
| `nav-right` | `none` | no |
| `nav-down` | `none` | no |
| `nav-left` | `none` | no |
| `scrollbar-margin` | `0` | no |
| `overscroll-behavior` | `auto` | no |
| `pointer-events` | `auto` | yes |
| `perspective` | `none` | no |
| `perspective-origin-x` | `50%` | no |
| `perspective-origin-y` | `50%` | no |
| `transform` | `none` | no |
| `transform-origin-x` | `50%` | no |
| `transform-origin-y` | `50%` | no |
| `transform-origin-z` | `0` | no |
| `transition` | `none` | no |
| `animation` | `none` | no |
| `decorator` | `(empty)` | no |
| `mask-image` | `(empty)` | no |
| `font-effect` | `(empty)` | yes |
| `filter` | `(empty)` | no |
| `backdrop-filter` | `(empty)` | no |
| `box-shadow` | `none` | no |
| `fill-image` | `(empty)` | no |
| `align-content` | `stretch` | no |
| `align-items` | `stretch` | no |
| `align-self` | `auto` | no |
| `flex-basis` | `auto` | no |
| `flex-direction` | `row` | no |
| `flex-grow` | `0` | no |
| `flex-shrink` | `1` | no |
| `flex-wrap` | `nowrap` | no |
| `justify-content` | `flex-start` | no |
| `--rmlui-language` | `(empty)` | yes |
| `--rmlui-direction` | `auto` | yes |

## Every registered core shorthand

| Shorthand | Expanded properties |
|---|---|
| `margin` | `margin-top, margin-right, margin-bottom, margin-left` |
| `padding` | `padding-top, padding-right, padding-bottom, padding-left` |
| `border-width` | `border-top-width, border-right-width, border-bottom-width, border-left-width` |
| `border-color` | `border-top-color, border-right-color, border-bottom-color, border-left-color` |
| `border-top` | `border-top-width, border-top-color` |
| `border-right` | `border-right-width, border-right-color` |
| `border-bottom` | `border-bottom-width, border-bottom-color` |
| `border-left` | `border-left-width, border-left-color` |
| `border` | `border-top, border-right, border-bottom, border-left` |
| `border-radius` | `border-top-left-radius, border-top-right-radius, border-bottom-right-radius, border-bottom-left-radius` |
| `overflow` | `overflow-x, overflow-y` |
| `background` | `background-color` |
| `font` | `font-style, font-weight, font-size, font-family` |
| `gap` | `row-gap, column-gap` |
| `nav` | `nav-up, nav-right, nav-down, nav-left` |
| `perspective-origin` | `perspective-origin-x, perspective-origin-y` |
| `transform-origin` | `transform-origin-x, transform-origin-y, transform-origin-z` |
| `flex` | `flex-grow, flex-shrink, flex-basis` |
| `flex-flow` | `flex-direction, flex-wrap` |

## Detailed syntax and version checks

The pinned source contains individual control attributes, property parsers, template/XML handlers and animation interpolation. Consult the upstream manuals for detailed usage, but verify additions against 6.1 before relying on them:

- [Markup structure, templates, elements, images, controls and forms](https://mikke89.github.io/RmlUiDoc/pages/rml.html)
- [Selectors, properties, layout, visual effects, font/text, media queries and decorators](https://mikke89.github.io/RmlUiDoc/pages/rcss.html)
- [Native events](https://mikke89.github.io/RmlUiDoc/pages/rml/events.html)
- [Generated slider/scrollbar/drop-down styling](https://mikke89.github.io/RmlUiDoc/pages/style_guide.html)
- [Native models, expressions, views and controllers](https://mikke89.github.io/RmlUiDoc/pages/data_bindings.html)

Keep this reference exhaustive for the pinned core registration inventory; add engine extensions and renderer support notes to the authoring guide when introducing them.
