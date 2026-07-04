#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Build loc_chunk_0_extended.json from loc_chunk_0_translated.json."""
import json
from pathlib import Path

# Hand-reviewed translations keyed by English source string
EXTRA = {
    "ON": {
        "it": "ON", "nl": "ON", "pt": "ON", "ru": "ON", "zh": "ON", "ko": "ON"
    },
    "Reset": {
        "it": "Reimposta", "nl": "Resetten", "pt": "Repor", "ru": "Сброс", "zh": "重置", "ko": "재설정"
    },
    "Browseâ\u0080¦": {
        "it": "Sfoglia…", "nl": "Bladeren…", "pt": "Procurar…", "ru": "Обзор…", "zh": "浏览…", "ko": "찾아보기…"
    },
    "Crosshair Color": {
        "it": "Colore mirino", "nl": "Kleur van richtkruis", "pt": "Cor da mira", "ru": "Цвет прицела", "zh": "准星颜色", "ko": "조준선 색상"
    },
    "Preview ON": {
        "it": "Anteprima ON", "nl": "Voorbeeld ON", "pt": "Pré-visualização ON", "ru": "Предпросмотр ON", "zh": "预览 ON", "ko": "미리보기 ON"
    },
    "Text": {
        "it": "Testo", "nl": "Tekst", "pt": "Texto", "ru": "Текст", "zh": "文字", "ko": "텍스트"
    },
    "System Font": {
        "it": "Carattere di sistema", "nl": "Systeemlettertype", "pt": "Fonte do sistema", "ru": "Системный шрифт", "zh": "系统字体", "ko": "시스템 글꼴"
    },
    "English": {
        "it": "Inglese", "nl": "Engels", "pt": "Inglês", "ru": "Английский", "zh": "英语", "ko": "영어"
    },
    "Open recent": {
        "it": "Apri recente", "nl": "Recent openen", "pt": "Abrir recente", "ru": "Открыть недавнее", "zh": "打开最近", "ko": "최근 열기"
    },
    "Eject cart": {
        "it": "Espelli cartuccia", "nl": "Cartridge uitwerpen", "pt": "Ejetar cartucho", "ru": "Извлечь картридж", "zh": "弹出卡带", "ko": "카트리지 꺼내기"
    },
    "Load state": {
        "it": "Carica stato", "nl": "Status laden", "pt": "Carregar estado", "ru": "Загрузить состояние", "zh": "读取即时存档", "ko": "상태 불러오기"
    },
    "System": {
        "it": "Sistema", "nl": "Systeem", "pt": "Sistema", "ru": "Система", "zh": "系统", "ko": "시스템"
    },
    "Date and time": {
        "it": "Data e ora", "nl": "Datum en tijd", "pt": "Data e hora", "ru": "Дата и время", "zh": "日期和时间", "ko": "날짜 및 시간"
    },
    "Manage DSi titles": {
        "it": "Gestisci titoli DSi", "nl": "DSi-titels beheren", "pt": "Gerir títulos DSi", "ru": "Управление заголовками DSi", "zh": "管理 DSi 标题", "ko": "DSi 타이틀 관리"
    },
    "View": {
        "it": "Visualizza", "nl": "Beeld", "pt": "Exibir", "ru": "Вид", "zh": "视图", "ko": "보기"
    },
    "Natural": {
        "it": "Naturale", "nl": "Natuurlijk", "pt": "Natural", "ru": "Естественный", "zh": "自然", "ko": "자연"
    },
    "Screen sizing": {
        "it": "Dimensione schermo", "nl": "Schermgrootte", "pt": "Tamanho da tela", "ru": "Размер экрана", "zh": "画面缩放", "ko": "화면 크기"
    },
    "Bottom only": {
        "it": "Solo inferiore", "nl": "Alleen onder", "pt": "Somente inferior", "ru": "Только нижний", "zh": "仅下屏", "ko": "하단만"
    },
    "Show OSD": {
        "it": "Mostra OSD", "nl": "OSD weergeven", "pt": "Mostrar OSD", "ru": "Показывать OSD", "zh": "显示 OSD", "ko": "OSD 표시"
    },
    "Video settings": {
        "it": "Impostazioni video", "nl": "Video-instellingen", "pt": "Configurações de vídeo", "ru": "Настройки видео", "zh": "视频设置", "ko": "비디오 설정"
    },
    "Firmware settings": {
        "it": "Impostazioni firmware", "nl": "Firmware-instellingen", "pt": "Configurações de firmware", "ru": "Настройки прошивки", "zh": "固件设置", "ko": "펌웨어 설정"
    },
    "MelonPrime": {
        "it": "MelonPrime", "nl": "MelonPrime", "pt": "MelonPrime", "ru": "MelonPrime", "zh": "MelonPrime", "ko": "MelonPrime"
    },
    "Disable SF (Shadow Freeze)": {
        "it": "Disabilita SF (Shadow Freeze)", "nl": "SF (Shadow Freeze) uitschakelen", "pt": "Desativar SF (Shadow Freeze)", "ru": "Отключить SF (Shadow Freeze)", "zh": "禁用 SF (Shadow Freeze)", "ko": "SF (Shadow Freeze) 비활성화"
    },
    "MelonPrime Settings": {
        "it": "Impostazioni MelonPrime", "nl": "MelonPrime-instellingen", "pt": "Configurações do MelonPrime", "ru": "Настройки MelonPrime", "zh": "MelonPrime 设置", "ko": "MelonPrime 설정"
    },
    "Input": {
        "it": "Input", "nl": "Invoer", "pt": "Entrada", "ru": "Ввод", "zh": "输入", "ko": "입력"
    },
    "CURSOR CLIP SETTINGS": {
        "it": "IMPOSTAZIONI LIMITE CURSORE", "nl": "CURSORBEPERKING", "pt": "LIMITES DO CURSOR", "ru": "ОГРАНИЧЕНИЕ КУРСОРА", "zh": "光标限制设置", "ko": "커서 제한 설정"
    },
    "BUG FIXES": {
        "it": "CORREZIONI BUG", "nl": "BUGFIXES", "pt": "CORREÇÕES DE ERROS", "ru": "ИСПРАВЛЕНИЯ ОШИБОК", "zh": "错误修复", "ko": "버그 수정"
    },
    "VIDEO QUALITY": {
        "it": "QUALITÀ VIDEO", "nl": "VIDEOKWALITEIT", "pt": "QUALIDADE DE VÍDEO", "ru": "КАЧЕСТВО ВИДЕО", "zh": "视频质量", "ko": "비디오 품질"
    },
    "OUTLINE OVERRIDE": {
        "it": "SOSTITUZIONE CONTORNO", "nl": "OORLIJNOVERSCHRIJVING", "pt": "SUBSTITUIÇÃO DE CONTORNO", "ru": "ПЕРЕОПРЕДЕЛЕНИЕ КОНТУРА", "zh": "轮廓覆盖", "ko": "외곽선 재정의"
    },
    "MATCH STATUS HUD": {
        "it": "HUD STATO PARTITA", "nl": "WEDSTRIJDSTATUS-HUD", "pt": "HUD DE ESTADO DA PARTIDA", "ru": "HUD СОСТОЯНИЯ МАТЧА", "zh": "比赛状态 HUD", "ko": "매치 상태 HUD"
    },
    "Joystick mappings": {
        "it": "Assegnazioni joystick", "nl": "Joystick-toewijzingen", "pt": "Mapeamentos de joystick", "ru": "Назначения джойстика", "zh": "摇杆映射", "ko": "조이스틱 매핑"
    },
    "[Metroid] (Mouse Left) Shoot/Scan": {
        "it": "[Metroid] (Tasto sinistro) Sparare/Scansionare", "nl": "[Metroid] (Linkermuisknop) Schieten/Scannen", "pt": "[Metroid] (Botão esquerdo) Atirar/Escanear", "ru": "[Metroid] (ЛКМ) Стрельба/Сканирование", "zh": "[Metroid] (鼠标左键) 射击/扫描", "ko": "[Metroid] (마우스 왼쪽) 사격/스캔"
    },
    "[Metroid] (Shift) Hold to Fast Morph Ball Boost": {
        "it": "[Metroid] (Shift) Tieni premuto per Morph Ball Boost veloce", "nl": "[Metroid] (Shift) Ingedrukt houden voor snelle Morph Ball Boost", "pt": "[Metroid] (Shift) Manter pressionado para impulso Morph Ball rápido", "ru": "[Metroid] (Shift) Удерживать для быстрого Morph Ball Boost", "zh": "[Metroid] (Shift) 按住以快速变形球加速", "ko": "[Metroid] (Shift) 길게 눌러 빠른 Morph Ball 부스트"
    },
    "[Metroid] (3) Weapon 3. Judicator": {
        "it": "[Metroid] (3) Arma 3: Judicator", "nl": "[Metroid] (3) Wapen 3: Judicator", "pt": "[Metroid] (3) Arma 3: Judicator", "ru": "[Metroid] (3) Оружие 3: Judicator", "zh": "[Metroid] (3) 武器 3：Judicator", "ko": "[Metroid] (3) 무기 3: Judicator"
    },
    "[Metroid] (Tab) Menu/Map": {
        "it": "[Metroid] (Tab) Menu/Mappa", "nl": "[Metroid] (Tab) Menu/Kaart", "pt": "[Metroid] (Tab) Menu/Mapa", "ru": "[Metroid] (Tab) Меню/Карта", "zh": "[Metroid] (Tab) 菜单/地图", "ko": "[Metroid] (Tab) 메뉴/맵"
    },
    "[Metroid] (C) Scan Visor": {
        "it": "[Metroid] (C) Visore di scansione", "nl": "[Metroid] (C) Scanvisor", "pt": "[Metroid] (C) Visor de escaneamento", "ru": "[Metroid] (C) Сканирующий визор", "zh": "[Metroid] (C) 扫描面罩", "ko": "[Metroid] (C) 스캔 바이저"
    },
    "[Metroid] (H) UI No (Enter Starship)": {
        "it": "[Metroid] (H) UI No (Entra nella nave)", "nl": "[Metroid] (H) UI Nee (Ruimteschip betreden)", "pt": "[Metroid] (H) UI Não (Entrar na nave)", "ru": "[Metroid] (H) UI Нет (Войти в корабль)", "zh": "[Metroid] (H) UI 否 (进入星舰)", "ko": "[Metroid] (H) UI 아니오 (우주선 진입)"
    },
    "Mode": {
        "it": "Modalità", "nl": "Modus", "pt": "Modo", "ru": "Режим", "zh": "模式", "ko": "모드"
    },
    "MoonLike Aim": {
        "it": "Mira MoonLike", "nl": "MoonLike-richting", "pt": "Mira MoonLike", "ru": "Прицел MoonLike", "zh": "MoonLike 瞄准", "ko": "MoonLike 조준"
    },
    "Enable Joy2KeySupport (enable this if keys sometimes get stuck; slightly increases input delay)": {
        "it": "Abilita Joy2KeySupport (attivalo se i tasti a volte restano bloccati; aumenta leggermente il ritardo di input)", "nl": "Joy2KeySupport inschakelen (inschakelen als toetsen soms blijven hangen; verhoogt licht de invoervertraging)", "pt": "Ativar Joy2KeySupport (ative se as teclas às vezes ficarem presas; aumenta ligeiramente o atraso de entrada)", "ru": "Включить Joy2KeySupport (включите, если клавиши иногда залипают; немного увеличивает задержку ввода)", "zh": "启用 Joy2KeySupport（若按键偶尔卡住请启用；会略微增加输入延迟）", "ko": "Joy2KeySupport 활성화 (키가 가끔 눌린 채로 남을 때 사용; 입력 지연이 약간 증가함)"
    },
    "Zoom Aim Scale %": {
        "it": "Scala mira zoom %", "nl": "Zoom-richtschaal %", "pt": "Escala de mira com zoom %", "ru": "Масштаб прицела при зуме %", "zh": "缩放瞄准比例 %", "ko": "줌 조준 배율 %"
    },
    "Apply Input to Custom HUD": {
        "it": "Applica input all'HUD personalizzato", "nl": "Invoer toepassen op aangepaste HUD", "pt": "Aplicar entrada ao HUD personalizado", "ru": "Применить ввод к пользовательскому HUD", "zh": "将输入应用到自定义 HUD", "ko": "입력을 사용자 HUD에 적용"
    },
    "In-game only, temporarily force Screen Sizing to Top Only and Screen Layout to Natural. Outside of gameplay, restore the normal window settings.": {
        "it": "Solo in gioco, forza temporaneamente Screen Sizing su Top Only e Screen Layout su Natural. Fuori dal gioco, ripristina le impostazioni normali della finestra.", "nl": "Alleen in het spel, dwing tijdelijk Screen Sizing naar Top Only en Screen Layout naar Natural. Buiten het spel worden de normale vensterinstellingen hersteld.", "pt": "Somente no jogo, força temporariamente Screen Sizing para Top Only e Screen Layout para Natural. Fora do jogo, restaura as configurações normais da janela.", "ru": "Только в игре временно принудительно устанавливает Screen Sizing на Top Only и Screen Layout на Natural. Вне игры восстанавливает обычные настройки окна.", "zh": "仅在游戏中临时强制 Screen Sizing 为 Top Only、Screen Layout 为 Natural。游戏外恢复正常的窗口设置。", "ko": "게임 중에만 Screen Sizing을 Top Only, Screen Layout을 Natural로 일시 강제합니다. 게임 외에는 일반 창 설정으로 복원합니다."
    },
    "16:10 (3DS)": {
        "it": "16:10 (3DS)", "nl": "16:10 (3DS)", "pt": "16:10 (3DS)", "ru": "16:10 (3DS)", "zh": "16:10 (3DS)", "ko": "16:10 (3DS)"
    },
    "Show Enemy HP Meter Online": {
        "it": "Mostra barra HP nemico online", "nl": "Vijand-HP-balk online weergeven", "pt": "Mostrar medidor de HP do inimigo online", "ru": "Показывать шкалу HP врага онлайн", "zh": "在线显示敌人 HP 条", "ko": "온라인에서 적 HP 미터 표시"
    },
    "Also unlock additional stages": {
        "it": "Sblocca anche fasi aggiuntive", "nl": "Ook extra levels ontgrendelen", "pt": "Também desbloquear fases adicionais", "ru": "Также разблокировать дополнительные этапы", "zh": "同时解锁额外关卡", "ko": "추가 스테이지도 잠금 해제"
    },
    "Cloak": {
        "it": "Mimetizzazione", "nl": "Camouflage", "pt": "Camuflagem", "ru": "Маскировка", "zh": "隐身", "ko": "클로킹"
    },
    "Video quality: High2 (Recommended. Best Performance)": {
        "it": "Qualità video: High2 (Consigliata. Migliori prestazioni)", "nl": "Videokwaliteit: High2 (Aanbevolen. Beste prestaties)", "pt": "Qualidade de vídeo: High2 (Recomendada. Melhor desempenho)", "ru": "Качество видео: High2 (Рекомендуется. Лучшая производительность)", "zh": "视频质量：High2（推荐。最佳性能）", "ko": "비디오 품질: High2 (권장. 최고 성능)"
    },
    "Select the hunter to apply.": {
        "it": "Seleziona il cacciatore da applicare.", "nl": "Selecteer de jager om toe te passen.", "pt": "Selecione o caçador a aplicar.", "ru": "Выберите охотника для применения.", "zh": "选择要应用的猎人。", "ko": "적용할 헌터를 선택하세요."
    },
    "Auto Scale â\u0080\u0094 Base": {
        "it": "Scala automatica — Base", "nl": "Autom. schaal — Basis", "pt": "Escala automática — Base", "ru": "Авто-масштаб — База", "zh": "自动缩放 — 基准", "ko": "자동 배율 — 기준"
    },
    "Per Damage (Low / Medium / High)": {
        "it": "Per danno (Basso / Medio / Alto)", "nl": "Per schade (Laag / Gemiddeld / Hoog)", "pt": "Por dano (Baixo / Médio / Alto)", "ru": "По урону (Низкий / Средний / Высокий)", "zh": "按伤害（低 / 中 / 高）", "ko": "피해별 (낮음 / 보통 / 높음)"
    },
    "Developer-only option enabled in this build.": {
        "it": "Opzione solo per sviluppatori abilitata in questa build.", "nl": "Ontwikkelaarsoptie ingeschakeld in deze build.", "pt": "Opção exclusiva para desenvolvedores ativada nesta compilação.", "ru": "Опция только для разработчиков включена в этой сборке.", "zh": "此版本中已启用仅限开发者的选项。", "ko": "이 빌드에서 개발자 전용 옵션이 활성화되어 있습니다."
    },
    "Checked: inject a native fire edge inside the game's Biped fire update. Unchecked: use the older fixed input/overlay path.": {
        "it": "Selezionato: inietta un segnale di fuoco nativo nell'aggiornamento fuoco Biped del gioco. Deselezionato: usa il vecchio percorso fisso input/overlay.", "nl": "Aangevinkt: injecteert een native vuur-edge in de Biped-vuurupdate van het spel. Uitgevinkt: gebruikt het oudere vaste invoer/overlay-pad.", "pt": "Marcado: injeta um sinal de disparo nativo na atualização de fogo Biped do jogo. Desmarcado: usa o caminho antigo fixo de entrada/sobreposição.", "ru": "Включено: внедряет нативный сигнал выстрела в обновление огня Biped игры. Выключено: использует старый фиксированный путь ввода/оверлея.", "zh": "勾选：在游戏 Biped 射击更新中注入原生射击信号。取消勾选：使用旧的固定输入/叠加路径。", "ko": "선택: 게임 Biped 사격 업데이트에 네이티브 발사 신호를 주입합니다. 해제: 이전 고정 입력/오버레이 경로를 사용합니다."
    },
    "Setting Key: Metroid.Volume.SFX (0â\u0080\u00939)": {
        "it": "Chiave impostazione: Metroid.Volume.SFX (0–9)", "nl": "Instellingssleutel: Metroid.Volume.SFX (0–9)", "pt": "Chave de configuração: Metroid.Volume.SFX (0–9)", "ru": "Ключ настройки: Metroid.Volume.SFX (0–9)", "zh": "设置键：Metroid.Volume.SFX (0–9)", "ko": "설정 키: Metroid.Volume.SFX (0–9)"
    },
    "Screen Sync Mode: Off = no sync call, glFinish = wait for GL commands to complete, DwmFlush = wait for DWM compositor (Windows only)": {
        "it": "Modalità sync schermo: Off = nessuna chiamata sync, glFinish = attendi completamento comandi GL, DwmFlush = attendi compositor DWM (solo Windows)", "nl": "Scherm-syncmodus: Uit = geen sync-aanroep, glFinish = wacht tot GL-opdrachten voltooid zijn, DwmFlush = wacht op DWM-compositor (alleen Windows)", "pt": "Modo de sincronização de tela: Desligado = sem chamada de sync, glFinish = aguardar conclusão dos comandos GL, DwmFlush = aguardar compositor DWM (somente Windows)", "ru": "Режим синхронизации экрана: Выкл = без вызова sync, glFinish = ожидание завершения GL-команд, DwmFlush = ожидание композитора DWM (только Windows)", "zh": "画面同步模式：关 = 不调用 sync，glFinish = 等待 GL 命令完成，DwmFlush = 等待 DWM 合成器（仅 Windows）", "ko": "화면 동기화 모드: 끔 = sync 호출 없음, glFinish = GL 명령 완료 대기, DwmFlush = DWM compositor 대기 (Windows만)"
    },
    "Changes the HP value at which the low-HP warning sound and warning HUD state trigger (vanilla: 25). Applied at the start of each match based on the current Damage setting.": {
        "it": "Modifica il valore HP che attiva il suono di avviso HP basso e lo stato HUD di avviso (vanilla: 25). Applicato all'inizio di ogni partita in base all'impostazione Danno attuale.", "nl": "Wijzigt de HP-waarde waarop het lage-HP-waarschuwingsgeluid en de waarschuwings-HUD-status worden geactiveerd (vanilla: 25). Toegepast aan het begin van elke wedstrijd op basis van de huidige Schade-instelling.", "pt": "Altera o valor de HP em que o som de aviso de HP baixo e o estado de aviso do HUD são acionados (vanilla: 25). Aplicado no início de cada partida com base na configuração de Dano atual.", "ru": "Изменяет значение HP, при котором срабатывают звук предупреждения о низком HP и состояние предупреждения HUD (vanilla: 25). Применяется в начале каждого матча на основе текущей настройки урона.", "zh": "更改触发低 HP 警告音和警告 HUD 状态的 HP 值（原版：25）。根据当前伤害设置在每场比赛开始时应用。", "ko": "낮은 HP 경고음과 경고 HUD 상태가 발생하는 HP 값을 변경합니다 (vanilla: 25). 현재 피해 설정에 따라 각 매치 시작 시 적용됩니다."
    },
    "Press Generate to build sharable Custom HUD TOML.": {
        "it": "Premi Genera per creare un TOML HUD personalizzato condivisibile.", "nl": "Druk op Genereren om deelbare aangepaste HUD TOML te maken.", "pt": "Pressione Gerar para criar um TOML de HUD personalizado compartilhável.", "ru": "Нажмите «Создать», чтобы собрать общий TOML пользовательского HUD.", "zh": "按“生成”以构建可共享的自定义 HUD TOML。", "ko": "생성을 눌러 공유 가능한 사용자 HUD TOML을 만드세요."
    },
    "Hide Ammo": {
        "it": "Nascondi munizioni", "nl": "Munitie verbergen", "pt": "Ocultar munição", "ru": "Скрыть боеприпасы", "zh": "隐藏弹药", "ko": "탄약 숨기기"
    },
    "Hide Score: Battle": {
        "it": "Nascondi punteggio: Battaglia", "nl": "Score verbergen: Gevecht", "pt": "Ocultar pontuação: Batalha", "ru": "Скрыть счёт: Битва", "zh": "隐藏分数：战斗", "ko": "점수 숨기기: 배틀"
    },
    "Hide Score: Defender": {
        "it": "Nascondi punteggio: Difensore", "nl": "Score verbergen: Verdediger", "pt": "Ocultar pontuação: Defensor", "ru": "Скрыть счёт: Защитник", "zh": "隐藏分数：防守", "ko": "점수 숨기기: 디펜더"
    },
    "Auto Scale Text Cap %": {
        "it": "Limite testo scala automatica %", "nl": "Autom. schaal tekstlimiet %", "pt": "Limite de texto da escala automática %", "ru": "Лимит текста авто-масштаба %", "zh": "自动缩放文字上限 %", "ko": "자동 배율 텍스트 상한 %"
    },
    "Font Size (px)": {
        "it": "Dimensione carattere (px)", "nl": "Lettergrootte (px)", "pt": "Tamanho da fonte (px)", "ru": "Размер шрифта (px)", "zh": "字体大小 (px)", "ko": "글꼴 크기 (px)"
    },
    "Color": {
        "it": "Colore", "nl": "Kleur", "pt": "Cor", "ru": "Цвет", "zh": "颜色", "ko": "색상"
    },
    "Zoom Opacity %": {
        "it": "Opacità zoom %", "nl": "Zoom-dekking %", "pt": "Opacidade do zoom %", "ru": "Непрозрачность зума %", "zh": "缩放不透明度 %", "ko": "줌 불투명도 %"
    },
    "Scope Thickness": {
        "it": "Spessore mirino", "nl": "Vizierdikte", "pt": "Espessura da mira", "ru": "Толщина прицела", "zh": "瞄准镜粗细", "ko": "스코프 두께"
    },
    "Scope Opacity %": {
        "it": "Opacità mirino %", "nl": "Vizierdekking %", "pt": "Opacidade da mira %", "ru": "Непрозрачность прицела %", "zh": "瞄准镜不透明度 %", "ko": "스코프 불투명도 %"
    },
    "Pulse Ring": {
        "it": "Anello pulsante", "nl": "Pulsring", "pt": "Anel pulsante", "ru": "Пульсирующее кольцо", "zh": "脉冲环", "ko": "펄스 링"
    },
    "Custom Scope Dot Color": {
        "it": "Colore punto mirino personalizzato", "nl": "Aangepaste vizierpuntkleur", "pt": "Cor personalizada do ponto da mira", "ru": "Пользовательский цвет точки прицела", "zh": "自定义瞄准点颜色", "ko": "사용자 스코프 점 색상"
    },
    "Glitch2": {
        "it": "Glitch2", "nl": "Glitch2", "pt": "Glitch2", "ru": "Glitch2", "zh": "Glitch2", "ko": "Glitch2"
    },
    "SF Movie": {
        "it": "Film SF", "nl": "SF-film", "pt": "Filme SF", "ru": "SF-фильм", "zh": "SF 影片", "ko": "SF 영화"
    },
    "Zoom": {
        "it": "Zoom", "nl": "Zoom", "pt": "Zoom", "ru": "Зум", "zh": "缩放", "ko": "줌"
    },
    "Outline Thick.": {
        "it": "Spess. contorno", "nl": "Omlijndikte", "pt": "Espess. contorno", "ru": "Толщ. контура", "zh": "轮廓粗细", "ko": "외곽선 두께"
    },
    "Scope Dot Shape": {
        "it": "Forma punto mirino", "nl": "Vizierpuntvorm", "pt": "Forma do ponto da mira", "ru": "Форма точки прицела", "zh": "瞄准点形状", "ko": "스코프 점 모양"
    },
    "Dot Thick.": {
        "it": "Spess. punto", "nl": "Puntdikte", "pt": "Espess. ponto", "ru": "Толщ. точки", "zh": "点粗细", "ko": "점 두께"
    },
    "Outer Lines": {
        "it": "Linee esterne", "nl": "Buitenlijnen", "pt": "Linhas externas", "ru": "Внешние линии", "zh": "外侧线条", "ko": "외곽선"
    },
    "Enable (Override All)": {
        "it": "Abilita (Sostituisci tutto)", "nl": "Inschakelen (Alles overschrijven)", "pt": "Ativar (Substituir tudo)", "ru": "Включить (Переопределить всё)", "zh": "启用（覆盖全部）", "ko": "활성화 (전체 재정의)"
    },
    "Link XY": {
        "it": "Collega XY", "nl": "XY koppelen", "pt": "Vincular XY", "ru": "Связать XY", "zh": "联动 XY", "ko": "XY 연동"
    },
    "Anchor": {
        "it": "Ancoraggio", "nl": "Anker", "pt": "Âncora", "ru": "Привязка", "zh": "锚点", "ko": "기준점"
    },
    "Middle Center": {
        "it": "Centro centrale", "nl": "Midden midden", "pt": "Centro central", "ru": "Центр по центру", "zh": "正中", "ko": "중앙"
    },
    "Mid Left": {
        "it": "Centro sinistra", "nl": "Midden links", "pt": "Centro esquerda", "ru": "Центр слева", "zh": "左中", "ko": "왼쪽 중앙"
    },
    "Bot Right": {
        "it": "In basso a destra", "nl": "Onder rechts", "pt": "Inferior direita", "ru": "Внизу справа", "zh": "右下", "ko": "오른쪽 아래"
    },
    "Bottom": {
        "it": "Inferiore", "nl": "Onder", "pt": "Inferior", "ru": "Низ", "zh": "底部", "ko": "하단"
    },
    "Horizontal": {
        "it": "Orizzontale", "nl": "Horizontaal", "pt": "Horizontal", "ru": "Горизонтально", "zh": "水平", "ko": "가로"
    },
    "Relative to Text": {
        "it": "Relativo al testo", "nl": "Relatief ten opzichte van tekst", "pt": "Relativo ao texto", "ru": "Относительно текста", "zh": "相对于文字", "ko": "텍스트 기준"
    },
    "Text â\u0086\u0092 Gauge": {
        "it": "Testo → Indicatore", "nl": "Tekst → Meter", "pt": "Texto → Medidor", "ru": "Текст → Шкала", "zh": "文字 → 计量条", "ko": "텍스트 → 게이지"
    },
    "Gauge Anchor": {
        "it": "Ancoraggio indicatore", "nl": "Meteranker", "pt": "Âncora do medidor", "ru": "Привязка шкалы", "zh": "计量条锚点", "ko": "게이지 기준점"
    },
    "Text Ofs X": {
        "it": "Ofs testo X", "nl": "Tekst-ofs X", "pt": "Desloc. texto X", "ru": "Смещ. текста X", "zh": "文字偏移 X", "ko": "텍스트 Ofs X"
    },
    "Align X": {
        "it": "Allineamento X", "nl": "Uitlijning X", "pt": "Alinhamento X", "ru": "Выравнивание X", "zh": "对齐 X", "ko": "정렬 X"
    },
    "Height": {
        "it": "Altezza", "nl": "Hoogte", "pt": "Altura", "ru": "Высота", "zh": "高度", "ko": "높이"
    },
    "Pos Y": {
        "it": "Pos Y", "nl": "Pos Y", "pt": "Pos Y", "ru": "Поз Y", "zh": "位置 Y", "ko": "Pos Y"
    },
    "Alternative": {
        "it": "Alternativa", "nl": "Alternatief", "pt": "Alternativa", "ru": "Альтернатива", "zh": "替代", "ko": "대체"
    },
    "Label Ofs X": {
        "it": "Ofs etichetta X", "nl": "Label-ofs X", "pt": "Desloc. rótulo X", "ru": "Смещ. метки X", "zh": "标签偏移 X", "ko": "라벨 Ofs X"
    },
    "Label: Ring Time": {
        "it": "Etichetta: Tempo anello", "nl": "Label: Ringtijd", "pt": "Rótulo: Tempo do anel", "ru": "Метка: Время кольца", "zh": "标签：环时间", "ko": "라벨: 링 시간"
    },
    "Defender": {
        "it": "Difensore", "nl": "Verdediger", "pt": "Defensor", "ru": "Защитник", "zh": "防守方", "ko": "디펜더"
    },
    "Slash": {
        "it": "Fendente", "nl": "Hak", "pt": "Corte", "ru": "Рубящий удар", "zh": "斩击", "ko": "슬래시"
    },
    "Value Color: Overall": {
        "it": "Colore valore: Totale", "nl": "Waardekleur: Totaal", "pt": "Cor do valor: Geral", "ru": "Цвет значения: Общий", "zh": "数值颜色：整体", "ko": "값 색상: 전체"
    },
    "Ordinal": {
        "it": "Ordinale", "nl": "Ordinaal", "pt": "Ordinal", "ru": "Порядковый", "zh": "序数", "ko": "서수"
    },
    "Display Size": {
        "it": "Dimensione visualizzazione", "nl": "Weergavegrootte", "pt": "Tamanho de exibição", "ru": "Размер отображения", "zh": "显示大小", "ko": "표시 크기"
    },
    "Src Radius": {
        "it": "Raggio sorgente", "nl": "Bronradius", "pt": "Raio de origem", "ru": "Исходный радиус", "zh": "源半径", "ko": "소스 반경"
    },
    "Highlight": {
        "it": "Evidenziazione", "nl": "Markering", "pt": "Destaque", "ru": "Подсветка", "zh": "高亮", "ko": "하이라이트"
    },
    "Highlight Padding": {
        "it": "Padding evidenziazione", "nl": "Markering-opvulling", "pt": "Preenchimento do destaque", "ru": "Отступ подсветки", "zh": "高亮内边距", "ko": "하이라이트 패딩"
    },
    "Highlight Offset Bottom": {
        "it": "Offset inferiore evidenziazione", "nl": "Markering-offset onder", "pt": "Deslocamento inferior do destaque", "ru": "Смещение подсветки снизу", "zh": "高亮下偏移", "ko": "하이라이트 하단 오프셋"
    },
    "Hl Opacity": {
        "it": "Opacità HL", "nl": "HL-dekking", "pt": "Opacidade HL", "ru": "Непрозрачность HL", "zh": "HL 不透明度", "ko": "HL 불투명도"
    },
    "Hl Ofs Right": {
        "it": "Ofs HL destra", "nl": "HL-ofs rechts", "pt": "Desloc. HL direita", "ru": "Смещ. HL вправо", "zh": "HL 右偏移", "ko": "HL Ofs 오른쪽"
    },
    "HP Label Color By Value": {
        "it": "Colore etichetta HP per valore", "nl": "HP-labelkleur op waarde", "pt": "Cor do rótulo HP por valor", "ru": "Цвет метки HP по значению", "zh": "HP 标签颜色（按数值）", "ko": "HP 라벨 색상 (값별)"
    },
    "Ammo": {
        "it": "Munizioni", "nl": "Munitie", "pt": "Munição", "ru": "Боеприпасы", "zh": "弹药", "ko": "탄약"
    },
    "Ammo Gauge Color By Value": {
        "it": "Colore indicatore munizioni per valore", "nl": "Munitiemeterkleur op waarde", "pt": "Cor do medidor de munição por valor", "ru": "Цвет шкалы боеприпасов по значению", "zh": "弹药条颜色（按数值）", "ko": "탄약 게이지 색상 (값별)"
    },
    "Weapon Icon Color Overlay": {
        "it": "Sovrapposizione colore icona arma", "nl": "Kleuroverlay wapenpictogram", "pt": "Sobreposição de cor do ícone de arma", "ru": "Цветовая накладка иконки оружия", "zh": "武器图标颜色叠加", "ko": "무기 아이콘 색상 오버레이"
    },
    "Match Status": {
        "it": "Stato partita", "nl": "Wedstrijdstatus", "pt": "Estado da partida", "ru": "Состояние матча", "zh": "比赛状态", "ko": "매치 상태"
    },
    "Rank / Time": {
        "it": "Grado / Tempo", "nl": "Rang / Tijd", "pt": "Classificação / Tempo", "ru": "Ранг / Время", "zh": "排名 / 时间", "ko": "순위 / 시간"
    },
    "Time Limit": {
        "it": "Limite di tempo", "nl": "Tijdslimiet", "pt": "Limite de tempo", "ru": "Лимит времени", "zh": "时间限制", "ko": "시간 제한"
    },
    "Bomb Icon": {
        "it": "Icona bomba", "nl": "Bompictogram", "pt": "Ícone de bomba", "ru": "Иконка бомбы", "zh": "炸弹图标", "ko": "폭탄 아이콘"
    },
    "Frame Outline": {
        "it": "Contorno cornice", "nl": "Frame-omlijning", "pt": "Contorno da moldura", "ru": "Контур рамки", "zh": "边框轮廓", "ko": "프레임 외곽선"
    },
    "WPN": {
        "it": "ARM", "nl": "WPN", "pt": "ARM", "ru": "ОРУ", "zh": "武器", "ko": "무기"
    },
    "Global Color": {
        "it": "Colore globale", "nl": "Globale kleur", "pt": "Cor global", "ru": "Глобальный цвет", "zh": "全局颜色", "ko": "전역 색상"
    },
    "Lost Lives": {
        "it": "Vite perse", "nl": "Verloren levens", "pt": "Vidas perdidas", "ru": "Потерянные жизни", "zh": "失去生命", "ko": "잃은 목숨"
    },
    "Acquiring Node": {
        "it": "Acquisizione nodo", "nl": "Knooppunt veroveren", "pt": "Capturando nó", "ru": "Захват узла", "zh": "占领节点中", "ko": "노드 점령 중"
    },
    "Octo Missing": {
        "it": "Octolito mancante", "nl": "Octoliet ontbreekt", "pt": "Octólito ausente", "ru": "Октолит отсутствует", "zh": "八边形缺失", "ko": "옥토리스 미보유"
    },
    "Applied once on settings close to currently displayed messages (flags=0x02).\nNew messages use the 'Kill / Death' literal color above.": {
        "it": "Applicato una volta alla chiusura delle impostazioni ai messaggi attualmente visualizzati (flags=0x02).\nI nuovi messaggi usano il colore letterale «Kill / Death» sopra.", "nl": "Eenmalig toegepast bij sluiten van instellingen op momenteel weergegeven berichten (flags=0x02).\nNieuwe berichten gebruiken de letterlijke kleur «Kill / Death» hierboven.", "pt": "Aplicado uma vez ao fechar as configurações às mensagens exibidas (flags=0x02).\nNovas mensagens usam a cor literal «Kill / Death» acima.", "ru": "Применяется один раз при закрытии настроек к текущим сообщениям (flags=0x02).\nНовые сообщения используют буквальный цвет «Kill / Death» выше.", "zh": "关闭设置时对当前显示的消息应用一次 (flags=0x02)。\n新消息使用上方的「Kill / Death」字面颜色。", "ko": "설정을 닫을 때 현재 표시된 메시지에 한 번 적용됩니다 (flags=0x02).\n새 메시지는 위의 «Kill / Death» 리터럴 색상을 사용합니다."
    },
    "Color  (acquiring node / node stolen H211)": {
        "it": "Colore  (acquisizione nodo / nodo rubato H211)", "nl": "Kleur  (knooppunt veroveren / knooppunt gestolen H211)", "pt": "Cor  (capturando nó / nó roubado H211)", "ru": "Цвет  (захват узла / узел украден H211)", "zh": "颜色  (占领节点 / 节点被夺 H211)", "ko": "색상  (노드 점령 중 / 노드 탈취 H211)"
    },
    "Yellow Green": {
        "it": "Giallo-verde", "nl": "Geelgroen", "pt": "Verde-amarelado", "ru": "Жёлто-зелёный", "zh": "黄绿色", "ko": "황록색"
    },
    "Pink": {
        "it": "Rosa", "nl": "Roze", "pt": "Rosa", "ru": "Розовый", "zh": "粉色", "ko": "분홍"
    },
    "Kanden Hud": {
        "it": "HUD Kanden", "nl": "Kanden-HUD", "pt": "HUD de Kanden", "ru": "HUD Kanden", "zh": "Kanden HUD", "ko": "Kanden HUD"
    },
    "Noxus Hud Outline": {
        "it": "Contorno HUD Noxus", "nl": "Noxus-HUD-omlijning", "pt": "Contorno do HUD de Noxus", "ru": "Контур HUD Noxus", "zh": "Noxus HUD 轮廓", "ko": "Noxus HUD 외곽선"
    },
    "Avium Purple": {
        "it": "Viola Avium", "nl": "Avium-paars", "pt": "Roxo Avium", "ru": "Фиолетовый Avium", "zh": "Avium 紫", "ko": "Avium 보라"
    },
    "VoltDriver": {
        "it": "VoltDriver", "nl": "VoltDriver", "pt": "VoltDriver", "ru": "VoltDriver", "zh": "VoltDriver", "ko": "VoltDriver"
    },
    "Judicator": {
        "it": "Judicator", "nl": "Judicator", "pt": "Judicator", "ru": "Judicator", "zh": "Judicator", "ko": "Judicator"
    },
    "PB Color": {
        "it": "Colore PB", "nl": "PB-kleur", "pt": "Cor PB", "ru": "Цвет PB", "zh": "PB 颜色", "ko": "PB 색상"
    },
    "JUD Color": {
        "it": "Colore JUD", "nl": "JUD-kleur", "pt": "Cor JUD", "ru": "Цвет JUD", "zh": "JUD 颜色", "ko": "JUD 색상"
    },
    "Extra Light": {
        "it": "Extra Light", "nl": "Extra Light", "pt": "Extra Light", "ru": "Extra Light", "zh": "Extra Light", "ko": "Extra Light"
    },
    "Bold": {
        "it": "Grassetto", "nl": "Vet", "pt": "Negrito", "ru": "Жирный", "zh": "粗体", "ko": "굵게"
    },
    "Threshold 2 (%)": {
        "it": "Soglia 2 (%)", "nl": "Drempel 2 (%)", "pt": "Limite 2 (%)", "ru": "Порог 2 (%)", "zh": "阈值 2 (%)", "ko": "임계값 2 (%)"
    },
    "Color 1": {
        "it": "Colore 1", "nl": "Kleur 1", "pt": "Cor 1", "ru": "Цвет 1", "zh": "颜色 1", "ko": "색상 1"
    },
    "Color 6": {
        "it": "Colore 6", "nl": "Kleur 6", "pt": "Cor 6", "ru": "Цвет 6", "zh": "颜色 6", "ko": "색상 6"
    },
    "Custom HUD code applied to the dialog.": {
        "it": "Codice HUD personalizzato applicato alla finestra di dialogo.", "nl": "Aangepaste HUD-code toegepast op het dialoogvenster.", "pt": "Código de HUD personalizado aplicado ao diálogo.", "ru": "Код пользовательского HUD применён к диалогу.", "zh": "自定义 HUD 代码已应用到对话框。", "ko": "사용자 HUD 코드가 대화 상자에 적용되었습니다."
    },
    "Use New Method for Biped Fire": {
        "it": "Usa nuovo metodo per fuoco Biped", "nl": "Nieuwe methode voor Biped-vuur gebruiken", "pt": "Usar novo método para disparo Biped", "ru": "Использовать новый метод для огня Biped", "zh": "使用 Biped 射击的新方法", "ko": "Biped 사격에 새 방식 사용"
    },
    "Video settings - melonDS": {
        "it": "Impostazioni video - melonDS", "nl": "Video-instellingen - melonDS", "pt": "Configurações de vídeo - melonDS", "ru": "Настройки видео - melonDS", "zh": "视频设置 - melonDS", "ko": "비디오 설정 - melonDS"
    },
    "General": {
        "it": "Generale", "nl": "Algemeen", "pt": "Geral", "ru": "Общие", "zh": "常规", "ko": "일반"
    },
    "DLDI": {
        "it": "DLDI", "nl": "DLDI", "pt": "DLDI", "ru": "DLDI", "zh": "DLDI", "ko": "DLDI"
    },
    "DS ARM9 BIOS:": {
        "it": "BIOS ARM9 DS:", "nl": "DS ARM9 BIOS:", "pt": "BIOS ARM9 do DS:", "ru": "BIOS ARM9 DS:", "zh": "DS ARM9 BIOS：", "ko": "DS ARM9 BIOS:"
    },
    "DSi firmware:": {
        "it": "Firmware DSi:", "nl": "DSi-firmware:", "pt": "Firmware DSi:", "ru": "Прошивка DSi:", "zh": "DSi 固件：", "ko": "DSi 펌웨어:"
    },
    "Image size:": {
        "it": "Dimensione immagine:", "nl": "Afbeeldingsgrootte:", "pt": "Tamanho da imagem:", "ru": "Размер изображения:", "zh": "图像大小：", "ko": "이미지 크기:"
    },
    "Branch optimisations": {
        "it": "Ottimizzazioni dei rami", "nl": "Vertakkingsoptimalisaties", "pt": "Otimizações de ramificação", "ru": "Оптимизация ветвлений", "zh": "分支优化", "ko": "분기 최적화"
    },
    "ARM9 port": {
        "it": "Porta ARM9", "nl": "ARM9-poort", "pt": "Porta ARM9", "ru": "Порт ARM9", "zh": "ARM9 端口", "ko": "ARM9 포트"
    },
    "DS": {
        "it": "DS", "nl": "DS", "pt": "DS", "ru": "DS", "zh": "DS", "ko": "DS"
    },
    "Software renderer": {
        "it": "Renderer software", "nl": "Softwarerenderer", "pt": "Renderizador por software", "ru": "Программный рендерер", "zh": "软件渲染器", "ko": "소프트웨어 렌더러"
    },
    "OpenGL display": {
        "it": "Visualizzazione OpenGL", "nl": "OpenGL-weergave", "pt": "Exibição OpenGL", "ru": "Отображение OpenGL", "zh": "OpenGL 显示", "ko": "OpenGL 표시"
    },
    "Use high resolution coordinates": {
        "it": "Usa coordinate ad alta risoluzione", "nl": "Hoge-resolutiecoördinaten gebruiken", "pt": "Usar coordenadas de alta resolução", "ru": "Использовать координаты высокого разрешения", "zh": "使用高分辨率坐标", "ko": "고해상도 좌표 사용"
    },
    "Preview": {
        "it": "Anteprima", "nl": "Voorbeeld", "pt": "Pré-visualização", "ru": "Предпросмотр", "zh": "预览", "ko": "미리보기"
    },
    "DSi outer camera": {
        "it": "Fotocamera esterna DSi", "nl": "DSi-buitencamera", "pt": "Câmera externa DSi", "ru": "Внешняя камера DSi", "zh": "DSi 外置摄像头", "ko": "DSi 외부 카메라"
    },
    "Bit depth:": {
        "it": "Profondità bit:", "nl": "Bitdiepte:", "pt": "Profundidade de bits:", "ru": "Разрядность:", "zh": "位深度：", "ko": "비트 깊이:"
    },
    "Cosine": {
        "it": "Coseno", "nl": "Cosinus", "pt": "Cosseno", "ru": "Косинус", "zh": "余弦", "ko": "코사인"
    },
    "16-bit": {
        "it": "16-bit", "nl": "16-bit", "pt": "16-bit", "ru": "16-bit", "zh": "16-bit", "ko": "16-bit"
    },
    "(no joysticks available)": {
        "it": "(nessun joystick disponibile)", "nl": "(geen joysticks beschikbaar)", "pt": "(nenhum joystick disponível)", "ru": "(джойстики недоступны)", "zh": "（无可用摇杆）", "ko": "(사용 가능한 조이스틱 없음)"
    },
    "Toggle fast forward": {
        "it": "Attiva/disattiva avanzamento rapido", "nl": "Snel vooruit schakelen", "pt": "Alternar avanço rápido", "ru": "Переключить ускорение", "zh": "切换快进", "ko": "빨리 감기 전환"
    },
    "Close/open lid": {
        "it": "Chiudi/apri coperchio", "nl": "Deksel sluiten/openen", "pt": "Fechar/abrir tampa", "ru": "Закрыть/открыть крышку", "zh": "合上/打开盖子", "ko": "뚜껑 닫기/열기"
    },
    "DSi Volume up": {
        "it": "Volume DSi +", "nl": "DSi-volume +", "pt": "Aumentar volume DSi", "ru": "Громкость DSi +", "zh": "DSi 音量加", "ko": "DSi 볼륨 업"
    },
    "Instance 1 only": {
        "it": "Solo istanza 1", "nl": "Alleen instantie 1", "pt": "Somente instância 1", "ru": "Только экземпляр 1", "zh": "仅实例 1", "ko": "인스턴스 1만"
    },
    "Wifi settings - melonDS": {
        "it": "Impostazioni Wi-Fi - melonDS", "nl": "Wi-Fi-instellingen - melonDS", "pt": "Configurações Wi-Fi - melonDS", "ru": "Настройки Wi-Fi - melonDS", "zh": "Wi-Fi 设置 - melonDS", "ko": "Wi-Fi 설정 - melonDS"
    },
    "MAC address:": {
        "it": "Indirizzo MAC:", "nl": "MAC-adres:", "pt": "Endereço MAC:", "ru": "MAC-адрес:", "zh": "MAC 地址：", "ko": "MAC 주소:"
    },
    "Override settings from external firmware": {
        "it": "Sostituisci impostazioni da firmware esterno", "nl": "Instellingen van externe firmware overschrijven", "pt": "Substituir configurações do firmware externo", "ru": "Переопределить настройки из внешней прошивки", "zh": "覆盖外部固件设置", "ko": "외부 펌웨어 설정 재정의"
    },
    "Language:": {
        "it": "Lingua:", "nl": "Taal:", "pt": "Idioma:", "ru": "Язык:", "zh": "语言：", "ko": "언어:"
    },
    "User interface": {
        "it": "Interfaccia utente", "nl": "Gebruikersinterface", "pt": "Interface do usuário", "ru": "Пользовательский интерфейс", "zh": "用户界面", "ko": "사용자 인터페이스"
    },
    "seconds": {
        "it": "secondi", "nl": "seconden", "pt": "segundos", "ru": "секунды", "zh": "秒", "ko": "초"
    },
    "Fast-Forward": {
        "it": "Avanzamento rapido", "nl": "Snel vooruit", "pt": "Avanço rápido", "ru": "Ускорение", "zh": "快进", "ko": "빨리 감기"
    },
    "1/4": {
        "it": "1/4", "nl": "1/4", "pt": "1/4", "ru": "1/4", "zh": "1/4", "ko": "1/4"
    },
    "Path settings - melonDS": {
        "it": "Impostazioni percorsi - melonDS", "nl": "Padinstellingen - melonDS", "pt": "Configurações de caminhos - melonDS", "ru": "Настройки путей - melonDS", "zh": "路径设置 - melonDS", "ko": "경로 설정 - melonDS"
    },
    "Cheat code editor - melonDS": {
        "it": "Editor codici trucco - melonDS", "nl": "Cheatcode-editor - melonDS", "pt": "Editor de códigos de trapaça - melonDS", "ru": "Редактор чит-кодов - melonDS", "zh": "金手指代码编辑器 - melonDS", "ko": "치트 코드 편집기 - melonDS"
    },
    "Cancel edit": {
        "it": "Annulla modifica", "nl": "Bewerking annuleren", "pt": "Cancelar edição", "ru": "Отменить редактирование", "zh": "取消编辑", "ko": "편집 취소"
    },
    "Enabled": {
        "it": "Abilitato", "nl": "Ingeschakeld", "pt": "Ativado", "ru": "Включено", "zh": "已启用", "ko": "활성화됨"
    },
    "Almost Empty": {
        "it": "Quasi vuoto", "nl": "Bijna leeg", "pt": "Quase vazio", "ru": "Почти пусто", "zh": "几乎为空", "ko": "거의 비어 있음"
    },
    "Okay": {
        "it": "Accettabile", "nl": "In orde", "pt": "Aceitável", "ru": "Нормально", "zh": "良好", "ko": "양호"
    },
    "Change to:": {
        "it": "Cambia in:", "nl": "Wijzigen in:", "pt": "Alterar para:", "ru": "Изменить на:", "zh": "更改为：", "ko": "변경:"
    },
    "English title:": {
        "it": "Titolo inglese:", "nl": "Engelse titel:", "pt": "Título em inglês:", "ru": "Английское название:", "zh": "英文标题：", "ko": "영어 제목:"
    },
    "Chinese title:": {
        "it": "Titolo cinese:", "nl": "Chinese titel:", "pt": "Título em chinês:", "ru": "Китайское название:", "zh": "中文标题：", "ko": "중국어 제목:"
    },
    "ARM9 RAM address:": {
        "it": "Indirizzo RAM ARM9:", "nl": "ARM9 RAM-adres:", "pt": "Endereço RAM ARM9:", "ru": "Адрес RAM ARM9:", "zh": "ARM9 RAM 地址：", "ko": "ARM9 RAM 주소:"
    },
    "ARM7 size:": {
        "it": "Dimensione ARM7:", "nl": "ARM7-grootte:", "pt": "Tamanho ARM7:", "ru": "Размер ARM7:", "zh": "ARM7 大小：", "ko": "ARM7 크기:"
    },
    "FAT size:": {
        "it": "Dimensione FAT:", "nl": "FAT-grootte:", "pt": "Tamanho FAT:", "ru": "Размер FAT:", "zh": "FAT 大小：", "ko": "FAT 크기:"
    },
    "Card size:": {
        "it": "Dimensione scheda:", "nl": "Kaartgrootte:", "pt": "Tamanho do cartão:", "ru": "Размер карты:", "zh": "卡大小：", "ko": "카드 크기:"
    },
    "Value:": {
        "it": "Valore:", "nl": "Waarde:", "pt": "Valor:", "ru": "Значение:", "zh": "值：", "ko": "값:"
    },
    "Value": {
        "it": "Valore", "nl": "Waarde", "pt": "Valor", "ru": "Значение", "zh": "值", "ko": "값"
    },
    "Player name:": {
        "it": "Nome giocatore:", "nl": "Spelersnaam:", "pt": "Nome do jogador:", "ru": "Имя игрока:", "zh": "玩家名称：", "ko": "플레이어 이름:"
    },
    "Host address:": {
        "it": "Indirizzo host:", "nl": "Hostadres:", "pt": "Endereço do host:", "ru": "Адрес хоста:", "zh": "主机地址：", "ko": "호스트 주소:"
    },
    "Idle": {
        "it": "Inattivo", "nl": "Inactief", "pt": "Inativo", "ru": "Ожидание", "zh": "空闲", "ko": "대기 중"
    },
    "Failed to connect to the host %0.": {
        "it": "Connessione all'host %0 non riuscita.", "nl": "Verbinding met host %0 mislukt.", "pt": "Falha ao conectar ao host %0.", "ru": "Не удалось подключиться к хосту %0.", "zh": "无法连接到主机 %0。", "ko": "호스트 %0에 연결하지 못했습니다."
    },
    "Close": {
        "it": "Chiudi", "nl": "Sluiten", "pt": "Fechar", "ru": "Закрыть", "zh": "关闭", "ko": "닫기"
    },
    "Orange": {
        "it": "Arancione", "nl": "Oranje", "pt": "Laranja", "ru": "Оранжевый", "zh": "橙色", "ko": "주황"
    },
    "Turquoise": {
        "it": "Turchese", "nl": "Turkoois", "pt": "Turquesa", "ru": "Бирюзовый", "zh": "青绿色", "ko": "청록"
    },
    "Light purple": {
        "it": "Viola chiaro", "nl": "Lichtpaars", "pt": "Roxo claro", "ru": "Светло-фиолетовый", "zh": "浅紫色", "ko": "연보라"
    },
    "Spanish": {
        "it": "Spagnolo", "nl": "Spaans", "pt": "Espanhol", "ru": "Испанский", "zh": "西班牙语", "ko": "스페인어"
    },
    "April": {
        "it": "Aprile", "nl": "April", "pt": "Abril", "ru": "Апрель", "zh": "4 月", "ko": "4월"
    },
    "September": {
        "it": "Settembre", "nl": "September", "pt": "Setembro", "ru": "Сентябрь", "zh": "9 月", "ko": "9월"
    },
}

def main():
    src = Path(__file__).parent / "loc_chunk_0_translated.json"
    dst = Path(__file__).parent / "loc_chunk_0_extended.json"

    with src.open(encoding="utf-8") as f:
        data = json.load(f)

    out = []
    missing = []
    for entry in data:
        en = entry["en"]
        if en not in EXTRA:
            missing.append(en)
            continue
        row = {
            "en": en,
            "ja": entry["ja"],
            "de": entry["de"],
            "es": entry["es"],
            "fr": entry["fr"],
            **EXTRA[en],
        }
        if "section" in entry:
            row["section"] = entry["section"]
        out.append(row)

    if missing:
        raise SystemExit(f"Missing translations for {len(missing)} entries: {missing!r}")

    with dst.open("w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
        f.write("\n")

    print(f"Wrote {len(out)} entries to {dst}")

if __name__ == "__main__":
    main()
