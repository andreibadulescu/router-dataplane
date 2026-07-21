# Router Dataplane – EN 🇬🇧

Solution by Andrei-Marcel Bădulescu, Politehnica University of Bucharest <br>
Date — April 16, 2026

### The Routing Process

<p>
For the routing process, I took each individual packet, first analyzing
the contents of the Ethernet header. Depending on the Ethertype, we decide
whether to drop the packet or process it as an IP or ARP packet.
</p>

<p>
If the packet was processed as one respecting the Internet Protocol, then
I analyzed the header to determine whether the packet is invalid, in which
case it is dropped. If it passes the checks (such as the one for the
Checksum field), then I move forward and analyze the TTL field, where I can
make the following decisions: either send back a "Time Exceeded" message if
the packet has "expired" over ICMP, or process it further. The next step is
to check whether it is addressed to the router, in which case I process the
ICMP header and respond using the same packet, only modifying the fields
that need to be updated, such as the IP addresses, the TTL, the IP and ICMP
checksums, and the ICMP message type. If the packet needs to be forwarded,
there is a possibility that I cannot send it further to the destination, in
which case I send back a "Destination Unreachable" message over ICMP. If I
find a valid route, I begin modifying the fields within the Ethernet, IP,
and ICMP headers. There is a possibility that I do not know the MAC address
of the next hop, in which case I will need to send an ARP request to obtain
it and set aside the packet that needs to be forwarded until I obtain the
hardware address.
</p>

<p>
If the packet was processed as one respecting the Address Resolution
Protocol, then we need to analyze the header to determine whether it is a
request or a reply. If it is a request, the router will respond with an ARP
reply message containing the hardware address of the interface from which
it received the request. If it is a reply, then the router will add the
correspondent's MAC address to the ARP table. After the reply message is
processed, the queued packets are checked to determine which one can be
sent following the resolution of the ARP request.
</p>

### Longest Prefix Match

<p>
Initially, for the Longest Prefix Match algorithm, I chose to do a linear
search to test the other functionalities. This iterates through the
routing table until it finds a matching entry.
</p>

<p>
Out of a desire to learn more, I searched for what structures are used to
implement such a routing table in a real-world scenario, which can contain
hundreds of thousands of entries. I came across reading about Radix trees,
which resemble Tries, but the essential difference between them is that
Radix trees apply a degree of compression up to the next entry, thereby
visibly reducing the number of nodes contained in the tree and therefore
the time complexity at the moment of insertion and/or search. Essentially,
a search is performed on the tree based on the match of a particular bit
from the destination IP; if the result of the AND operation is 0, we
continue to the left, if the result is 1, we continue to the right. There,
we need to analyze the bit sequence saved by the node; if it matches and
represents a route, then we can keep it as the best result up to that
point. If no match occurs, then it is said that "we fell" out of the tree
and the saved result is the actual result. If we have no saved result then
there is no route.
</p>

### About the Correctness of the Solution

<p>
Running the local checker, I obtained the maximum score, passing all tests
(100 points). Below I will describe what can be observed in the screenshots
included in the archive to illustrate the router's behavior in various
scenarios.
</p>

### About Forwarding

In the screenshot "subiect1.png", we can observe an ICMP "Echo" message being sent from the machine with address 192.168.0.2 (h1) to 192.168.3.2 (h3). We can observe in the upper Wireshark window (r0) how before
each forwarding of the ICMP packet an exchange
of ARP packets takes place to allow the correct writing of the Ethernet headers. In the lower window is r1. We can see how the request is sent h0-r0, r0-r1, in the
lower window r1-h3, h3-r1, r1-r0 and back in the upper one r0-h0.

### About ARP

In the screenshot we can see how h0 sends an ARP request to r0 through the arping command, and r0 responds with an ARP reply. We can observe the two packets in the Wireshark window.

### About ICMP

In the screenshot we can see how h0 sends to r0 a message
of type ICMP Echo through the ping command, and r0 responds with an
ICMP Echo Reply. We can observe the two packets in the Wireshark window.

# Router Dataplane – RO 🇷🇴

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
