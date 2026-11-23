-- #########################################################
-- Playerbots - trainer learnone: teach exactly one trainer spell
-- Localized for all WotLK locales (koKR, frFR, deDE, zhCN,
-- zhTW, esES, esMX, ruRU)
-- #########################################################

DELETE FROM ai_playerbot_texts
WHERE name IN ('trainer_learnone_usage', 'trainer_learnone_not_my_trainer', 'trainer_learnone_not_taught',
               'trainer_learnone_cannot_learn', 'trainer_learnone_too_expensive', 'trainer_learnone_not_confirmed');

DELETE FROM ai_playerbot_texts_chance
WHERE name IN ('trainer_learnone_usage', 'trainer_learnone_not_my_trainer', 'trainer_learnone_not_taught',
               'trainer_learnone_cannot_learn', 'trainer_learnone_too_expensive', 'trainer_learnone_not_confirmed');

INSERT INTO `ai_playerbot_texts`
(`id`, `name`, `text`, `say_type`, `reply_type`,
 `text_loc1`, `text_loc2`, `text_loc3`, `text_loc4`,
 `text_loc5`, `text_loc6`, `text_loc7`, `text_loc8`)
VALUES
(1946, 'trainer_learnone_usage',
 'Use: trainer learnone [one spell link or spell id]. Nothing learned.', 0, 0,
 '사용법: trainer learnone [주문 링크 하나 또는 주문 ID]. 아무것도 배우지 않았습니다.',
 'Utilisation : trainer learnone [un lien de sort ou un identifiant de sort]. Rien appris.',
 'Verwendung: trainer learnone [ein Zauberlink oder eine Zauber-ID]. Nichts gelernt.',
 '用法：trainer learnone [一个法术链接或法术ID]。没有学习任何技能。',
 '用法：trainer learnone [一個法術連結或法術ID]。沒有學習任何技能。',
 'Uso: trainer learnone [un enlace de hechizo o un id de hechizo]. No he aprendido nada.',
 'Uso: trainer learnone [un enlace de hechizo o un id de hechizo]. No aprendí nada.',
 'Использование: trainer learnone [одна ссылка на заклинание или его ID]. Ничего не изучено.'),
(1947, 'trainer_learnone_not_my_trainer',
 '%trainer does not train me. Nothing learned.', 0, 0,
 '%trainer 님은 저를 가르치지 않습니다. 아무것도 배우지 않았습니다.',
 '%trainer ne m''entraîne pas. Rien appris.',
 '%trainer bildet mich nicht aus. Nichts gelernt.',
 '%trainer 不训练我。没有学习任何技能。',
 '%trainer 不訓練我。沒有學習任何技能。',
 '%trainer no me entrena. No he aprendido nada.',
 '%trainer no me entrena. No aprendí nada.',
 '%trainer меня не обучает. Ничего не изучено.'),
(1948, 'trainer_learnone_not_taught',
 '%trainer does not teach spell %spell. Nothing learned.', 0, 0,
 '%trainer 님은 주문 %spell을(를) 가르치지 않습니다. 아무것도 배우지 않았습니다.',
 '%trainer n''enseigne pas le sort %spell. Rien appris.',
 '%trainer lehrt den Zauber %spell nicht. Nichts gelernt.',
 '%trainer 不教授法术 %spell。没有学习任何技能。',
 '%trainer 不教授法術 %spell。沒有學習任何技能。',
 '%trainer no enseña el hechizo %spell. No he aprendido nada.',
 '%trainer no enseña el hechizo %spell. No aprendí nada.',
 '%trainer не обучает заклинанию %spell. Ничего не изучено.'),
(1949, 'trainer_learnone_cannot_learn',
 'I cannot learn spell %spell now: already known, or my level, skill or earlier rank is too low. Nothing learned.', 0, 0,
 '지금은 주문 %spell을(를) 배울 수 없습니다. 이미 알고 있거나 레벨, 숙련도 또는 이전 단계가 부족합니다. 아무것도 배우지 않았습니다.',
 'Je ne peux pas apprendre le sort %spell maintenant : déjà connu, ou niveau, compétence ou rang précédent insuffisant. Rien appris.',
 'Ich kann den Zauber %spell jetzt nicht lernen: schon bekannt, oder Stufe, Fertigkeit oder vorheriger Rang zu niedrig. Nichts gelernt.',
 '我现在无法学习法术 %spell：已经学会，或等级、技能或前一级不足。没有学习任何技能。',
 '我現在無法學習法術 %spell：已經學會，或等級、技能或前一級不足。沒有學習任何技能。',
 'No puedo aprender el hechizo %spell ahora: ya lo conozco, o mi nivel, habilidad o rango anterior es demasiado bajo. No he aprendido nada.',
 'No puedo aprender el hechizo %spell ahora: ya lo conozco, o mi nivel, habilidad o rango anterior es muy bajo. No aprendí nada.',
 'Я не могу сейчас изучить заклинание %spell: оно уже известно, или мой уровень, навык или предыдущий ранг слишком низки. Ничего не изучено.'),
(1950, 'trainer_learnone_too_expensive',
 'I cannot spare the money for spell %spell. Nothing learned.', 0, 0,
 '주문 %spell에 쓸 돈이 부족합니다. 아무것도 배우지 않았습니다.',
 'Je ne peux pas dépenser l''argent pour le sort %spell. Rien appris.',
 'Ich kann das Geld für den Zauber %spell nicht entbehren. Nichts gelernt.',
 '我没有多余的钱学习法术 %spell。没有学习任何技能。',
 '我沒有多餘的錢學習法術 %spell。沒有學習任何技能。',
 'No me sobra el dinero para el hechizo %spell. No he aprendido nada.',
 'No me alcanza el dinero para el hechizo %spell. No aprendí nada.',
 'Мне не хватает денег на заклинание %spell. Ничего не изучено.'),
(1951, 'trainer_learnone_not_confirmed',
 'Learning spell %spell could not be confirmed. Nothing was charged; check my spell book.', 0, 0,
 '주문 %spell 습득을 확인할 수 없습니다. 비용은 청구되지 않았습니다. 제 주문책을 확인해 주세요.',
 'L''apprentissage du sort %spell n''a pas pu être confirmé. Rien n''a été payé ; vérifiez mon grimoire.',
 'Das Erlernen des Zaubers %spell konnte nicht bestätigt werden. Nichts wurde berechnet; prüfe mein Zauberbuch.',
 '无法确认已学会法术 %spell。没有收取费用，请检查我的法术书。',
 '無法確認已學會法術 %spell。沒有收取費用，請檢查我的法術書。',
 'No se pudo confirmar que aprendí el hechizo %spell. No se cobró nada; revisa mi libro de hechizos.',
 'No se pudo confirmar que aprendí el hechizo %spell. No se cobró nada; revisa mi libro de hechizos.',
 'Не удалось подтвердить изучение заклинания %spell. Деньги не списаны; проверьте мою книгу заклинаний.');

INSERT INTO ai_playerbot_texts_chance (name, probability)
VALUES ('trainer_learnone_usage', 100), ('trainer_learnone_not_my_trainer', 100), ('trainer_learnone_not_taught', 100),
       ('trainer_learnone_cannot_learn', 100), ('trainer_learnone_too_expensive', 100),
       ('trainer_learnone_not_confirmed', 100);
