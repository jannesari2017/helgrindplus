=== Grundsätzliches ===

== Superblöcke und Grundblöcke ==

In der Informatik bezeichnet man einen Grundblock als einen zusammenhängenden
Codeblock, der keine Kontrollflussänderungen enthält.
D.h., dass innerhalb von Grundblocken keine Sprünge möglich sind.

In Valgrind bestehen Superblöcke aus bis zu 3 "Grundblöcken". Diese "Grundblöcke"
weichen in Details von den strengen Grundblöcken aus der Theorie ab.
Es ist z.B. nicht möglich oder nötig zu garantieren, dass nicht von irgendwo im
Programm mitten in ein "Grundblock" hineingesprungen wird.

Also definieren wir für Valgrind den Grundblock als eine zusammenhängende Menge
an Code, an dessen Ende ein Sprung stattfinden *kann*. Dieser Sprung *darf* an 
eine Bedingung geknüpft sein.

Aus Optimierungsgründen fasst Valgrind bis zu 3 Grundblöcke zu einem Superblock
zusammen.
Es kommt übrigens häufig vor, dass sich der Code in den Superblöcken
überschneiden.

== Der normale Ablauf eines Valgrind-Werkzeugs == 

Valgrind stellt die Basis für dynamische Instrumentierungswerkzeuge dar. Die
Werkzeuge sollen den Code des Gastprogramms ergängen (instrumentieren).
Das Gastprogramm wird dann mit dem Zusatz ausgeführt, wobei die Ergänzung das
Verhalten des Gastprogramm analysiert.
Valgrind instrumentiert das Gastprogramm dabei ebenfalls zur Laufzeit - on demand.

Das Werkzeug muss hauptsächlich eine zentrale Funktion bereitstellen, nämlich
die Instrumentierungsfunktion, welches diese Ergänzungen vollzieht.
Valgrind erleichtert es dem Werkzeugbauer, indem das Gastcode in Häppchen
serviert wird (den Superblöcken) und außerdem in einer architekturunabhängen
Repräsentation, der Immediate Repräsentation.

Weiterhin ist anzumerken, dass ein Superblock nur einmal instrumentiert und
zwischengespeichert wird. Der Superblock kann dann mehrfach ausgeführt werden.
Da eine Codezeile / Prozessorinstuktion in mehreren Superblöcken vorkommen kann,
könnte eine Codezeile mehrfach instrumentiert werden. Daher wird eigentlich von
der Instrumentierungsfunktion *Stationärität* verlangt, d.h. eine Codezeile muss
unabhängig vom Context immer gleich instrumentiert werden.
Wir werden noch sehen, dass unsere Instrumentierungsfunktion die
Stationäritatsbedingung nicht erfüllt.
Dies geschieht teilweise aus Zwang, teilweise aus Performancegründen, da manche
Analysen bereits zur Instrumentierungszeit vollzogen werden und nicht jedes
Mal zur Laufzeit. Vor allem bei Schleifen führt es zu einem Performancegewinn.

Die Instrumentierung geschieht on demand, wenn es unbedingt notwendig ist,
normalerweise spätestens kurz bevor ein Block ausgeführt werden soll.
Umgekehrt heisst das, dass Codeblöcke, die niemals ausgeführt werden auch
niemals intrumentiert werden. Das Werkzeug "sieht" also normalerweise nie das
ganze Programm, was bei unserer Problemstellung zu Schwierigkeiten führt.
Deshalb wurde Valgrind um die Möglichkeit erweitert, beliebinge Codeblöcke zur
Instrumentierung anfordern zu können (VG_(translate_ahead)).

=== Schleifenerkennung ===

== Idee ==

Für bestimmte Fragestellungen ist es nötig, im Programmablauf Schleifen erkennen
zu können. Und dies möglichst schon zur Instrumentierungszeit.
Wir wissen, dass Superblöcke bis zu 3 bedingte Sprünge enthalten können plus ein
"natürlichen" Nachfolgeblock. 
Als Graph dargestellt wären die Superblöcke Knoten und hatten einen maximalen
Ausgangsgrad von 4.
Schleifen erkennt man damit anhand von Kreise in diesem Graphen.
Natürlich sieht die Realität etwas anders aus aufgrund der Codeüberschneidungen
und auf grund der Tatsache, dass nicht der komplette Graph nicht zur Verfügung
steht.
Weiterhin gibt es technische Fragestellungen, wie z.B.
 - Welchen Algorithmus benutzt man für die Suche nach Kreisen?
 - Wie tief soll der Algorithmus suchen?
 - Eine Schleife kann aus mehreren ineinerander verschränkten Kreisen bestehen.
   Wie behandelt man diese?

== Algorithmus ==

Um Scheifen zu finden, wird dynamisch ein Kontrollflussgraph über Superblöcke
aufgebaut. Ein Superblock wird von der Stuktur _SuperBlock dargestellt.
Jeder Superblock hat bis zu 4 Ausgangskanten: Ein next-Zeiger und bis zu drei
branch-Zeiger.
Die Funktion analyse_branches() extrahiert von einem IR-Superblock diese
Referenzen.

Der Algorithmus ist in der Instrumentierungsfunktion hg_instrument() verankert.
Schleifen werden vor dem eigentlichen instrumentieren gesucht. Dies geschieht
mittels Tiefensuche mit beschränkter Suchtiefe n, welches durch den
Kommandozeilenparamater --cfg=n variabel eingestellt werden kann.
Es ist nicht ratsam, n zu groß zu wählen, da für die Kontrollflussanalyse nur
ein begrenzter Speicherplatz zur Verfügung steht und der Speicherverbrauch mit
O(4 ^ n) zunimmt.

Für jeden zu instrumentierenden Superblock sb:
1. Pfad = [ sb ]
2. Schleife = []
3. Schleifendetektor( Schleife, Pfad, sb.next, CFG )
4. Schleifendetektor( Schleife, Pfad, sb.branch1, CFG )
5. Schleifendetektor( Schleife, Pfad, sb.branch2, CFG )
6. Schleifendetektor( Schleife, Pfad, sb.branch3, CFG )
7. Wenn Schleife != []:
7a.    Schleife erkannt!

Schleifendetektor( schleife, pfad, superblock, tiefe ):
1. Wenn superblock == pfad[0]:
	   // Schleife erkannt
1a.    schleife = schleife + pfad // Konkatenierung
1b.    return
2. Wenn superblock überschneidet sich mit pfad[0] :
       // Schleife mit Initialisierung erkannt
2a.    schleife = schleife + superblock + pfad[1:]
							// Erster Superblock des Pfades wird ersetzt
2b.    return
3. Wenn superblock nicht analysiert:
3a.    Analysiere Kontrollfluss von superblock 
4. Wenn tiefe > 0:
4a.    neuer_pfad = pfad + [ superblock ]
4b.    Schleifendetektor( schleife, neuer_pfad, superblock.next, tiefe-1 )
4c.    Schleifendetektor( schleife, neuer_pfad, superblock.branch1, tiefe-1 )
4d.    Schleifendetektor( schleife, neuer_pfad, superblock.branch2, tiefe-1 )
4e.    Schleifendetektor( schleife, neuer_pfad, superblock.branch3, tiefe-1 )

Der Algorithmus versucht Schleifen zu finden, die zurück zum Wurzel-Superblock
führen.
Weil sich Superblöcke überschneiden können, also gemeinsame Codezeilen
haben, ist es nicht ausreichend, die Identität von Wurzel und Kindknoten zu
überprüfen. Stattdessen wird ein Pfad als Schleife angesehen, sobald Wurzel und 
Kindknoten mindestens eine Assemblercodezeile gemeinsam haben.
Dadurch lassen sich alle die Fälle behandeln, bei denen der Schleifenrücksprung
in die Mitte des Wurzelknotens führt anstatt genau zum Anfang. In dem Fall wird
nicht der Wurzelsuperblock zur Schleife gezählt. Stattdessen zählt der
Superblock, der am Sprungziel beginnt.
Somit werden Schleifeninitialisierung außen vor gelassen. 

= Kontrollflussänderungen innerhalb der Schleife =

Weiterhin ist zu Bemerken, dass der Algorithmus die Schleife als eine Menge
von Knoten zurück gibt, welches einen Pfad darstellt.
Sobald ein Rücksprung zum Wurzelknoten erkannt wird, so wird der komplette Pfad
zur Knotenliste "Schleife" konkateniert.
Wenn es mehrere mögliche Rücksprünge gibt, dann enthält die Knotenliste
"Schleife" mehrere Pfade hintereinander. Dies geschieht, wenn innerhalb des
Schleifenrumpfes Kontrollflußänderungen stattfinden, wie folgendes Beispiel:

[1] - [2] - [3] - [4] - [1] 
    \ [5] - [6] /
          \ [7] /

Die Schleife wird dann folgendermaßen repräsentiert:

[1] - [2] - [3] - [4] - [1] - [5] - [6] - [4] - [1] - [5] - [7] - [4] 

Interpretiert man das, dann ist es so, als würde die Schleife mehrmals
ausgeführt werden und jedes Mal einen anderen Kontrollfluss nehmen.

= Funktionsaufrufe =

Der next-Zeiger eines Superblocks kann unter Umständen auch ein Rücksprung
am Ende einer Funktion repräsentieren.
Aus diesem Grund muss während der Tiefensuche parallel ein
Rücksprungadressen-Stack aufgebaut werden.
Der Stack wird befüllt, wenn in einem Superblock ein Funktionsaufruf erkannt
wurde. Das Erkennen geschieht zum einen über eindeutige Assemblerbefehle wie
"call", welche im IR mittels jumpkind == Ijk_Call zu erkennen ist. Zum anderen
überprüft eine Heuristik, ob eine potentielle Rücksprungadresse in den
(realen) Stack geschrieben wird. Ist dies der Fall, so wird ein Funktions-
aufruf erkannt.

Aus Übersichtlichkeit wurde dieser Teil im Pseudo-Algorithmus ausgelassen.

= Unvorhersagbare Funktionsaufrufe =

Wenn im Code Funktionszeiger benutzt werden, was bei C++ implizit bei 
virtual geschieht, so lässt sich zur Instrumentierungszeit kaum ein Aussage über
die Ausgangsknoten eines Superblocks treffen.

Stattdessen muss gewartet werden, bis ein solcher "Unvorhersagbarer Sprung"
tatsächlich zur Laufzeit ausgeführt wird und muss zurückverfolgt werden.

Daher wir jeder Anfang eines Superblocks instrumentiert. So kann Pro Thread
aufgezeichnet werden, in welchem Superblock er sich gerade befindet.
Wenn ein Superblockwechsel zur Laufzeit stattfindet, so wird überprüft,
ob der vergangene Superblock mit einem unvorhersehbaren Sprung beendet wurde.
Vom alten wird die Referenz auf den neuen Superblock aktualisiert.
Die neu entstandene Referenz könnte das fehlende Glied einer Schleife darstellen,
weshalb der alte Superblock invalidiert wird. D.h. Vom alten Superblock aus-
gehend wird erneut nach Schleifen gesucht und neu instrumentiert.

=== Spinread Erkennung ===

Um spinning reads zu erkennen, werden zunächst Schleifen ausfindig gemacht.
In den Schleifen wird dann eine Datenabhängigkeitsanalyse erstellt.  

Wenn die Schleifenbedigung folgende Punkte erfüllt, so wird die Schleife als
spin-Schleife erkannt:
1. Die Bedingung hängt mindestens von einem Speicherlesezugriff ab.
2. Dieser Speicherzugriff findet auf eine konstakte Adresse statt.
3. Alle sonstigen Datenabhängigkeiten dürfen während der Schleife nicht
modifiziert worden sein. 

== Datenabhängigkeitsanalyse ==

Die Datenabhängigkeitsanalyse wird über Variablen berechnet.
In einem IR-Superblock werden Temps, Register, Konstanten, Ladeinstruktionen
und Sprungbedingungen als Variablen betrachtet.

Datenabhängigkeiten von Temps werden nur temporär gespeichert. Sie dienen nur 
als Zwischenspeicher, da sie über Superblockgrenzen hinweg nicht gespeichert
werden.

Alle Konstanten werden prinzipiell als Speicheradressen interpretiert.

Ladeinstruktionen werden durch die Instruktionsadresse, bei dem der Speicher-
zugriff stattfindet, charakterisiert.
Eine Ladeinstruktion hängt von allen Variablen ab, die zur Berechnung der
Speicheradresse beigetragen haben.

Die Sprungbedingung entspricht der Schleifenbedingung. Sämtliche Datenabhängig-
keiten der Bedingungen von Sprüngen innerhalb der Schleife werden vereinigt
in eine virtuelle "Schleifenbedigungsvariable".

=== Beispiele ===

= Beispielschleife 1: Einfacher Spinread =

Hier ist ein Beispiel einer Spin-Schleife, welche auf das Setzen eines Signals 
über eine Variable wartet:

...
  while (spinlock != 0) {
    sched_yield();
  }
...

Der Assemblercode für x86_64-Architektur sieht wie folgt aus:

...
  400718:       eb 05                   jmp    40071f <main+0x34>
  40071a:       e8 81 fe ff ff          callq  4005a0 <sched_yield@plt>
  40071f:       8b 05 1b 09 20 00       mov    0x20091b(%rip),%eax        # 601040 <spinlock>
  400725:       85 c0                   test   %eax,%eax
  400727:       75 f1                   jne    40071a <main+0x2f>
...

Der Code wird intern in Valgrind durch 3 Superblöcke repräsentiert.

Der erste Superblock beginnt an der Adresse 0x400718. Der dortige Sprung wird
direkt verfolgt und der Code am Sprungziel wird in ein Superblock vereinigt.

IRSB {
   t0:I64   t1:I32   t2:I32   t3:I32   t4:I64   t5:I32   t6:I64   t7:I1
   t8:I64   t9:I64   t10:I64   t11:I64   t12:I64   t13:I64   t14:I64   t15:I64
   t16:I1   t17:I64   t18:I1   

   ------ IMark(0x400718, 2) ------
   ------ IMark(0x40071F, 6) ------
   PUT(168) = 0x40071F:I64
   t5 = LDle:I32(0x601040:I64)
   t13 = 32Uto64(t5)
   t4 = t13
   PUT(0) = t4
   ------ IMark(0x400725, 2) ------
   t3 = GET:I32(0)
   PUT(128) = 0x13:I64
   t14 = 32Uto64(t3)
   t6 = t14
   PUT(136) = t6
   PUT(144) = 0x0:I64
   ------ IMark(0x400727, 2) ------
   PUT(168) = 0x400727:I64
   IR-NoOp
   t17 = Shl64(t6,0x20:I8)
   t16 = CmpEQ64(t17,0x0:I64)
   t15 = 1Uto64(t16)
   t12 = t15
   t18 = 64to1(t12)
   t7 = t18
   if (t7) goto {Boring} 0x400729:I64
   goto {Boring} 0x40071A:I64
}

Der zweite Superblock an Adresse 0x40071a enthält einen Funktionsaufruf
an eine dynamisch geladene Bibliothek.

IRSB {
   t0:I64   t1:I64   t2:I32   t3:I64   t4:I64   t5:I64   t6:I64   t7:I64


   ------ IMark(0x40071A, 5) ------
   t6 = GET:I64(32)
   t5 = Sub64(t6,0x8:I64)
   PUT(32) = t5
   STle(t5) = 0x40071F:I64
   t7 = Sub64(t5,0x80:I64)
   ====== AbiHint(t7, 128, 0x4005A0:I64) ======
   ------ IMark(0x4005A0, 6) ------
   PUT(168) = 0x4005A0:I64
   t4 = LDle:I64(0x601010:I64)
   goto {Boring} t4
}

Der dritte Superblock bei 0x40071f ist fast identisch zu der an 0x400718.
Es fehlt nur der Sprung gleich am Anfang.
Hier sieht man, dass der Code zwischen 0x40071f und 0x400727 in zwei
verschiedenen Superblöcken vorkommt - dupliziert wird.

IRSB {
   t0:I64   t1:I32   t2:I32   t3:I32   t4:I64   t5:I32   t6:I64   t7:I1
   t8:I64   t9:I64   t10:I64   t11:I64   t12:I64   t13:I64   t14:I64   t15:I64
   t16:I1   t17:I64   t18:I1   

   ------ IMark(0x40071F, 6) ------
   t5 = LDle:I32(0x601040:I64)
   t13 = 32Uto64(t5)
   t4 = t13
   PUT(0) = t4
   ------ IMark(0x400725, 2) ------
   t3 = GET:I32(0)
   PUT(128) = 0x13:I64
   t14 = 32Uto64(t3)
   t6 = t14
   PUT(136) = t6
   PUT(144) = 0x0:I64
   ------ IMark(0x400727, 2) ------
   PUT(168) = 0x400727:I64
   IR-NoOp
   t17 = Shl64(t6,0x20:I8)
   t16 = CmpEQ64(t17,0x0:I64)
   t15 = 1Uto64(t16)
   t12 = t15
   t18 = 64to1(t12)
   t7 = t18
   if (t7) goto {Boring} 0x400729:I64
   goto {Boring} 0x40071A:I64
}

= Spinread Erkennung an Beispielschleife 1 =

Ausschnitt aus der Ausgabe mit --verbose-cfg:

...
 0x404A4BF98 : 0x40071f -> 0x40071a -> []
<main> (spinlock.c:22)
    varmap {
        == 0x0 0x20 load@(0x40071f) 
       r:0  == load@(0x40071f) 
       r:32  == r:32 0x8 0x40071f 
       r:128  == 0x13 
       r:136  == r:0 
       r:144  == 0x0 
       r:168  == 0x4005a0 
       0x0  == 
       0x8  == 0x40071f 
       0x13  == 
       0x20  == 
       0x80  == 
       0x4005a0  == 
       0x40071f  == 
       0x400727  == 
       0x601010  == 
       0x601040  == 
       load@(0x4005a0)  == 0x601010 
       load@(0x40071f)  == 0x601040 
    }
************spin found in /home/biin/workspace/spinlock/spinlock/spinlock64
...

Der Schleifenerkenner gibt zunächst aus, dass eine Schleife mit den Superblöcken
0x40071f und 0x40071a erkannt wurde.
Für die Spinread Erkennung berechnet Helgrind+ die Datenabhängigkeiten der 
Bedingungen aller (bedingten) Sprünge innerhalb der Schleife.
In diesem Fall gab es nur einen bedingten Sprung innerhalb der Schleife und
sie hängt von 0x0, 0x20 und einem Speicherlesezugriff bei Instruktionsadresse
0x40071f ab (load@(0x40071f)).
Die Konstanten 0x0 und 0x20 wurden während der Schleife nicht verändert.
Der Speicherzugriff bei Instruktionsadresse 0x40071f hängt ebenfalls von 
der Konstante 0x601040 ab.

Insgesamt hängt die Bedigung also von mindestens einer Ladeoperation ab, die
konstant ist und alle Abhängigkeiten werden während der Schleife nicht verändert.
Somit wird ein spinning read erkannt - an Instruktionsadresse 0x40071f.

= Instrumentierte Beispielschleife 1 =

Jeder Superblock wird zu Beginn mit der Funktion set_current_SB_and_UC() instrumentiert.
Mit Hilfe dieser Funktion lassen sich die unpredictable function calls verfolgen.

Außerdem wurde an der Assemblercodeadresse 0x40071f ein spin read erkannt.
Die betreffende Instruktion wird mit start/stop_spin_reading umklammert.
Die memory state machine (MSM) kann bei jedem read prüfen, ob gerade ein spinning read
oder ein normaler read stattfindet.

IRSB {
   t0:I64   t1:I32   t2:I32   t3:I32   t4:I64   t5:I32   t6:I64   t7:I1
   t8:I64   t9:I64   t10:I64   t11:I64   t12:I64   t13:I64   t14:I64   t15:I64
   t16:I1   t17:I64   t18:I1   

   DIRTY 1:I1 ::: set_current_SB_and_UC[rp=1]{0x380066e0}(0x404B015E0:I64)
   ------ IMark(0x400718, 2) ------
   DIRTY 1:I1 ::: start_spin_reading{0x380054b0}()
   ------ IMark(0x40071F, 6) ------
   PUT(168) = 0x40071F:I64
   DIRTY 1:I1 ::: evh__mem_help_read_4[rp=1]{0x38010440}(0x601040:I64)
   t5 = LDle:I32(0x601040:I64)
   t13 = 32Uto64(t5)
   t4 = t13
   PUT(0) = t4
   DIRTY 1:I1 ::: stop_spin_reading{0x380054c0}()
   ------ IMark(0x400725, 2) ------
   t3 = GET:I32(0)
   PUT(128) = 0x13:I64
   t14 = 32Uto64(t3)
   t6 = t14
   PUT(136) = t6
   PUT(144) = 0x0:I64
   ------ IMark(0x400727, 2) ------
   PUT(168) = 0x400727:I64
   IR-NoOp
   t17 = Shl64(t6,0x20:I8)
   t16 = CmpEQ64(t17,0x0:I64)
   t15 = 1Uto64(t16)
   t12 = t15
   t18 = 64to1(t12)
   t7 = t18
   if (t7) goto {Boring} 0x400729:I64
   goto {Boring} 0x40071A:I64
}

IRSB {
   t0:I64   t1:I32   t2:I32   t3:I32   t4:I64   t5:I32   t6:I64   t7:I1
   t8:I64   t9:I64   t10:I64   t11:I64   t12:I64   t13:I64   t14:I64   t15:I64
   t16:I1   t17:I64   t18:I1   

   DIRTY 1:I1 ::: set_current_SB_and_UC[rp=1]{0x380066e0}(0x404BBEE10:I64)
   DIRTY 1:I1 ::: start_spin_reading{0x380054b0}()
   ------ IMark(0x40071F, 6) ------
   DIRTY 1:I1 ::: evh__mem_help_read_4[rp=1]{0x38010440}(0x601040:I64)
   t5 = LDle:I32(0x601040:I64)
   t13 = 32Uto64(t5)
   t4 = t13
   PUT(0) = t4
   DIRTY 1:I1 ::: stop_spin_reading{0x380054c0}()
   ------ IMark(0x400725, 2) ------
   t3 = GET:I32(0)
   PUT(128) = 0x13:I64
   t14 = 32Uto64(t3)
   t6 = t14
   PUT(136) = t6
   PUT(144) = 0x0:I64
   ------ IMark(0x400727, 2) ------
   PUT(168) = 0x400727:I64
   IR-NoOp
   t17 = Shl64(t6,0x20:I8)
   t16 = CmpEQ64(t17,0x0:I64)
   t15 = 1Uto64(t16)
   t12 = t15
   t18 = 64to1(t12)
   t7 = t18
   if (t7) goto {Boring} 0x400729:I64
   goto {Boring} 0x40071A:I64
}

= Beispielschleife 2: Unpredictable function calls =

Funktionszeiger, welche vor allem in C++ verwendet werden (siehe Schlüsselwort "virtual"),
stellen ein Problem dar, weil sie Adresse der aufgerufenen Funktion nahezu unmöglich
zum Instrumentierungszeit bestimmt werden kann.

int test(void) {
  return (spinlock != 0);
}

in main():
...
int (*test_func) (void) = &test;

while (test_func()) {
sched_yield();
}
...

Es folgt der Code in Assembler.
Bei Adresse 0x400749 wird der Inhalt einer lokale Variable in den Akkumulator %rax geladen.
Daraufhin wird bei 0x40074d %rax als Funktionszeigeradresse interpretiert und es findet
ein Aufruf statt.
Es ist in der Regel nicht möglich, den Funktionszeiger genau zu bestimmen, da dieser sich eventuell
auf dem Heap befinden könnte.
In diesem einfachen Beispiel "sieht" man, dass der Inhalt des Funktionszeigers bei Adresse 
0x40073a beschrieben wird.

test():
  4006eb:       55                      push   %rbp
  4006ec:       48 89 e5                mov    %rsp,%rbp
  4006ef:       8b 05 4b 09 20 00       mov    0x20094b(%rip),%eax        # 601040 <spinlock>
  4006f5:       8b 05 45 09 20 00       mov    0x200945(%rip),%eax        # 601040 <spinlock>
  4006fb:       85 c0                   test   %eax,%eax
  4006fd:       0f 95 c0                setne  %al
  400700:       0f b6 c0                movzbl %al,%eax
  400703:       c9                      leaveq 
  400704:       c3                      retq   

main():
  ...
  40073a:       48 c7 45 f8 eb 06 40    movq   $0x4006eb,-0x8(%rbp)
  400741:       00 
  400742:       eb 05                   jmp    400749 <main+0x44>
  400744:       e8 57 fe ff ff          callq  4005a0 <sched_yield@plt>
  400749:       48 8b 45 f8             mov    -0x8(%rbp),%rax
  40074d:       ff d0                   callq  *%rax
  40074f:       85 c0                   test   %eax,%eax
  400751:       75 f1                   jne    400744 <main+0x3f>
  ...

In Valgrind fängt bei 0x40073a ein Superblock an:

IRSB {
   t0:I64   t1:I64   t2:I32   t3:I64   t4:I64   t5:I64   t6:I64   t7:I64
   t8:I64   t9:I64   t10:I64   t11:I64   t12:I64   

   ------ IMark(0x40073A, 8) ------
   t6 = GET:I64(40)
   t5 = Add64(t6,0xFFFFFFFFFFFFFFF8:I64)
   STle(t5) = 0x4006EB:I64
   ------ IMark(0x400742, 2) ------
   ------ IMark(0x400749, 4) ------
   PUT(168) = 0x400749:I64
   t7 = Add64(t6,0xFFFFFFFFFFFFFFF8:I64)
   t9 = LDle:I64(t7)
   PUT(0) = t9
   ------ IMark(0x40074D, 2) ------
   PUT(168) = 0x40074D:I64
   IR-NoOp
   t11 = GET:I64(32)
   t10 = Sub64(t11,0x8:I64)
   PUT(32) = t10
   STle(t10) = 0x40074F:I64
   t12 = Sub64(t10,0x80:I64)
   ====== AbiHint(t12, 128, t9) ======
   goto {Call} t9
}

Nach Beendigung des Funktionsaufrufs wird der Superblock ab 0x40074f
ausgeführt:

IRSB {
   t0:I32   t1:I32   t2:I32   t3:I64   t4:I1   t5:I64   t6:I64   t7:I64
   t8:I64   t9:I64   t10:I64   t11:I64   t12:I1   t13:I64   t14:I1   
   ------ IMark(0x40074F, 2) ------
   t2 = GET:I32(0)
   PUT(128) = 0x13:I64
   t10 = 32Uto64(t2)
   t3 = t10
   PUT(136) = t3
   PUT(144) = 0x0:I64
   ------ IMark(0x400751, 2) ------
   PUT(168) = 0x400751:I64
   IR-NoOp
   t13 = Shl64(t3,0x20:I8)
   t12 = CmpEQ64(t13,0x0:I64)
   t11 = 1Uto64(t12)
   t9 = t11
   t14 = 64to1(t9)
   t4 = t14
   if (t4) goto {Boring} 0x400753:I64
   goto {Boring} 0x400744:I64
}

Die Funktion test() selbst passt komplett in einen einzigen Superblock:

IRSB {
   t0:I64   t1:I64   t2:I64   t3:I64   t4:I32   t5:I32   t6:I32   t7:I8
   t8:I64   t9:I64   t10:I64   t11:I64   t12:I64   t13:I64   t14:I64   t15:I64
   t16:I64   t17:I32   t18:I64   t19:I32   t20:I64   t21:I8   t22:I1   t23:I64
   t24:I64   t25:I64   t26:I64   t27:I64   t28:I64   t29:I32   t30:I8   t31:I64
   t32:I64   t33:I64   t34:I64   t35:I64   t36:I1   t37:I64   t38:I1   t39:I8
   t40:I32   t41:I64   

   ------ IMark(0x4006EB, 1) ------
   t0 = GET:I64(40)
   t14 = GET:I64(32)
   t13 = Sub64(t14,0x8:I64)
   PUT(32) = t13
   STle(t13) = t0
   ------ IMark(0x4006EC, 3) ------
   PUT(40) = t13
   ------ IMark(0x4006EF, 6) ------
   PUT(168) = 0x4006EF:I64
   IR-NoOp
   IR-NoOp
   ------ IMark(0x4006F5, 6) ------
   PUT(168) = 0x4006F5:I64
   t19 = LDle:I32(0x601040:I64)
   t33 = 32Uto64(t19)
   t18 = t33
   PUT(0) = t18
   ------ IMark(0x4006FB, 2) ------
   t6 = GET:I32(0)
   PUT(128) = 0x13:I64
   t34 = 32Uto64(t6)
   t20 = t34
   PUT(136) = t20
   PUT(144) = 0x0:I64
   ------ IMark(0x4006FD, 3) ------
   IR-NoOp
   t37 = Shl64(t20,0x20:I8)
   t36 = CmpNE64(t37,0x0:I64)
   t35 = 1Uto64(t36)
   t27 = t35
   t38 = 64to1(t27)
   t22 = t38
   t39 = 1Uto8(t22)
   t21 = t39
   PUT(0) = t21
   ------ IMark(0x400700, 3) ------
   t40 = 8Uto32(t21)
   t29 = t40
   t41 = 32Uto64(t29)
   t28 = t41
   PUT(0) = t28
   ------ IMark(0x400703, 1) ------
   PUT(168) = 0x400703:I64
   PUT(32) = t13
   t9 = LDle:I64(t13)
   PUT(40) = t9
   t31 = Add64(t13,0x8:I64)
   PUT(32) = t31
   ------ IMark(0x400704, 1) ------
   PUT(168) = 0x400704:I64
   t11 = LDle:I64(t31)
   t12 = Add64(t31,0x8:I64)
   PUT(32) = t12
   t32 = Sub64(t12,0x80:I64)
   ====== AbiHint(t32, 128, t11) ======
   goto {Return} t11
}

= Instrumentierte Beispielschleife 2 =

Der Algorithmus erkennt sowohl das Laden des Funktionszeigers bei Adresse 0x400749
als auch den tatsächlichen spinning read in der test()-Funktion bei Adresse
0x4006f5 als spinning reads.
Das ist der Fall, da die Adressen der beide Ladeoperationen in der Schleife
als nicht modifiziert erkannt wurden.

IRSB {
   t0:I64   t1:I64   t2:I32   t3:I64   t4:I64   t5:I64   t6:I64   t7:I64
   t8:I64   t9:I64   t10:I64   t11:I64   t12:I64   

   DIRTY 1:I1 ::: set_current_SB_and_UC[rp=1]{0x380066e0}(0x404B52CB0:I64)
   ------ IMark(0x40073A, 8) ------
   t6 = GET:I64(40)
   t5 = Add64(t6,0xFFFFFFFFFFFFFFF8:I64)
   DIRTY 1:I1 ::: evh__mem_help_write_8[rp=1]{0x380106c0}(t5)
   STle(t5) = 0x4006EB:I64
   ------ IMark(0x400742, 2) ------
   DIRTY 1:I1 ::: start_spin_reading{0x380054b0}()
   ------ IMark(0x400749, 4) ------
   PUT(168) = 0x400749:I64
   t7 = Add64(t6,0xFFFFFFFFFFFFFFF8:I64)
   DIRTY 1:I1 ::: evh__mem_help_read_8[rp=1]{0x380103a0}(t7)
   t9 = LDle:I64(t7)
   PUT(0) = t9
   DIRTY 1:I1 ::: stop_spin_reading{0x380054c0}()
   ------ IMark(0x40074D, 2) ------
   PUT(168) = 0x40074D:I64
   IR-NoOp
   t11 = GET:I64(32)
   t10 = Sub64(t11,0x8:I64)
   PUT(32) = t10
   DIRTY 1:I1 ::: evh__mem_help_write_8[rp=1]{0x380106c0}(t10)
   STle(t10) = 0x40074F:I64
   t12 = Sub64(t10,0x80:I64)
   ====== AbiHint(t12, 128, t9) ======
   goto {Call} t9
}

IRSB {
   t0:I64   t1:I64   t2:I64   t3:I64   t4:I32   t5:I32   t6:I32   t7:I8
   t8:I64   t9:I64   t10:I64   t11:I64   t12:I64   t13:I64   t14:I64   t15:I64
   t16:I64   t17:I32   t18:I64   t19:I32   t20:I64   t21:I8   t22:I1   t23:I64
   t24:I64   t25:I64   t26:I64   t27:I64   t28:I64   t29:I32   t30:I8   t31:I64
   t32:I64   t33:I64   t34:I64   t35:I64   t36:I1   t37:I64   t38:I1   t39:I8
   t40:I32   t41:I64   

   DIRTY 1:I1 ::: set_current_SB_and_UC[rp=1]{0x380066e0}(0x404C44AD8:I64)
   ------ IMark(0x4006EB, 1) ------
   t0 = GET:I64(40)
   t14 = GET:I64(32)
   t13 = Sub64(t14,0x8:I64)
   PUT(32) = t13
   DIRTY 1:I1 ::: evh__mem_help_write_8[rp=1]{0x380106c0}(t13)
   STle(t13) = t0
   ------ IMark(0x4006EC, 3) ------
   PUT(40) = t13
   ------ IMark(0x4006EF, 6) ------
   PUT(168) = 0x4006EF:I64
   IR-NoOp
   IR-NoOp
   DIRTY 1:I1 ::: start_spin_reading{0x380054b0}()
   ------ IMark(0x4006F5, 6) ------
   PUT(168) = 0x4006F5:I64
   DIRTY 1:I1 ::: evh__mem_help_read_4[rp=1]{0x38010440}(0x601040:I64)
   t19 = LDle:I32(0x601040:I64)
   t33 = 32Uto64(t19)
   t18 = t33
   PUT(0) = t18
   DIRTY 1:I1 ::: stop_spin_reading{0x380054c0}()
   ------ IMark(0x4006FB, 2) ------
   t6 = GET:I32(0)
   PUT(128) = 0x13:I64
   t34 = 32Uto64(t6)
   t20 = t34
   PUT(136) = t20
   PUT(144) = 0x0:I64
   ------ IMark(0x4006FD, 3) ------
   IR-NoOp
   t37 = Shl64(t20,0x20:I8)
   t36 = CmpNE64(t37,0x0:I64)
   t35 = 1Uto64(t36)
   t27 = t35
   t38 = 64to1(t27)
   t22 = t38
   t39 = 1Uto8(t22)
   t21 = t39
   PUT(0) = t21
   ------ IMark(0x400700, 3) ------
   t40 = 8Uto32(t21)
   t29 = t40
   t41 = 32Uto64(t29)
   t28 = t41
   PUT(0) = t28
   ------ IMark(0x400703, 1) ------
   PUT(168) = 0x400703:I64
   PUT(32) = t13
   DIRTY 1:I1 ::: evh__mem_help_read_8[rp=1]{0x380103a0}(t13)
   t9 = LDle:I64(t13)
   PUT(40) = t9
   t31 = Add64(t13,0x8:I64)
   PUT(32) = t31
   ------ IMark(0x400704, 1) ------
   PUT(168) = 0x400704:I64
   DIRTY 1:I1 ::: evh__mem_help_read_8[rp=1]{0x380103a0}(t31)
   t11 = LDle:I64(t31)
   t12 = Add64(t31,0x8:I64)
   PUT(32) = t12
   t32 = Sub64(t12,0x80:I64)
   ====== AbiHint(t32, 128, t11) ======
   goto {Return} t11
}

= Beispielschleife 3: Lost Signal Detector =

Ziel des LSD ist es, jede Schleife zu instrumentieren, welches einen Aufruf
an pthread_cond_wait enthält.

...
  pthread_mutex_lock(&MU);
  while (COND == 1) {
     pthread_cond_wait(&CV, &MU);
  }
  pthread_mutex_unlock(&MU);
...

In Assembler sieht dies folgendermaßen aus:

  400658:       bf c0 10 60 00          mov    $0x6010c0,%edi
  40065d:       e8 ee fe ff ff          callq  400550 <pthread_mutex_lock@plt>
  400662:       eb 0f                   jmp    400673 <main+0x1f>
  400664:       be c0 10 60 00          mov    $0x6010c0,%esi
  400669:       bf 80 10 60 00          mov    $0x601080,%edi
  40066e:       e8 cd fe ff ff          callq  400540 <pthread_cond_wait@plt>
  400673:       8b 05 e7 09 20 00       mov    0x2009e7(%rip),%eax        # 601060 <COND>
  400679:       83 f8 01                cmp    $0x1,%eax
  40067c:       74 e6                   je     400664 <main+0x10>
  40067e:       bf c0 10 60 00          mov    $0x6010c0,%edi
  400683:       e8 d8 fe ff ff          callq  400560 <pthread_mutex_unlock@plt>

Da bei 0x40065d ein dynamischer Funktionsaufruf stattfindet, beginnt bei
0x400662 ein neuer Superblock:

IRSB {
   t0:I64   t1:I32   t2:I32   t3:I32   t4:I64   t5:I32   t6:I64   t7:I64
   t8:I1   t9:I64   t10:I64   t11:I64   t12:I64   t13:I64   t14:I64   t15:I64
   t16:I64   t17:I1   t18:I64   t19:I64   t20:I1   

   ------ IMark(0x400662, 2) ------
   ------ IMark(0x400673, 6) ------
   PUT(168) = 0x400673:I64
   t5 = LDle:I32(0x601060:I64)
   t14 = 32Uto64(t5)
   t4 = t14
   PUT(0) = t4
   ------ IMark(0x400679, 3) ------
   t3 = GET:I32(0)
   IR-NoOp
   PUT(128) = 0x7:I64
   t15 = 32Uto64(t3)
   t6 = t15
   PUT(136) = t6
   PUT(144) = 0x1:I64
   ------ IMark(0x40067C, 2) ------
   PUT(168) = 0x40067C:I64
   IR-NoOp
   t18 = Shl64(0x1:I64,0x20:I8)
   t19 = Shl64(t6,0x20:I8)
   t17 = CmpEQ64(t19,t18)
   t16 = 1Uto64(t17)
   t13 = t16
   t20 = 64to1(t13)
   t8 = t20
   if (t8) goto {Boring} 0x400664:I64
   goto {Boring} 0x40067E:I64
}

Der Funktionsrumpf bei 0x400664 wird übersprungen und sieht folgendermaßen aus:

IRSB {
   t0:I64   t1:I64   t2:I32   t3:I64   t4:I64   t5:I64   t6:I64   t7:I64
   t8:I64   t9:I64   

   ------ IMark(0x400664, 5) ------
   PUT(48) = 0x6010C0:I64
   ------ IMark(0x400669, 5) ------
   PUT(56) = 0x601080:I64
   ------ IMark(0x40066E, 5) ------
   PUT(168) = 0x40066E:I64
   t8 = GET:I64(32)
   t7 = Sub64(t8,0x8:I64)
   PUT(32) = t7
   STle(t7) = 0x400673:I64
   t9 = Sub64(t7,0x80:I64)
   ====== AbiHint(t9, 128, 0x400540:I64) ======
   ------ IMark(0x400540, 6) ------
   PUT(168) = 0x400540:I64
   t4 = LDle:I64(0x601008:I64)
   goto {Boring} t4
}

Durch eine Erweiterung in Valgrind lässt sich die Adresse 0x601008 mit der
dynamischen Funktion pthread_cond_wait() assoziieren.
Der Lost Signal Detector erkennt die Schleife und instrumentiert den Superblock
0x400662 wie folgt:

IRSB {
   t0:I64   t1:I32   t2:I32   t3:I32   t4:I64   t5:I32   t6:I64   t7:I64
   t8:I1   t9:I64   t10:I64   t11:I64   t12:I64   t13:I64   t14:I64   t15:I64
   t16:I64   t17:I1   t18:I64   t19:I64   t20:I1   t21:I64   t22:I64   t23:I64
   t24:I64   t25:I64   t26:I64   t27:I64   t28:I64   t29:I64   

   ------ IMark(0x400662, 2) ------
   ------ IMark(0x400673, 6) ------
   PUT(168) = 0x400673:I64
   DIRTY 1:I1 ::: evh__mem_help_read_4[rp=1]{0x38010440}(0x601060:I64)
   t5 = LDle:I32(0x601060:I64)
   t14 = 32Uto64(t5)
   t4 = t14
   PUT(0) = t4
   ------ IMark(0x400679, 3) ------
   t3 = GET:I32(0)
   IR-NoOp
   PUT(128) = 0x7:I64
   t15 = 32Uto64(t3)
   t6 = t15
   PUT(136) = t6
   PUT(144) = 0x1:I64
   ------ IMark(0x40067C, 2) ------
   PUT(168) = 0x40067C:I64
   IR-NoOp
   t18 = Shl64(0x1:I64,0x20:I8)
   t19 = Shl64(t6,0x20:I8)
   t17 = CmpEQ64(t19,t18)
   t16 = 1Uto64(t17)
   t13 = t16
   t20 = 64to1(t13)
   t8 = t20
   t21 = GET:I64(48)
   PUT(48) = 0x6010C0:I64
   t22 = GET:I64(56)
   PUT(56) = 0x601080:I64
   t23 = GET:I64(168)
   PUT(168) = 0x40066E:I64
   t24 = GET:I64(32)
   t25 = Sub64(t24,0x8:I64)
   t26 = Sub64(t25,0x80:I64)
   PUT(168) = 0x400540:I64
   t27 = DIRTY 1:I1 ::: safe_load_8[rp=1]{0x38003b30}(0x601008:I64)
   t28 = GET:I64(56)
   t29 = GET:I64(48)
   PUT(48) = t21
   PUT(56) = t22
   PUT(168) = t23
   DIRTY 1:I1 ::: evh__waiting_loop[rp=2]{0x380027c0}(t28,t29)
   if (t8) goto {Boring} 0x400664:I64
   goto {Boring} 0x40067E:I64
}

Zwischen den Zeilen "t8 = t20" und "if (t8) goto {Boring} 0x400664:I64"
wird ein Teil des Schleifenrumpfes zur Laufzeit simuliert, um die
Parameter der while-Schleife zu ermitteln.

