// Включить русские переносы, кавычки и т.д.
#set text(lang: "ru")
// "Локализация" для определений
#set terms(separator: [~--- ])

// Нормальный русскоязычный абзацный отступ
#set par(first-line-indent: 1em)
// Костыль https://github.com/typst/typst/issues/311#issuecomment-1863205291
#show heading: it => { it;text()[#v(0.3em, weak: true)];text()[#h(0em)] }
#show figure: it => { it;text()[#v(0.3em, weak: true)];text()[#h(0em)] }
// расстояние между строками абзаца и между абзацами
#set par(leading: 0.65em)
#show par: set block(spacing: 0.65em)

// Разметка страницы
#set page("a4", margin: 1.5cm)
#set par(justify: true)

// Включить нумерацию заголовков
#set heading(numbering: "1.")

// Показывать подписи к таблицам над таблицами
#show figure.where(kind: table): set figure.caption(position: top)

#align(center, text(17pt)[
  *RAID Triple Parity*
])
#align(center)[
  Григоренко Павел
]

= Принцип построения

Алгоритм RAID Triple Parity (RTP), описанный в @RTP-paper, является расширением
алгоритма Row-Diagonal Parity (RDP), описанного в @RDP-paper, так что для начала
кратко опишем его.

== RDP

За основу берётся RAID-4 массив, состоящий из $p-1$ дисков с данными, где $p$~---
простое число, и одного диска с чек-суммами
#footnote[Здесь и далее под "чек-суммами" имеются в виду XOR-суммы]
(далее "R"). К этому массиву добавляется ещё один диск с чек-суммами (далее "Diag"),
только чек-суммы в нём считаются не по рядам (как в R), а "по диагоналям".

Опишем, что это значит, более формально.

Массив разбивается на группы по $p-1$ рядов #footnote[По-английски~--- "stripe"].
Диагональные чек-суммы в каждой такой группе считаются отдельно, без учёта всех
остальных групп. Как следствие, восстановление данных в такой группе также может
происходить независимо от остальных групп.

Более точно, если пронумеровать диски с данными от $0$ до $p-2$ включительно, а
диску $R$ присвоить номер $p-1$, то (при нумерации рядов с нуля) блок,
находящийся в ряду $j$ на диске $i$ будет принадлежать диагонали с номером $(i+j) mod p$.

// typstfmt::off
#figure(
  caption: [Разбиение на диагонали в RDP при $p=7$],
  table(
    columns: 7,
    [Диск 0], [Диск 1], [Диск 2], [Диск 3], [Диск 4], [Диск 5], [Диск R],
    [0], [1], [2], [3], [4], [5], [6],
    [1], [2], [3], [4], [5], [6], [0],
    [2], [3], [4], [5], [6], [0], [1],
    [3], [4], [5], [6], [0], [1], [2],
    [4], [5], [6], [0], [1], [2], [3],
    [5], [6], [0], [1], [2], [3], [4],
  )
)
// typstfmt::on

Таким образом мы получаем $p$ диагоналей. Для $p-1$ из них подсчитывается
чек-сумма и записывается в Diag.

// typstfmt::off
#figure(
  caption: [Подсчёт диагональных чек-сумм в RDP при $p=7$],
  table(
    columns: 8,
    [Диск 0], [Диск 1], [Диск 2], [Диск 3], [Диск 4], [Диск 5], [Диск R],  [Diag],
    [0], [1], [2], [3], [4], [5], [ ], [0],
    [1], [2], [3], [4], [5], [ ], [0], [1],
    [2], [3], [4], [5], [ ], [0], [1], [2],
    [3], [4], [5], [ ], [0], [1], [2], [3],
    [4], [5], [ ], [0], [1], [2], [3], [4],
    [5], [ ], [0], [1], [2], [3], [4], [5],
  )
)
// typstfmt::on

Стоит отметить, что данные в Diag не учитываются при подсчёте R, но данные R
учитываются при подсчёте Diag. То есть исходный RAID-4 массив остаётся
неизменённым и для восстановления при отказе одного диска из исходного массива
можно использовать стандартную процедуру.

== RTP <RTP-how>

Добавим к исходному RAID-4 массиву ещё один диск с диагональными чек-суммами,
только в этот раз с другим разбиением на диагонали: используется формула $(i-j-1) mod p$.
Условимся называть такие диагонали "анти-диагоналями".

// typstfmt::off
#figure(
  caption: [Разбиение на анти-диагонали в RTP при $p=7$],
  table(
    columns: 7,
    [Диск 0], [Диск 1], [Диск 2], [Диск 3], [Диск 4], [Диск 5], [Диск R],
    [6], [0], [1], [2], [3], [4], [5],
    [5], [6], [0], [1], [2], [3], [4],
    [4], [5], [6], [0], [1], [2], [3],
    [3], [4], [5], [6], [0], [1], [2],
    [2], [3], [4], [5], [6], [0], [1],
    [1], [2], [3], [4], [5], [6], [0],
  ),
)
// typstfmt::on

Получаем $p$ анти-диагоналей, чек-суммы для $p-1$ из них записываем в новый
диск, "A-Diag".

// typstfmt::off
#figure(
  caption: [Подсчёт анти-диагональных чек-сумм в RTP при $p=7$],
  table(
    columns: 8,
    [Диск 0], [Диск 1], [Диск 2], [Диск 3], [Диск 4], [Диск 5], [Диск R], [A-Diag],
    [6], [ ], [1], [2], [3], [4], [5], [6],
    [5], [6], [ ], [1], [2], [3], [4], [5],
    [4], [5], [6], [ ], [1], [2], [3], [4],
    [3], [4], [5], [6], [ ], [1], [2], [3],
    [2], [3], [4], [5], [6], [ ], [1], [2],
    [1], [2], [3], [4], [5], [6], [ ], [1],
  ),
)
// typstfmt::on

Стоит отметить, что данные Diag при подсчёте анти-диагональных сумм никак не
учитываются.

= Восстановление стираний

Для удобства введём собирательное название для дисков с данными и диска R: "RAID-4
диски".

== Отказ одного диска <1-failure>

=== Diag/A-Diag

Переподсчёт чек-сумм.

=== Один RAID-4 диск

Можно убрать Diag и A-Diag из рассмотрения и получить, по сути, RAID-4 массив, в
котором произошёл отказ одного диска. Процедура восстановления очевидна.

== Отказ двух дисков

=== Diag и A-Diag

Переподсчёт чек-сумм.

=== Один RAID-4 диск и Diag/A-Diag

Восстанавливаем RAID-4 диск, используя процедуру для RAID-4. Переподсчитываем
Diag/A-Diag.

=== Два RAID-4 диска.

Можно воспользоваться процедурой восстановления для RDP, описанной в @RDP-paper.

TODO: пересказать её здесь.

== Три стирания

=== Один RAID-4 диск, Diag и A-Diag.

Восстанавливаем RAID-4 диск, используя процедуру для RAID-4. Переподсчитываем
Diag и A-Diag.

=== Два RAID-4 диска и A-Diag

Используем процедуру для RDP, переподсчитываем A-Diag.

=== Два RAID-4 диска и Diag

Заметим, что процедуру восстановления для RDP можно использовать и с A-Diag
вместо Diag. Так что сначала воспользуемся ей, а затем переподсчитаем Diag.

=== Три RAID-4 диска

TODO: пересказать

#bibliography("works.bib", full: true, style: "ieee")
