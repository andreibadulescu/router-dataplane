# Router Dataplane

Soluție realizată de Andrei-Marcel Bădulescu, Universitatea Politehnica București <br>
Dată — 16 aprilie 2026

### Procesul de rutare

<p>
Pentru procesul de rutare, am luat fiecare pachet individual, analizand mai intai
continutul header-ului Ethernet. In functie de Ethertype, luam decizia daca
aruncam pachetul sau il procesam ca pachet IP sau ARP.
</p>

<p>
Daca pachetul era procesat ca fiind unul ce respecta Internet Protocol, atunci
analizam header-ul pentru a stabili daca pachetul este invalid, caz in care este
aruncat. Daca acesta trece verificarile (cum ar fi cea pentru campul Checksum),
atunci merg mai departe si analizez campul TTL, unde pot lua urmatoarele decizii:
fie trimit inapoi un mesaj "Time Exceeded" daca pachetul a "expirat" peste ICMP,
fie il procesez mai departe. Pasul urmator este sa verific daca acesta este
adresat router-ului, caz in care procesez header-ul ICMP si raspund folosind
acelasi pachet, modificand doar campurile care trebuie actualizate, cum ar fi
adresele IP, TTL-ul, checksum-urile IP si ICMP, tipul de mesaj ICMP. Daca pachetul
trebuie trimis mai departe, exista posibilitatea sa nu il pot trimite mai departe
catre destinatie, caz in care trimit inapoi un mesaj "Destination Unreachable"
peste ICMP. Daca gasesc o ruta valida, incep sa modific campurile din cadrul
header-ului Ethernet, IP si ICMP. Exista posibilitatea sa nu cunosc adresa MAC a
urmatorului hop, caz in care va trebui sa trimit o cerere ARP pentru a o obtine
si sa pun "de o parte" pachetul care trebuie forwardat pana obtin adresa hardware.
</p>

<p>
Daca pachetul era procesat ca fiind unul ce respecta Address Resolution Protocol,
atunci va trebui sa analizam header-ul pentru a stabili daca este request sau reply.
Daca este request, router-ul va raspunde printr-un mesaj ARP reply cu adresa hardware
a interfetei din care a primit cererea. Daca este reply, atunci router-ul va adauga
adresa MAC a corespondentului in tabela ARP. Dupa ce este procesat mesajul de reply,
sunt verificate pachetele puse in coada pentru a stabili care poate fi trimis dupa
solutionarea cererii ARP.
</p>

### Longest Prefix Match

<p>
Initial pentru algoritmul de Longest Prefix Match am ales sa fac o cautare liniara
pentru a testa celelalte functionalitati. Aceasta itereaza prin tabela de rutare
pana gaseste un entry potrivit.
</p>

<p>
Din dorinta de a invata mai multe, am cautat care sunt structurile folosite pentru a
implementa intr-un scenariu real o astfel de tabela de rutare, care poate contine
sute de mii de intrari. Am ajuns sa citesc despre arbori de tip Radix, care seamana
cu Trie, insa diferenta esentiala dintre ei este ca cei Radix aplica un grad de
compresie pana la urmatoarea intrare, astfel reducand vizibil numarul de noduri
continute in arbore si prin urmare complexitatea temporala la momentul introducerii
si / sau a cautarii. Practic se face o cautare pe arbore in functie de match-ul unui
bit anume din IP-ul destinatie, daca rezultatul operatiei de AND este 0, vom continua
spre stanga, daca rezultatul este 1, vom continua spre dreapta. Acolo va trebui sa
analizam secventa de biti salvata de nod, daca se potriveste si reprezinta o ruta,
atunci il putem retine ca fiind cel mai bun rezultat pana in acel punct. Daca nu
are loc o potrivire, atunci se spune ca "am cazut" din arbore si rezultatul salvat
este rezultatul real. Daca nu avem niciun rezultat salvat atunci nu exista ruta.
</p>

### Despre corectitudinea solutiei

<p>
Ruland checker-ul local, am obtinut punctajul maxim, trecand toate testele (100
puncte). Mai jos voi descrie ce se poate observa in capturile de ecran incluse
in arhiva pentru a ilustra comportamentul router-ului in diverse scenarii.
</p>

### Despre Forwarding

In captura de ecran "subiect1.png", se poate observa cum trimit un mesaj ICMP de tip "Echo" de la masinaria cu adresa 192.168.0.2 (h1) catre 192.168.3.2 (h3). Putem observa in fereastra Wireshark de sus (r0) cum inainte
de fiecare forwardare a pachetului ICMP se face un schimb
de pachete ARP pentru a permite scrierea corecta a antetelor Ethernet. In fereastra de jos este r1. Putem vedea cum request-ul este trimis h0-r0, r0-r1, in fereastra
de jos r1-h3, h3-r1, r1-r0 si inapoi in cea de sus r0-h0.

### Despre ARP

In captura de ecran se vede cum h0 trimite catre r0 un request ARP prin comanda arping, iar r0 raspunde cu un ARP reply. Putem observa cele doua pachete in fereastra Wireshark.

### Despre ICMP

In captura de ecran se vede cum h0 trimite catre r0 un mesaj
de tip ICMP Echo prin comanda ping, iar r0 raspunde cu un
ICMP Echo Reply. Putem observa cele doua pachete in fereastra Wireshark.

<b> Copyright 2026 © Andrei-Marcel Bădulescu. All rights reserved. </b>