#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Focused V7 audit for Morph Ball Boost Assist Sensitivity translations."""
from __future__ import annotations
import json, re, sys
from pathlib import Path

REPO=Path(__file__).resolve().parents[4]
PATH=REPO/'src/frontend/qt_sdl/MelonPrimeLocalization/inc/MelonPrimeTranslationsMouseBoost.inc'
EXPECTED=json.loads('{\n  "Morph Ball Boost Assist Sensitivity": {\n    "Japanese": "モーフボールブーストアシスト感度",\n    "German": "Empfindlichkeit der MORPH-BALL-BOOST-Unterstützung",\n    "Spanish": "Sensibilidad de asistencia del TURBO de la MORFOSFERA",\n    "French": "Sensibilité de l’assistance au BOOST de la BOULE MORPHING",\n    "Italian": "Sensibilità dell’assistenza TURBO della MORFOSFERA",\n    "Zulu": "Ukuzwela kosizo lwe-Morph Ball Boost",\n    "Slovak": "Citlivosť asistencie boostu Morph Ball",\n    "Slovenian": "Občutljivost pomoči pri pospešku Morph Ball",\n    "Basque": "Morph Ball boost-laguntzaren sentikortasuna",\n    "Kazakh": "Morph Ball үдету көмегінің сезімталдығы",\n    "Hebrew": "רגישות הסיוע ל-Boost של Morph Ball",\n    "Amharic": "የMorph Ball Boost እገዛ ትብነት",\n    "Catalan": "Sensibilitat de l’assistència del turbo de la Morfosfera",\n    "Odia": "ମର୍ଫ ବଲ୍ ବୁଷ୍ଟ ସହାୟତା ସମ୍ବେଦନଶୀଳତା",\n    "Estonian": "Morph Balli boostiabi tundlikkus",\n    "Assamese": "মৰ্ফ বল বুষ্ট সহায়তা সংবেদনশীলতা",\n    "Kyrgyz": "Morph Ball буст жардамчысынын сезгичтиги",\n    "Filipino": "Sensitivity ng tulong sa Morph Ball Boost",\n    "Swahili": "Unyeti wa usaidizi wa Boost ya Morph Ball"\n  },\n  "Mouse mode only. 0% disables mouse swipe boost. Values below 100% require more movement; 100% preserves the default threshold; values above 100% require less movement; 9000% is the effective maximum and needs about one ninetieth. Right-click R boost and the Shift auto-cycle are unchanged.": {\n    "Japanese": "マウスモード専用。0%ではマウスを素早く動かして発動するブーストを無効化します。100%未満では発動に必要な移動量が増え、100%はゲーム標準のしきい値、100%超では必要な移動量が減ります。9000%が実効上の最大で、必要な移動量は約90分の1です。右クリックのRブーストとShift自動サイクルは変更されません。",\n    "German": "Nur im Mausmodus. 0% deaktiviert den BOOST durch eine schnelle Mausbewegung. Werte unter 100% erfordern mehr Bewegung; 100% behält den Standard-Schwellenwert bei; Werte über 100% erfordern weniger Bewegung. 9000% ist das wirksame Maximum und benötigt ungefähr ein Neunzigstel der Bewegung. Der R-BOOST per Rechtsklick und der automatische Shift-Zyklus bleiben unverändert.",\n    "Spanish": "Solo en modo ratón. 0% desactiva el TURBO activado al deslizar rápidamente el ratón. Los valores inferiores al 100% requieren más movimiento; 100% mantiene el umbral predeterminado; los valores superiores al 100% requieren menos movimiento. 9000% es el máximo efectivo y requiere aproximadamente una nonagésima parte del movimiento. El TURBO R con clic derecho y el ciclo automático de Shift no cambian.",\n    "French": "Mode souris uniquement. 0% désactive le BOOST déclenché par un mouvement rapide de la souris. Les valeurs inférieures à 100% exigent plus de mouvement ; 100% conserve le seuil par défaut ; les valeurs supérieures à 100% exigent moins de mouvement. 9000% est le maximum effectif et demande environ un quatre-vingt-dixième du mouvement. Le BOOST R au clic droit et le cycle automatique de Maj restent inchangés.",\n    "Italian": "Solo in modalità mouse. 0% disattiva il TURBO attivato da un rapido movimento del mouse. I valori inferiori al 100% richiedono più movimento; 100% mantiene la soglia predefinita; i valori superiori al 100% richiedono meno movimento. 9000% è il massimo effettivo e richiede circa un novantesimo del movimento. Il TURBO R con clic destro e il ciclo automatico di Maiusc restano invariati.",\n    "Zulu": "Imodi yegundane kuphela. U-0% uvala i-boost yokuswayipha ngegundane. Amanani angaphansi kuka-100% adinga ukunyakaza okwengeziwe; u-100% ugcina umkhawulo ojwayelekile; amanani angaphezu kuka-100% adinga ukunyakaza okuncane. U-9000% uyinani eliphezulu elisebenzayo futhi udinga cishe ingxenye eyodwa kwezingamashumi ayisishiyagalolunye yokunyakaza. I-R boost yokuchofoza kwesokudla nomjikelezo ozenzakalelayo we-Shift akuguquki.",\n    "Slovak": "Len pre režim myši. 0% vypne boost vyvolaný švihnutím myši. Hodnoty pod 100% vyžadujú väčší pohyb; 100% zachová predvolený prah; hodnoty nad 100% vyžadujú menší pohyb. 9000% je účinné maximum a vyžaduje približne jednu deväťdesiatinu pohybu. Boost tlačidlom R cez pravé kliknutie ani automatický cyklus klávesu Shift sa nemenia.",\n    "Slovenian": "Le za način z miško. 0% onemogoči pospešek s potegom miške. Vrednosti pod 100% zahtevajo večji premik; 100% ohrani privzeti prag; vrednosti nad 100% zahtevajo manjši premik. 9000% je dejanski maksimum in zahteva približno eno devetdesetino premika. Pospešek R z desnim klikom in samodejni cikel tipke Shift ostaneta nespremenjena.",\n    "Basque": "Sagu moduan soilik. 0% balioak saguaren irristatze bidezko boosta desgaitzen du. 100%etik beherako balioek mugimendu handiagoa eskatzen dute; 100%ek atalase lehenetsia mantentzen du; 100%etik gorako balioek mugimendu txikiagoa eskatzen dute. 9000% da gehieneko balio eraginkorra, eta mugimenduaren laurogeita hamarren bat inguru behar du. Eskuineko klikaren R boosta eta Shift-en ziklo automatikoa ez dira aldatzen.",\n    "Kazakh": "Тек тінтуір режимінде. 0% тінтуірді сермеу арқылы үдетуді өшіреді. 100%-дан төмен мәндер көбірек қозғалысты талап етеді; 100% әдепкі шекті сақтайды; 100%-дан жоғары мәндер азырақ қозғалысты талап етеді. 9000% — тиімді ең жоғары мән және шамамен тоқсаннан бір қозғалысты қажет етеді. Тінтуірдің оң жақ батырмасындағы R үдетуі мен Shift автоматты циклі өзгермейді.",\n    "Hebrew": "למצב עכבר בלבד. 0% משבית את ה-Boost בהחלקת העכבר. ערכים מתחת ל-100% דורשים תנועה גדולה יותר; 100% שומר על סף ברירת המחדל; ערכים מעל 100% דורשים תנועה קטנה יותר. 9000% הוא המקסימום היעיל ודורש בערך אחד חלקי תשעים מהתנועה. ה-Boost של R בלחיצה ימנית והמחזור האוטומטי של Shift אינם משתנים.",\n    "Amharic": "ለመዳፊት ሁነታ ብቻ። 0% በመዳፊት ማንሸራተት የሚነሳውን Boost ያሰናክላል። ከ100% በታች ያሉ እሴቶች የበለጠ እንቅስቃሴ ይፈልጋሉ፤ 100% ነባሪውን ገደብ ይጠብቃል፤ ከ100% በላይ ያሉ እሴቶች ያነሰ እንቅስቃሴ ይፈልጋሉ። 9000% የሚሰራው ከፍተኛ ዋጋ ሲሆን ከ90 አንድ ያህል እንቅስቃሴ ይፈልጋል። በቀኝ ጠቅታ የR Boost እና የShift ራስ-ሰር ዑደት አይቀየሩም።",\n    "Catalan": "Només en mode ratolí. 0% desactiva el turbo activat amb un lliscament ràpid del ratolí. Els valors inferiors al 100% requereixen més moviment; 100% conserva el llindar predeterminat; els valors superiors al 100% requereixen menys moviment. 9000% és el màxim efectiu i requereix aproximadament una norantena part del moviment. El turbo R amb clic dret i el cicle automàtic de Shift no canvien.",\n    "Odia": "କେବଳ ମାଉସ୍ ମୋଡ୍ ପାଇଁ। 0% ମାଉସ୍ ସ୍ୱାଇପ୍ ବୁଷ୍ଟକୁ ବନ୍ଦ କରେ। 100% ଠାରୁ କମ୍ ମୂଲ୍ୟ ପାଇଁ ଅଧିକ ଗତି ଆବଶ୍ୟକ; 100% ଡିଫଲ୍ଟ ସୀମାକୁ ରଖେ; 100% ଠାରୁ ଅଧିକ ମୂଲ୍ୟ ପାଇଁ କମ୍ ଗତି ଆବଶ୍ୟକ। 9000% କାର୍ଯ୍ୟକାରୀ ସର୍ବାଧିକ ଏବଂ ପ୍ରାୟ ଏକ-ନବତିଅଂଶ ଗତି ଆବଶ୍ୟକ। ଡାହାଣ-କ୍ଲିକ୍ R ବୁଷ୍ଟ ଏବଂ Shift ସ୍ୱୟଂଚାଳିତ ଚକ୍ର ଅପରିବର୍ତ୍ତିତ ରହେ।",\n    "Estonian": "Ainult hiirerežiimis. 0% lülitab hiireviibutusega käivitatava boosti välja. Alla 100% väärtused nõuavad suuremat liigutust; 100% säilitab vaikimisi läve; üle 100% väärtused nõuavad väiksemat liigutust. 9000% on tegelik maksimum ja nõuab ligikaudu üht üheksakümnendikku liigutusest. Paremklõpsu R-boost ja Shifti automaattsükkel ei muutu.",\n    "Assamese": "কেৱল মাউছ মোডৰ বাবে। 0% এ মাউছ স্বাইপ বুষ্ট নিষ্ক্ৰিয় কৰে। 100%তকৈ কম মানে অধিক মাউছ চলাচল প্ৰয়োজন কৰে; 100% এ ডিফল্ট সীমা বজাই ৰাখে; 100%তকৈ বেছি মানে কম চলাচল প্ৰয়োজন কৰে। 9000% হৈছে কাৰ্যকৰী সৰ্বাধিক আৰু প্ৰায় নব্বৈ ভাগৰ এভাগ চলাচল প্ৰয়োজন কৰে। ৰাইট-ক্লিক R বুষ্ট আৰু Shift স্বয়ংক্ৰিয় চক্ৰ অপৰিৱৰ্তিত থাকে।",\n    "Kyrgyz": "Чычкан режими үчүн гана. 0% чычканды серпүү менен иштеген бустту өчүрөт. 100%дан төмөн маанилер көбүрөөк кыймылды талап кылат; 100% демейки чекти сактайт; 100%дан жогору маанилер азыраак кыймылды талап кылат. 9000% — иш жүзүндөгү эң жогорку маани жана болжол менен токсоннан бир кыймылды талап кылат. Оң чыкылдатуудагы R бусту жана Shift автоматтык цикли өзгөрбөйт.",\n    "Filipino": "Para lamang sa mouse mode. Ino-off ng 0% ang boost na napapagana sa pag-swipe ng mouse. Ang mga value na mas mababa sa 100% ay nangangailangan ng mas malaking galaw; pinapanatili ng 100% ang default na threshold; ang mga value na mas mataas sa 100% ay nangangailangan ng mas maliit na galaw. Ang 9000% ang epektibong maximum at nangangailangan ng humigit-kumulang isang bahagi sa siyamnapu ng galaw. Hindi nagbabago ang right-click R boost at awtomatikong Shift cycle.",\n    "Swahili": "Kwa hali ya kipanya pekee. 0% huzima Boost inayowashwa kwa kutelezesha kipanya. Thamani zilizo chini ya 100% zinahitaji mwendo mkubwa zaidi; 100% huhifadhi kikomo chaguomsingi; thamani zilizo juu ya 100% zinahitaji mwendo mdogo zaidi. 9000% ndiyo kiwango cha juu kinachofanya kazi na huhitaji takriban sehemu moja ya tisini ya mwendo. Boost ya R kwa kubofya kulia na mzunguko otomatiki wa Shift havibadiliki."\n  }\n}')
EXPECTED_COUNT=76

def fail(msg:str)->None:
    print('[FAIL] '+msg)
    raise SystemExit(1)

def row(text:str,key:str)->str:
    pos=text.find('"'+key.replace('"','\\"')+'"')
    if pos<0: fail('missing source key: '+key)
    start=text.rfind('    {',0,pos)
    depth=0; ins=False; esc=False
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
    return ''

def values(r:str)->dict[str,str]:
    out={}
    for lang,raw in re.findall(r'\{MenuLangId::([A-Za-z0-9_]+),\s*"((?:\\.|[^"\\])*)"\},',r):
        if lang in out: fail('duplicate language: '+lang)
        out[lang]=raw.replace('\\"','"').replace('\\n','\n').replace('\\\\','\\')
    return out

def main()->int:
    text=PATH.read_text(encoding='utf-8')
    if 'MELONPRIME_MORPH_BOOST_9000_V7' not in text:
        fail('V7 translation marker missing')
    for key,expected in EXPECTED.items():
        got=values(row(text,key))
        if len(got)!=EXPECTED_COUNT: fail(f'{key}: expected {EXPECTED_COUNT} languages, found {len(got)}')
        for lang,want in expected.items():
            have=got.get(lang)
            if have!=want: fail(f'{key} / {lang} mismatch')
            if not have.strip(): fail(f'{key} / {lang} empty')
            if have==key: fail(f'{key} / {lang} is unchanged English')
            if re.match(r'^\s*[^:]{2,30}:\s*[A-Za-z]|^\s*.{2,30}\s-\s[A-Za-z]',have):
                fail(f'{key} / {lang} looks like a display-name leak')
    print('[PASS] Morph Ball Boost Assist translation rows: 76/76')
    print('[PASS] Focused reviewed languages: '+', '.join(next(iter(EXPECTED.values())).keys()))
    return 0
if __name__=='__main__': raise SystemExit(main())
