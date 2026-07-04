# MelonPrime Localization

This directory holds the implementation pieces behind the public
`MelonPrimeLocalization.h` facade. Keep call-site-facing APIs in the facade
header unless a later migration intentionally changes the boundary.

- `MelonPrimeLanguageRegistry.*`: language ids, locale detection, fallback, and selection state.
- `MelonPrimeTranslationCatalog.*`: exact and object-name translation lookup.
- `MelonPrimeTranslationDynamic.*`: decorated and dynamic `Tr()` handling.
- `MelonPrimeWidgetLocalizer.*`: widget, action, menu, and dialog localization.
- `MelonPrimeSplashLocalization.*`: no-ROM splash text and UI-font rendering.
- `MelonPrimeTranslations.inc` / `MelonPrimeObjectTranslations.inc`: translation data included by the catalog implementation.
