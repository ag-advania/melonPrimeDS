#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# MELONPRIME_DISABLE_CHECKBOX_SEMANTICS_V15
"""Focused structural audit for mouse-wheel weapon cycling translations."""
from __future__ import annotations
import re, sys
from pathlib import Path
REPO=Path(__file__).resolve().parents[4]
PATH=REPO/'src/frontend/qt_sdl/MelonPrimeLocalization/inc/MelonPrimeTranslationsMouseWheelWeaponCycle.inc'
UI_PATH=REPO/'src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp'
KEYS=["Disable Mouse Wheel Weapon Cycling", "Check this to disable cycling weapons with the mouse wheel and leave wheel scrolling available for other bindings. Next Weapon, Previous Weapon, and direct weapon keys still work."]
EXPECTED_LANGS=["Japanese", "German", "Spanish", "French", "Italian", "Dutch", "Portuguese", "Russian", "ChineseSimplified", "ChineseTraditional", "Korean", "Arabic", "Indonesian", "Ukrainian", "Greek", "Swedish", "Thai", "Czech", "Danish", "Turkish", "Norwegian", "Hungarian", "Finnish", "Vietnamese", "Polish", "Romanian", "Afrikaans", "Irish", "Icelandic", "Azerbaijani", "Assamese", "Amharic", "Albanian", "Armenian", "Uzbek", "Urdu", "Estonian", "Odia", "Kazakh", "Catalan", "Kannada", "Kyrgyz", "Gujarati", "Khmer", "Croatian", "Georgian", "Sinhala", "Swahili", "Slovak", "Slovenian", "Zulu", "Serbian", "Tamil", "ChineseHongKong", "Telugu", "Nepali", "Basque", "Punjabi", "Hindi", "Filipino", "Bulgarian", "Hebrew", "Belarusian", "Bengali", "Persian", "Bosnian", "Macedonian", "Marathi", "Malayalam", "Maltese", "Malay", "Burmese", "Mongolian", "Lao", "Latvian", "Lithuanian"]
def fail(msg):
 print('[FAIL] '+msg); raise SystemExit(1)
def row(text,key):
 pos=text.find('"'+key.replace('"','\\"')+'"')
 if pos<0: fail('missing source key: '+key)
 start=text.rfind('    {',0,pos); depth=0; ins=False; esc=False
 for i in range(start,len(text)):
  ch=text[i]
  if ins:
   if esc: esc=False
   elif ch=='\\': esc=True
   elif ch=='"': ins=False
  else:
   if ch=='"': ins=True
   elif ch=='{': depth+=1
   elif ch=='}':
    depth-=1
    if depth==0:return text[start:i+1]
 fail('unterminated row: '+key)
def values(r):
 out={}
 for lang,raw in re.findall(r'\{MenuLangId::([A-Za-z0-9_]+),\s*"((?:\\.|[^"\\])*)"\},',r):
  if lang in out: fail('duplicate language: '+lang)
  out[lang]=raw
 return out
def main():
 text=PATH.read_text(encoding='utf-8')
 ui_text=re.sub(r'"\s*"', '', UI_PATH.read_text(encoding='utf-8'))
 if 'MELONPRIME_DISABLE_CHECKBOX_SEMANTICS_WHEEL_TRANSLATIONS_V15' not in text: fail('V15 marker missing')
 for key in KEYS:
  got=values(row(text,key))
  if list(got)!=EXPECTED_LANGS: fail(key+': language order/coverage mismatch')
  if any(not v.strip() for v in got.values()): fail(key+': empty translation')
  placeholders=[lang for lang,value in got.items() if value==key]
  if placeholders: fail(key+': English source placeholder remains for '+', '.join(placeholders))
  latin_fragments=re.findall(r'[A-Za-z]+', got['Sinhala'])
  if latin_fragments: fail(key+': untranslated Latin text remains in Sinhala: '+', '.join(latin_fragments))
  if key not in ui_text: fail('UI source does not use the exact catalog key: '+key)
 print('[PASS] V15 mouse-wheel disable-checkbox translation quality: 76/76 x 2 rows')
 return 0
if __name__=='__main__': raise SystemExit(main())
