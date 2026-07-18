# MelonPrime Localization

This directory holds the implementation pieces behind the public
`MelonPrimeLocalization.h` facade. Keep call-site-facing APIs in the facade
header unless a later migration intentionally changes the boundary.

- `MelonPrimeLanguageRegistry.*`: language ids, locale detection, fallback, and selection state.
- `MelonPrimeTranslationCatalog.*`: exact and object-name translation lookup.
- `MelonPrimeTranslationDynamic.*`: decorated and dynamic `Tr()` handling.
- `MelonPrimeWidgetLocalizer.*`: widget, action, menu, and dialog localization.
- `MelonPrimeSplashLocalization.*`: no-ROM splash text and UI-font rendering.
- `inc/MelonPrimeTranslations.inc`: thin include manifest for exact-match translation shards.
- `inc/MelonPrimeDialogsTranslations.inc`: thin include manifest for upstream melonDS dialog translation shards.
- `inc/MelonPrimeObjectTranslations.inc`: thin include manifest for object-name translation shards.
- `inc/MelonPrime*Translations*.inc`: bounded, topic-based data shards. Add or edit rows in the matching shard; do not grow the manifests into monoliths again.
