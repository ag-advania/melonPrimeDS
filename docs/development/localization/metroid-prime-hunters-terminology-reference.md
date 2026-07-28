# Metroid Prime Hunters 用語 76言語 Web調査報告

- 調査日: 2026-07-28
- 対象: Morph Ball、Morph Ball Boost、Transform、Normal Form (Biped Form)、Power Beam、Missile、Volt Driver、Magmaul、Imperialist、Judicator、Shock Coil、Battlehammer、Omega Cannon
- 調査制約: 公開Web資料のみ。MelonPrimeDSの翻訳キーやリポジトリ内翻訳値は参照していない。

## 1. 結論

1. **Morph Ballは一律に直訳しない。** 公式取扱説明書では、英語・ドイツ語は `Morph Ball`、スペイン語・イタリア語は `Morfosfera`、フランス語は `Boule morphing`、日本語は `モーフボール` である。ポルトガル語の現行Nintendo公式ページは `Morph Ball` を維持している。
2. **Morph Ball Boostは、ゲーム内公式語の短い名称を優先する。** 日本語は `ブーストボール`、英語・フランス語・ドイツ語は `BOOST`、スペイン語・イタリア語は `TURBO`。設定画面で機能を明示する場合だけ、各言語でMorph Ball相当語を付けた説明的名称にする。
3. **Transformは英語版の公式名称と完全一致しない。** 英語版取扱説明書ではUI名が `Alt-Form`、日本語版では `トランスフォームアイコン`。したがって、ボタン名なら各言語の「代替形態」、動作名なら各言語の「変形する」に分けるのが自然である。本表は動作ラベルとして翻訳した。
4. **Normal FormはBiped Formより自然な表示語を優先する。** 英語版は本文で `normal form` と `bipedal form` の両方を用い、日本語版は画面比較で `通常時`。本表は `通常形態（二足形態）` 相当とした。
5. **6種のサブウェポン固有名を全言語で意味翻訳しない。** 公式5欧州言語でも方針が大きく異なり、ドイツ語は英語名を維持する一方、スペイン語・フランス語・イタリア語は独自名へ変更している。公式名称が存在しない言語では英語の固有名を維持するのが最も安全である。
6. **Omega Cannonは提供された取扱説明書の6種サブウェポン一覧に含まれない。** 本表の各言語名は、固有名Omegaを保持して一般名詞Cannonだけを自然に訳した推奨表記であり、地域別ROM内の公式文字列としては扱わない。

## 2. 根拠レベル

| 等級 | 意味 | 使用方法 |
|---|---|---|
| A | Metroid Prime HuntersのNintendo公式取扱説明書で確認 | 原則そのまま採用 |
| B | 別のMetroid作品のNintendo公式ページで確認 | シリーズ継続用語として採用可能 |
| C | Wikipedia多言語ページ、各言語の一般的ゲーム用語、固有名維持方針を組み合わせた推奨訳 | ネイティブレビュー対象。公式名称とは表示しない |

## 3. 公式取扱説明書で確認できる6言語

| 言語 | Morph Ball | Boost | Transform系UI | Normal/Biped | Power Beam | Missile | サブウェポンセット |
|---|---|---|---|---|---|---|---|
| 日本語 | モーフボール | ブーストボール | トランスフォームアイコン | 通常時 | パワービーム | ミサイル | S-JA |
| English | Morph Ball | BOOST / Boost Ball | Alt-Form Icon | normal form / bipedal form | Power Beam | Missile | S-EN |
| Español | Morfosfera | TURBO | icono de la forma alternativa | forma normal | Rayo | Lanzamisiles | S-ES |
| Français | Boule morphing | BOOST | icône de forme alternative | apparence d’origine | Rayon de puissance | Lance-missiles | S-FR |
| Deutsch | Morph Ball | BOOST / Boost Ball | Alt.-Form-Symbol | bipedale Ausgangsform | Power Beam | Raketenwerfer | S-DE |
| Italiano | Morfosfera | TURBO | icona forma alternativa | forma normale | Raggio Energia | Missili | S-IT |

## 4. 76言語 推奨ローカライズ表

### 4.1 読み方

- `サブ武器`列は、後掲の6語セットを参照する。
- A/B以外は公式訳ではなく、UIとして破綻しにくい推奨訳。
- `Transform`は動作ラベルとして訳している。アイコン名にする場合は各言語で「代替形態」相当へ変更する。
- 右から左へ書く言語では、英語固有名と現地語の混在時にBiDi制御・括弧方向を実機確認する。

| # | code | 言語 | Morph Ball | Morph Ball Boost | Transform | Normal Form (Biped Form) | Power Beam | Missile | Omega Cannon | サブ武器 | 根拠 |
|---:|---|---|---|---|---|---|---|---|---|---|---|
| 1 | en | English | Morph Ball | Morph Ball Boost | Transform | Normal Form (Biped Form) | Power Beam | Missile | Omega Cannon | S-EN | A |
| 2 | ja | 日本語 | モーフボール | ブーストボール（UI表現: モーフボールブースト） | トランスフォーム | 通常形態（二足形態） | パワービーム | ミサイル | オメガキャノン | S-JA | A |
| 3 | fr | Français | Boule morphing | Boost de la boule morphing | Se transformer | Forme normale (forme bipède) | Rayon de puissance | Lance-missiles | Canon Oméga | S-FR | A |
| 4 | de | Deutsch | Morph Ball | Morph-Ball-Boost | Verwandeln | Normalform (bipedale Form) | Power Beam | Raketenwerfer | Omega-Kanone | S-DE | A |
| 5 | es | Español | Morfosfera | Turbo de la Morfosfera | Transformarse | Forma normal (forma bípeda) | Rayo | Lanzamisiles | Cañón Omega | S-ES | A |
| 6 | it | Italiano | Morfosfera | Turbo Morfosfera | Trasformarsi | Forma normale (forma bipede) | Raggio Energia | Missili | Cannone Omega | S-IT | A |
| 7 | pt-BR | Português (Brasil) | Morph Ball | Boost da Morph Ball | Transformar | Forma normal (forma bípede) | Power Beam | Míssil | Canhão Ômega | S-EN | B |
| 8 | pt-PT | Português (Portugal) | Morph Ball | Impulso da Morph Ball | Transformar | Forma normal (forma bípede) | Power Beam | Míssil | Canhão Ómega | S-EN | C |
| 9 | nl | Nederlands | Morph Ball | Morph Ball-boost | Transformeren | Normale vorm (tweevoetige vorm) | Krachtstraal | Raket | Omega-kanon | S-EN | C |
| 10 | ru | Русский | Морфосфера | Ускорение морфосферы | Трансформация | Обычная форма (двуногая форма) | Силовой луч | Ракета | Омега-пушка | S-EN | C |
| 11 | uk | Українська | Морфосфера | Прискорення морфосфери | Трансформація | Звичайна форма (двонога форма) | Силовий промінь | Ракета | Омега-гармата | S-EN | C |
| 12 | pl | Polski | Morph Ball | Przyspieszenie Morph Ball | Transformacja | Forma normalna (dwunożna) | Promień mocy | Pocisk rakietowy | Działo Omega | S-EN | C |
| 13 | cs | Čeština | Morph Ball | Zrychlení Morph Ball | Transformace | Normální forma (dvounohá) | Silový paprsek | Raketa | Omega kanón | S-EN | C |
| 14 | sk | Slovenčina | Morph Ball | Zrýchlenie Morph Ball | Transformácia | Normálna forma (dvojnohá) | Silový lúč | Raketa | Omega kanón | S-EN | C |
| 15 | hu | Magyar | Morph Ball | Morph Ball gyorsítás | Átalakulás | Normál forma (kétlábú forma) | Erősugár | Rakéta | Omega-ágyú | S-EN | C |
| 16 | ro | Română | Morph Ball | Accelerare Morph Ball | Transformare | Formă normală (formă bipedă) | Rază de putere | Rachetă | Tun Omega | S-EN | C |
| 17 | bg | Български | Морфосфера | Ускорение на морфосферата | Трансформация | Нормална форма (двукрака форма) | Силов лъч | Ракета | Омега оръдие | S-EN | C |
| 18 | sr-Latn | Srpski (latinica) | Morf lopta | Ubrzanje morf lopte | Transformacija | Normalni oblik (dvonožni oblik) | Zrak snage | Raketa | Omega top | S-EN | C |
| 19 | hr | Hrvatski | Morf kugla | Ubrzanje morf kugle | Transformacija | Normalni oblik (dvonožni oblik) | Zraka moći | Projektil | Omega top | S-EN | C |
| 20 | sl | Slovenščina | Morf krogla | Pospešek morf krogle | Preobrazba | Običajna oblika (dvonožna oblika) | Žarek moči | Raketa | Omega top | S-EN | C |
| 21 | bs | Bosanski | Morf kugla | Ubrzanje morf kugle | Transformacija | Normalni oblik (dvonožni oblik) | Zrak moći | Raketa | Omega top | S-EN | C |
| 22 | mk | Македонски | Морф топка | Забрзување на морф топката | Трансформација | Нормална форма (двоножна форма) | Зрак на моќ | Ракета | Омега-топ | S-EN | C |
| 23 | sq | Shqip | Topi Morph | Përshpejtimi i Topit Morph | Transformim | Forma normale (forma dykëmbëshe) | Rreze fuqie | Raketë | Topi Omega | S-EN | C |
| 24 | el | Ελληνικά | Μορφόσφαιρα | Επιτάχυνση Μορφόσφαιρας | Μεταμόρφωση | Κανονική μορφή (δίποδη μορφή) | Ακτίνα ισχύος | Πύραυλος | Κανόνι Ωμέγα | S-EN | C |
| 25 | tr | Türkçe | Morph Ball | Morph Ball Hızlandırması | Dönüşüm | Normal Form (İki Ayaklı Form) | Güç Işını | Füze | Omega Topu | S-EN | C |
| 26 | sv | Svenska | Morph Ball | Morph Ball-boost | Förvandla | Normal form (tvåbent form) | Kraftstråle | Missil | Omegakanon | S-EN | C |
| 27 | nb | Norsk bokmål | Morph Ball | Morph Ball-boost | Forvandle | Normal form (tobeint form) | Kraftstråle | Missil | Omegakanon | S-EN | C |
| 28 | da | Dansk | Morph Ball | Morph Ball-boost | Forvandl | Normal form (tobenet form) | Kraftstråle | Missil | Omega-kanon | S-EN | C |
| 29 | fi | Suomi | Morph Ball | Morph Ball -kiihdytys | Muuntaudu | Normaali muoto (kaksijalkainen muoto) | Voimasäde | Ohjus | Omega-tykki | S-EN | C |
| 30 | is | Íslenska | Morph Ball | Morph Ball-hröðun | Umbreyta | Venjulegt form (tvífætt form) | Kraftgeisli | Flugskeyti | Omega-fallbyssa | S-EN | C |
| 31 | et | Eesti | Morph Ball | Morph Balli kiirendus | Muundu | Tavavorm (kahejalgne vorm) | Jõukiir | Rakett | Omega-kahur | S-EN | C |
| 32 | lv | Latviešu | Morph Ball | Morph Ball paātrinājums | Pārveidoties | Parastā forma (divkājainā forma) | Spēka stars | Raķete | Omega lielgabals | S-EN | C |
| 33 | lt | Lietuvių | Morph Ball | Morph Ball pagreitis | Transformuotis | Įprasta forma (dvikojė forma) | Galios spindulys | Raketa | Omega patranka | S-EN | C |
| 34 | ga | Gaeilge | Liathróid Morph | Treisiú Liathróid Morph | Claochlaigh | Gnáthfhoirm (foirm dhéchosach) | Ga Cumhachta | Diúracán | Gunna Omega | S-EN | C |
| 35 | cy | Cymraeg | Pêl Morph | Hwb Pêl Morph | Trawsnewid | Ffurf arferol (ffurf ddeudroed) | Pelydr Pŵer | Taflegryn | Canon Omega | S-EN | C |
| 36 | ca | Català | Morfosfera | Impuls de la morfosfera | Transformar-se | Forma normal (forma bípeda) | Raig de potència | Míssil | Canó Omega | S-EN | C |
| 37 | eu | Euskara | Morfosfera | Morfosferaren bultzada | Eraldatu | Forma normala (forma bipedoa) | Potentzia-izpia | Misila | Omega kanoia | S-EN | C |
| 38 | gl | Galego | Morfosfera | Impulso da morfosfera | Transformarse | Forma normal (forma bípede) | Raio de potencia | Mísil | Canón Omega | S-EN | C |
| 39 | af | Afrikaans | Morph Ball | Morph Ball-versnelling | Transformeer | Normale vorm (tweevoetige vorm) | Kragstraal | Missiel | Omega-kanon | S-EN | C |
| 40 | sw | Kiswahili | Mpira wa Morph | Msukumo wa Mpira wa Morph | Badilika | Umbo la kawaida (umbo la miguu miwili) | Miale ya nguvu | Kombora | Kanuni ya Omega | S-EN | C |
| 41 | zu | isiZulu | Ibhola le-Morph | Isivinini sebhola le-Morph | Guquka | Isimo esivamile (esinemilenze emibili) | Umsebe wamandla | Umcibisholo | Inganono ye-Omega | S-EN | C |
| 42 | xh | isiXhosa | Ibhola le-Morph | Ukukhawulezisa iBhola le-Morph | Guquka | Imo eqhelekileyo (imo enemilenze emibini) | Umtha wamandla | Umjukujelwa | Inkanunu ye-Omega | S-EN | C |
| 43 | am | አማርኛ | ሞርፍ ቦል | የሞርፍ ቦል ፍጥነት ጭማሪ | ተለወጥ | መደበኛ ቅርጽ (ባለሁለት እግር ቅርጽ) | የኃይል ጨረር | ሚሳይል | ኦሜጋ መድፍ | S-EN | C |
| 44 | ar | العربية | مورف بول | تعزيز مورف بول | تحوّل | الهيئة العادية (الهيئة ثنائية القدمين) | شعاع القوة | صاروخ | مدفع أوميغا | S-EN | C |
| 45 | he | עברית | כדור מורף | האצת כדור מורף | שינוי צורה | צורה רגילה (צורה דו-רגלית) | קרן כוח | טיל | תותח אומגה | S-EN | C |
| 46 | fa | فارسی | مورف بال | شتاب مورف بال | تغییر شکل | حالت عادی (حالت دوپا) | پرتو قدرت | موشک | توپ امگا | S-EN | C |
| 47 | ur | اردو | مورف بال | مورف بال بوسٹ | تبدیل ہونا | عام حالت (دو پاؤں والی حالت) | پاور بیم | میزائل | اومیگا توپ | S-EN | C |
| 48 | hi | हिन्दी | मॉर्फ बॉल | मॉर्फ बॉल बूस्ट | रूपांतरण | सामान्य रूप (द्विपाद रूप) | पावर बीम | मिसाइल | ओमेगा तोप | S-EN | C |
| 49 | bn | বাংলা | মরফ বল | মরফ বল বুস্ট | রূপান্তর | স্বাভাবিক রূপ (দ্বিপদ রূপ) | পাওয়ার বিম | ক্ষেপণাস্ত্র | ওমেগা কামান | S-EN | C |
| 50 | pa | ਪੰਜਾਬੀ | ਮੋਰਫ ਬਾਲ | ਮੋਰਫ ਬਾਲ ਬੂਸਟ | ਰੂਪ ਬਦਲੋ | ਸਧਾਰਣ ਰੂਪ (ਦੋਪੈਰੀ ਰੂਪ) | ਪਾਵਰ ਬੀਮ | ਮਿਸਾਈਲ | ਓਮੇਗਾ ਤੋਪ | S-EN | C |
| 51 | gu | ગુજરાતી | મોર્ફ બોલ | મોર્ફ બોલ બૂસ્ટ | રૂપાંતર | સામાન્ય સ્વરૂપ (દ્વિપાદ સ્વરૂપ) | પાવર બીમ | મિસાઇલ | ઓમેગા તોપ | S-EN | C |
| 52 | mr | मराठी | मॉर्फ बॉल | मॉर्फ बॉल बूस्ट | रूपांतर | सामान्य रूप (द्विपाद रूप) | पॉवर बीम | क्षेपणास्त्र | ओमेगा तोफ | S-EN | C |
| 53 | ta | தமிழ் | மோர்ஃப் பால் | மோர்ஃப் பால் பூஸ்ட் | உருமாறு | இயல்பு வடிவம் (இருகால் வடிவம்) | பவர் பீம் | ஏவுகணை | ஒமேகா பீரங்கி | S-EN | C |
| 54 | te | తెలుగు | మార్ఫ్ బాల్ | మార్ఫ్ బాల్ బూస్ట్ | రూపాంతరం | సాధారణ రూపం (ద్విపాద రూపం) | పవర్ బీమ్ | క్షిపణి | ఒమెగా ఫిరంగి | S-EN | C |
| 55 | ml | മലയാളം | മോർഫ് ബോൾ | മോർഫ് ബോൾ ബൂസ്റ്റ് | രൂപാന്തരപ്പെടുക | സാധാരണ രൂപം (ദ്വിപാദ രൂപം) | പവർ ബീം | മിസൈൽ | ഒമേഗ പീരങ്കി | S-EN | C |
| 56 | kn | ಕನ್ನಡ | ಮಾರ್ಫ್ ಬಾಲ್ | ಮಾರ್ಫ್ ಬಾಲ್ ಬೂಸ್ಟ್ | ರೂಪಾಂತರ | ಸಾಮಾನ್ಯ ರೂಪ (ದ್ವಿಪಾದ ರೂಪ) | ಪವರ್ ಬೀಮ್ | ಕ್ಷಿಪಣಿ | ಒಮೆಗಾ ಫಿರಂಗಿ | S-EN | C |
| 57 | ne | नेपाली | मोर्फ बल | मोर्फ बल बुस्ट | रूपान्तरण | सामान्य रूप (द्विपाद रूप) | पावर बिम | क्षेप्यास्त्र | ओमेगा तोप | S-EN | C |
| 58 | si | සිංහල | මෝර්ෆ් බෝල් | මෝර්ෆ් බෝල් බූස්ට් | පරිවර්තනය | සාමාන්‍ය ස්වරූපය (දෙපා ස්වරූපය) | පවර් බීම් | මිසයිලය | ඔමේගා කාලතුවක්කුව | S-EN | C |
| 59 | th | ไทย | มอร์ฟบอล | บูสต์มอร์ฟบอล | แปลงร่าง | ร่างปกติ (ร่างสองขา) | พาวเวอร์บีม | มิสไซล์ | ปืนใหญ่โอเมกา | S-EN | C |
| 60 | vi | Tiếng Việt | Morph Ball | Tăng tốc Morph Ball | Biến hình | Dạng thường (dạng hai chân) | Tia sức mạnh | Tên lửa | Pháo Omega | S-EN | C |
| 61 | id | Bahasa Indonesia | Morph Ball | Dorongan Morph Ball | Bertransformasi | Wujud normal (wujud bipedal) | Sinar Daya | Misil | Meriam Omega | S-EN | C |
| 62 | ms | Bahasa Melayu | Morph Ball | Lonjakan Morph Ball | Berubah bentuk | Bentuk normal (bentuk dua kaki) | Sinar Kuasa | Peluru berpandu | Meriam Omega | S-EN | C |
| 63 | fil | Filipino | Morph Ball | Boost ng Morph Ball | Mag-transform | Normal na anyo (anyong dalawang paa) | Power Beam | Misil | Kanyon Omega | S-EN | C |
| 64 | zh-Hans | 简体中文 | 变形球 | 变形球加速 | 变形 | 普通形态（双足形态） | 力量光束 | 导弹 | 欧米伽加农炮 | S-EN | C |
| 65 | zh-Hant | 繁體中文 | 變形球 | 變形球加速 | 變形 | 一般形態（雙足形態） | 力量光束 | 飛彈 | 歐米伽加農炮 | S-EN | C |
| 66 | ko | 한국어 | 모프 볼 | 모프 볼 부스트 | 변신 | 노멀 폼(이족 보행 형태) | 파워 빔 | 미사일 | 오메가 캐논 | S-EN | C |
| 67 | my | မြန်မာ | မော်ဖ်ဘောလုံး | မော်ဖ်ဘောလုံး အရှိန်မြှင့် | အသွင်ပြောင်း | ပုံမှန်ပုံစံ (ခြေနှစ်ချောင်းပုံစံ) | ပါဝါဘီမ် | ဒုံးကျည် | အိုမီဂါအမြောက် | S-EN | C |
| 68 | km | ខ្មែរ | បាល់ Morph | បង្កើនល្បឿនបាល់ Morph | បម្លែងរាង | ទម្រង់ធម្មតា (ទម្រង់ជើងពីរ) | កាំរស្មីថាមពល | មីស៊ីល | កាណុងអូមេហ្គា | S-EN | C |
| 69 | lo | ລາວ | ມອຟບອນ | ບູສມອຟບອນ | ປ່ຽນຮູບ | ຮູບແບບປົກກະຕິ (ຮູບແບບສອງຂາ) | ລຳແສງພະລັງ | ຂີປະນາວຸດ | ປືນໃຫຍ່ໂອເມກາ | S-EN | C |
| 70 | mn | Монгол | Морф бөмбөг | Морф бөмбөгийн хурдасгал | Хувирах | Ердийн хэлбэр (хоёр хөлт хэлбэр) | Хүчний цацраг | Пуужин | Омега их буу | S-EN | C |
| 71 | kk | Қазақша | Морф добы | Морф добының үдеуі | Түрлену | Қалыпты пішін (екі аяқты пішін) | Қуат сәулесі | Зымыран | Омега зеңбірегі | S-EN | C |
| 72 | uz-Latn | O‘zbekcha | Morph to‘pi | Morph to‘pi tezlatgichi | O‘zgarish | Oddiy shakl (ikki oyoqli shakl) | Quvvat nuri | Raketa | Omega to‘pi | S-EN | C |
| 73 | az | Azərbaycanca | Morf topu | Morf topu sürətləndirilməsi | Çevrilmə | Normal forma (ikiayaqlı forma) | Güc şüası | Raket | Omeqa topu | S-EN | C |
| 74 | hy | Հայերեն | Մորֆ գնդակ | Մորֆ գնդակի արագացում | Կերպափոխվել | Սովորական ձև (երկոտանի ձև) | Ուժային ճառագայթ | Հրթիռ | Օմեգա թնդանոթ | S-EN | C |
| 75 | ka | ქართული | მორფ ბურთი | მორფ ბურთის აჩქარება | გარდაქმნა | ჩვეულებრივი ფორმა (ორფეხა ფორმა) | ძალის სხივი | რაკეტა | ომეგა ქვემეხი | S-EN | C |
| 76 | ps | پښتو | مورف بال | د مورف بال چټکتیا | بدلون | عادي بڼه (دوه پښو بڼه) | د ځواک وړانګه | توغندی | اومیګا توپ | S-EN | C |

## 5. サブウェポン固有名セット

列順: **Volt Driver / Magmaul / Imperialist / Judicator / Shock Coil / Battlehammer**

| セット | Volt Driver | Magmaul | Imperialist | Judicator | Shock Coil | Battlehammer | 根拠 |
|---|---|---|---|---|---|---|---|
| S-EN | Volt Driver | Magmaul | Imperialist | Judicator | Shock Coil | Battlehammer | 英語公式名。非公式言語の安全な既定値 |
| S-JA | ボルトドライバー | マグモール | インペリアリスト | ジュディケイター | ショックコイル | バトルハンマー | Nintendo公式MPH取扱説明書 |
| S-ES | Voltric | Magmaul | Imperialist | Judicator | Neutrinarm | Destruktor | Nintendo公式MPH取扱説明書 |
| S-FR | Voltar | Magma | Impérialiste | Justicier | Hélicoïchoc | Marteau | Nintendo公式MPH取扱説明書 |
| S-DE | Volt Driver | Magmaul | Imperialist | Judicator | Shock Coil | Battlehammer | Nintendo公式MPH取扱説明書 |
| S-IT | Raggio Voltaico | Raggio Magmatico | Raggio Imperium | Raggio del Giudizio | Raggio Shock | Raggio da Guerra | Nintendo公式MPH取扱説明書 |

### 5.1 重要な実装判断

- スペイン語版は英語名の直訳ではなく、`Destruktor`、`Voltric`、`Neutrinarm`など独自の製品名を使う。
- フランス語版も `Voltar`、`Magma`、`Hélicoïchoc`、`Marteau`など独自名を使う。
- イタリア語版は6種をすべて `Raggio ...` 系へ再命名している。
- ドイツ語版は6種の英語名をほぼそのまま維持する。
- この差から、未公式言語で意味を推測して新しい武器名を作るのは危険。S-ENを既定値とし、公式ローカライズ資料が見つかった場合のみ言語別セットを追加する。

## 6. 用語別の推奨ルール

### 6.1 Morph Ball

- 公式表記がある言語は必ずそれを優先する。
- ラテン文字圏で公式表記がない場合は `Morph Ball` 維持を第一候補とする。
- 非ラテン文字圏では、シリーズ内で定着した音写がある場合は音写、ない場合は短い説明訳を採用する。
- `morph`を一般動詞として逐語訳すると、Metroid固有能力名として認識されにくくなるため注意する。

### 6.2 Morph Ball Boost

- ゲーム操作名としては短い `Boost` / `Turbo` / `ブーストボール` 系を優先。
- 設定項目として単独表示する場合は、何のブーストか判別できるようMorph Ball相当語を含める。
- 日本語の最も原作寄りな表示は `ブーストボール`。MelonPrimeDS独自設定として説明性を優先するなら `モーフボールブースト`。

### 6.3 Transform

- 原作UIに合わせるなら `Alt-Form` / 「代替形態」。
- 操作コマンドに合わせるなら「変形」「変身」「変形する」相当。
- 名詞と動詞を同じ翻訳キーで共用しない。例えばフランス語では名詞 `forme alternative` と動作 `se transformer` が異なる。

### 6.4 Normal Form (Biped Form)

- プレイヤー向け表示は各言語の「通常形態」を主語にする。
- 二足形態は括弧内説明に留める。Hunterによって通常形態の外見が異なるため、英語 `Biped Form` を全言語へ強制しない。

### 6.5 Power Beam / Missile

- 公式MPH内でもPower Beamは、スペイン語 `Rayo`、フランス語 `Rayon de puissance`、ドイツ語 `Power Beam`、イタリア語 `Raggio Energia`と大きく異なる。
- Missileは武器選択UIか弾薬名かで単数・複数・ランチャー表現が変わる。MPHの武器選択に合わせる場合、スペイン語とフランス語とドイツ語ではランチャー名を使う。

### 6.6 Omega Cannon

- `Omega`は固有要素として保持し、`Cannon`のみ各言語の自然な重火器名へ訳す。
- `Omega`の転写方式は各言語の一般的なギリシャ文字名に合わせる。
- 公式ROM文字列が確認できた場合は本表よりROM表記を優先する。

## 7. UI実装時の注意

1. 長い言語では `Normal Form (Biped Form)` が非常に長くなる。ボタン幅を固定せず、ツールチップまたは2行表示を用意する。
2. Arabic、Hebrew、Persian、Urdu、PashtoはRTL。英語固有名を混在させる場合はUnicode BiDi分離を行う。
3. Hindi、Bengali、Punjabi、Gujarati、Marathi、Tamil、Telugu、Malayalam、Kannada、Nepali、Sinhala、Thai、Myanmar、Khmer、Lao、Amharicは結合文字・字形形成を前提に、文字単位の切り詰めを行わない。
4. 中国語は簡体字と繁体字を別エントリにする。`导弹`と`飛彈`など地域差を維持する。
5. PortugueseはBrazilとPortugalで `Ômega` / `Ómega` の差がある。
6. 固有名6種は翻訳文中でも表記を統一し、同じ言語内で英語名と独自名を混在させない。

## 8. 採用前にネイティブ確認を優先する言語

C等級のうち、特に次は語形変化・ゲーム慣用語・文字方向の確認を優先する。

- Arabic、Hebrew、Persian、Urdu、Pashto
- Amharic、Swahili、Zulu、Xhosa
- Hindi、Bengali、Punjabi、Gujarati、Marathi、Tamil、Telugu、Malayalam、Kannada、Nepali、Sinhala
- Myanmar、Khmer、Lao
- Serbian、Croatian、Bosnian、Slovenian、Macedonian、Albanian
- Kazakh、Uzbek、Azerbaijani、Armenian、Georgian、Mongolian

## 9. 参照した公開Web資料

- [日本語版取扱説明書](https://www.nintendo.co.jp/data/software/manual/AMHJ_J.pdf) - 印刷ページ22～31。モーフボール、トランスフォームアイコン、通常時、ブーストボール、パワービーム、ミサイル、6種サブウェポンを確認。
- [英語版取扱説明書](https://www.nintendo.com/eu/media/downloads/games_8/emanuals/nintendo_ds_21/Manual_NintendoDS_MetroidPrimeHunters_EN.pdf) - 印刷ページ21～31。Morph Ball、normal/bipedal form、BOOST、Power Beam、Missile、英語固有名を確認。
- [スペイン語版取扱説明書](https://www.nintendo.com/eu/media/downloads/games_8/emanuals/nintendo_ds_21/Manual_NintendoDS_MetroidPrimeHunters_ES.pdf) - 印刷ページ21～31。Morfosfera、forma normal、TURBO、Rayo、Lanzamisiles、スペイン語版固有名を確認。
- [フランス語版取扱説明書](https://www.nintendo.com/eu/media/downloads/games_8/emanuals/nintendo_ds_21/Manual_NintendoDS_MetroidPrimeHunters_FR.pdf) - 印刷ページ21～31。Boule morphing、apparence d’origine、BOOST、Rayon de puissance、Lance-missiles、フランス語版固有名を確認。
- [ドイツ語版取扱説明書](https://www.nintendo.com/eu/media/downloads/games_8/emanuals/nintendo_ds_21/Manual_NintendoDS_MetroidPrimeHunters_DE.pdf) - 印刷ページ21～31。Morph Ball、bipedale Ausgangsform、BOOST、Power Beam、Raketenwerfer、英語固有名維持を確認。
- [イタリア語版取扱説明書](https://www.nintendo.com/eu/media/downloads/games_8/emanuals/nintendo_ds_21/Manual_NintendoDS_MetroidPrimeHunters_IT.pdf) - 印刷ページ21～31。Morfosfera、forma normale、TURBO、Raggio Energia、Missili、イタリア語版固有名を確認。
- [Nintendo公式 Metroid Dread スペイン語ページ](https://metroid.nintendo.com/dread/es/samus/) - 後年の公式用語でもMorfosferaとMisilesが継続していることを確認。
- [Nintendo公式 Metroid Dread ポルトガル語ページ](https://metroid.nintendo.com/dread/pt/samus/) - ポルトガル語公式ページがMorph Ball、Missile、Power等の英語名を維持する方針を確認。
- [Nintendo公式フランス語ページ](https://www.nintendo.com/fr-fr/Jeux/Jeux-Nintendo-Switch/Metroid-Dread-1987653.html) - Boule morphingがシリーズ公式表記として継続していることを確認。
- [Nintendo公式イタリア語ページ](https://www.nintendo.com/it-it/Giochi/NES/Metroid--275726.html) - Morfosferaがシリーズ公式表記として継続していることを確認。
- [Wikipedia メトロイドシリーズ](https://ja.wikipedia.org/wiki/メトロイドシリーズ) - 多言語ページの所在と各言語圏でのシリーズ用語探索の起点として使用。
- [Wikipedia サムス・アラン](https://ja.wikipedia.org/wiki/サムス・アラン) - 各言語版の装備・能力記述を横断するための起点として使用。
- [Wikipedia メトロイドプライム ハンターズ](https://ja.wikipedia.org/wiki/メトロイドプライム_ハンターズ) - 日本語での武器説明と固有名確認の補助資料。

## 10. 免責と更新方針

- A等級は、指定されたNintendo公式取扱説明書で確認した表記。
- B等級は、別作品を含むNintendo公式Webページで確認したシリーズ用語。
- C等級は、公開Web上の言語資料とゲームUIとしての自然さを基にした推奨案であり、Nintendoの公式訳ではない。
- 新たな地域版マニュアル、ROM内テキスト、Nintendo公式ページが見つかった場合は、CからA/Bへ格上げし、該当言語のサブウェポンセットを独立させる。

