#!/usr/bin/env python3
"""Build loc_chunk_3_extended.json with it, nl, pt, ru, zh, ko translations."""

import json
from pathlib import Path

# Hand-reviewed translations keyed by English source string.
EXTRA = {
    "Save": {
        "it": "Salva", "nl": "Opslaan", "pt": "Guardar",
        "ru": "Сохранить", "zh": "保存", "ko": "저장",
    },
    "Copy Output to Input": {
        "it": "Copia uscita in ingresso", "nl": "Uitvoer naar invoer kopiëren",
        "pt": "Copiar saída para entrada", "ru": "Копировать выход во вход",
        "zh": "将输出复制到输入", "ko": "출력을 입력으로 복사",
    },
    "Pick Color": {
        "it": "Scegli colore", "nl": "Kleur kiezen", "pt": "Escolher cor",
        "ru": "Выбрать цвет", "zh": "选取颜色", "ko": "색상 선택",
    },
    "OVR": {
        "it": "Gen.", "nl": "Alg.", "pt": "Geral",
        "ru": "Общ.", "zh": "总体", "ko": "전체",
    },
    "Zoomed (Scope)": {
        "it": "Zoom (mirino)", "nl": "Ingezoomd (vizier)", "pt": "Com zoom (mira)",
        "ru": "С увеличением (прицел)", "zh": "缩放时（准星）", "ko": "확대 시 (스코프)",
    },
    "Custom": {
        "it": "Personalizzato", "nl": "Aangepast", "pt": "Personalizado",
        "ru": "Пользовательский", "zh": "自定义", "ko": "사용자 지정",
    },
    "Menu Language": {
        "it": "Lingua del menu", "nl": "Menutaal", "pt": "Idioma do menu",
        "ru": "Язык меню", "zh": "菜单语言", "ko": "메뉴 언어",
    },
    "File->Open ROM...": {
        "it": "File → Apri ROM...", "nl": "Bestand → ROM openen...",
        "pt": "Arquivo → Abrir ROM...", "ru": "Файл → Открыть ROM...",
        "zh": "文件 → 打开 ROM...", "ko": "파일 → ROM 열기...",
    },
    "GBA slot": {
        "it": "Slot GBA", "nl": "GBA-slot", "pt": "Slot GBA",
        "ru": "Слот GBA", "zh": "GBA 插槽", "ko": "GBA 슬롯",
    },
    "Import savefile": {
        "it": "Importa salvataggio", "nl": "Savegame importeren", "pt": "Importar save",
        "ru": "Импортировать сохранение", "zh": "导入存档", "ko": "세이브 파일 가져오기",
    },
    "Open melonDS directory": {
        "it": "Apri cartella melonDS", "nl": "melonDS-map openen", "pt": "Abrir pasta do melonDS",
        "ru": "Открыть папку melonDS", "zh": "打开 melonDS 文件夹", "ko": "melonDS 폴더 열기",
    },
    "Frame step": {
        "it": "Avanzamento fotogramma", "nl": "Frame-stap", "pt": "Avanço de quadro",
        "ru": "Пошаговый кадр", "zh": "逐帧前进", "ko": "프레임 단위 진행",
    },
    "ROM info": {
        "it": "Info ROM", "nl": "ROM-info", "pt": "Info da ROM",
        "ru": "Сведения о ROM", "zh": "ROM 信息", "ko": "ROM 정보",
    },
    "Host LAN game": {
        "it": "Ospita partita LAN", "nl": "LAN-spel hosten", "pt": "Hospedar jogo LAN",
        "ru": "Создать LAN-игру", "zh": "托管 LAN 游戏", "ko": "LAN 게임 호스트",
    },
    "Screen gap": {
        "it": "Spazio tra schermi", "nl": "Schermafstand", "pt": "Espaço entre telas",
        "ru": "Зазор между экранами", "zh": "屏幕间距", "ko": "화면 간격",
    },
    "Hybrid": {
        "it": "Ibrido", "nl": "Hybride", "pt": "Híbrido",
        "ru": "Гибридный", "zh": "混合", "ko": "하이브리드",
    },
    "Emphasize bottom": {
        "it": "Enfatizza schermo inferiore", "nl": "Onderste scherm benadrukken",
        "pt": "Enfatizar tela inferior", "ru": "Выделить нижний экран",
        "zh": "强调下屏", "ko": "하단 화면 강조",
    },
    "Open new window": {
        "it": "Apri nuova finestra", "nl": "Nieuw venster openen", "pt": "Abrir nova janela",
        "ru": "Открыть новое окно", "zh": "打开新窗口", "ko": "새 창 열기",
    },
    "Preferences...": {
        "it": "Preferenze...", "nl": "Voorkeuren...", "pt": "Preferências...",
        "ru": "Настройки...", "zh": "偏好设置...", "ko": "환경 설정...",
    },
    "Multiplayer settings": {
        "it": "Impostazioni multigiocatore", "nl": "Multiplayer-instellingen",
        "pt": "Configurações multijogador", "ru": "Настройки мультиплеера",
        "zh": "多人游戏设置", "ko": "멀티플레이 설정",
    },
    "Limit framerate": {
        "it": "Limita framerate", "nl": "Framerate beperken", "pt": "Limitar FPS",
        "ru": "Ограничить частоту кадров", "zh": "限制帧率", "ko": "프레임레이트 제한",
    },
    "MelonPrime settings": {
        "it": "Impostazioni MelonPrime", "nl": "MelonPrime-instellingen",
        "pt": "Configurações do MelonPrime", "ru": "Настройки MelonPrime",
        "zh": "MelonPrime 设置", "ko": "MelonPrime 설정",
    },
    "About...": {
        "it": "Informazioni...", "nl": "Over...", "pt": "Sobre...",
        "ru": "О программе...", "zh": "关于...", "ko": "정보...",
    },
    "Custom HUD": {
        "it": "HUD personalizzato", "nl": "Aangepaste HUD", "pt": "HUD personalizado",
        "ru": "Пользовательский HUD", "zh": "自定义 HUD", "ko": "사용자 지정 HUD",
    },
    "INPUT METHOD": {
        "it": "METODO DI INPUT", "nl": "INVOERMETHODE", "pt": "MÉTODO DE ENTRADA",
        "ru": "МЕТОД ВВОДА", "zh": "输入方式", "ko": "입력 방식",
    },
    "LOW HP WARNING": {
        "it": "AVVISO HP BASSO", "nl": "WAARSCHUWING LAGE HP", "pt": "AVISO DE HP BAIXO",
        "ru": "ПРЕДУПРЕЖДЕНИЕ О НИЗКОМ HP", "zh": "低 HP 警告", "ko": "낮은 HP 경고",
    },
    "Power-Up Pickup Effects": {
        "it": "Effetti raccolta potenziamenti", "nl": "Effecten bij oppakken power-ups",
        "pt": "Efeitos ao recolher power-ups", "ru": "Эффекты подбора усилений",
        "zh": "强化道具拾取效果", "ko": "파워업 획득 효과",
    },
    "DEVELOPER ONLY": {
        "it": "SOLO SVILUPPATORI", "nl": "ALLEEN ONTWIKKELAARS", "pt": "SOMENTE DESENVOLVEDORES",
        "ru": "ТОЛЬКО ДЛЯ РАЗРАБОТЧИКОВ", "zh": "仅限开发者", "ko": "개발자 전용",
    },
    "CROSSHAIR": {
        "it": "RETICOLO", "nl": "RICHTKRUIS", "pt": "MIRA",
        "ru": "ПРИЦЕЛ", "zh": "准星", "ko": "조준선",
    },
    "Keyboard mappings": {
        "it": "Mappatura tastiera", "nl": "Toetsenbordtoewijzingen", "pt": "Mapeamento do teclado",
        "ru": "Назначения клавиатуры", "zh": "键盘映射", "ko": "키보드 매핑",
    },
    "[Metroid] (A) Move Left": {
        "it": "[Metroid] (A) Muovi a sinistra", "nl": "[Metroid] (A) Naar links",
        "pt": "[Metroid] (A) Mover para a esquerda", "ru": "[Metroid] (A) Влево",
        "zh": "[Metroid] (A) 向左移动", "ko": "[Metroid] (A) 왼쪽 이동",
    },
    "[Metroid] (Space) Jump": {
        "it": "[Metroid] (Spazio) Salta", "nl": "[Metroid] (Spatie) Springen",
        "pt": "[Metroid] (Espaço) Pular", "ru": "[Metroid] (Пробел) Прыжок",
        "zh": "[Metroid] (空格) 跳跃", "ko": "[Metroid] (스페이스) 점프",
    },
    "[Metroid] (1) Weapon 1. ShockCoil": {
        "it": "[Metroid] (1) Arma 1: ShockCoil", "nl": "[Metroid] (1) Wapen 1: ShockCoil",
        "pt": "[Metroid] (1) Arma 1: ShockCoil", "ru": "[Metroid] (1) Оружие 1: ShockCoil",
        "zh": "[Metroid] (1) 武器 1：ShockCoil", "ko": "[Metroid] (1) 무기 1: ShockCoil",
    },
    "[Metroid] (6) Weapon 6. VoltDriver": {
        "it": "[Metroid] (6) Arma 6: VoltDriver", "nl": "[Metroid] (6) Wapen 6: VoltDriver",
        "pt": "[Metroid] (6) Arma 6: VoltDriver", "ru": "[Metroid] (6) Оружие 6: VoltDriver",
        "zh": "[Metroid] (6) 武器 6：VoltDriver", "ko": "[Metroid] (6) 무기 6: VoltDriver",
    },
    "[Metroid] (J) Next Weapon in the sorted order": {
        "it": "[Metroid] (J) Arma successiva", "nl": "[Metroid] (J) Volgend wapen",
        "pt": "[Metroid] (J) Próxima arma", "ru": "[Metroid] (J) Следующее оружие",
        "zh": "[Metroid] (J) 下一武器", "ko": "[Metroid] (J) 다음 무기",
    },
    "[Metroid] (F) UI Ok": {
        "it": "[Metroid] (F) Conferma UI", "nl": "[Metroid] (F) UI bevestigen",
        "pt": "[Metroid] (F) Confirmar UI", "ru": "[Metroid] (F) Подтвердить UI",
        "zh": "[Metroid] (F) UI 确定", "ko": "[Metroid] (F) UI 확인",
    },
    "Aim sensitivity (default: 63)": {
        "it": "Sensibilità mira (predefinito: 63)", "nl": "Richtgevoeligheid (standaard: 63)",
        "pt": "Sensibilidade de mira (padrão: 63)", "ru": "Чувствительность прицеливания (по умолчанию: 63)",
        "zh": "瞄准灵敏度（默认：63）", "ko": "조준 감도 (기본값: 63)",
    },
    "Instant Aim Follow (Developer Only)": {
        "it": "Seguito mira istantaneo (solo sviluppatori)", "nl": "Directe richtvolging (alleen ontwikkelaars)",
        "pt": "Seguimento de mira instantâneo (somente desenvolvedores)",
        "ru": "Мгновенное следование прицела (только для разработчиков)",
        "zh": "即时瞄准跟随（仅限开发者）", "ko": "즉시 조준 추적 (개발자 전용)",
    },
    "Set MPH audio settings to headphones.(recommended) (Change a setting in MPH and save to update the save data. Uncheck this option after saving)": {
        "it": "Imposta l'audio MPH su cuffie (consigliato). (Modifica un'impostazione in MPH e salva per aggiornare i dati di salvataggio. Deseleziona dopo il salvataggio)",
        "nl": "MPH-audio instellen op koptelefoon (aanbevolen). (Wijzig een instelling in MPH en sla op om de save bij te werken. Vink uit na het opslaan)",
        "pt": "Definir áudio do MPH para fones de ouvido (recomendado). (Altere uma configuração no MPH e salve para atualizar os dados. Desmarque após salvar)",
        "ru": "Установить аудио MPH на наушники (рекомендуется). (Измените настройку в MPH и сохраните, чтобы обновить данные. Снимите флажок после сохранения)",
        "zh": "将 MPH 音频设置为耳机（推荐）。（在 MPH 中更改设置并保存以更新存档。保存后取消勾选此选项）",
        "ko": "MPH 오디오 설정을 헤드폰으로 설정 (권장). (MPH에서 설정을 변경하고 저장하여 세이브 데이터를 업데이트하세요. 저장 후 이 옵션을 해제하세요)",
    },
    "Enable Aim Sub-pixel Accumulator (Carry fractional mouse movement across frames. Enable for smoother low-sensitivity aiming)": {
        "it": "Abilita accumulatore sub-pixel mira (Trasferisce il movimento frazionale del mouse tra i fotogrammi. Per una mira più fluida a bassa sensibilità)",
        "nl": "Subpixel-richtaccumulator inschakelen (Draagt fractionele muisbeweging over naar volgende frames. Voor vloeiender richten bij lage gevoeligheid)",
        "pt": "Ativar acumulador subpixel de mira (Mantém movimento fracionário do mouse entre quadros. Ative para mira mais suave com baixa sensibilidade)",
        "ru": "Включить субпиксельный аккумулятор прицела (Переносит дробное движение мыши между кадрами. Для более плавного прицеливания при низкой чувствительности)",
        "zh": "启用瞄准亚像素累加器（将小数鼠标移动量传递到下一帧。低灵敏度瞄准时更流畅）",
        "ko": "조준 서브픽셀 누적기 활성화 (프레임 간 마우스 이동의 소수 부분을 이월합니다. 낮은 감도 조준을 더 부드럽게 합니다)",
    },
    "Enable Native Aim Delta Hook (PostFold Write)": {
        "it": "Abilita hook delta mira nativo (scrittura PostFold)", "nl": "Native aim-delta-hook inschakelen (PostFold-schrijven)",
        "pt": "Ativar hook nativo de delta de mira (escrita PostFold)", "ru": "Включить нативный хук дельты прицела (запись PostFold)",
        "zh": "启用原生瞄准增量 Hook（PostFold 写入）", "ko": "네이티브 조준 델타 Hook 활성화 (PostFold 쓰기)",
    },
    "Screen Sync Mode: Off = no sync call, glFinish = wait for GL commands to complete": {
        "it": "Modalità sync schermo: Off = nessuna chiamata sync, glFinish = attende il completamento dei comandi GL",
        "nl": "Scherm-syncmodus: Uit = geen sync-aanroep, glFinish = wacht tot GL-opdrachten voltooid zijn",
        "pt": "Modo de sincronização de tela: Desligado = sem chamada sync, glFinish = aguardar conclusão dos comandos GL",
        "ru": "Режим синхронизации экрана: Выкл = без вызова sync, glFinish = ожидание завершения GL-команд",
        "zh": "屏幕同步模式：关 = 无 sync 调用，glFinish = 等待 GL 命令完成",
        "ko": "화면 동기화 모드: OFF = sync 호출 없음, glFinish = GL 명령 완료 대기",
    },
    "Auto (match Aspect Ratio)": {
        "it": "Auto (adatta al rapporto d'aspetto)", "nl": "Auto (beeldverhouding aanpassen)",
        "pt": "Automático (ajustar proporção)", "ru": "Авто (по соотношению сторон)",
        "zh": "自动（匹配宽高比）", "ko": "자동 (화면 비율 맞춤)",
    },
    "Use DS Firmware Language (EU-style Auto Patch)": {
        "it": "Usa lingua firmware DS (patch auto stile UE)", "nl": "DS-firmwaretaal gebruiken (EU auto-patch)",
        "pt": "Usar idioma do firmware DS (patch automático estilo UE)", "ru": "Использовать язык прошивки DS (автопатч в стиле EU)",
        "zh": "使用 DS 固件语言（欧版风格自动补丁）", "ko": "DS 펌웨어 언어 사용 (EU 스타일 자동 패치)",
    },
    "Shadow Freeze Fix (Ice Wave full 3D angle check)": {
        "it": "Fix congelamento ombre (Ice Wave: controllo angolo 3D completo)",
        "nl": "Schaduwbevriezingsfix (Ice Wave: volledige 3D-hoekcontrole)",
        "pt": "Correção de congelamento de sombras (Ice Wave: verificação 3D completa de ângulos)",
        "ru": "Исправление заморозки теней (Ice Wave: полная 3D-проверка угла)",
        "zh": "阴影冻结修复（Ice Wave 完整 3D 角度检测）", "ko": "그림자 정지 수정 (Ice Wave 전체 3D 각도 검사)",
    },
    "Power-Ups: Pick Up With No Effect": {
        "it": "Potenziamenti: raccogli senza effetto", "nl": "Power-ups: oppakken zonder effect",
        "pt": "Power-ups: recolher sem efeito", "ru": "Усиления: подбор без эффекта",
        "zh": "强化道具：拾取无效果", "ko": "파워업: 효과 없이 획득",
    },
    "Video quality: Low (High Performance)": {
        "it": "Qualità video: Bassa (alte prestazioni)", "nl": "Videokwaliteit: Laag (hoge prestaties)",
        "pt": "Qualidade de vídeo: Baixa (alto desempenho)", "ru": "Качество видео: Низкое (высокая производительность)",
        "zh": "视频质量：低（高性能）", "ko": "비디오 품질: 낮음 (고성능)",
    },
    "Apply the selected hunter to your license. (Renaming will update the save data)": {
        "it": "Applica il cacciatore selezionato alla licenza. (Rinominare aggiornerà i dati di salvataggio)",
        "nl": "Geselecteerde hunter op licentie toepassen. (Hernoemen werkt save bij)",
        "pt": "Aplicar o caçador selecionado à licença. (Renomear atualizará os dados de save)",
        "ru": "Применить выбранного охотника к лицензии. (Переименование обновит сохранение)",
        "zh": "将所选猎人应用到许可证。（重命名将更新存档数据）", "ko": "선택한 헌터를 라이선스에 적용합니다. (이름 변경 시 세이브 데이터가 업데이트됩니다)",
    },
    "Blue(US)": {
        "it": "Blu (US)", "nl": "Blauw (US)", "pt": "Azul (US)",
        "ru": "Синий (US)", "zh": "蓝 (US)", "ko": "파랑 (US)",
    },
    "Fixed": {
        "it": "Fisso", "nl": "Vast", "pt": "Fixo",
        "ru": "Фиксированный", "zh": "固定", "ko": "고정",
    },
    "Per Damage \u00e2\u0080\u0094 High": {
        "it": "Per danno — Alto", "nl": "Per schade — Hoog", "pt": "Por dano — Alto",
        "ru": "За урон — Высокий", "zh": "按伤害 — 高", "ko": "피해별 — 높음",
    },
    "Controls how the game's current aim direction follows the target aim direction.": {
        "it": "Controlla come la direzione di mira attuale del gioco segue la direzione di mira target.",
        "nl": "Bepaalt hoe de huidige richtrichting in het spel de doelrichting volgt.",
        "pt": "Controla como a direção de mira atual do jogo segue a direção alvo.",
        "ru": "Определяет, как текущее направление прицела в игре следует за целевым.",
        "zh": "控制游戏中当前瞄准方向如何跟随目标瞄准方向。", "ko": "게임 내 현재 조준 방향이 목표 조준 방향을 따르는 방식을 제어합니다.",
    },
    "Checked: toggle native weapon zoom by calling the game's SetPlayerScopeZoom setter. Unchecked with New Method also off: use Legacy fixed R-button input.": {
        "it": "Selezionato: attiva/disattiva lo zoom nativo dell'arma chiamando SetPlayerScopeZoom del gioco. Deselezionato e Nuovo metodo disattivato: usa l'input legacy del pulsante R fisso.",
        "nl": "Aangevinkt: native wapenzoom schakelen via SetPlayerScopeZoom van het spel. Uitgevinkt en Nieuwe methode ook uit: legacy vaste R-knopinvoer gebruiken.",
        "pt": "Marcado: alternar zoom nativo da arma chamando SetPlayerScopeZoom do jogo. Desmarcado e Novo método também desativado: usar entrada fixa do botão R legado.",
        "ru": "Включено: переключение нативного зума оружия через SetPlayerScopeZoom игры. Выключено и Новый метод тоже выкл.: использовать устаревший ввод кнопки R.",
        "zh": "勾选：调用游戏的 SetPlayerScopeZoom 切换原生武器缩放。未勾选且新方法也关闭：使用旧版固定 R 键输入。",
        "ko": "체크: 게임의 SetPlayerScopeZoom을 호출하여 네이티브 무기 줌 전환. 체크 해제 및 새 방식도 OFF: 레거시 고정 R 버튼 입력 사용.",
    },
    "Enables applying the selected hunter to the license.": {
        "it": "Consente di applicare il cacciatore selezionato alla licenza.",
        "nl": "Maakt het mogelijk de geselecteerde hunter op de licentie toe te passen.",
        "pt": "Permite aplicar o caçador selecionado à licença.",
        "ru": "Позволяет применить выбранного охотника к лицензии.",
        "zh": "允许将所选猎人应用到许可证。", "ko": "선택한 헌터를 라이선스에 적용할 수 있게 합니다.",
    },
    "Does not overwrite your saved window layout settings. The override is applied only while Metroid Prime Hunters is in-game.": {
        "it": "Non sovrascrive le impostazioni salvate del layout finestra. L'override si applica solo durante Metroid Prime Hunters in gioco.",
        "nl": "Overschrijft je opgeslagen vensterlayout-instellingen niet. De override geldt alleen tijdens Metroid Prime Hunters in het spel.",
        "pt": "Não sobrescreve as configurações salvas de layout da janela. A substituição só se aplica enquanto Metroid Prime Hunters estiver em jogo.",
        "ru": "Не перезаписывает сохранённые настройки расположения окон. Переопределение применяется только во время игры в Metroid Prime Hunters.",
        "zh": "不会覆盖已保存的窗口布局设置。仅在 Metroid Prime Hunters 游戏内时应用覆盖。",
        "ko": "저장된 창 레이아웃 설정을 덮어쓰지 않습니다. Metroid Prime Hunters 게임 중에만 재정의가 적용됩니다.",
    },
    "Enable Custom HUD (Replaces the in-game HUD with a custom overlay showing HP, ammo, weapon icons and crosshair)": {
        "it": "Abilita HUD personalizzato (Sostituisce l'HUD di gioco con un overlay personalizzato con HP, munizioni, icone armi e reticolo)",
        "nl": "Aangepaste HUD inschakelen (Vervangt de in-game HUD door een overlay met HP, munitie, wapeniconen en richtkruis)",
        "pt": "Ativar HUD personalizado (Substitui o HUD do jogo por uma sobreposição com HP, munição, ícones de armas e mira)",
        "ru": "Включить пользовательский HUD (Заменяет игровой HUD наложением с HP, боеприпасами, иконками оружия и прицелом)",
        "zh": "启用自定义 HUD（用显示 HP、弹药、武器图标和准星的自定义叠加层替换游戏内 HUD）",
        "ko": "사용자 지정 HUD 활성화 (게임 내 HUD를 HP, 탄약, 무기 아이콘, 조준선이 있는 사용자 지정 오버레이로 대체)",
    },
    "\u00e2\u0080\u0094 Score Row (per mode) \u00e2\u0080\u0094": {
        "it": "— Riga punteggio (per modalità) —", "nl": "— Score rij (per modus) —",
        "pt": "— Linha de pontuação (por modo) —", "ru": "— Строка счёта (по режиму) —",
        "zh": "— 得分行（按模式）—", "ko": "— 점수 행 (모드별) —",
    },
    "Hide Crosshair": {
        "it": "Nascondi reticolo", "nl": "Richtkruis verbergen", "pt": "Ocultar mira",
        "ru": "Скрыть прицел", "zh": "隐藏准星", "ko": "조준선 숨기기",
    },
    "Hide Score: Bounty": {
        "it": "Nascondi punteggio: Taglia", "nl": "Score verbergen: Bounty", "pt": "Ocultar pontuação: Recompensa",
        "ru": "Скрыть счёт: Награда", "zh": "隐藏得分：赏金", "ko": "점수 숨기기: 바운티",
    },
    "Auto Scale Enable": {
        "it": "Abilita scala automatica", "nl": "Automatische schaling inschakelen",
        "pt": "Ativar escala automática", "ru": "Включить автомасштаб",
        "zh": "启用自动缩放", "ko": "자동 크기 조절 활성화",
    },
    "Auto Scale Crosshair Cap %": {
        "it": "Limite reticolo scala auto %", "nl": "Richtkruislimiet auto-schaling %",
        "pt": "Limite de mira com escala automática %", "ru": "Предел прицела при автомасштабе %",
        "zh": "自动缩放准星上限 %", "ko": "자동 크기 조절 조준선 상한 %",
    },
    "Underline": {
        "it": "Sottolineato", "nl": "Onderstrepen", "pt": "Sublinhado",
        "ru": "Подчёркивание", "zh": "下划线", "ko": "밑줄",
    },
    "Zoom Base Scale %": {
        "it": "Scala base reticolo zoom %", "nl": "Basis richtkruisschaal bij zoom %",
        "pt": "Escala base de mira com zoom %", "ru": "Базовый масштаб прицела при зуме %",
        "zh": "缩放时准星基础比例 %", "ko": "확대 시 기본 조준선 크기 %",
    },
    "Scope Radius": {
        "it": "Raggio mirino", "nl": "Vizierradius", "pt": "Raio da mira",
        "ru": "Радиус прицела", "zh": "准星半径", "ko": "스코프 반경",
    },
    "Scope Dot Size": {
        "it": "Dimensione punto mirino", "nl": "Vizierpuntgrootte", "pt": "Tamanho do ponto da mira",
        "ru": "Размер точки прицела", "zh": "准星点大小", "ko": "스코프 점 크기",
    },
    "Transition Speed %": {
        "it": "Velocità transizione %", "nl": "Overgangssnelheid %", "pt": "Velocidade de transição %",
        "ru": "Скорость перехода %", "zh": "过渡速度 %", "ko": "전환 속도 %",
    },
    "Zoom Crosshair": {
        "it": "Reticolo zoom", "nl": "Zoom-richtkruis", "pt": "Mira com zoom",
        "ru": "Прицел при зуме", "zh": "缩放准星", "ko": "확대 조준선",
    },
    "Fade": {
        "it": "Dissolvenza", "nl": "Vervagen", "pt": "Desvanecer",
        "ru": "Затухание", "zh": "淡入淡出", "ko": "페이드",
    },
    "Pulse Wave": {
        "it": "Onda pulsante", "nl": "Pulsgolf", "pt": "Onda pulsante",
        "ru": "Импульсная волна", "zh": "脉冲波", "ko": "펄스 파",
    },
    "Drone LIDAR": {
        "it": "LIDAR drone", "nl": "Drone-LIDAR", "pt": "LIDAR do drone",
        "ru": "LIDAR дрона", "zh": "无人机 LIDAR", "ko": "드론 LIDAR",
    },
    "Outline Opacity": {
        "it": "Opacità contorno", "nl": "Contourdekking", "pt": "Opacidade do contorno",
        "ru": "Непрозрачность контура", "zh": "轮廓不透明度", "ko": "외곽선 불투명도",
    },
    "Dot Color": {
        "it": "Colore punto", "nl": "Puntkleur", "pt": "Cor do ponto",
        "ru": "Цвет точки", "zh": "点颜色", "ko": "점 색상",
    },
    "Dot Opacity": {
        "it": "Opacità punto", "nl": "Puntdekking", "pt": "Opacidade do ponto",
        "ru": "Непрозрачность точки", "zh": "点不透明度", "ko": "점 불투명도",
    },
    "Outer": {
        "it": "Esterno", "nl": "Buiten", "pt": "Exterior",
        "ru": "Внешний", "zh": "外侧", "ko": "외측",
    },
    "Show Number": {
        "it": "Mostra numero", "nl": "Nummer tonen", "pt": "Mostrar número",
        "ru": "Показать число", "zh": "显示数值", "ko": "숫자 표시",
    },
    "Length X": {
        "it": "Lunghezza X", "nl": "Lengte X", "pt": "Comprimento X",
        "ru": "Длина X", "zh": "长度 X", "ko": "길이 X",
    },
    "Offset X": {
        "it": "Offset X", "nl": "Offset X", "pt": "Deslocamento X",
        "ru": "Смещение X", "zh": "偏移 X", "ko": "오프셋 X",
    },
    "Top Right": {
        "it": "In alto a destra", "nl": "Rechtsboven", "pt": "Superior direito",
        "ru": "Вверху справа", "zh": "右上", "ko": "오른쪽 위",
    },
    "Bottom Center": {
        "it": "In basso al centro", "nl": "Onder midden", "pt": "Inferior central",
        "ru": "Внизу по центру", "zh": "下中", "ko": "아래 중앙",
    },
    "Bot Left": {
        "it": "In basso a sinistra", "nl": "Linksonder", "pt": "Inferior esquerdo",
        "ru": "Внизу слева", "zh": "左下", "ko": "왼쪽 아래",
    },
    "Right": {
        "it": "Destra", "nl": "Rechts", "pt": "Direita",
        "ru": "Справа", "zh": "右", "ko": "오른쪽",
    },
    "Start": {
        "it": "Inizio", "nl": "Start", "pt": "Início",
        "ru": "Начало", "zh": "开始", "ko": "시작",
    },
    "Vert": {
        "it": "Vert.", "nl": "Vert.", "pt": "Vert.",
        "ru": "Верт.", "zh": "纵", "ko": "세로",
    },
    "Text\u00e2\u0086\u0092Gauge": {
        "it": "Testo→Indicatore", "nl": "Tekst→Meter", "pt": "Texto→Medidor",
        "ru": "Текст→Индикатор", "zh": "文字→量表", "ko": "텍스트→게이지",
    },
    "Gauge Side": {
        "it": "Lato indicatore", "nl": "Meterzijde", "pt": "Lado do medidor",
        "ru": "Сторона индикатора", "zh": "量表侧", "ko": "게이지 측면",
    },
    "Text Offset X": {
        "it": "Offset testo X", "nl": "Tekstoffset X", "pt": "Deslocamento do texto X",
        "ru": "Смещение текста X", "zh": "文字偏移 X", "ko": "텍스트 오프셋 X",
    },
    "Suffix": {
        "it": "Suffisso", "nl": "Achtervoegsel", "pt": "Sufixo",
        "ru": "Суффикс", "zh": "后缀", "ko": "접미사",
    },
    "Orient": {
        "it": "Orientamento", "nl": "Uitlijning", "pt": "Orientação",
        "ru": "Ориентация", "zh": "方向", "ko": "방향",
    },
    "Pos Anchor": {
        "it": "Ancora posizione", "nl": "Positieanker", "pt": "Âncora de posição",
        "ru": "Якорь позиции", "zh": "位置锚点", "ko": "위치 앵커",
    },
    "Weapon Layout": {
        "it": "Layout armi", "nl": "Wapenlayout", "pt": "Layout de armas",
        "ru": "Расположение оружия", "zh": "武器布局", "ko": "무기 레이아웃",
    },
    "Label Offset X": {
        "it": "Offset etichetta X", "nl": "Labeloffset X", "pt": "Deslocamento do rótulo X",
        "ru": "Смещение метки X", "zh": "标签偏移 X", "ko": "라벨 오프셋 X",
    },
    "Label: Octoliths": {
        "it": "Etichetta: Octoliti", "nl": "Label: Octolieten", "pt": "Rótulo: Octolitos",
        "ru": "Метка: Октолиты", "zh": "标签：Octoliths", "ko": "라벨: 옥톨리스",
    },
    "Bounty": {
        "it": "Taglia", "nl": "Bounty", "pt": "Recompensa",
        "ru": "Награда", "zh": "赏金", "ko": "바운티",
    },
    "Value": {
        "it": "Valore", "nl": "Waarde", "pt": "Valor",
        "ru": "Значение", "zh": "值", "ko": "값",
    },
    "Label Color: Overall": {
        "it": "Colore etichetta: Generale", "nl": "Labelkleur: Totaal", "pt": "Cor do rótulo: Geral",
        "ru": "Цвет метки: Общий", "zh": "标签颜色：总体", "ko": "라벨 색상: 전체",
    },
    "Goal Color": {
        "it": "Colore obiettivo", "nl": "Doelkleur", "pt": "Cor do objetivo",
        "ru": "Цвет цели", "zh": "目标颜色", "ko": "목표 색상",
    },
    "Use Hunter Color": {
        "it": "Usa colore cacciatore", "nl": "Hunterkleur gebruiken", "pt": "Usar cor do caçador",
        "ru": "Использовать цвет охотника", "zh": "使用猎人颜色", "ko": "헌터 색상 사용",
    },
    "Dst Y": {
        "it": "Dest. Y", "nl": "Doel Y", "pt": "Dest. Y",
        "ru": "Цел. Y", "zh": "显示 Y", "ko": "표시 Y",
    },
    "Spacing": {
        "it": "Spaziatura", "nl": "Afstand", "pt": "Espaçamento",
        "ru": "Интервал", "zh": "间距", "ko": "간격",
    },
    "Highlight Opacity": {
        "it": "Opacità evidenziazione", "nl": "Highlight-dekking", "pt": "Opacidade do destaque",
        "ru": "Непрозрачность подсветки", "zh": "高亮不透明度", "ko": "하이라이트 불투명도",
    },
    "Highlight Offset Right": {
        "it": "Offset evidenziazione destra", "nl": "Highlight-offset rechts", "pt": "Deslocamento do destaque à direita",
        "ru": "Смещение подсветки вправо", "zh": "高亮右偏移", "ko": "하이라이트 오른쪽 오프셋",
    },
    "Size Offset Top": {
        "it": "Offset dimensione alto", "nl": "Grootte-offset boven", "pt": "Deslocamento de tamanho superior",
        "ru": "Смещение размера сверху", "zh": "尺寸上偏移", "ko": "크기 상단 오프셋",
    },
    "Hl Corner Radius": {
        "it": "Raggio angolo HL", "nl": "HL-hoekradius", "pt": "Raio de canto HL",
        "ru": "Радиус угла HL", "zh": "HL 圆角", "ko": "HL 모서리 반경",
    },
    "HP": {
        "it": "HP", "nl": "HP", "pt": "HP", "ru": "HP", "zh": "HP", "ko": "HP",
    },
    "HP Gauge Color By Value": {
        "it": "Colore indicatore HP per valore", "nl": "HP-meterkleur op waarde",
        "pt": "Cor do medidor de HP por valor", "ru": "Цвет индикатора HP по значению",
        "zh": "HP 量表颜色（按数值）", "ko": "HP 게이지 색상 (값 연동)",
    },
    "Ammo Outline": {
        "it": "Contorno munizioni", "nl": "Munitiecontour", "pt": "Contorno de munição",
        "ru": "Контур боеприпасов", "zh": "弹药轮廓", "ko": "탄약 외곽선",
    },
    "Weapon Icon": {
        "it": "Icona arma", "nl": "Wapenpictogram", "pt": "Ícone de arma",
        "ru": "Иконка оружия", "zh": "武器图标", "ko": "무기 아이콘",
    },
    "Weapon Inventory Outline": {
        "it": "Contorno inventario armi", "nl": "Wapeninventariscontour", "pt": "Contorno do inventário de armas",
        "ru": "Контур инвентаря оружия", "zh": "武器栏轮廓", "ko": "무기 인벤토리 외곽선",
    },
    "Score Colors": {
        "it": "Colori punteggio", "nl": "Scorekleuren", "pt": "Cores de pontuação",
        "ru": "Цвета счёта", "zh": "得分颜色", "ko": "점수 색상",
    },
    "Time Left": {
        "it": "Tempo rimanente", "nl": "Resterende tijd", "pt": "Tempo restante",
        "ru": "Оставшееся время", "zh": "剩余时间", "ko": "남은 시간",
    },
    "Bomb Left": {
        "it": "Bombe rimaste", "nl": "Bommen over", "pt": "Bombas restantes",
        "ru": "Бомб осталось", "zh": "剩余炸弹", "ko": "남은 폭탄",
    },
    "Radar Settings": {
        "it": "Impostazioni radar", "nl": "Radarinstellingen", "pt": "Configurações do radar",
        "ru": "Настройки радара", "zh": "雷达设置", "ko": "레이더 설정",
    },
    "Bmb\nIcon": {
        "it": "Icona\nbomba", "nl": "Bom\npictogram", "pt": "Ícone\nde bomba",
        "ru": "Иконка\nбомбы", "zh": "炸弹\n图标", "ko": "폭탄\n아이콘",
    },
    "Bombs": {
        "it": "Bombe", "nl": "Bommen", "pt": "Bombas",
        "ru": "Бомбы", "zh": "炸弹", "ko": "폭탄",
    },
    "Color (Default: Red)": {
        "it": "Colore (predefinito: rosso)", "nl": "Kleur (standaard: rood)", "pt": "Cor (padrão: vermelho)",
        "ru": "Цвет (по умолчанию: красный)", "zh": "颜色（默认：红）", "ko": "색상 (기본값: 빨강)",
    },
    "No Ammo": {
        "it": "Nessuna munizione", "nl": "Geen munitie", "pt": "Sem munição",
        "ru": "Нет боеприпасов", "zh": "无弹药", "ko": "탄약 없음",
    },
    "Octo Drop": {
        "it": "Drop octoliti", "nl": "Octoliet-drop", "pt": "Queda de octolito",
        "ru": "Выпадение октолита", "zh": "Octolith 掉落", "ko": "옥톨리스 드롭",
    },
    "Slot: Objective     [flags=0x01]": {
        "it": "Slot: Obiettivo     [flags=0x01]", "nl": "Slot: Doel     [flags=0x01]",
        "pt": "Slot: Objetivo     [flags=0x01]", "ru": "Слот: Цель     [flags=0x01]",
        "zh": "槽位：目标     [flags=0x01]", "ko": "슬롯: 목표     [flags=0x01]",
    },
    "Applied once on settings close to currently displayed messages (flags=0x00).\nNew messages use their individual literal colors above (Lost Lives / Coward Detect / Turret ...).\nNote: HEADSHOT! (H228) is flags=0x00, not 0x02.": {
        "it": "Applicato una volta alla chiusura delle impostazioni ai messaggi attualmente visualizzati (flags=0x00).\nI nuovi messaggi usano i colori letterali individuali sopra (Vite perse / Codardo rilevato / Torretta ...).\nNota: HEADSHOT! (H228) è flags=0x00, non 0x02.",
        "nl": "Eenmalig toegepast bij sluiten instellingen op momenteel getoonde berichten (flags=0x00).\nNieuwe berichten gebruiken hun individuele letterlijke kleuren hierboven (Verloren levens / Lafaard gedetecteerd / Turret ...).\nOpmerking: HEADSHOT! (H228) is flags=0x00, niet 0x02.",
        "pt": "Aplicado uma vez ao fechar as configurações às mensagens exibidas atualmente (flags=0x00).\nNovas mensagens usam suas cores literais individuais acima (Vidas perdidas / Covarde detectado / Torreta ...).\nNota: HEADSHOT! (H228) é flags=0x00, não 0x02.",
        "ru": "Применяется один раз при закрытии настроек к текущим сообщениям (flags=0x00).\nНовые сообщения используют индивидуальные цвета выше (Потерянные жизни / Трус обнаружен / Турель ...).\nПримечание: HEADSHOT! (H228) — flags=0x00, а не 0x02.",
        "zh": "关闭设置时对当前显示的消息应用一次（flags=0x00）。\n新消息使用上方各自的字面颜色（失去生命 / 检测到懦夫 / 炮塔 ...）。\n注意：HEADSHOT! (H228) 为 flags=0x00，而非 0x02。",
        "ko": "설정을 닫을 때 현재 표시 중인 메시지에 한 번 적용됩니다 (flags=0x00).\n새 메시지는 위의 개별 리터럴 색상을 사용합니다 (잃은 목숨 / 겁쟁이 감지 / 터렛 ...).\n참고: HEADSHOT! (H228)은 flags=0x00이며 0x02가 아닙니다.",
    },
    "White": {
        "it": "Bianco", "nl": "Wit", "pt": "Branco",
        "ru": "Белый", "zh": "白", "ko": "흰색",
    },
    "Pure Cyan": {
        "it": "Ciano puro", "nl": "Zuiver cyaan", "pt": "Ciano puro",
        "ru": "Чистый циан", "zh": "纯青", "ko": "순수 시안",
    },
    "Samus Hud": {
        "it": "HUD Samus", "nl": "Samus-HUD", "pt": "HUD da Samus",
        "ru": "HUD Samus", "zh": "Samus HUD", "ko": "사무스 HUD",
    },
    "Trace Hud": {
        "it": "HUD Trace", "nl": "Trace-HUD", "pt": "HUD do Trace",
        "ru": "HUD Trace", "zh": "Trace HUD", "ko": "트레이스 HUD",
    },
    "Weavel Hud": {
        "it": "HUD Weavel", "nl": "Weavel-HUD", "pt": "HUD do Weavel",
        "ru": "HUD Weavel", "zh": "Weavel HUD", "ko": "위벨 HUD",
    },
    "Power Beam": {
        "it": "Power Beam", "nl": "Power Beam", "pt": "Power Beam",
        "ru": "Power Beam", "zh": "Power Beam", "ko": "Power Beam",
    },
    "Battlehammer": {
        "it": "Battlehammer", "nl": "Battlehammer", "pt": "Battlehammer",
        "ru": "Battlehammer", "zh": "Battlehammer", "ko": "Battlehammer",
    },
    "ShockCoil": {
        "it": "ShockCoil", "nl": "ShockCoil", "pt": "ShockCoil",
        "ru": "ShockCoil", "zh": "ShockCoil", "ko": "ShockCoil",
    },
    "BH Color": {
        "it": "Colore BH", "nl": "BH-kleur", "pt": "Cor BH",
        "ru": "Цвет BH", "zh": "BH 颜色", "ko": "BH 색상",
    },
    "OC Color": {
        "it": "Colore OC", "nl": "OC-kleur", "pt": "Cor OC",
        "ru": "Цвет OC", "zh": "OC 颜色", "ko": "OC 색상",
    },
    "Medium": {
        "it": "Medio", "nl": "Gemiddeld", "pt": "Médio",
        "ru": "Средний", "zh": "中", "ko": "중간",
    },
    "Number of Colors": {
        "it": "Numero di colori", "nl": "Aantal kleuren", "pt": "Número de cores",
        "ru": "Количество цветов", "zh": "颜色数量", "ko": "색상 수",
    },
    "Threshold 5 (%)": {
        "it": "Soglia 5 (%)", "nl": "Drempel 5 (%)", "pt": "Limite 5 (%)",
        "ru": "Порог 5 (%)", "zh": "阈值 5 (%)", "ko": "임계값 5 (%)",
    },
    "Color 4": {
        "it": "Colore 4", "nl": "Kleur 4", "pt": "Cor 4",
        "ru": "Цвет 4", "zh": "颜色 4", "ko": "색상 4",
    },
    "Custom HUD code copied to the clipboard.": {
        "it": "Codice HUD personalizzato copiato negli appunti.",
        "nl": "Aangepaste HUD-code gekopieerd naar klembord.",
        "pt": "Código de HUD personalizado copiado para a área de transferência.",
        "ru": "Код пользовательского HUD скопирован в буфер обмена.",
        "zh": "自定义 HUD 代码已复制到剪贴板。", "ko": "사용자 지정 HUD 코드가 클립보드에 복사되었습니다.",
    },
    "Failed to load Custom HUD code: %1": {
        "it": "Impossibile caricare il codice HUD personalizzato: %1",
        "nl": "Aangepaste HUD-code laden mislukt: %1",
        "pt": "Falha ao carregar código de HUD personalizado: %1",
        "ru": "Не удалось загрузить код пользовательского HUD: %1",
        "zh": "加载自定义 HUD 代码失败：%1", "ko": "사용자 지정 HUD 코드 로드 실패: %1",
    },
    "Use New Method 2 for Zoom": {
        "it": "Usa nuovo metodo 2 per lo zoom", "nl": "Nieuwe methode 2 voor zoom gebruiken",
        "pt": "Usar novo método 2 para zoom", "ru": "Использовать новый метод 2 для зума",
        "zh": "缩放使用新方法 2", "ko": "확대에 새 방식 2 사용",
    },
    # melonds section
    "Input and hotkeys - melonDS": {
        "it": "Input e scorciatoie - melonDS", "nl": "Invoer en sneltoetsen - melonDS",
        "pt": "Entrada e atalhos - melonDS", "ru": "Ввод и горячие клавиши - melonDS",
        "zh": "输入与快捷键 - melonDS", "ko": "입력 및 단축키 - melonDS",
    },
    "CPU emulation": {
        "it": "Emulazione CPU", "nl": "CPU-emulatie", "pt": "Emulação de CPU",
        "ru": "Эмуляция CPU", "zh": "CPU 模拟", "ko": "CPU 에뮬레이션",
    },
    "Browse...": {
        "it": "Sfoglia...", "nl": "Bladeren...", "pt": "Procurar...",
        "ru": "Обзор...", "zh": "浏览...", "ko": "찾아보기...",
    },
    "DSi ARM9 BIOS:": {
        "it": "BIOS ARM9 DSi:", "nl": "DSi ARM9 BIOS:", "pt": "BIOS ARM9 DSi:",
        "ru": "BIOS ARM9 DSi:", "zh": "DSi ARM9 BIOS：", "ko": "DSi ARM9 BIOS:",
    },
    "Sync SD to folder:": {
        "it": "Sincronizza SD con cartella:", "nl": "SD synchroniseren met map:",
        "pt": "Sincronizar SD com pasta:", "ru": "Синхронизировать SD с папкой:",
        "zh": "同步 SD 到文件夹：", "ko": "SD를 폴더와 동기화:",
    },
    "Enable JIT recompiler": {
        "it": "Abilita ricompilatore JIT", "nl": "JIT-recompiler inschakelen",
        "pt": "Ativar recompilador JIT", "ru": "Включить JIT-перекомпилятор",
        "zh": "启用 JIT 重编译器", "ko": "JIT 리컴파일러 활성화",
    },
    "Enable DLDI (for homebrew)": {
        "it": "Abilita DLDI (per homebrew)", "nl": "DLDI inschakelen (voor homebrew)",
        "pt": "Ativar DLDI (para homebrew)", "ru": "Включить DLDI (для homebrew)",
        "zh": "启用 DLDI（用于 homebrew）", "ko": "DLDI 활성화 (homebrew용)",
    },
    "Note: melonDS must be restarted in order for these changes to have effect": {
        "it": "Nota: melonDS deve essere riavviato affinché queste modifiche abbiano effetto",
        "nl": "Opmerking: melonDS moet opnieuw worden gestart om deze wijzigingen toe te passen",
        "pt": "Nota: o melonDS deve ser reiniciado para que estas alterações tenham efeito",
        "ru": "Примечание: для применения изменений необходимо перезапустить melonDS",
        "zh": "注意：必须重启 melonDS 才能使这些更改生效", "ko": "참고: 변경 사항을 적용하려면 melonDS를 다시 시작해야 합니다",
    },
    "Display settings": {
        "it": "Impostazioni display", "nl": "Weergave-instellingen", "pt": "Configurações de exibição",
        "ru": "Настройки отображения", "zh": "显示设置", "ko": "디스플레이 설정",
    },
    "OpenGL (Classic)": {
        "it": "OpenGL (Classic)", "nl": "OpenGL (Classic)", "pt": "OpenGL (Classic)",
        "ru": "OpenGL (Classic)", "zh": "OpenGL (Classic)", "ko": "OpenGL (Classic)",
    },
    "Internal resolution:": {
        "it": "Risoluzione interna:", "nl": "Interne resolutie:", "pt": "Resolução interna:",
        "ru": "Внутреннее разрешение:", "zh": "内部分辨率：", "ko": "내부 해상도:",
    },
    "Picture source": {
        "it": "Sorgente immagine", "nl": "Beeldbron", "pt": "Fonte de imagem",
        "ru": "Источник изображения", "zh": "图像来源", "ko": "이미지 소스",
    },
    "Physical camera:": {
        "it": "Fotocamera fisica:", "nl": "Fysieke camera:", "pt": "Câmera física:",
        "ru": "Физическая камера:", "zh": "物理摄像头：", "ko": "물리 카메라:",
    },
    "Microphone input": {
        "it": "Ingresso microfono", "nl": "Microfooningang", "pt": "Entrada de microfone",
        "ru": "Вход микрофона", "zh": "麦克风输入", "ko": "마이크 입력",
    },
    "None": {
        "it": "Nessuno", "nl": "Geen", "pt": "Nenhum",
        "ru": "Нет", "zh": "无", "ko": "없음",
    },
    "Automatic": {
        "it": "Automatico", "nl": "Automatisch", "pt": "Automático",
        "ru": "Автоматически", "zh": "自动", "ko": "자동",
    },
    "WAV file:": {
        "it": "File WAV:", "nl": "WAV-bestand:", "pt": "Arquivo WAV:",
        "ru": "Файл WAV:", "zh": "WAV 文件：", "ko": "WAV 파일:",
    },
    "Toggle FPS limit": {
        "it": "Attiva/disattiva limite FPS", "nl": "FPS-limiet schakelen",
        "pt": "Alternar limite de FPS", "ru": "Переключить ограничение FPS",
        "zh": "切换 FPS 限制", "ko": "FPS 제한 전환",
    },
    "Swap screen emphasis": {
        "it": "Scambia enfasi schermi", "nl": "Schermnadruk wisselen",
        "pt": "Alternar ênfase de tela", "ru": "Переключить акцент экрана",
        "zh": "切换屏幕强调", "ko": "화면 강조 전환",
    },
    "Multiplayer settings - melonDS": {
        "it": "Impostazioni multigiocatore - melonDS", "nl": "Multiplayer-instellingen - melonDS",
        "pt": "Configurações multijogador - melonDS", "ru": "Настройки мультиплеера - melonDS",
        "zh": "多人游戏设置 - melonDS", "ko": "멀티플레이 설정 - melonDS",
    },
    "Data reception timeout: ": {
        "it": "Timeout ricezione dati: ", "nl": "Time-out gegevensontvangst: ",
        "pt": "Tempo limite de recepção de dados: ", "ru": "Таймаут приёма данных: ",
        "zh": "数据接收超时： ", "ko": "데이터 수신 타임아웃: ",
    },
    "Direct mode [TEXT PLACEHOLDER]": {
        "it": "Modalità diretta [TEXT PLACEHOLDER]", "nl": "Directe modus [TEXT PLACEHOLDER]",
        "pt": "Modo direto [TEXT PLACEHOLDER]", "ru": "Прямой режим [TEXT PLACEHOLDER]",
        "zh": "直连模式 [TEXT PLACEHOLDER]", "ko": "직접 모드 [TEXT PLACEHOLDER]",
    },
    "(none)": {
        "it": "(nessuno)", "nl": "(geen)", "pt": "(nenhum)",
        "ru": "(нет)", "zh": "（无）", "ko": "(없음)",
    },
    "Message:": {
        "it": "Messaggio:", "nl": "Bericht:", "pt": "Mensagem:",
        "ru": "Сообщение:", "zh": "消息：", "ko": "메시지:",
    },
    "(leave empty to use default MAC)": {
        "it": "(lasciare vuoto per MAC predefinito)", "nl": "(leeg laten voor standaard-MAC)",
        "pt": "(deixar vazio para usar MAC padrão)", "ru": "(оставьте пустым для MAC по умолчанию)",
        "zh": "（留空以使用默认 MAC）", "ko": "(비워 두면 기본 MAC 사용)",
    },
    "Hide mouse after inactivity": {
        "it": "Nascondi mouse dopo inattività", "nl": "Muis verbergen na inactiviteit",
        "pt": "Ocultar mouse após inatividade", "ru": "Скрывать мышь при бездействии",
        "zh": "无操作后隐藏鼠标", "ko": "비활성 시 마우스 숨기기",
    },
    "Framerate ": {
        "it": "Framerate ", "nl": "Framerate ", "pt": "Taxa de quadros ",
        "ru": "Частота кадров ", "zh": "帧率 ", "ko": "프레임레이트 ",
    },
    "Accurate": {
        "it": "Preciso", "nl": "Nauwkeurig", "pt": "Preciso",
        "ru": "Точный", "zh": "精确", "ko": "정확",
    },
    "3x": {
        "it": "3x", "nl": "3x", "pt": "3x", "ru": "3x", "zh": "3x", "ko": "3x",
    },
    "Save files path:": {
        "it": "Percorso salvataggi:", "nl": "Pad savebestanden:", "pt": "Caminho dos saves:",
        "ru": "Путь к сохранениям:", "zh": "存档路径：", "ko": "세이브 파일 경로:",
    },
    "Import...": {
        "it": "Importa...", "nl": "Importeren...", "pt": "Importar...",
        "ru": "Импорт...", "zh": "导入...", "ko": "가져오기...",
    },
    "Description:": {
        "it": "Descrizione:", "nl": "Beschrijving:", "pt": "Descrição:",
        "ru": "Описание:", "zh": "说明：", "ko": "설명:",
    },
    "DSi Battery": {
        "it": "Batteria DSi", "nl": "DSi-batterij", "pt": "Bateria DSi",
        "ru": "Батарея DSi", "zh": "DSi 电池", "ko": "DSi 배터리",
    },
    "Battery Level": {
        "it": "Livello batteria", "nl": "Batterijniveau", "pt": "Nível da bateria",
        "ru": "Уровень заряда", "zh": "电池电量", "ko": "배터리 잔량",
    },
    "Reset to system date and time": {
        "it": "Reimposta a data e ora di sistema", "nl": "Reset naar systeemdatum en -tijd",
        "pt": "Redefinir para data e hora do sistema", "ru": "Сбросить на системные дату и время",
        "zh": "重置为系统日期和时间", "ko": "시스템 날짜 및 시간으로 재설정",
    },
    "Titles": {
        "it": "Titoli", "nl": "Titels", "pt": "Títulos",
        "ru": "Названия", "zh": "标题", "ko": "제목",
    },
    "Italian title:": {
        "it": "Titolo italiano:", "nl": "Italiaanse titel:", "pt": "Título em italiano:",
        "ru": "Итальянское название:", "zh": "意大利语标题：", "ko": "이탈리아어 제목:",
    },
    "ARM9 ROM offset: ": {
        "it": "Offset ROM ARM9: ", "nl": "ARM9 ROM-offset: ", "pt": "Deslocamento ROM ARM9: ",
        "ru": "Смещение ROM ARM9: ", "zh": "ARM9 ROM 偏移： ", "ko": "ARM9 ROM 오프셋: ",
    },
    "ARM7 entry address:": {
        "it": "Indirizzo di ingresso ARM7:", "nl": "ARM7-instapadres:", "pt": "Endereço de entrada ARM7:",
        "ru": "Адрес входа ARM7:", "zh": "ARM7 入口地址：", "ko": "ARM7 진입 주소:",
    },
    "FNT size:": {
        "it": "Dimensione FNT:", "nl": "FNT-grootte:", "pt": "Tamanho FNT:",
        "ru": "Размер FNT:", "zh": "FNT 大小：", "ko": "FNT 크기:",
    },
    "Game code:": {
        "it": "Codice gioco:", "nl": "Spelcode:", "pt": "Código do jogo:",
        "ru": "Код игры:", "zh": "游戏代码：", "ko": "게임 코드:",
    },
    "RAM info - melonDS": {
        "it": "Info RAM - melonDS", "nl": "RAM-info - melonDS", "pt": "Info de RAM - melonDS",
        "ru": "Сведения о RAM - melonDS", "zh": "RAM 信息 - melonDS", "ko": "RAM 정보 - melonDS",
    },
    "4bytes": {
        "it": "4 byte", "nl": "4 bytes", "pt": "4 bytes",
        "ru": "4 байта", "zh": "4 字节", "ko": "4바이트",
    },
    "Host LAN game - melonDS": {
        "it": "Ospita partita LAN - melonDS", "nl": "LAN-spel hosten - melonDS",
        "pt": "Hospedar jogo LAN - melonDS", "ru": "Создать LAN-игру - melonDS",
        "zh": "托管 LAN 游戏 - melonDS", "ko": "LAN 게임 호스트 - melonDS",
    },
    "Direct connect...": {
        "it": "Connessione diretta...", "nl": "Direct verbinden...", "pt": "Conexão direta...",
        "ru": "Прямое подключение...", "zh": "直接连接...", "ko": "직접 연결...",
    },
    "Status": {
        "it": "Stato", "nl": "Status", "pt": "Status",
        "ru": "Статус", "zh": "状态", "ko": "상태",
    },
    "Failed to start LAN game.": {
        "it": "Impossibile avviare la partita LAN.", "nl": "LAN-spel starten mislukt.",
        "pt": "Falha ao iniciar jogo LAN.", "ru": "Не удалось запустить LAN-игру.",
        "zh": "启动 LAN 游戏失败。", "ko": "LAN 게임 시작에 실패했습니다.",
    },
    "View on &GitHub": {
        "it": "Visualizza su &GitHub", "nl": "Bekijken op &GitHub", "pt": "Ver no &GitHub",
        "ru": "Открыть на &GitHub", "zh": "在 &GitHub 上查看", "ko": "&GitHub에서 보기",
    },
    "Red": {
        "it": "Rosso", "nl": "Rood", "pt": "Vermelho",
        "ru": "Красный", "zh": "红", "ko": "빨강",
    },
    "Light green": {
        "it": "Verde chiaro", "nl": "Lichtgroen", "pt": "Verde claro",
        "ru": "Светло-зелёный", "zh": "浅绿", "ko": "연두",
    },
    "Dark blue": {
        "it": "Blu scuro", "nl": "Donkerblauw", "pt": "Azul escuro",
        "ru": "Тёмно-синий", "zh": "深蓝", "ko": "진한 파랑",
    },
    "German": {
        "it": "Tedesco", "nl": "Duits", "pt": "Alemão",
        "ru": "Немецкий", "zh": "德语", "ko": "독일어",
    },
    "February": {
        "it": "Febbraio", "nl": "Februari", "pt": "Fevereiro",
        "ru": "Февраль", "zh": "2 月", "ko": "2월",
    },
    "July": {
        "it": "Luglio", "nl": "Juli", "pt": "Julho",
        "ru": "Июль", "zh": "7 月", "ko": "7월",
    },
    "December": {
        "it": "Dicembre", "nl": "December", "pt": "Dezembro",
        "ru": "Декабрь", "zh": "12 月", "ko": "12월",
    },
}


def main():
    src = Path(__file__).parent / "loc_chunk_3_translated.json"
    dst = Path(__file__).parent / "loc_chunk_3_extended.json"

    with open(src, encoding="utf-8") as f:
        entries = json.load(f)

    out = []
    missing = []
    for entry in entries:
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
        raise SystemExit(f"Missing translations for {len(missing)} entries:\n" + "\n".join(missing))

    with open(dst, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=2)
        f.write("\n")

    print(f"Wrote {len(out)} entries to {dst}")


if __name__ == "__main__":
    main()
