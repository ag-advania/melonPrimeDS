#!/usr/bin/env python3
"""Apply hand-reviewed new-language translations to loc_chunk_3_full.json."""

import json
from pathlib import Path

NEW_LANGS = [
    "ar", "id", "uk", "el", "sv", "th", "cs", "da", "tr", "nb", "hu", "fi", "vi", "pl", "ro",
]


def t(ar, id_, uk, el, sv, th, cs, da, tr_, nb, hu, fi, vi, pl, ro):
    return dict(zip(NEW_LANGS, [ar, id_, uk, el, sv, th, cs, da, tr_, nb, hu, fi, vi, pl, ro]))


# Per-entry translations (index 0-191)
T = [
    # 0 Save
    t("حفظ", "Simpan", "Зберегти", "Αποθήκευση", "Spara", "บันทึก", "Uložit", "Gem", "Kaydet", "Lagre", "Mentés", "Tallenna", "Lưu", "Zapisz", "Salvare"),
    # 1 Copy Output to Input
    t("نسخ الإخراج إلى الإدخال", "Salin output ke input", "Копіювати вихід у вхід", "Αντιγραφή εξόδου σε είσοδο", "Kopiera utdata till indata", "คัดลอกเอาต์พุตไปยังอินพุต", "Kopírovat výstup do vstupu", "Kopiér output til input", "Çıktıyı girişe kopyala", "Kopier utdata til inndata", "Kimenet másolása bemenetbe", "Kopioi tuloste syötteeseen", "Sao chép đầu ra sang đầu vào", "Kopiuj wyjście do wejścia", "Copiază ieșirea în intrare"),
    # 2 Pick Color
    t("اختيار لون", "Pilih warna", "Вибрати колір", "Επιλογή χρώματος", "Välj färg", "เลือกสี", "Vybrat barvu", "Vælg farve", "Renk seç", "Velg farge", "Szín választása", "Valitse väri", "Chọn màu", "Wybierz kolor", "Alege culoarea"),
    # 3 OVR
    t("إجمالي", "Umum", "Заг.", "Γεν.", "Allm.", "รวม", "Celk.", "Samlet", "Gen.", "Tot.", "Össz.", "Kok.", "Tổng", "Ogół.", "Gen."),
    # 4 Zoomed (Scope)
    t("مكبّر (منظار)", "Diperbesar (Scope)", "Збільшено (приціл)", "Με ζουμ (σκόπευτήρας)", "Inzoomad (sikte)", "ซูม (สโคป)", "Přiblíženo (zaměřovač)", "Zoom (sigte)", "Yakınlaştırılmış (nişangah)", "Innzoomet (sikte)", "Nagyítva (távcső)", "Zoomattu (tähtäin)", "Phóng to (Scope)", "Powiększenie (celownik)", "Zoom (lunetă)"),
    # 5 Custom
    t("مخصص", "Kustom", "Користувацький", "Προσαρμοσμένο", "Anpassad", "กำหนดเอง", "Vlastní", "Brugerdefineret", "Özel", "Tilpasset", "Egyéni", "Mukautettu", "Tùy chỉnh", "Niestandardowy", "Personalizat"),
    # 6 Menu Language
    t("لغة القائمة", "Bahasa menu", "Мова меню", "Γλώσσα μενού", "Menyspråk", "ภาษาเมนู", "Jazyk menu", "Menusprog", "Menü dili", "Menyspråk", "Menü nyelve", "Valikon kieli", "Ngôn ngữ menu", "Język menu", "Limba meniului"),
    # 7 File->Open ROM...
    t("ملف → فتح ROM...", "File → Buka ROM...", "Файл → Відкрити ROM...", "Αρχείο → Άνοιγμα ROM...", "Arkiv → Öppna ROM...", "ไฟล์ → เปิด ROM...", "Soubor → Otevřít ROM...", "Fil → Åbn ROM...", "Dosya → ROM aç...", "Fil → Åpne ROM...", "Fájl → ROM megnyitása...", "Tiedosto → Avaa ROM...", "Tệp → Mở ROM...", "Plik → Otwórz ROM...", "Fișier → Deschide ROM..."),
    # 8 GBA slot
    t("فتحة GBA", "Slot GBA", "Слот GBA", "Θύρα GBA", "GBA-plats", "ช่อง GBA", "Slot GBA", "GBA-slot", "GBA yuvası", "GBA-spor", "GBA foglalat", "GBA-paikka", "Khe GBA", "Gniazdo GBA", "Slot GBA"),
    # 9 Import savefile
    t("استيراد ملف الحفظ", "Impor file save", "Імпортувати збереження", "Εισαγωγή αρχείου αποθήκευσης", "Importera sparfil", "นำเข้าไฟล์เซฟ", "Importovat uloženou hru", "Importer savefil", "Kayıt dosyası içe aktar", "Importer lagringsfil", "Mentés importálása", "Tuo tallennustiedosto", "Nhập file save", "Importuj zapis", "Importă fișier de salvare"),
    # 10 Open melonDS directory
    t("فتح مجلد melonDS", "Buka direktori melonDS", "Відкрити папку melonDS", "Άνοιγμα φακέλου melonDS", "Öppna melonDS-mapp", "เปิดโฟลเดอร์ melonDS", "Otevřít složku melonDS", "Åbn melonDS-mappe", "melonDS klasörünü aç", "Åpne melonDS-mappe", "melonDS mappa megnyitása", "Avaa melonDS-kansio", "Mở thư mục melonDS", "Otwórz folder melonDS", "Deschide directorul melonDS"),
    # 11 Frame step
    t("تقدم إطار", "Langkah frame", "Крок кадру", "Βήμα καρέ", "Bildsteg", "ขั้นเฟรม", "Krok snímku", "Billedtrin", "Kare adımı", "Bildesteg", "Képkocka léptetés", "Ruudun askel", "Bước khung hình", "Krok klatki", "Pas cadru"),
    # 12 ROM info
    t("معلومات ROM", "Info ROM", "Інфо ROM", "Πληροφορίες ROM", "ROM-info", "ข้อมูล ROM", "Info ROM", "ROM-info", "ROM bilgisi", "ROM-info", "ROM infó", "ROM-tiedot", "Thông tin ROM", "Info ROM", "Info ROM"),
    # 13 Host LAN game
    t("استضافة لعبة LAN", "Host game LAN", "Хостити LAN-гру", "Φιλοξενία παιχνιδιού LAN", "Värd LAN-spel", "โฮสต์เกม LAN", "Hostovat LAN hru", "Vært LAN-spil", "LAN oyunu barındır", "Vert LAN-spill", "LAN játék hosztolása", "Isännöi LAN-peliä", "Chủ trì game LAN", "Hostuj grę LAN", "Găzduiește joc LAN"),
    # 14 Screen gap
    t("فجوة الشاشة", "Jarak layar", "Зазор між екранами", "Κενό οθόνης", "Skärmavstånd", "ช่องว่างหน้าจอ", "Mezera obrazovek", "Skærmafstand", "Ekran aralığı", "Skjermavstand", "Képernyőrés", "Näyttöväli", "Khoảng cách màn hình", "Odstęp ekranów", "Spațiu ecran"),
    # 15 Hybrid
    t("هجين", "Hibrida", "Гібридний", "Υβριδικό", "Hybrid", "ไฮบริด", "Hybridní", "Hybrid", "Hibrit", "Hybrid", "Hibrid", "Hybridi", "Lai ghép", "Hybrydowy", "Hibrid"),
    # 16 Emphasize bottom
    t("إبراز الشاشة السفلية", "Tekankan layar bawah", "Виділити нижній екран", "Έμφαση κάτω οθόνης", "Betona nedre skärm", "เน้นหน้าจอล่าง", "Zvýraznit spodní obrazovku", "Fremhæv nederste skærm", "Alt ekranı vurgula", "Fremhev nedre skjerm", "Alsó képernyő kiemelése", "Korosta alanäyttöä", "Nhấn màn hình dưới", "Podkreśl dolny ekran", "Evidențiază ecranul inferior"),
    # 17 Open new window
    t("فتح نافذة جديدة", "Buka jendela baru", "Відкрити нове вікно", "Άνοιγμα νέου παραθύρου", "Öppna nytt fönster", "เปิดหน้าต่างใหม่", "Otevřít nové okno", "Åbn nyt vindue", "Yeni pencere aç", "Åpne nytt vindu", "Új ablak megnyitása", "Avaa uusi ikkuna", "Mở cửa sổ mới", "Otwórz nowe okno", "Deschide fereastră nouă"),
    # 18 Preferences...
    t("التفضيلات...", "Preferensi...", "Налаштування...", "Προτιμήσεις...", "Inställningar...", "การตั้งค่า...", "Předvolby...", "Indstillinger...", "Tercihler...", "Innstillinger...", "Beállítások...", "Asetukset...", "Tùy chọn...", "Preferencje...", "Preferințe..."),
    # 19 Multiplayer settings
    t("إعدادات اللعب الجماعي", "Pengaturan multipemain", "Налаштування мультиплеєра", "Ρυθμίσεις multiplayer", "Flerspelarinställningar", "การตั้งค่าเล่นหลายคน", "Nastavení multiplayeru", "Multiplayer-indstillinger", "Çok oyunculu ayarları", "Flerspillerinnstillinger", "Többjátékos beállítások", "Moninpeliasetukset", "Cài đặt nhiều người chơi", "Ustawienia wieloosobowe", "Setări multiplayer"),
    # 20 Limit framerate
    t("تحديد معدل الإطارات", "Batasi framerate", "Обмежити частоту кадрів", "Περιορισμός ρυθμού καρέ", "Begränsa bildfrekvens", "จำกัดอัตราเฟรม", "Omezit snímkovou frekvenci", "Begræns billedhastighed", "Kare hızını sınırla", "Begrens bildefrekvens", "Képkockasebesség korlátozása", "Rajoita ruudunpäivitysnopeutta", "Giới hạn tốc độ khung hình", "Ogranicz liczbę klatek", "Limitează rata de cadre"),
    # 21 MelonPrime settings
    t("إعدادات MelonPrime", "Pengaturan MelonPrime", "Налаштування MelonPrime", "Ρυθμίσεις MelonPrime", "MelonPrime-inställningar", "การตั้งค่า MelonPrime", "Nastavení MelonPrime", "MelonPrime-indstillinger", "MelonPrime ayarları", "MelonPrime-innstillinger", "MelonPrime beállítások", "MelonPrime-asetukset", "Cài đặt MelonPrime", "Ustawienia MelonPrime", "Setări MelonPrime"),
    # 22 About...
    t("حول...", "Tentang...", "Про програму...", "Σχετικά...", "Om...", "เกี่ยวกับ...", "O programu...", "Om...", "Hakkında...", "Om...", "Névjegy...", "Tietoja...", "Giới thiệu...", "O programie...", "Despre..."),
    # 23 Custom HUD
    t("HUD مخصص", "HUD kustom", "Користувацький HUD", "Προσαρμοσμένο HUD", "Anpassad HUD", "HUD กำหนดเอง", "Vlastní HUD", "Brugerdefineret HUD", "Özel HUD", "Tilpasset HUD", "Egyéni HUD", "Mukautettu HUD", "HUD tùy chỉnh", "Niestandardowy HUD", "HUD personalizat"),
    # 24 INPUT METHOD
    t("طريقة الإدخال", "METODE INPUT", "МЕТОД ВВОДУ", "ΜΕΘΟΔΟΣ ΕΙΣΟΔΟΥ", "INMATNINGSMETOD", "วิธีป้อนข้อมูล", "METODA VSTUPU", "INPUTMETODE", "GİRİŞ YÖNTEMİ", "INNDATAMETODE", "BEVITELI MÓDSZER", "SYÖTTÖMENETELMÄ", "PHƯƠNG THỨC NHẬP", "METODA WEJŚCIA", "METODĂ DE INTRODUCERE"),
    # 25 LOW HP WARNING
    t("تحذير HP منخفض", "PERINGATAN HP RENDAH", "ПОПЕРЕДЖЕННЯ ПРО НИЗЬКИЙ HP", "ΠΡΟΕΙΔΟΠΟΙΗΣΗ ΧΑΜΗΛΟΥ HP", "VARNING LÅGT HP", "คำเตือน HP ต่ำ", "VAROVÁNÍ NÍZKÉHO HP", "ADVARSEL LAVT HP", "DÜŞÜK HP UYARISI", "ADVARSEL LAV HP", "ALACSONY HP FIGYELMEZTETÉS", "VAROITUS ALHAISESTA HP:STÄ", "CẢNH BÁO HP THẤP", "OSTRZEŻENIE NISKIEGO HP", "AVERTISMENT HP SCĂZUT"),
    # 26 Power-Up Pickup Effects
    t("تأثيرات التقاط Power-Up", "Efek pengambilan Power-Up", "Ефекти підбору Power-Up", "Εφέ μαζέματος Power-Up", "Effekter vid upplockning av power-ups", "เอฟเฟกต์เก็บ Power-Up", "Efekty sebrání power-upů", "Effekter ved opsamling af power-ups", "Power-Up toplama efektleri", "Effekter ved oppsamling av power-ups", "Power-Up felvételi effektek", "Power-Up-keräysefektit", "Hiệu ứng nhặt Power-Up", "Efekty podnoszenia power-upów", "Efecte la ridicarea power-up-urilor"),
    # 27 DEVELOPER ONLY
    t("للمطورين فقط", "KHUSUS PENGEMBANG", "ЛИШЕ ДЛЯ РОЗРОБНИКІВ", "ΜΟΝΟ ΓΙΑ ΠΡΟΓΡΑΜΜΑΤΙΣΤΕΣ", "ENDAST UTVECKLARE", "สำหรับนักพัฒนาเท่านั้น", "POUZE PRO VÝVOJÁŘE", "KUN UDVIKLERE", "YALNIZCA GELİŞTİRİCİLER", "KUN UTVIKLERE", "CSAK FEJLESZTŐKNEK", "VAIN KEHITTÄJILLE", "CHỈ DÀNH CHO NHÀ PHÁT TRIỂN", "TYLKO DLA DEWELOPERÓW", "DOAR PENTRU DEZVOLTATORI"),
    # 28 CROSSHAIR
    t("التقاطع", "BIDIK", "ПРИЦІЛ", "ΣΤΟΧΕΥΤΗΡΑΣ", "SIKTE", "เล็ง", "ZAMĚŘOVAČ", "SIGTEKORS", "NİŞANGAH", "TRÅDKORS", "IRÁNYZÉK", "TÄHTÄIN", "TÂM NGẮM", "CELOWNIK", "ȚINTĂ"),
    # 29 Keyboard mappings
    t("تعيينات لوحة المفاتيح", "Pemetaan keyboard", "Призначення клавіатури", "Αντιστοιχίσεις πληκτρολογίου", "Tangentbordsmappningar", "การแมปคีย์บอร์ด", "Mapování klávesnice", "Tastaturtilknytninger", "Klavye eşlemeleri", "Tastaturtilordninger", "Billentyűzet-hozzárendelések", "Näppäimistökartoitukset", "Ánh xạ bàn phím", "Mapowanie klawiatury", "Mapări tastatură"),
    # 30 [Metroid] (A) Move Left
    t("[Metroid] (A) تحريك لليسار", "[Metroid] (A) Gerak kiri", "[Metroid] (A) Вліво", "[Metroid] (A) Κίνηση αριστερά", "[Metroid] (A) Flytta vänster", "[Metroid] (A) เลื่อนซ้าย", "[Metroid] (A) Pohyb vlevo", "[Metroid] (A) Flyt til venstre", "[Metroid] (A) Sola git", "[Metroid] (A) Flytt til venstre", "[Metroid] (A) Balra mozgatás", "[Metroid] (A) Siirry vasemmalle", "[Metroid] (A) Di chuyển trái", "[Metroid] (A) Ruch w lewo", "[Metroid] (A) Mută stânga"),
    # 31 [Metroid] (Space) Jump
    t("[Metroid] (Space) قفز", "[Metroid] (Space) Lompat", "[Metroid] (Space) Стрибок", "[Metroid] (Space) Άλμα", "[Metroid] (Space) Hoppa", "[Metroid] (Space) กระโดด", "[Metroid] (Space) Skok", "[Metroid] (Space) Hop", "[Metroid] (Space) Zıpla", "[Metroid] (Space) Hopp", "[Metroid] (Space) Ugrás", "[Metroid] (Space) Hyppää", "[Metroid] (Space) Nhảy", "[Metroid] (Space) Skok", "[Metroid] (Space) Săritură"),
    # 32 [Metroid] (1) Weapon 1. ShockCoil
    t("[Metroid] (1) سلاح 1: ShockCoil", "[Metroid] (1) Senjata 1: ShockCoil", "[Metroid] (1) Зброя 1: ShockCoil", "[Metroid] (1) Όπλο 1: ShockCoil", "[Metroid] (1) Vapen 1: ShockCoil", "[Metroid] (1) อาวุธ 1: ShockCoil", "[Metroid] (1) Zbraň 1: ShockCoil", "[Metroid] (1) Våben 1: ShockCoil", "[Metroid] (1) Silah 1: ShockCoil", "[Metroid] (1) Våpen 1: ShockCoil", "[Metroid] (1) Fegyver 1: ShockCoil", "[Metroid] (1) Ase 1: ShockCoil", "[Metroid] (1) Vũ khí 1: ShockCoil", "[Metroid] (1) Broń 1: ShockCoil", "[Metroid] (1) Armă 1: ShockCoil"),
    # 33 [Metroid] (6) Weapon 6. VoltDriver
    t("[Metroid] (6) سلاح 6: VoltDriver", "[Metroid] (6) Senjata 6: VoltDriver", "[Metroid] (6) Зброя 6: VoltDriver", "[Metroid] (6) Όπλο 6: VoltDriver", "[Metroid] (6) Vapen 6: VoltDriver", "[Metroid] (6) อาวุธ 6: VoltDriver", "[Metroid] (6) Zbraň 6: VoltDriver", "[Metroid] (6) Våben 6: VoltDriver", "[Metroid] (6) Silah 6: VoltDriver", "[Metroid] (6) Våpen 6: VoltDriver", "[Metroid] (6) Fegyver 6: VoltDriver", "[Metroid] (6) Ase 6: VoltDriver", "[Metroid] (6) Vũ khí 6: VoltDriver", "[Metroid] (6) Broń 6: VoltDriver", "[Metroid] (6) Armă 6: VoltDriver"),
    # 34 [Metroid] (J) Next Weapon in the sorted order
    t("[Metroid] (J) السلاح التالي", "[Metroid] (J) Senjata berikutnya", "[Metroid] (J) Наступна зброя", "[Metroid] (J) Επόμενο όπλο", "[Metroid] (J) Nästa vapen", "[Metroid] (J) อาวุธถัดไป", "[Metroid] (J) Další zbraň", "[Metroid] (J) Næste våben", "[Metroid] (J) Sonraki silah", "[Metroid] (J) Neste våpen", "[Metroid] (J) Következő fegyver", "[Metroid] (J) Seuraava ase", "[Metroid] (J) Vũ khí tiếp theo", "[Metroid] (J) Następna broń", "[Metroid] (J) Următoarea armă"),
    # 35 [Metroid] (F) UI Ok
    t("[Metroid] (F) تأكيد UI", "[Metroid] (F) UI OK", "[Metroid] (F) Підтвердити UI", "[Metroid] (F) Επιβεβαίωση UI", "[Metroid] (F) UI OK", "[Metroid] (F) UI ตกลง", "[Metroid] (F) Potvrdit UI", "[Metroid] (F) UI OK", "[Metroid] (F) UI Tamam", "[Metroid] (F) UI OK", "[Metroid] (F) UI OK", "[Metroid] (F) UI OK", "[Metroid] (F) UI OK", "[Metroid] (F) UI OK", "[Metroid] (F) UI OK"),
    # 36 Aim sensitivity (default: 63)
    t("حساسية التصويب (افتراضي: 63)", "Sensitivitas bidik (default: 63)", "Чутливість прицілу (за замовч.: 63)", "Ευαισθησία στόχευσης (προεπιλογή: 63)", "Siktkänslighet (standard: 63)", "ความไวการเล็ง (ค่าเริ่มต้น: 63)", "Citlivost míření (výchozí: 63)", "Sigtekänslighed (standard: 63)", "Nişan hassasiyeti (varsayılan: 63)", "Siktesensitivitet (standard: 63)", "Célzás érzékenysége (alapértelmezett: 63)", "Tähtäysherkkyys (oletus: 63)", "Độ nhạy ngắm (mặc định: 63)", "Czułość celowania (domyślnie: 63)", "Sensibilitate țintire (implicit: 63)"),
    # 37 Instant Aim Follow (Developer Only)
    t("متابعة التصويب الفورية (للمطورين فقط)", "Ikuti bidik instan (Khusus pengembang)", "Миттєве слідування прицілу (лише для розробників)", "Άμεση παρακολούθηση στόχευσης (μόνο προγραμματιστές)", "Omedelbar siktning (endast utvecklare)", "ติดตามการเล็งทันที (เฉพาะนักพัฒนา)", "Okamžité sledování míření (pouze pro vývojáře)", "Øjeblikkelig sigtefølgning (kun udviklere)", "Anlık nişan takibi (Yalnızca geliştiriciler)", "Umiddelbar siktefølging (kun utviklere)", "Azonnali célzéskövetés (csak fejlesztőknek)", "Välitön tähtäysseuranta (vain kehittäjille)", "Theo dõi ngắm tức thì (Chỉ dành cho nhà phát triển)", "Natychmiastowe śledzenie celu (tylko dla deweloperów)", "Urmărire instantanee a țintirii (doar dezvoltatori)"),
    # 38 MPH audio headphones
    t("تعيين إعدادات صوت MPH إلى سماعات الرأس (موصى به). (غيّر إعدادًا في MPH واحفظ لتحديث بيانات الحفظ. ألغِ تحديد هذا الخيار بعد الحفظ)", "Atur audio MPH ke headphone (disarankan). (Ubah pengaturan di MPH dan simpan untuk memperbarui data save. Hapus centang opsi ini setelah menyimpan)", "Установити аудіо MPH на навушники (рекомендовано). (Змініть налаштування в MPH і збережіть, щоб оновити дані. Зніміть прапорець після збереження)", "Ορισμός ήχου MPH σε ακουστικά (συνιστάται). (Αλλάξτε ρύθμιση στο MPH και αποθηκεύστε για ενημέρωση. Αποεπιλέξτε μετά την αποθήκευση)", "Ställ in MPH-ljud på hörlurar (rekommenderas). (Ändra en inställning i MPH och spara för att uppdatera. Avmarkera efter sparning)", "ตั้งค่าเสียง MPH เป็นหูฟัง (แนะนำ) (เปลี่ยนการตั้งค่าใน MPH แล้วบันทึกเพื่ออัปเดตข้อมูลเซฟ ยกเลิกตัวเลือกนี้หลังบันทึก)", "Nastavit audio MPH na sluchátka (doporučeno). (Změňte nastavení v MPH a uložte pro aktualizaci. Po uložení zrušte zaškrtnutí)", "Indstil MPH-lyd til hovedtelefoner (anbefales). (Ændr en indstilling i MPH og gem for at opdatere. Fjern markering efter gem)", "MPH ses ayarlarını kulaklığa ayarla (önerilir). (MPH'de bir ayarı değiştirip kaydederek güncelleyin. Kaydettikten sonra işareti kaldırın)", "Sett MPH-lyd til hodetelefoner (anbefalt). (Endre en innstilling i MPH og lagre for å oppdatere. Fjern avkrysning etter lagring)", "MPH hang beállítása fejhallgatóra (ajánlott). (Módosíts egy beállítást az MPH-ben és mentsd a mentés frissítéséhez. Mentés után vedd ki a pipát)", "Aseta MPH-ääniasetukset kuulokkeisiin (suositeltu). (Muuta asetusta MPH:ssä ja tallenna päivittääksesi. Poista valinta tallennuksen jälkeen)", "Đặt âm thanh MPH sang tai nghe (khuyến nghị). (Thay đổi cài đặt trong MPH và lưu để cập nhật. Bỏ chọn sau khi lưu)", "Ustaw audio MPH na słuchawki (zalecane). (Zmień ustawienie w MPH i zapisz, aby zaktualizować. Odznacz po zapisaniu)", "Setează audio MPH pe căști (recomandat). (Schimbă o setare în MPH și salvează pentru actualizare. Debifează după salvare)"),
    # 39 Aim Sub-pixel Accumulator
    t("تفعيل مجمع التصويب دون البكسل (نقل حركة الفأرة الكسرية عبر الإطارات. فعّله لتصويب أنعم بحساسية منخفضة)", "Aktifkan Akumulator Sub-pixel Bidik (Bawa gerakan mouse fraksional antar frame. Aktifkan untuk bidik halus sensitivitas rendah)", "Увімкнути субпіксельний акумулятор прицілу (Переносить дробовий рух миші між кадрами. Для плавнішого прицілювання при низькій чутливості)", "Ενεργοποίηση συσσωρευτή υπο-pixel στόχευσης (Μεταφέρει κλασματική κίνηση ποντικιού μεταξύ καρέ. Για ομαλότερη στόχευση χαμηλής ευαισθησίας)", "Aktivera subpixel-siktackumulator (Överför bråkdel av musrörelse mellan bilder. För mjukare siktning vid låg känslighet)", "เปิดใช้ตัวสะสม Sub-pixel การเล็ง (ถ่ายโอนการเคลื่อนไหวเมาส์เศษส่วนข้ามเฟรม เปิดเพื่อเล็งลื่นไหลเมื่อความไวต่ำ)", "Povolit subpixelový akumulátor míření (Přenáší zlomkový pohyb myši mezi snímky. Pro plynulejší míření při nízké citlivosti)", "Aktivér subpixel sigteakkumulator (Overfører brøkdele af musebevægelse mellem billeder. For jævnere sigtning ved lav følsomhed)", "Nişan alt piksel biriktiriciyi etkinleştir (Kesirli fare hareketini kareler arasında taşır. Düşük hassasiyette daha akıcı nişan için)", "Aktiver subpiksel sikteakkumulator (Overfører brøkdel av musebevegelse mellom bilder. For jevnere sikting ved lav sensitivitet)", "Célzás subpixel akkumulátor engedélyezése (Tört egérmozgást visz át képkockák között. Alacsony érzékenységű simább célzáshoz)", "Ota käyttöön tähtäyksen subpikselikertymä (Siirtää murto-osa hiiren liikettä ruudulta toiselle. Tasaisempaan tähtäykseen alhaisella herkkyydellä)", "Bật Bộ tích lũy Sub-pixel ngắm (Chuyển chuyển động chuột phân số qua khung hình. Bật để ngắm mượt hơn ở độ nhạy thấp)", "Włącz akumulator subpikselowy celowania (Przenosi ułamkowy ruch myszy między klatkami. Dla płynniejszego celowania przy niskiej czułości)", "Activează acumulatorul sub-pixel de țintire (Transferă mișcarea fracțională a mouse-ului între cadre. Pentru țintire mai lină la sensibilitate scăzută)"),
    # 40 Enable Native Aim Delta Hook (PostFold Write)
    t("تفعيل Native Aim Delta Hook (كتابة PostFold)", "Aktifkan Native Aim Delta Hook (PostFold Write)", "Увімкнути нативний Aim Delta Hook (запис PostFold)", "Ενεργοποίηση Native Aim Delta Hook (εγγραφή PostFold)", "Aktivera Native Aim Delta Hook (PostFold-skrivning)", "เปิดใช้ Native Aim Delta Hook (PostFold Write)", "Povolit nativní Aim Delta Hook (zápis PostFold)", "Aktivér Native Aim Delta Hook (PostFold-skrivning)", "Native Aim Delta Hook'u etkinleştir (PostFold Write)", "Aktiver Native Aim Delta Hook (PostFold-skriving)", "Native Aim Delta Hook engedélyezése (PostFold írás)", "Ota käyttöön Native Aim Delta Hook (PostFold Write)", "Bật Native Aim Delta Hook (PostFold Write)", "Włącz natywny Aim Delta Hook (zapis PostFold)", "Activează Native Aim Delta Hook (scriere PostFold)"),
    # 41 Screen Sync Mode
    t("وضع مزامنة الشاشة: Off = بدون استدعاء sync، glFinish = انتظار اكتمال أوامر GL", "Mode sinkronisasi layar: Off = tanpa panggilan sync, glFinish = tunggu perintah GL selesai", "Режим синхронізації екрана: Off = без виклику sync, glFinish = очікування завершення GL-команд", "Λειτουργία συγχρονισμού οθόνης: Off = χωρίς κλήση sync, glFinish = αναμονή ολοκλήρωσης εντολών GL", "Skärmsynkroniseringsläge: Off = inget sync-anrop, glFinish = vänta på GL-kommandon", "โหมดซิงค์หน้าจอ: Off = ไม่เรียก sync, glFinish = รอคำสั่ง GL เสร็จ", "Režim synchronizace obrazovky: Off = bez volání sync, glFinish = čekání na dokončení GL příkazů", "Skærmsynkroniseringstilstand: Off = intet sync-kald, glFinish = vent på GL-kommandoer", "Ekran senkronizasyon modu: Off = sync çağrısı yok, glFinish = GL komutlarının tamamlanmasını bekle", "Skjermsynkroniseringsmodus: Off = ingen sync-kall, glFinish = vent på GL-kommandoer", "Képernyőszinkron mód: Off = nincs sync hívás, glFinish = GL parancsok befejezésének várása", "Näytön synkronointitila: Off = ei sync-kutsua, glFinish = odota GL-komentojen valmistumista", "Chế độ đồng bộ màn hình: Off = không gọi sync, glFinish = chờ lệnh GL hoàn tất", "Tryb synchronizacji ekranu: Off = brak wywołania sync, glFinish = oczekiwanie na zakończenie poleceń GL", "Mod sincronizare ecran: Off = fără apel sync, glFinish = așteaptă finalizarea comenzilor GL"),
    # 42 Auto (match Aspect Ratio)
    t("تلقائي (مطابقة نسبة العرض إلى الارتفاع)", "Otomatis (sesuaikan rasio aspek)", "Авто (за співвідношенням сторін)", "Αυτόματο (ταιριάζει με αναλογία διαστάσεων)", "Auto (matcha bildförhållande)", "อัตโนมัติ (ตรงอัตราส่วนภาพ)", "Auto (podle poměru stran)", "Auto (match billedformat)", "Otomatik (en boy oranını eşle)", "Auto (match sideforhold)", "Auto (képarány illesztése)", "Auto (sovita kuvasuhteeseen)", "Tự động (khớp tỷ lệ khung hình)", "Auto (dopasuj proporcje)", "Auto (potrivește raportul de aspect)"),
    # 43 Use DS Firmware Language
    t("استخدام لغة firmware DS (تصحيح تلقائي بأسلوب EU)", "Gunakan bahasa firmware DS (Auto Patch gaya EU)", "Використовувати мову прошивки DS (автопатч у стилі EU)", "Χρήση γλώσσας firmware DS (αυτόματο patch στυλ EU)", "Använd DS-firmwarespråk (EU-stil auto patch)", "ใช้ภาษา firmware DS (Auto Patch แบบ EU)", "Použít jazyk firmware DS (auto patch ve stylu EU)", "Brug DS-firmwaresprog (EU-stil auto patch)", "DS firmware dilini kullan (EU tarzı otomatik yama)", "Bruk DS-firmwarespråk (EU-stil auto patch)", "DS firmware nyelv használata (EU-stílusú auto patch)", "Käytä DS-firmwaren kieltä (EU-tyylinen auto patch)", "Dùng ngôn ngữ firmware DS (Auto Patch kiểu EU)", "Użyj języka firmware DS (auto patch w stylu EU)", "Folosește limba firmware DS (auto patch stil EU)"),
    # 44 Shadow Freeze Fix
    t("إصلاح Shadow Freeze (فحص زاوية Ice Wave ثلاثي الأبعاد كامل)", "Perbaikan Shadow Freeze (pemeriksaan sudut Ice Wave 3D penuh)", "Виправлення Shadow Freeze (Ice Wave: повна 3D-перевірка кута)", "Διόρθωση Shadow Freeze (Ice Wave: πλήρης έλεγχος γωνίας 3D)", "Shadow Freeze-fix (Ice Wave: fullständig 3D-vinkelkontroll)", "แก้ Shadow Freeze (ตรวจมุม Ice Wave 3D เต็มรูปแบบ)", "Oprava Shadow Freeze (Ice Wave: úplná 3D kontrola úhlu)", "Shadow Freeze-rettelse (Ice Wave: fuld 3D-vinkelkontrol)", "Shadow Freeze düzeltmesi (Ice Wave tam 3D açı kontrolü)", "Shadow Freeze-fiks (Ice Wave: full 3D-vinkelkontroll)", "Shadow Freeze javítás (Ice Wave teljes 3D szögellenőrzés)", "Shadow Freeze -korjaus (Ice Wave: täysi 3D-kulmatarkistus)", "Sửa Shadow Freeze (kiểm tra góc Ice Wave 3D đầy đủ)", "Poprawka Shadow Freeze (Ice Wave: pełna kontrola kąta 3D)", "Remediere Shadow Freeze (Ice Wave: verificare completă unghi 3D)"),
    # 45 Power-Ups: Pick Up With No Effect
    t("Power-Ups: التقاط بدون تأثير", "Power-Ups: Ambil tanpa efek", "Power-Ups: підбір без ефекту", "Power-Ups: μάζεμα χωρίς εφέ", "Power-ups: plocka upp utan effekt", "Power-Ups: เก็บโดยไม่มีเอฟเฟกต์", "Power-Ups: sebrání bez efektu", "Power-ups: saml op uden effekt", "Power-Ups: Etkisiz toplama", "Power-ups: plukk opp uten effekt", "Power-Ups: Felvétel effekt nélkül", "Power-Ups: Kerää ilman efektiä", "Power-Ups: Nhặt không hiệu ứng", "Power-Ups: Podnieś bez efektu", "Power-Ups: Ridicare fără efect"),
    # 46 Video quality: Low (High Performance)
    t("جودة الفيديو: منخفضة (أداء عالٍ)", "Kualitas video: Rendah (Performa tinggi)", "Якість відео: Низька (висока продуктивність)", "Ποιότητα βίντεο: Χαμηλή (υψηλή απόδοση)", "Videokvalitet: Låg (hög prestanda)", "คุณภาพวิดีโอ: ต่ำ (ประสิทธิภาพสูง)", "Kvalita videa: Nízká (vysoký výkon)", "Videokvalitet: Lav (høj ydeevne)", "Video kalitesi: Düşük (Yüksek performans)", "Videokvalitet: Lav (høy ytelse)", "Videó minőség: Alacsony (nagy teljesítmény)", "Videon laatu: Matala (korkea suorituskyky)", "Chất lượng video: Thấp (Hiệu năng cao)", "Jakość wideo: Niska (wysoka wydajność)", "Calitate video: Scăzută (performanță ridicată)"),
    # 47 Apply hunter to license
    t("تطبيق الصياد المحدد على الترخيص. (إعادة التسمية ستحدّث بيانات الحفظ)", "Terapkan hunter yang dipilih ke lisensi. (Mengganti nama akan memperbarui data save)", "Застосувати вибраного мисливця до ліцензії. (Перейменування оновить збереження)", "Εφαρμογή του επιλεγμένου κυνηγού στην άδεια. (Η μετονομασία θα ενημερώσει τα δεδομένα αποθήκευσης)", "Tillämpa vald jägare på licensen. (Omdöpning uppdaterar spardata)", "ใช้ hunter ที่เลือกกับใบอนุญาต (การเปลี่ยนชื่อจะอัปเดตข้อมูลเซฟ)", "Použít vybraného lovce na licenci. (Přejmenování aktualizuje uložená data)", "Anvend valgt hunter på licensen. (Omdøbning opdaterer gemte data)", "Seçilen avcıyı lisansa uygula. (Yeniden adlandırma kayıt verilerini günceller)", "Bruk valgt hunter på lisensen. (Omdøping oppdaterer lagrede data)", "A kiválasztott vadász alkalmazása a licencre. (Az átnevezés frissíti a mentést)", "Käytä valittua metsästäjää lisenssiin. (Uudelleennimeäminen päivittää tallennuksen)", "Áp dụng hunter đã chọn vào giấy phép. (Đổi tên sẽ cập nhật dữ liệu save)", "Zastosuj wybranego myśliwego do licencji. (Zmiana nazwy zaktualizuje zapis)", "Aplică vânătorul selectat la licență. (Redenumirea va actualiza salvarea)"),
    # 48 Blue(US)
    t("أزرق (US)", "Biru (US)", "Синій (US)", "Μπλε (US)", "Blå (US)", "น้ำเงิน (US)", "Modrá (US)", "Blå (US)", "Mavi (US)", "Blå (US)", "Kék (US)", "Sininen (US)", "Xanh dương (US)", "Niebieski (US)", "Albastru (US)"),
    # 49 Fixed
    t("ثابت", "Tetap", "Фіксований", "Σταθερό", "Fast", "คงที่", "Pevné", "Fast", "Sabit", "Fast", "Rögzített", "Kiinteä", "Cố định", "Stały", "Fix"),
    # 50 Per Damage — High (mojibake en key)
    t("لكل ضرر — مرتفع", "Per kerusakan — Tinggi", "За шкоду — Високий", "Ανά ζημιά — Υψηλό", "Per skada — Hög", "ต่อความเสียหาย — สูง", "Za poškození — Vysoké", "Per skade — Høj", "Hasar başına — Yüksek", "Per skade — Høy", "Sebzésenként — Magas", "Vahinkoa kohden — Korkea", "Theo sát thương — Cao", "Za obrażenia — Wysoki", "Per daună — Ridicat"),
    # 51 Controls aim direction follow
    t("يتحكم في كيفية متابعة اتجاه التصويب الحالي في اللعبة لاتجاه التصويب المستهدف.", "Mengontrol bagaimana arah bidik saat ini di game mengikuti arah bidik target.", "Керує тим, як поточний напрям прицілу в грі слідує за цільовим.", "Ελέγχει πώς η τρέχουσα κατεύθυνση στόχευσης στο παιχνίδι ακολουθεί την κατεύθυνση-στόχο.", "Styr hur spelets nuvarande siktriktning följer målsiktriktningen.", "ควบคุมว่าทิศทางเล็งปัจจุบันในเกมติดตามทิศทางเป้าหมายอย่างไร", "Řídí, jak aktuální směr míření ve hře sleduje cílový směr.", "Styrer hvordan spillets nuværende sigteretning følger målsigteretningen.", "Oyunun mevcut nişan yönünün hedef nişan yönünü nasıl takip ettiğini kontrol eder.", "Styrer hvordan spillets nåværende sikteretning følger målsikteretningen.", "Szabályozza, hogy a játék aktuális célzási iránya hogyan követi a célcélzási irányt.", "Ohjaa, miten pelin nykyinen tähtäyssuunta seuraa kohdetta.", "Điều khiển cách hướng ngắm hiện tại trong game theo hướng ngắm mục tiêu.", "Kontroluje, jak aktualny kierunek celowania w grze podąża za celem.", "Controlează modul în care direcția curentă de țintire din joc urmează direcția țintă."),
    # 52 Checked toggle native weapon zoom
    t("محدد: تبديل تكبير السلاح الأصلي باستدعاء SetPlayerScopeZoom للعبة. غير محدد والطريقة الجديدة أيضًا معطلة: استخدم إدخال زر R الثابت القديم.", "Dicentang: alihkan zoom senjata native dengan memanggil SetPlayerScopeZoom game. Tidak dicentang dan Metode Baru juga off: gunakan input tombol R tetap Legacy.", "Увімкнено: перемикання нативного зуму зброї через SetPlayerScopeZoom гри. Вимкнено і Новий метод теж вимк.: використовувати застарілий фіксований ввід кнопки R.", "Επιλεγμένο: εναλλαγή εγγενούς ζουμ όπλου καλώντας SetPlayerScopeZoom του παιχνιδιού. Μη επιλεγμένο και Νέα μέθοδος επίσης off: χρήση παλαιού σταθερού εισόδου κουμπιού R.", "Ikryssad: växla native vapenzoom via spelets SetPlayerScopeZoom. Avmarkerad och Ny metod också av: använd legacy fast R-knappinmatning.", "เลือก: สลับซูมอาวุธ native โดยเรียก SetPlayerScopeZoom ของเกม ไม่เลือกและวิธีใหม่ปิดด้วย: ใช้ปุ่ม R แบบ Legacy", "Zaškrtnuto: přepínání nativního zoomu zbraně voláním SetPlayerScopeZoom hry. Nezaškrtnuto a Nová metoda také vyp.: použít legacy pevný vstup tlačítka R.", "Markeret: skift native våbenzoom via spillets SetPlayerScopeZoom. Afmarkeret og Ny metode også fra: brug legacy fast R-knapinput.", "İşaretli: oyunun SetPlayerScopeZoom'unu çağırarak native silah zoom'unu değiştir. İşaretsiz ve Yeni Yöntem de kapalı: Legacy sabit R düğmesi girişi kullan.", "Avkrysset: bytt native våpenzoom via spillets SetPlayerScopeZoom. Avkrysset og Ny metode også av: bruk legacy fast R-knappinndata.", "Bejelölve: natív fegyverzoom váltása a játék SetPlayerScopeZoom hívásával. Nincs bejelölve és Új módszer is ki: legacy fix R gomb bemenet.", "Valittu: vaihda natiivi asezoomia kutsumalla pelin SetPlayerScopeZoom. Ei valittu ja Uusi menetelmä myös pois: käytä legacy kiinteää R-painiketta.", "Đã chọn: bật/tắt zoom vũ khí native bằng SetPlayerScopeZoom của game. Bỏ chọn và Phương pháp mới cũng tắt: dùng nút R cố định Legacy.", "Zaznaczone: przełącz natywny zoom broni przez SetPlayerScopeZoom gry. Odznaczone i Nowa metoda też wył.: użyj legacy stałego przycisku R.", "Bifat: comută zoom-ul nativ al armei apelând SetPlayerScopeZoom al jocului. Nebifat și Metoda nouă dezactivată: folosește intrarea legacy buton R fix."),
    # 53 Enables applying hunter to license
    t("يتيح تطبيق الصياد المحدد على الترخيص.", "Memungkinkan menerapkan hunter yang dipilih ke lisensi.", "Дозволяє застосувати вибраного мисливця до ліцензії.", "Επιτρέπει την εφαρμογή του επιλεγμένου κυνηγού στην άδεια.", "Möjliggör att tillämpa vald jägare på licensen.", "เปิดใช้การนำ hunter ที่เลือกไปใช้กับใบอนุญาต", "Umožňuje použít vybraného lovce na licenci.", "Gør det muligt at anvende valgt hunter på licensen.", "Seçilen avcıyı lisansa uygulamayı etkinleştirir.", "Gjør det mulig å bruke valgt hunter på lisensen.", "Lehetővé teszi a kiválasztott vadász licencre való alkalmazását.", "Mahdollistaa valitun metsästäjän soveltamisen lisenssiin.", "Cho phép áp dụng hunter đã chọn vào giấy phép.", "Umożliwia zastosowanie wybranego myśliwego do licencji.", "Permite aplicarea vânătorului selectat la licență."),
    # 54 Does not overwrite window layout
    t("لا يستبدل إعدادات تخطيط النافذة المحفوظة. يُطبَّق التجاوز فقط أثناء لعب Metroid Prime Hunters.", "Tidak menimpa pengaturan tata letak jendela yang disimpan. Override hanya diterapkan saat Metroid Prime Hunters sedang dimainkan.", "Не перезаписує збережені налаштування розташування вікон. Перевизначення застосовується лише під час гри в Metroid Prime Hunters.", "Δεν αντικαθιστά τις αποθηκευμένες ρυθμίσεις διάταξης παραθύρου. Η παράκαμψη εφαρμόζεται μόνο κατά τη διάρκεια του Metroid Prime Hunters.", "Skriver inte över sparade fönsterlayoutinställningar. Åsidosättningen gäller endast under Metroid Prime Hunters.", "ไม่เขียนทับการตั้งค่าเลย์เอาต์หน้าต่างที่บันทึกไว้ ใช้ override เฉพาะขณะเล่น Metroid Prime Hunters", "Nepřepisuje uložená nastavení rozložení oken. Přepsání platí pouze během hry Metroid Prime Hunters.", "Overskriver ikke gemte vindueslayoutindstillinger. Tilsidesættelsen gælder kun under Metroid Prime Hunters.", "Kayıtlı pencere düzeni ayarlarının üzerine yazmaz. Geçersiz kılma yalnızca Metroid Prime Hunters oynanırken uygulanır.", "Overskriver ikke lagrede vinduslayoutinnstillinger. Overstyringen gjelder bare under Metroid Prime Hunters.", "Nem írja felül a mentett abakelrendezési beállításokat. A felülírás csak Metroid Prime Hunters játék közben érvényes.", "Ei korvaa tallennettuja ikkunasommitusasetuksia. Ohitus koskee vain Metroid Prime Hunters -peliä.", "Không ghi đè cài đặt bố cục cửa sổ đã lưu. Ghi đè chỉ áp dụng khi đang chơi Metroid Prime Hunters.", "Nie nadpisuje zapisanych ustawień układu okien. Nadpisanie działa tylko podczas gry w Metroid Prime Hunters.", "Nu suprascrie setările salvate de layout fereastră. Suprascrierea se aplică doar în timpul Metroid Prime Hunters."),
    # 55 Enable Custom HUD
    t("تفعيل HUD مخصص (يستبدل HUD داخل اللعبة بطبقة مخصصة تعرض HP والذخيرة وأيقونات الأسلحة والتقاطع)", "Aktifkan HUD kustom (Mengganti HUD dalam game dengan overlay kustom yang menampilkan HP, amunisi, ikon senjata, dan bidik)", "Увімкнути користувацький HUD (Замінює ігровий HUD накладенням з HP, боєприпасами, іконками зброї та прицілом)", "Ενεργοποίηση προσαρμοσμένου HUD (Αντικαθιστά το HUD του παιχνιδιού με επικάλυψη HP, πυρομαχικά, εικονίδια όπλων και στόχευση)", "Aktivera anpassad HUD (Ersätter spelets HUD med ett anpassat lager som visar HP, ammo, vapenikoner och sikte)", "เปิดใช้ HUD กำหนดเอง (แทนที่ HUD ในเกมด้วย overlay แสดง HP กระสุน ไอคอนอาวุธ และเล็ง)", "Povolit vlastní HUD (Nahradí herní HUD vlastní vrstvou s HP, municí, ikonami zbraní a zaměřovačem)", "Aktivér brugerdefineret HUD (Erstatter spillets HUD med et tilpasset lag med HP, ammo, våbenikoner og sigte)", "Özel HUD'u etkinleştir (Oyun içi HUD'u HP, cephane, silah simgeleri ve nişangah gösteren özel katmanla değiştirir)", "Aktiver tilpasset HUD (Erstatter spillets HUD med et tilpasset lag som viser HP, ammo, våpenikoner og sikte)", "Egyéni HUD engedélyezése (A játék HUD-ját egyéni rétegre cseréli HP, lőszer, fegyverikonok és irányzék megjelenítésével)", "Ota käyttöön mukautettu HUD (Korvaa pelin HUD:n mukautetulla kerroksella, joka näyttää HP:n, ammot, aseikonit ja tähtäimen)", "Bật HUD tùy chỉnh (Thay HUD trong game bằng lớp phủ hiển thị HP, đạn, biểu tượng vũ khí và tâm ngắm)", "Włącz niestandardowy HUD (Zastępuje HUD w grze nakładką z HP, amunicją, ikonami broni i celownikiem)", "Activează HUD personalizat (Înlocuiește HUD-ul din joc cu un overlay cu HP, muniție, icoane arme și țintă)"),
    # 56 — Score Row (per mode) —
    t("— صف النقاط (لكل وضع) —", "— Baris skor (per mode) —", "— Рядок рахунку (за режимом) —", "— Σειρά σκορ (ανά λειτουργία) —", "— Poängrad (per läge) —", "— แถวคะแนน (ต่อโหมด) —", "— Řádek skóre (podle režimu) —", "— Score række (pr. tilstand) —", "— Skor satırı (mod başına) —", "— Poengrekke (per modus) —", "— Pontsor (mód szerint) —", "— Pisterivi (tilaa kohti) —", "— Hàng điểm (theo chế độ) —", "— Wiersz wyniku (na tryb) —", "— Rând scor (per mod) —"),
    # 57 Hide Crosshair
    t("إخفاء التقاطع", "Sembunyikan bidik", "Сховати приціл", "Απόκρυψη στόχευσης", "Dölj sikte", "ซ่อนเล็ง", "Skrýt zaměřovač", "Skjul sigtekors", "Nişangahı gizle", "Skjul trådkors", "Irányzék elrejtése", "Piilota tähtäin", "Ẩn tâm ngắm", "Ukryj celownik", "Ascunde ținta"),
    # 58 Hide Score: Bounty
    t("إخفاء النقاط: Bounty", "Sembunyikan skor: Bounty", "Сховати рахунок: Bounty", "Απόκρυψη σκορ: Bounty", "Dölj poäng: Bounty", "ซ่อนคะแนน: Bounty", "Skrýt skóre: Bounty", "Skjul score: Bounty", "Skoru gizle: Bounty", "Skjul poeng: Bounty", "Pont elrejtése: Bounty", "Piilota pisteet: Bounty", "Ẩn điểm: Bounty", "Ukryj wynik: Bounty", "Ascunde scorul: Bounty"),
    # 59 Auto Scale Enable
    t("تفعيل المقياس التلقائي", "Aktifkan skala otomatis", "Увімкнути автомасштаб", "Ενεργοποίηση αυτόματης κλίμακας", "Aktivera autoskalning", "เปิดใช้มาตราส่วนอัตโนมัติ", "Povolit automatické měřítko", "Aktivér autoskalering", "Otomatik ölçeği etkinleştir", "Aktiver autoskalering", "Automatikus skála engedélyezése", "Ota automaattinen skaalaus käyttöön", "Bật tỷ lệ tự động", "Włącz skalowanie automatyczne", "Activează scalare automată"),
    # 60 Auto Scale Crosshair Cap %
    t("حد التقاطع للمقياس التلقائي %", "Batas bidik skala otomatis %", "Ліміт прицілу при автомасштабі %", "Όριο στόχευσης αυτόματης κλίμακας %", "Autoskalning siktgräns %", "เพดานเล็งมาตราส่วนอัตโนมัติ %", "Limit zaměřovače auto měřítka %", "Autoskalering sigtegrænse %", "Otomatik ölçek nişangah sınırı %", "Autoskalering siktetak %", "Automatikus skála irányzék felső határ %", "Automaattisen skaalauksen tähtäimen yläraja %", "Giới hạn tâm ngắm tỷ lệ tự động %", "Limit celownika auto skali %", "Plafon țintă scalare automată %"),
    # 61 Underline
    t("تسطير", "Garis bawah", "Підкреслення", "Υπογράμμιση", "Understrykning", "ขีดเส้นใต้", "Podtržení", "Understregning", "Altı çizili", "Understrekning", "Aláhúzás", "Alleviivaus", "Gạch chân", "Podkreślenie", "Subliniere"),
    # 62 Zoom Base Scale %
    t("مقياس التقاطع الأساسي عند التكبير %", "Skala dasar bidik zoom %", "Базовий масштаб прицілу при зумі %", "Βασική κλίμακα στόχευσης ζουμ %", "Zoom bas-skala sikte %", "สเกลฐานเล็งซูม %", "Základní měřítko zaměřovače při zoomu %", "Zoom basisskala sigte %", "Zoom temel nişangah ölçeği %", "Zoom grunnskalering sikte %", "Zoom alap irányzék skála %", "Zoomin perustähtäimen skaala %", "Tỷ lệ cơ sở tâm ngắm zoom %", "Bazowa skala celownika zoom %", "Scală de bază țintă zoom %"),
    # 63 Scope Radius
    t("نصف قطر المنظار", "Radius scope", "Радіус прицілу", "Ακτίνα σκοπευτήρα", "Sikteradius", "รัศมีสโคป", "Poloměr zaměřovače", "Sigteradius", "Nişangah yarıçapı", "Sikteradius", "Távcső sugara", "Tähtäimen säde", "Bán kính scope", "Promień celownika", "Rază lunetă"),
    # 64 Scope Dot Size
    t("حجم نقطة المنظار", "Ukuran titik scope", "Розмір точки прицілу", "Μέγεθος σημείου σκοπευτήρα", "Siktpunktstorlek", "ขนาดจุดสโคป", "Velikost bodu zaměřovače", "Sigtepunktstørrelse", "Nişangah nokta boyutu", "Siktepunktstørrelse", "Távcső pont mérete", "Tähtäimen pisteen koko", "Kích thước chấm scope", "Rozmiar punktu celownika", "Dimensiune punct lunetă"),
    # 65 Transition Speed %
    t("سرعة الانتقال %", "Kecepatan transisi %", "Швидкість переходу %", "Ταχύτητα μετάβασης %", "Övergångshastighet %", "ความเร็วการเปลี่ยน %", "Rychlost přechodu %", "Overgangshastighed %", "Geçiş hızı %", "Overgangshastighet %", "Átmeneti sebesség %", "Siirtymänopeus %", "Tốc độ chuyển %", "Szybkość przejścia %", "Viteză tranziție %"),
    # 66 Zoom Crosshair
    t("تقاطع التكبير", "Bidik zoom", "Приціл при зумі", "Στόχευση ζουμ", "Zoom-sikte", "เล็งซูม", "Zaměřovač při zoomu", "Zoom-sigtekors", "Zoom nişangahı", "Zoom-trådkors", "Zoom irányzék", "Zoom-tähtäin", "Tâm ngắm zoom", "Celownik zoom", "Țintă zoom"),
    # 67 Fade
    t("تلاشي", "Fade", "Затухання", "Ξεθώριασμα", "Tona in", "เฟด", "Prolnutí", "Fade", "Solma", "Fade", "Áttűnés", "Häivytys", "Mờ dần", "Zanikanie", "Estompare"),
    # 68 Pulse Wave
    t("موجة نبضية", "Gelombang pulsa", "Імпульсна хвиля", "Παλμική κυματική", "Pulsvåg", "คลื่นพัลส์", "Pulzní vlna", "Pulsbølge", "Darbe dalgası", "Pulsbølge", "Impulzushullám", "Pulssiaalto", "Sóng xung", "Fala impulsowa", "Undă puls"),
    # 69 Drone LIDAR
    t("LIDAR الطائرة بدون طيار", "LIDAR drone", "LIDAR дрона", "LIDAR drone", "Drönar-LIDAR", "LIDAR โดรน", "Dron LIDAR", "Drone-LIDAR", "Drone LIDAR", "Drone-LIDAR", "Drón LIDAR", "Drone-LIDAR", "LIDAR drone", "LIDAR drona", "LIDAR dronă"),
    # 70 Outline Opacity
    t("شفافية الحدود", "Opasitas garis luar", "Непрозорість контуру", "Αδιαφάνεια περιγράμματος", "Konturopacitet", "ความทึบเส้นขอบ", "Neprůhlednost obrysu", "Konturopacitet", "Dış çizgi opaklığı", "Konturopasitet", "Körvonal átlátszatlansága", "Ääriviivan läpinäkymättömyys", "Độ mờ viền", "Krycie obrysu", "Opacitate contur"),
    # 71 Dot Color
    t("لون النقطة", "Warna titik", "Колір точки", "Χρώμα σημείου", "Punktfärg", "สีจุด", "Barva bodu", "Punktfarve", "Nokta rengi", "Punktfarge", "Pont színe", "Pisteen väri", "Màu chấm", "Kolor punktu", "Culoare punct"),
    # 72 Dot Opacity
    t("شفافية النقطة", "Opasitas titik", "Непрозорість точки", "Αδιαφάνεια σημείου", "Punktopacitet", "ความทึบจุด", "Neprůhlednost bodu", "Punktopacitet", "Nokta opaklığı", "Punktopasitet", "Pont átlátszatlansága", "Pisteen läpinäkymättömyys", "Độ mờ chấm", "Krycie punktu", "Opacitate punct"),
    # 73 Outer
    t("خارجي", "Luar", "Зовнішній", "Εξωτερικό", "Yttre", "ด้านนอก", "Vnější", "Ydre", "Dış", "Ytre", "Külső", "Ulkopuoli", "Ngoài", "Zewnętrzny", "Exterior"),
    # 74 Show Number
    t("إظهار الرقم", "Tampilkan angka", "Показати число", "Εμφάνιση αριθμού", "Visa nummer", "แสดงตัวเลข", "Zobrazit číslo", "Vis nummer", "Sayıyı göster", "Vis nummer", "Szám megjelenítése", "Näytä numero", "Hiển thị số", "Pokaż liczbę", "Afișează numărul"),
    # 75 Length X
    t("الطول X", "Panjang X", "Довжина X", "Μήκος X", "Längd X", "ความยาว X", "Délka X", "Længde X", "Uzunluk X", "Lengde X", "Hossz X", "Pituus X", "Chiều dài X", "Długość X", "Lungime X"),
    # 76 Offset X
    t("إزاحة X", "Offset X", "Зміщення X", "Μετατόπιση X", "Offset X", "ออฟเซ็ต X", "Odsazení X", "Forskydning X", "Ofset X", "Offset X", "Eltolás X", "Siirtymä X", "Offset X", "Przesunięcie X", "Offset X"),
    # 77 Top Right
    t("أعلى اليمين", "Kanan atas", "Вгорі справа", "Πάνω δεξιά", "Övre höger", "บนขวา", "Vpravo nahoře", "Øverst til højre", "Sağ üst", "Øverst til høyre", "Jobb felső", "Yläoikea", "Trên phải", "Góra prawo", "Sus dreapta"),
    # 78 Bottom Center
    t("أسفل الوسط", "Tengah bawah", "Внизу по центру", "Κάτω κέντρο", "Nedre mitten", "ล่างกลาง", "Dole uprostřed", "Nederst i midten", "Alt orta", "Nederst i midten", "Alsó közép", "Alhaalla keskellä", "Dưới giữa", "Dół środek", "Jos centru"),
    # 79 Bot Left
    t("أسفل اليسار", "Kiri bawah", "Внизу зліва", "Κάτω αριστερά", "Nedre vänster", "ล่างซ้าย", "Dole vlevo", "Nederst til venstre", "Sol alt", "Nederst til venstre", "Bal alsó", "Alhaalla vasemmalla", "Dưới trái", "Dół lewo", "Jos stânga"),
    # 80 Right
    t("يمين", "Kanan", "Праворуч", "Δεξιά", "Höger", "ขวา", "Vpravo", "Højre", "Sağ", "Høyre", "Jobb", "Oikea", "Phải", "Prawo", "Dreapta"),
    # 81 Start
    t("بداية", "Mulai", "Початок", "Έναρξη", "Start", "เริ่ม", "Začátek", "Start", "Başlangıç", "Start", "Kezdet", "Alku", "Bắt đầu", "Początek", "Început"),
    # 82 Vert
    t("عمودي", "Vert.", "Верт.", "Καθ.", "Vert.", "แนวตั้ง", "Vert.", "Vert.", "Dikey", "Vert.", "Függ.", "Pysty", "Dọc", "Pion.", "Vert."),
    # 83 Text→Gauge
    t("نص→مقياس", "Teks→Gauge", "Текст→Індикатор", "Κείμενο→Ένδειξη", "Text→Mätare", "ข้อความ→เกจ", "Text→Ukazatel", "Tekst→Måler", "Metin→Gösterge", "Tekst→Måler", "Szöveg→Mérő", "Teksti→Mittari", "Chữ→Đồng hồ", "Tekst→Wskaźnik", "Text→Indicator"),
    # 84 Gauge Side
    t("جانب المقياس", "Sisi gauge", "Сторона індикатора", "Πλευρά ένδειξης", "Mätarsida", "ด้านเกจ", "Strana ukazatele", "Målerside", "Gösterge tarafı", "Målerside", "Mérő oldala", "Mittarin puoli", "Cạnh đồng hồ", "Strona wskaźnika", "Latura indicatorului"),
    # 85 Text Offset X
    t("إزاحة النص X", "Offset teks X", "Зміщення тексту X", "Μετατόπιση κειμένου X", "Textoffset X", "ออฟเซ็ตข้อความ X", "Odsazení textu X", "Tekstforskydning X", "Metin ofseti X", "Tekstoffset X", "Szöveg eltolás X", "Tekstin siirtymä X", "Offset chữ X", "Przesunięcie tekstu X", "Offset text X"),
    # 86 Suffix
    t("لاحقة", "Akhiran", "Суфікс", "Επίθημα", "Suffix", "คำต่อท้าย", "Přípona", "Suffiks", "Sonek", "Suffiks", "Utótag", "Pääte", "Hậu tố", "Przyrostek", "Sufix"),
    # 87 Orient
    t("توجيه", "Orientasi", "Орієнтація", "Προσανατολισμός", "Orientering", "ทิศทาง", "Orientace", "Retning", "Yönelim", "Retning", "Tájolás", "Suunta", "Hướng", "Orientacja", "Orientare"),
    # 88 Pos Anchor
    t("مرساة الموضع", "Jangkar posisi", "Якір позиції", "Άγκυρα θέσης", "Positionsankare", "จุดยึดตำแหน่ง", "Kotva pozice", "Positionsanker", "Konum çapası", "Posisjonsanker", "Pozíció horgony", "Sijainnin ankkuri", "Neo vị trí", "Kotwica pozycji", "Ancoră poziție"),
    # 89 Weapon Layout
    t("تخطيط الأسلحة", "Tata letak senjata", "Розташування зброї", "Διάταξη όπλων", "Vapenlayout", "เลย์เอาต์อาวุธ", "Rozložení zbraní", "Våbenlayout", "Silah düzeni", "Våpenlayout", "Fegyver elrendezés", "Aseasettelu", "Bố cục vũ khí", "Układ broni", "Layout arme"),
    # 90 Label Offset X
    t("إزاحة التسمية X", "Offset label X", "Зміщення мітки X", "Μετατόπιση ετικέτας X", "Etikettoffset X", "ออฟเซ็ตป้ายกำกับ X", "Odsazení popisku X", "Etiketforskydning X", "Etiket ofseti X", "Etikettoffset X", "Címke eltolás X", "Tunnisteen siirtymä X", "Offset nhãn X", "Przesunięcie etykiety X", "Offset etichetă X"),
    # 91 Label: Octoliths
    t("تسمية: Octoliths", "Label: Octoliths", "Мітка: Octoliths", "Ετικέτα: Octoliths", "Etikett: Octoliths", "ป้ายกำกับ: Octoliths", "Popisek: Octoliths", "Etiket: Octoliths", "Etiket: Octoliths", "Etikett: Octoliths", "Címke: Octoliths", "Tunniste: Octoliths", "Nhãn: Octoliths", "Etykieta: Octoliths", "Etichetă: Octoliths"),
    # 92 Bounty
    t("مكافأة", "Bounty", "Нагорода", "Αμοιβή", "Bounty", "Bounty", "Odměna", "Bounty", "Ödül", "Bounty", "Fejpénz", "Palkkio", "Bounty", "Nagroda", "Recompensă"),
    # 93 Value
    t("قيمة", "Nilai", "Значення", "Τιμή", "Värde", "ค่า", "Hodnota", "Værdi", "Değer", "Verdi", "Érték", "Arvo", "Giá trị", "Wartość", "Valoare"),
    # 94 Label Color: Overall
    t("لون التسمية: إجمالي", "Warna label: Keseluruhan", "Колір мітки: Загальний", "Χρώμα ετικέτας: Συνολικό", "Etikettfärg: Totalt", "สีป้ายกำกับ: รวม", "Barva popisku: Celková", "Etiketfarve: Samlet", "Etiket rengi: Genel", "Etikettfarge: Totalt", "Címke színe: Összesített", "Tunnisteen väri: Kokonaisuus", "Màu nhãn: Tổng thể", "Kolor etykiety: Ogólny", "Culoare etichetă: General"),
    # 95 Goal Color
    t("لون الهدف", "Warna tujuan", "Колір цілі", "Χρώμα στόχου", "Målfärg", "สีเป้าหมาย", "Barva cíle", "Målfarve", "Hedef rengi", "Målfarge", "Cél színe", "Tavoitteen väri", "Màu mục tiêu", "Kolor celu", "Culoare obiectiv"),
    # 96 Use Hunter Color
    t("استخدام لون الصياد", "Gunakan warna hunter", "Використовувати колір мисливця", "Χρήση χρώματος κυνηγού", "Använd jägarfärg", "ใช้สี hunter", "Použít barvu lovce", "Brug hunterfarve", "Avcı rengini kullan", "Bruk hunterfarge", "Vadász szín használata", "Käytä metsästäjän väriä", "Dùng màu hunter", "Użyj koloru myśliwego", "Folosește culoarea vânătorului"),
    # 97 Dst Y
    t("عرض Y", "Dst Y", "Ціл. Y", "Προβ. Y", "Dst Y", "แสดง Y", "Cíl Y", "Dst Y", "Hedef Y", "Dst Y", "Cél Y", "Dst Y", "Hiển thị Y", "Doc. Y", "Dst Y"),
    # 98 Spacing
    t("تباعد", "Jarak", "Інтервал", "Διάκενο", "Avstånd", "ระยะห่าง", "Rozestup", "Afstand", "Aralık", "Avstand", "Távolság", "Väli", "Khoảng cách", "Odstęp", "Spațiere"),
    # 99 Highlight Opacity
    t("شفافية التمييز", "Opasitas sorotan", "Непрозорість підсвітки", "Αδιαφάνεια επισήμανσης", "Markeringsopacitet", "ความทึบไฮไลต์", "Neprůhlednost zvýraznění", "Fremhævningsopacitet", "Vurgu opaklığı", "Uthevingsopasitet", "Kiemelés átlátszatlansága", "Korostuksen läpinäkymättömyys", "Độ mờ nổi bật", "Krycie podświetlenia", "Opacitate evidențiere"),
    # 100 Highlight Offset Right
    t("إزاحة التمييز لليمين", "Offset sorotan kanan", "Зміщення підсвітки вправо", "Μετατόπιση επισήμανσης δεξιά", "Markeringsoffset höger", "ออฟเซ็ตไฮไลต์ขวา", "Odsazení zvýraznění vpravo", "Fremhævningsoffset højre", "Vurgu sağ ofseti", "Uthevingsoffset høyre", "Kiemelés jobb eltolás", "Korostuksen siirtymä oikealle", "Offset nổi bật phải", "Przesunięcie podświetlenia w prawo", "Offset evidențiere dreapta"),
    # 101 Size Offset Top
    t("إزاحة الحجم للأعلى", "Offset ukuran atas", "Зміщення розміру зверху", "Μετατόπιση μεγέθους πάνω", "Storleksoffset topp", "ออฟเซ็ตขนาดบน", "Odsazení velikosti nahoře", "Størrelsesoffset top", "Boyut üst ofseti", "Størrelsesoffset topp", "Méret felső eltolás", "Koon siirtymä ylös", "Offset kích thước trên", "Przesunięcie rozmiaru góra", "Offset dimensiune sus"),
    # 102 Hl Corner Radius
    t("نصف قطر زاوية HL", "Radius sudut HL", "Радіус кута HL", "Ακτίνα γωνίας HL", "HL-hörnradius", "รัศมีมุม HL", "Poloměr rohu HL", "HL-hjørneradius", "HL köşe yarıçapı", "HL-hjørneradius", "HL sarok sugara", "HL-kulman säde", "Bán kính góc HL", "Promień rogu HL", "Rază colț HL"),
    # 103 HP
    {l: "HP" for l in NEW_LANGS},
    # 104 HP Gauge Color By Value
    t("لون مقياس HP حسب القيمة", "Warna gauge HP menurut nilai", "Колір індикатора HP за значенням", "Χρώμα ένδειξης HP ανά τιμή", "HP-mätarfärg efter värde", "สีเกจ HP ตามค่า", "Barva ukazatele HP podle hodnoty", "HP-målerfarve efter værdi", "Değere göre HP göstergesi rengi", "HP-målerfarge etter verdi", "HP mérő színe érték szerint", "HP-mittarin väri arvon mukaan", "Màu đồng hồ HP theo giá trị", "Kolor wskaźnika HP według wartości", "Culoare indicator HP după valoare"),
    # 105 Ammo Outline
    t("حدود الذخيرة", "Garis luar amunisi", "Контур боєприпасів", "Περίγραμμα πυρομαχικών", "Ammokontur", "เส้นขอบกระสุน", "Obrys munice", "Ammokontur", "Cephane dış çizgisi", "Ammokontur", "Lőszer körvonal", "Ammon ääriviiva", "Viền đạn", "Obrys amunicji", "Contur muniție"),
    # 106 Weapon Icon
    t("أيقونة السلاح", "Ikon senjata", "Іконка зброї", "Εικονίδιο όπλου", "Vapenikon", "ไอคอนอาวุธ", "Ikona zbraně", "Våbenikon", "Silah simgesi", "Våpenikon", "Fegyver ikon", "Aseikoni", "Biểu tượng vũ khí", "Ikona broni", "Pictogram armă"),
    # 107 Weapon Inventory Outline
    t("حدود مخزون الأسلحة", "Garis luar inventaris senjata", "Контур інвентаря зброї", "Περίγραμμα αποθέματος όπλων", "Vapeninventarie-kontur", "เส้นขอบคลังอาวุธ", "Obrys inventáře zbraní", "Våbeninventar-kontur", "Silah envanteri dış çizgisi", "Våpeninventar-kontur", "Fegyver készlet körvonal", "Asevaraston ääriviiva", "Viền kho vũ khí", "Obrys ekwipunku broni", "Contur inventar arme"),
    # 108 Score Colors
    t("ألوان النقاط", "Warna skor", "Кольори рахунку", "Χρώματα σκορ", "Poängfärger", "สีคะแนน", "Barvy skóre", "Scorefarver", "Skor renkleri", "Poengfarger", "Pontszín", "Pisteiden värit", "Màu điểm", "Kolory wyniku", "Culori scor"),
    # 109 Time Left
    t("الوقت المتبقي", "Waktu tersisa", "Час, що залишився", "Χρόνος που απομένει", "Tid kvar", "เวลาที่เหลือ", "Zbývající čas", "Tid tilbage", "Kalan süre", "Tid igjen", "Hátralévő idő", "Aikaa jäljellä", "Thời gian còn lại", "Pozostały czas", "Timp rămas"),
    # 110 Bomb Left
    t("القنابل المتبقية", "Bom tersisa", "Бомб залишилось", "Βόμβες που απομένουν", "Bomber kvar", "ระเบิดที่เหลือ", "Zbývající bomby", "Bomber tilbage", "Kalan bomba", "Bomber igjen", "Hátralévő bombák", "Pommeja jäljellä", "Bom còn lại", "Pozostałe bomby", "Bombe rămase"),
    # 111 Radar Settings
    t("إعدادات الرادار", "Pengaturan radar", "Налаштування радара", "Ρυθμίσεις ραντάρ", "Radarinställningar", "การตั้งค่าเรดาร์", "Nastavení radaru", "Radarindstillinger", "Radar ayarları", "Radarinnstillinger", "Radar beállítások", "Tutka-asetukset", "Cài đặt radar", "Ustawienia radaru", "Setări radar"),
    # 112 Bmb\nIcon
    t("أيقونة\nقنبلة", "Ikon\nbom", "Іконка\nбомби", "Εικονίδιο\nβόμβας", "Bomb-\nikon", "ไอคอน\nระเบิด", "Ikona\nbomby", "Bombe-\nikon", "Bomba\nsimgesi", "Bombe-\nikon", "Bomba\nikon", "Pommi-\nikoni", "Biểu tượng\nbom", "Ikona\nbomby", "Pictogram\nbombă"),
    # 113 Bombs
    t("قنابل", "Bom", "Бомби", "Βόμβες", "Bomber", "ระเบิด", "Bomby", "Bomber", "Bombalar", "Bomber", "Bombák", "Pommit", "Bom", "Bomby", "Bombe"),
    # 114 Color (Default: Red)
    t("لون (افتراضي: أحمر)", "Warna (default: Merah)", "Колір (за замовч.: червоний)", "Χρώμα (προεπιλογή: κόκκινο)", "Färg (standard: röd)", "สี (ค่าเริ่มต้น: แดง)", "Barva (výchozí: červená)", "Farve (standard: rød)", "Renk (varsayılan: kırmızı)", "Farge (standard: rød)", "Szín (alapértelmezett: piros)", "Väri (oletus: punainen)", "Màu (mặc định: đỏ)", "Kolor (domyślnie: czerwony)", "Culoare (implicit: roșu)"),
    # 115 No Ammo
    t("لا ذخيرة", "Tidak ada amunisi", "Немає боєприпасів", "Χωρίς πυρομαχικά", "Ingen ammo", "ไม่มีกระสุน", "Žádná munice", "Ingen ammo", "Cephane yok", "Ingen ammo", "Nincs lőszer", "Ei ammoja", "Hết đạn", "Brak amunicji", "Fără muniție"),
    # 116 Octo Drop
    t("سقوط Octolith", "Drop Octolith", "Випадення Octolith", "Πτώση Octolith", "Octolith-drop", "ดรอป Octolith", "Drop Octolith", "Octolith-drop", "Octolith düşüşü", "Octolith-drop", "Octolith drop", "Octolith-pudotus", "Rơi Octolith", "Drop Octolith", "Drop Octolith"),
    # 117 Slot: Objective [flags=0x01]
    t("فتحة: الهدف     [flags=0x01]", "Slot: Objektif     [flags=0x01]", "Слот: Ціль     [flags=0x01]", "Θύρα: Στόχος     [flags=0x01]", "Plats: Mål     [flags=0x01]", "สล็อต: เป้าหมาย     [flags=0x01]", "Slot: Cíl     [flags=0x01]", "Slot: Mål     [flags=0x01]", "Yuva: Hedef     [flags=0x01]", "Spor: Mål     [flags=0x01]", "Slot: Cél     [flags=0x01]", "Paikka: Tavoite     [flags=0x01]", "Slot: Mục tiêu     [flags=0x01]", "Slot: Cel     [flags=0x01]", "Slot: Obiectiv     [flags=0x01]"),
    # 118 Applied once on settings close...
    t("يُطبَّق مرة واحدة عند إغلاق الإعدادات على الرسائل المعروضة حاليًا (flags=0x00).\nتستخدم الرسائل الجديدة ألوانها الحرفية الفردية أعلاه (Lost Lives / Coward Detect / Turret ...).\nملاحظة: HEADSHOT! (H228) هو flags=0x00، وليس 0x02.", "Diterapkan sekali saat menutup pengaturan ke pesan yang ditampilkan (flags=0x00).\nPesan baru menggunakan warna literal individual di atas (Lost Lives / Coward Detect / Turret ...).\nCatatan: HEADSHOT! (H228) adalah flags=0x00, bukan 0x02.", "Застосовується один раз при закритті налаштувань до поточних повідомлень (flags=0x00).\nНові повідомлення використовують індивідуальні кольори вище (Lost Lives / Coward Detect / Turret ...).\nПримітка: HEADSHOT! (H228) — flags=0x00, а не 0x02.", "Εφαρμόζεται μία φορά στο κλείσιμο ρυθμίσεων στα τρέχοντα μηνύματα (flags=0x00).\nΤα νέα μηνύματα χρησιμοποιούν τα ατομικά χρώματά τους παραπάνω (Lost Lives / Coward Detect / Turret ...).\nΣημ.: HEADSHOT! (H228) είναι flags=0x00, όχι 0x02.", "Tillämpas en gång vid stängning av inställningar på aktuella meddelanden (flags=0x00).\nNya meddelanden använder sina individuella färger ovan (Lost Lives / Coward Detect / Turret ...).\nObs: HEADSHOT! (H228) är flags=0x00, inte 0x02.", "ใช้ครั้งเดียวเมื่อปิดการตั้งค่ากับข้อความที่แสดง (flags=0x00).\nข้อความใหม่ใช้สีเฉพาะด้านบน (Lost Lives / Coward Detect / Turret ...)\nหมายเหตุ: HEADSHOT! (H228) คือ flags=0x00 ไม่ใช่ 0x02", "Použito jednou při zavření nastavení na aktuální zprávy (flags=0x00).\nNové zprávy používají individuální barvy výše (Lost Lives / Coward Detect / Turret ...).\nPozn.: HEADSHOT! (H228) je flags=0x00, ne 0x02.", "Anvendes én gang ved lukning af indstillinger på viste meddelelser (flags=0x00).\nNye meddelelser bruger deres individuelle farver ovenfor (Lost Lives / Coward Detect / Turret ...).\nBemærk: HEADSHOT! (H228) er flags=0x00, ikke 0x02.", "Ayarlar kapatıldığında mevcut mesajlara bir kez uygulanır (flags=0x00).\nYeni mesajlar yukarıdaki bireysel renklerini kullanır (Lost Lives / Coward Detect / Turret ...).\nNot: HEADSHOT! (H228) flags=0x00'dır, 0x02 değil.", "Brukes én gang ved lukking av innstillinger på viste meldinger (flags=0x00).\nNye meldinger bruker individuelle farger over (Lost Lives / Coward Detect / Turret ...).\nMerk: HEADSHOT! (H228) er flags=0x00, ikke 0x02.", "Egyszer alkalmazódik a beállítások bezárásakor a jelenlegi üzenetekre (flags=0x00).\nAz új üzenetek a fenti egyedi színeket használják (Lost Lives / Coward Detect / Turret ...).\nMegj.: HEADSHOT! (H228) flags=0x00, nem 0x02.", "Sovelletaan kerran asetusten sulkemisen yhteydessä nykyisiin viesteihin (flags=0x00).\nUudet viestit käyttävät yllä olevia yksilöllisiä värejään (Lost Lives / Coward Detect / Turret ...).\nHuom: HEADSHOT! (H228) on flags=0x00, ei 0x02.", "Áp dụng một lần khi đóng cài đặt cho tin nhắn hiện tại (flags=0x00).\nTin mới dùng màu riêng ở trên (Lost Lives / Coward Detect / Turret ...).\nLưu ý: HEADSHOT! (H228) là flags=0x00, không phải 0x02.", "Stosowane raz przy zamknięciu ustawień do bieżących wiadomości (flags=0x00).\nNowe wiadomości używają indywidualnych kolorów powyżej (Lost Lives / Coward Detect / Turret ...).\nUwaga: HEADSHOT! (H228) to flags=0x00, nie 0x02.", "Aplicat o dată la închiderea setărilor mesajelor afișate (flags=0x00).\nMesajele noi folosesc culorile individuale de mai sus (Lost Lives / Coward Detect / Turret ...).\nNotă: HEADSHOT! (H228) este flags=0x00, nu 0x02."),
    # 119 White
    t("أبيض", "Putih", "Білий", "Λευκό", "Vit", "ขาว", "Bílá", "Hvid", "Beyaz", "Hvit", "Fehér", "Valkoinen", "Trắng", "Biały", "Alb"),
    # 120 Pure Cyan
    t("سماوي نقي", "Sian murni", "Чистий ціан", "Καθαρό κυανό", "Rent cyan", "ฟ้าบริสุทธิ์", "Čistý azurový", "Ren cyan", "Saf cyan", "Ren cyan", "Tiszta cián", "Puhdas syaani", "Xanh lơ thuần", "Czysty cyjan", "Cian pur"),
    # 121 Samus Hud
    t("HUD Samus", "HUD Samus", "HUD Samus", "HUD Samus", "Samus-HUD", "HUD Samus", "HUD Samus", "Samus-HUD", "Samus HUD", "Samus-HUD", "Samus HUD", "Samus-HUD", "HUD Samus", "HUD Samus", "HUD Samus"),
    # 122 Trace Hud
    t("HUD Trace", "HUD Trace", "HUD Trace", "HUD Trace", "Trace-HUD", "HUD Trace", "HUD Trace", "Trace-HUD", "Trace HUD", "Trace-HUD", "Trace HUD", "Trace-HUD", "HUD Trace", "HUD Trace", "HUD Trace"),
    # 123 Weavel Hud
    t("HUD Weavel", "HUD Weavel", "HUD Weavel", "HUD Weavel", "Weavel-HUD", "HUD Weavel", "HUD Weavel", "Weavel-HUD", "Weavel HUD", "Weavel-HUD", "Weavel HUD", "Weavel-HUD", "HUD Weavel", "HUD Weavel", "HUD Weavel"),
    # 124 Power Beam
    {l: "Power Beam" for l in NEW_LANGS},
    # 125 Battlehammer
    {l: "Battlehammer" for l in NEW_LANGS},
    # 126 ShockCoil
    {l: "ShockCoil" for l in NEW_LANGS},
    # 127 BH Color
    t("لون BH", "Warna BH", "Колір BH", "Χρώμα BH", "BH-färg", "สี BH", "Barva BH", "BH-farve", "BH rengi", "BH-farge", "BH szín", "BH-väri", "Màu BH", "Kolor BH", "Culoare BH"),
    # 128 OC Color
    t("لون OC", "Warna OC", "Колір OC", "Χρώμα OC", "OC-färg", "สี OC", "Barva OC", "OC-farve", "OC rengi", "OC-farge", "OC szín", "OC-väri", "Màu OC", "Kolor OC", "Culoare OC"),
    # 129 Medium
    t("متوسط", "Sedang", "Середній", "Μέτριο", "Medel", "ปานกลาง", "Střední", "Mellem", "Orta", "Middels", "Közepes", "Keskitaso", "Trung bình", "Średni", "Mediu"),
    # 130 Number of Colors
    t("عدد الألوان", "Jumlah warna", "Кількість кольорів", "Αριθμός χρωμάτων", "Antal färger", "จำนวนสี", "Počet barev", "Antal farver", "Renk sayısı", "Antall farger", "Színek száma", "Värien määrä", "Số màu", "Liczba kolorów", "Număr de culori"),
    # 131 Threshold 5 (%)
    t("عتبة 5 (%)", "Ambang 5 (%)", "Поріг 5 (%)", "Όριο 5 (%)", "Tröskel 5 (%)", "เกณฑ์ 5 (%)", "Prah 5 (%)", "Tærskel 5 (%)", "Eşik 5 (%)", "Terskel 5 (%)", "Küszöb 5 (%)", "Kynnys 5 (%)", "Ngưỡng 5 (%)", "Próg 5 (%)", "Prag 5 (%)"),
    # 132 Color 4
    t("لون 4", "Warna 4", "Колір 4", "Χρώμα 4", "Färg 4", "สี 4", "Barva 4", "Farve 4", "Renk 4", "Farge 4", "Szín 4", "Väri 4", "Màu 4", "Kolor 4", "Culoare 4"),
    # 133 Custom HUD code copied
    t("تم نسخ كود HUD المخصص إلى الحافظة.", "Kode HUD kustom disalin ke clipboard.", "Код користувацького HUD скопійовано в буфер обміну.", "Ο κώδικας προσαρμοσμένου HUD αντιγράφηκε στο πρόχειρο.", "Anpassad HUD-kod kopierad till urklipp.", "คัดลอกโค้ด HUD กำหนดเองไปยังคลิปบอร์ดแล้ว", "Kód vlastního HUD zkopírován do schránky.", "Brugerdefineret HUD-kode kopieret til udklipsholder.", "Özel HUD kodu panoya kopyalandı.", "Tilpasset HUD-kode kopiert til utklippstavle.", "Egyéni HUD kód a vágólapra másolva.", "Mukautettu HUD-koodi kopioitu leikepöydälle.", "Đã sao chép mã HUD tùy chỉnh vào clipboard.", "Kod niestandardowego HUD skopiowany do schowka.", "Codul HUD personalizat a fost copiat în clipboard."),
    # 134 Failed to load Custom HUD code: %1
    t("فشل تحميل كود HUD المخصص: %1", "Gagal memuat kode HUD kustom: %1", "Не вдалося завантажити код користувацького HUD: %1", "Αποτυχία φόρτωσης κώδικα προσαρμοσμένου HUD: %1", "Det gick inte att ladda anpassad HUD-kod: %1", "โหลดโค้ด HUD กำหนดเองล้มเหลว: %1", "Nepodařilo se načíst kód vlastního HUD: %1", "Kunne ikke indlæse brugerdefineret HUD-kode: %1", "Özel HUD kodu yüklenemedi: %1", "Kunne ikke laste tilpasset HUD-kode: %1", "Az egyéni HUD kód betöltése sikertelen: %1", "Mukautetun HUD-koodin lataus epäonnistui: %1", "Không tải được mã HUD tùy chỉnh: %1", "Nie udało się wczytać kodu niestandardowego HUD: %1", "Nu s-a putut încărca codul HUD personalizat: %1"),
    # 135 Use New Method 2 for Zoom
    t("استخدام الطريقة الجديدة 2 للتكبير", "Gunakan Metode Baru 2 untuk zoom", "Використовувати новий метод 2 для зуму", "Χρήση νέας μεθόδου 2 για ζουμ", "Använd ny metod 2 för zoom", "ใช้วิธีใหม่ 2 สำหรับซูม", "Použít novou metodu 2 pro zoom", "Brug ny metode 2 til zoom", "Zoom için Yeni Yöntem 2'yi kullan", "Bruk ny metode 2 for zoom", "Új 2. módszer használata zoomhoz", "Käytä uutta menetelmää 2 zoomiin", "Dùng Phương pháp mới 2 cho zoom", "Użyj nowej metody 2 do zoomu", "Folosește metoda nouă 2 pentru zoom"),
    # 136 Input and hotkeys - melonDS
    t("الإدخال والاختصارات - melonDS", "Input dan hotkey - melonDS", "Ввід і гарячі клавіші - melonDS", "Είσοδος και συντομεύσεις - melonDS", "Inmatning och snabbtangenter - melonDS", "อินพุตและปุ่มลัด - melonDS", "Vstup a klávesové zkratky - melonDS", "Input og genveje - melonDS", "Giriş ve kısayollar - melonDS", "Inndata og hurtigtaster - melonDS", "Bevitel és gyorsbillentyűk - melonDS", "Syöte ja pikanäppäimet - melonDS", "Nhập liệu và phím tắt - melonDS", "Wejście i skróty - melonDS", "Intrare și taste rapide - melonDS"),
    # 137 CPU emulation
    t("محاكاة CPU", "Emulasi CPU", "Емуляція CPU", "Εξομοίωση CPU", "CPU-emulering", "จำลอง CPU", "Emulace CPU", "CPU-emulering", "CPU emülasyonu", "CPU-emulering", "CPU emuláció", "CPU-emulointi", "Giả lập CPU", "Emulacja CPU", "Emulare CPU"),
    # 138 Browse...
    t("استعراض...", "Telusuri...", "Огляд...", "Περιήγηση...", "Bläddra...", "เรียกดู...", "Procházet...", "Gennemse...", "Gözat...", "Bla gjennom...", "Tallózás...", "Selaa...", "Duyệt...", "Przeglądaj...", "Răsfoiește..."),
    # 139 DSi ARM9 BIOS:
    t("DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:", "DSi ARM9 BIOS:"),
    # 140 Sync SD to folder:
    t("مزامنة SD مع المجلد:", "Sinkronkan SD ke folder:", "Синхронізувати SD з папкою:", "Συγχρονισμός SD με φάκελο:", "Synka SD till mapp:", "ซิงค์ SD กับโฟลเดอร์:", "Synchronizovat SD se složkou:", "Synkroniser SD til mappe:", "SD'yi klasörle senkronize et:", "Synkroniser SD til mappe:", "SD szinkronizálása mappával:", "Synkronoi SD kansioon:", "Đồng bộ SD với thư mục:", "Synchronizuj SD z folderem:", "Sincronizează SD cu folderul:"),
    # 141 Enable JIT recompiler
    t("تفعيل مُعيد تجميع JIT", "Aktifkan recompiler JIT", "Увімкнути JIT-перекомпілятор", "Ενεργοποίηση επαναμεταγλωττιστή JIT", "Aktivera JIT-omkompilator", "เปิดใช้ JIT recompiler", "Povolit JIT rekompilátor", "Aktivér JIT-recompiler", "JIT yeniden derleyiciyi etkinleştir", "Aktiver JIT-rekompilator", "JIT újrafordító engedélyezése", "Ota JIT-uudelleenkääntäjä käyttöön", "Bật trình biên dịch lại JIT", "Włącz rekompilator JIT", "Activează recompilatorul JIT"),
    # 142 Enable DLDI (for homebrew)
    t("تفعيل DLDI (لـ homebrew)", "Aktifkan DLDI (untuk homebrew)", "Увімкнути DLDI (для homebrew)", "Ενεργοποίηση DLDI (για homebrew)", "Aktivera DLDI (för homebrew)", "เปิดใช้ DLDI (สำหรับ homebrew)", "Povolit DLDI (pro homebrew)", "Aktivér DLDI (til homebrew)", "DLDI'yi etkinleştir (homebrew için)", "Aktiver DLDI (for homebrew)", "DLDI engedélyezése (homebrewhez)", "Ota DLDI käyttöön (homebrew)", "Bật DLDI (cho homebrew)", "Włącz DLDI (dla homebrew)", "Activează DLDI (pentru homebrew)"),
    # 143 Note: melonDS must be restarted
    t("ملاحظة: يجب إعادة تشغيل melonDS لتفعيل هذه التغييرات", "Catatan: melonDS harus di-restart agar perubahan ini berlaku", "Примітка: для застосування змін потрібно перезапустити melonDS", "Σημείωση: πρέπει να επανεκκινήσετε το melonDS για να ισχύσουν οι αλλαγές", "Obs: melonDS måste startas om för att ändringarna ska gälla", "หมายเหตุ: ต้องรีสตาร์ท melonDS เพื่อให้การเปลี่ยนแปลงมีผล", "Poznámka: melonDS musí být restartován, aby se změny projevily", "Bemærk: melonDS skal genstartes for at ændringerne træder i kraft", "Not: Bu değişikliklerin etkili olması için melonDS yeniden başlatılmalıdır", "Merk: melonDS må startes på nytt for at endringene skal tre i kraft", "Megjegyzés: a változtatások érvénybe lépéséhez újra kell indítani a melonDS-t", "Huom: melonDS on käynnistettävä uudelleen, jotta muutokset tulevat voimaan", "Lưu ý: phải khởi động lại melonDS để các thay đổi có hiệu lực", "Uwaga: melonDS musi zostać uruchomiony ponownie, aby zmiany weszły w życie", "Notă: melonDS trebuie repornit pentru ca modificările să aibă efect"),
    # 144 Display settings
    t("إعدادات العرض", "Pengaturan tampilan", "Налаштування відображення", "Ρυθμίσεις οθόνης", "Skärminställningar", "การตั้งค่าการแสดงผล", "Nastavení zobrazení", "Visningsindstillinger", "Görüntü ayarları", "Skjerminnstillinger", "Kijelző beállítások", "Näyttöasetukset", "Cài đặt hiển thị", "Ustawienia wyświetlania", "Setări afișare"),
    # 145 OpenGL (Classic)
    {l: "OpenGL (Classic)" for l in NEW_LANGS},
    # 146 Internal resolution:
    t("الدقة الداخلية:", "Resolusi internal:", "Внутрішня роздільність:", "Εσωτερική ανάλυση:", "Intern upplösning:", "ความละเอียดภายใน:", "Interní rozlišení:", "Intern opløsning:", "Dahili çözünürlük:", "Intern oppløsning:", "Belső felbontás:", "Sisäinen resoluutio:", "Độ phân giải nội bộ:", "Rozdzielczość wewnętrzna:", "Rezoluție internă:"),
    # 147 Picture source
    t("مصدر الصورة", "Sumber gambar", "Джерело зображення", "Πηγή εικόνας", "Bildkälla", "แหล่งภาพ", "Zdroj obrazu", "Billedkilde", "Görüntü kaynağı", "Bildekilde", "Képforrás", "Kuvan lähde", "Nguồn hình ảnh", "Źródło obrazu", "Sursă imagine"),
    # 148 Physical camera:
    t("الكاميرا الفعلية:", "Kamera fisik:", "Фізична камера:", "Φυσική κάμερα:", "Fysisk kamera:", "กล้องจริง:", "Fyzická kamera:", "Fysisk kamera:", "Fiziksel kamera:", "Fysisk kamera:", "Fizikai kamera:", "Fyysinen kamera:", "Camera vật lý:", "Kamera fizyczna:", "Cameră fizică:"),
    # 149 Microphone input
    t("إدخال الميكروفون", "Input mikrofon", "Вхід мікрофона", "Είσοδος μικροφώνου", "Mikrofoningång", "อินพุตไมโครโฟน", "Vstup mikrofonu", "Mikrofoninput", "Mikrofon girişi", "Mikrofoninngang", "Mikrofon bemenet", "Mikrofonitulo", "Đầu vào micro", "Wejście mikrofonu", "Intrare microfon"),
    # 150 None
    t("لا شيء", "Tidak ada", "Немає", "Κανένα", "Ingen", "ไม่มี", "Žádné", "Ingen", "Yok", "Ingen", "Nincs", "Ei mitään", "Không", "Brak", "Niciunul"),
    # 151 Automatic
    t("تلقائي", "Otomatis", "Автоматично", "Αυτόματο", "Automatisk", "อัตโนมัติ", "Automaticky", "Automatisk", "Otomatik", "Automatisk", "Automatikus", "Automaattinen", "Tự động", "Automatycznie", "Automat"),
    # 152 WAV file:
    t("ملف WAV:", "File WAV:", "Файл WAV:", "Αρχείο WAV:", "WAV-fil:", "ไฟล์ WAV:", "Soubor WAV:", "WAV-fil:", "WAV dosyası:", "WAV-fil:", "WAV fájl:", "WAV-tiedosto:", "Tệp WAV:", "Plik WAV:", "Fișier WAV:"),
    # 153 Frame step
    t("تقدم إطار", "Langkah frame", "Крок кадру", "Βήμα καρέ", "Bildsteg", "ขั้นเฟรม", "Krok snímku", "Billedtrin", "Kare adımı", "Bildesteg", "Képkocka léptetés", "Ruudun askel", "Bước khung hình", "Krok klatki", "Pas cadru"),
    # 154 Toggle FPS limit
    t("تبديل حد FPS", "Alihkan batas FPS", "Перемкнути обмеження FPS", "Εναλλαγή ορίου FPS", "Växla FPS-gräns", "สลับขีดจำกัด FPS", "Přepnout limit FPS", "Skift FPS-grænse", "FPS sınırını değiştir", "Bytt FPS-grense", "FPS limit váltása", "Vaihda FPS-rajoitus", "Bật/tắt giới hạn FPS", "Przełącz limit FPS", "Comută limita FPS"),
    # 155 Swap screen emphasis
    t("تبديل إبراز الشاشة", "Tukar penekanan layar", "Перемкнути акцент екрана", "Εναλλαγή έμφασης οθόνης", "Byt skärmton", "สลับการเน้นหน้าจอ", "Přepnout zvýraznění obrazovky", "Skift skærmfremhævning", "Ekran vurgusunu değiştir", "Bytt skjermfremheving", "Képernyő kiemelés váltása", "Vaihda näytön korostusta", "Đổi nhấn màn hình", "Przełącz podkreślenie ekranu", "Comută evidențierea ecranului"),
    # 156 Multiplayer settings - melonDS
    t("إعدادات اللعب الجماعي - melonDS", "Pengaturan multipemain - melonDS", "Налаштування мультиплеєра - melonDS", "Ρυθμίσεις multiplayer - melonDS", "Flerspelarinställningar - melonDS", "การตั้งค่าเล่นหลายคน - melonDS", "Nastavení multiplayeru - melonDS", "Multiplayer-indstillinger - melonDS", "Çok oyunculu ayarları - melonDS", "Flerspillerinnstillinger - melonDS", "Többjátékos beállítások - melonDS", "Moninpeliasetukset - melonDS", "Cài đặt nhiều người chơi - melonDS", "Ustawienia wieloosobowe - melonDS", "Setări multiplayer - melonDS"),
    # 157 Data reception timeout:
    t("مهلة استقبال البيانات: ", "Batas waktu penerimaan data: ", "Таймаут прийому даних: ", "Λήξη χρόνου λήψης δεδομένων: ", "Datamottagningstimeout: ", "หมดเวลารับข้อมูล: ", "Časový limit příjmu dat: ", "Datareceptionstimeout: ", "Veri alma zaman aşımı: ", "Datamottakstimeout: ", "Adatfogadási időkorlát: ", "Tietojen vastaanoton aikakatkaisu: ", "Hết thời gian nhận dữ liệu: ", "Limit czasu odbioru danych: ", "Timeout recepție date: "),
    # 158 Direct mode [TEXT PLACEHOLDER]
    t("الوضع المباشر [TEXT PLACEHOLDER]", "Mode langsung [TEXT PLACEHOLDER]", "Прямий режим [TEXT PLACEHOLDER]", "Άμεση λειτουργία [TEXT PLACEHOLDER]", "Direktläge [TEXT PLACEHOLDER]", "โหมดตรง [TEXT PLACEHOLDER]", "Přímý režim [TEXT PLACEHOLDER]", "Direkte tilstand [TEXT PLACEHOLDER]", "Doğrudan mod [TEXT PLACEHOLDER]", "Direktemodus [TEXT PLACEHOLDER]", "Közvetlen mód [TEXT PLACEHOLDER]", "Suora tila [TEXT PLACEHOLDER]", "Chế độ trực tiếp [TEXT PLACEHOLDER]", "Tryb bezpośredni [TEXT PLACEHOLDER]", "Mod direct [TEXT PLACEHOLDER]"),
    # 159 (none)
    t("(لا شيء)", "(tidak ada)", "(немає)", "(κανένα)", "(ingen)", "(ไม่มี)", "(žádné)", "(ingen)", "(yok)", "(ingen)", "(nincs)", "(ei mitään)", "(không)", "(brak)", "(niciunul)"),
    # 160 Message:
    t("الرسالة:", "Pesan:", "Повідомлення:", "Μήνυμα:", "Meddelande:", "ข้อความ:", "Zpráva:", "Besked:", "Mesaj:", "Melding:", "Üzenet:", "Viesti:", "Tin nhắn:", "Wiadomość:", "Mesaj:"),
    # 161 (leave empty to use default MAC)
    t("(اتركه فارغًا لاستخدام MAC الافتراضي)", "(kosongkan untuk menggunakan MAC default)", "(залиште порожнім для MAC за замовчуванням)", "(αφήστε κενό για προεπιλεγμένο MAC)", "(lämna tomt för standard-MAC)", "(เว้นว่างเพื่อใช้ MAC เริ่มต้น)", "(nechte prázdné pro výchozí MAC)", "(lad tom for standard-MAC)", "(varsayılan MAC için boş bırakın)", "(la stå tom for standard-MAC)", "(hagyd üresen az alapértelmezett MAC-hez)", "(jätä tyhjäksi oletus-MAC:lle)", "(để trống để dùng MAC mặc định)", "(zostaw puste, aby użyć domyślnego MAC)", "(lasă gol pentru MAC implicit)"),
    # 162 Hide mouse after inactivity
    t("إخفاء الماوس بعد عدم النشاط", "Sembunyikan mouse setelah tidak aktif", "Сховати мишу при бездіяльності", "Απόκρυψη ποντικιού μετά από αδράνεια", "Dölj mus efter inaktivitet", "ซ่อนเมาส์เมื่อไม่ใช้งาน", "Skrýt myš po nečinnosti", "Skjul mus efter inaktivitet", "Hareketsizlikten sonra fareyi gizle", "Skjul mus etter inaktivitet", "Egér elrejtése inaktivitás után", "Piilota hiiri käyttämättömyyden jälkeen", "Ẩn chuột sau khi không hoạt động", "Ukryj mysz po bezczynności", "Ascunde mouse-ul după inactivitate"),
    # 163 Framerate
    t("معدل الإطارات ", "Framerate ", "Частота кадрів ", "Ρυθμός καρέ ", "Bildfrekvens ", "อัตราเฟรม ", "Snímková frekvence ", "Billedhastighed ", "Kare hızı ", "Bildefrekvens ", "Képkockasebesség ", "Ruudunpäivitysnopeus ", "Tốc độ khung hình ", "Liczba klatek ", "Rată de cadre "),
    # 164 Accurate
    t("دقيق", "Akurat", "Точний", "Ακριβές", "Exakt", "แม่นยำ", "Přesné", "Nøjagtig", "Hassas", "Nøyaktig", "Pontos", "Tarkka", "Chính xác", "Dokładny", "Precis"),
    # 165 3x
    {l: "3x" for l in NEW_LANGS},
    # 166 Save files path:
    t("مسار ملفات الحفظ:", "Path file save:", "Шлях до збережень:", "Διαδρομή αρχείων αποθήκευσης:", "Sökväg till sparfiler:", "เส้นทางไฟล์เซฟ:", "Cesta k uloženým hrám:", "Sti til savefiler:", "Kayıt dosyaları yolu:", "Sti til lagringsfiler:", "Mentésfájlok útvonala:", "Tallennustiedostojen polku:", "Đường dẫn file save:", "Ścieżka zapisów:", "Cale fișiere salvare:"),
    # 167 Import...
    t("استيراد...", "Impor...", "Імпорт...", "Εισαγωγή...", "Importera...", "นำเข้า...", "Importovat...", "Importer...", "İçe aktar...", "Importer...", "Importálás...", "Tuo...", "Nhập...", "Importuj...", "Importă..."),
    # 168 Description:
    t("الوصف:", "Deskripsi:", "Опис:", "Περιγραφή:", "Beskrivning:", "คำอธิบาย:", "Popis:", "Beskrivelse:", "Açıklama:", "Beskrivelse:", "Leírás:", "Kuvaus:", "Mô tả:", "Opis:", "Descriere:"),
    # 169 DSi Battery
    t("بطارية DSi", "Baterai DSi", "Батарея DSi", "Μπαταρία DSi", "DSi-batteri", "แบตเตอรี่ DSi", "Baterie DSi", "DSi-batteri", "DSi pili", "DSi-batteri", "DSi akkumulátor", "DSi-akku", "Pin DSi", "Bateria DSi", "Baterie DSi"),
    # 170 Battery Level
    t("مستوى البطارية", "Level baterai", "Рівень заряду", "Επίπεδο μπαταρίας", "Batterinivå", "ระดับแบตเตอรี่", "Úroveň baterie", "Batteriniveau", "Pil seviyesi", "Batterinivå", "Akkumulátor szint", "Akun taso", "Mức pin", "Poziom baterii", "Nivel baterie"),
    # 171 Reset to system date and time
    t("إعادة ضبط إلى تاريخ ووقت النظام", "Atur ulang ke tanggal dan waktu sistem", "Скинути на системні дату й час", "Επαναφορά σε ημερομηνία και ώρα συστήματος", "Återställ till systemdatum och -tid", "รีเซ็ตเป็นวันที่และเวลาของระบบ", "Obnovit na systémové datum a čas", "Nulstil til systemdato og -tid", "Sistem tarih ve saatine sıfırla", "Tilbakestill til systemdato og -tid", "Visszaállítás a rendszer dátumára és idejére", "Palauta järjestelmän päivämäärään ja aikaan", "Đặt lại theo ngày giờ hệ thống", "Resetuj do daty i czasu systemu", "Resetează la data și ora sistemului"),
    # 172 Titles
    t("العناوين", "Judul", "Заголовки", "Τίτλοι", "Titlar", "ชื่อเรื่อง", "Názvy", "Titler", "Başlıklar", "Titler", "Címek", "Otsikot", "Tiêu đề", "Tytuły", "Titluri"),
    # 173 Italian title:
    t("العنوان الإيطالي:", "Judul Italia:", "Італійська назва:", "Ιταλικός τίτλος:", "Italiensk titel:", "ชื่อเรื่องภาษาอิตาลี:", "Italský název:", "Italiensk titel:", "İtalyanca başlık:", "Italiensk tittel:", "Olasz cím:", "Italialainen otsikko:", "Tiêu đề tiếng Ý:", "Tytuł włoski:", "Titlu italian:"),
    # 174 ARM9 ROM offset:
    t("إزاحة ROM ARM9: ", "Offset ROM ARM9: ", "Зміщення ROM ARM9: ", "Μετατόπιση ROM ARM9: ", "ARM9 ROM-offset: ", "ออฟเซ็ต ROM ARM9: ", "Offset ROM ARM9: ", "ARM9 ROM-offset: ", "ARM9 ROM ofseti: ", "ARM9 ROM-offset: ", "ARM9 ROM eltolás: ", "ARM9 ROM -siirtymä: ", "Offset ROM ARM9: ", "Przesunięcie ROM ARM9: ", "Offset ROM ARM9: "),
    # 175 ARM7 entry address:
    t("عنوان دخول ARM7:", "Alamat masuk ARM7:", "Адрес входу ARM7:", "Διεύθυνση εισόδου ARM7:", "ARM7-startadress:", "ที่อยู่เข้า ARM7:", "Vstupní adresa ARM7:", "ARM7-indgangsadresse:", "ARM7 giriş adresi:", "ARM7-inngangsadresse:", "ARM7 belépési cím:", "ARM7:n sisääntuloosoite:", "Địa chỉ vào ARM7:", "Adres wejścia ARM7:", "Adres intrare ARM7:"),
    # 176 FNT size:
    t("حجم FNT:", "Ukuran FNT:", "Розмір FNT:", "Μέγεθος FNT:", "FNT-storlek:", "ขนาด FNT:", "Velikost FNT:", "FNT-størrelse:", "FNT boyutu:", "FNT-størrelse:", "FNT méret:", "FNT-koko:", "Kích thước FNT:", "Rozmiar FNT:", "Dimensiune FNT:"),
    # 177 Game code:
    t("رمز اللعبة:", "Kode game:", "Код гри:", "Κωδικός παιχνιδιού:", "Spelkod:", "รหัสเกม:", "Kód hry:", "Spilkode:", "Oyun kodu:", "Spillkode:", "Játékkód:", "Pelikoodi:", "Mã game:", "Kod gry:", "Cod joc:"),
    # 178 RAM info - melonDS
    t("معلومات RAM - melonDS", "Info RAM - melonDS", "Інфо RAM - melonDS", "Πληροφορίες RAM - melonDS", "RAM-info - melonDS", "ข้อมูล RAM - melonDS", "Info RAM - melonDS", "RAM-info - melonDS", "RAM bilgisi - melonDS", "RAM-info - melonDS", "RAM infó - melonDS", "RAM-tiedot - melonDS", "Thông tin RAM - melonDS", "Info RAM - melonDS", "Info RAM - melonDS"),
    # 179 4bytes
    t("4 بايت", "4 byte", "4 байти", "4 byte", "4 byte", "4 ไบต์", "4 bajty", "4 bytes", "4 bayt", "4 byte", "4 bájt", "4 tavua", "4 byte", "4 bajty", "4 octeți"),
    # 180 Host LAN game - melonDS
    t("استضافة لعبة LAN - melonDS", "Host game LAN - melonDS", "Хостити LAN-гру - melonDS", "Φιλοξενία παιχνιδιού LAN - melonDS", "Värd LAN-spel - melonDS", "โฮสต์เกม LAN - melonDS", "Hostovat LAN hru - melonDS", "Vært LAN-spil - melonDS", "LAN oyunu barındır - melonDS", "Vert LAN-spill - melonDS", "LAN játék hosztolása - melonDS", "Isännöi LAN-peliä - melonDS", "Chủ trì game LAN - melonDS", "Hostuj grę LAN - melonDS", "Găzduiește joc LAN - melonDS"),
    # 181 Direct connect...
    t("اتصال مباشر...", "Hubungkan langsung...", "Пряме підключення...", "Άμεση σύνδεση...", "Direktanslutning...", "เชื่อมต่อโดยตรง...", "Přímé připojení...", "Direkte forbindelse...", "Doğrudan bağlan...", "Direkte tilkobling...", "Közvetlen csatlakozás...", "Suora yhteys...", "Kết nối trực tiếp...", "Połącz bezpośrednio...", "Conectare directă..."),
    # 182 Status
    t("الحالة", "Status", "Статус", "Κατάσταση", "Status", "สถานะ", "Stav", "Status", "Durum", "Status", "Állapot", "Tila", "Trạng thái", "Status", "Stare"),
    # 183 Failed to start LAN game.
    t("فشل بدء لعبة LAN.", "Gagal memulai game LAN.", "Не вдалося запустити LAN-гру.", "Αποτυχία εκκίνησης παιχνιδιού LAN.", "Det gick inte att starta LAN-spel.", "เริ่มเกม LAN ล้มเหลว", "Nepodařilo se spustit LAN hru.", "Kunne ikke starte LAN-spil.", "LAN oyunu başlatılamadı.", "Kunne ikke starte LAN-spill.", "A LAN játék indítása sikertelen.", "LAN-pelin käynnistys epäonnistui.", "Không khởi động được game LAN.", "Nie udało się uruchomić gry LAN.", "Nu s-a putut porni jocul LAN."),
    # 184 View on &GitHub
    t("عرض على &GitHub", "Lihat di &GitHub", "Переглянути на &GitHub", "Προβολή στο &GitHub", "Visa på &GitHub", "ดูบน &GitHub", "Zobrazit na &GitHub", "Vis på &GitHub", "&GitHub'da görüntüle", "Vis på &GitHub", "Megtekintés a &GitHub-on", "Näytä &GitHubissa", "Xem trên &GitHub", "Zobacz na &GitHub", "Vezi pe &GitHub"),
    # 185 Red
    t("أحمر", "Merah", "Червоний", "Κόκκινο", "Röd", "แดง", "Červená", "Rød", "Kırmızı", "Rød", "Piros", "Punainen", "Đỏ", "Czerwony", "Roșu"),
    # 186 Light green
    t("أخضر فاتح", "Hijau muda", "Світло-зелений", "Ανοιχτό πράσινο", "Ljusgrön", "เขียวอ่อน", "Světle zelená", "Lysegrøn", "Açık yeşil", "Lysegrønn", "Világoszöld", "Vaaleanvihreä", "Xanh nhạt", "Jasnozielony", "Verde deschis"),
    # 187 Dark blue
    t("أزرق داكن", "Biru tua", "Темно-синій", "Σκούρο μπλε", "Mörkblå", "น้ำเงินเข้ม", "Tmavě modrá", "Mørkeblå", "Koyu mavi", "Mørkeblå", "Sötétkék", "Tummansininen", "Xanh đậm", "Ciemnoniebieski", "Albastru închis"),
    # 188 German
    t("الألمانية", "Jerman", "Німецька", "Γερμανικά", "Tyska", "เยอรมัน", "Němčina", "Tysk", "Almanca", "Tysk", "Német", "Saksa", "Tiếng Đức", "Niemiecki", "Germană"),
    # 189 February
    t("فبراير", "Februari", "Лютий", "Φεβρουάριος", "Februari", "กุมภาพันธ์", "Únor", "Februar", "Şubat", "Februar", "Február", "Helmikuu", "Tháng 2", "Luty", "Februarie"),
    # 190 July
    t("يوليو", "Juli", "Липень", "Ιούλιος", "Juli", "กรกฎาคม", "Červenec", "Juli", "Temmuz", "Juli", "Július", "Heinäkuu", "Tháng 7", "Lipiec", "Iulie"),
    # 191 December
    t("ديسمبر", "Desember", "Грудень", "Δεκέμβριος", "December", "ธันวาคม", "Prosinec", "December", "Aralık", "Desember", "December", "Joulukuu", "Tháng 12", "Grudzień", "Decembrie"),
]


def main():
    path = Path(__file__).parent / "loc_chunk_3_full.json"
    entries = json.loads(path.read_text(encoding="utf-8"))

    if len(entries) != len(T):
        raise SystemExit(f"Expected {len(T)} translation rows, got {len(entries)} entries")

    out = []
    for i, entry in enumerate(entries):
        row = dict(entry)
        for lang in NEW_LANGS:
            row[lang] = T[i][lang]
        out.append(row)

    path.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(out)} entries to {path}")


if __name__ == "__main__":
    main()

