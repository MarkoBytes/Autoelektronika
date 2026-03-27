# Autoelektronika
# Sistem za merenje temperature motora u automobilu

## Opis projekta
Ovaj projekat predstavlja simulaciju sistema za merenje temperature motora u automobilu. Sistem koristi dva temperaturna senzora, čije se vrednosti simuliraju putem softverskog alata za serijsku komunikaciju. Na osnovu očitanih vrednosti vrši se izračunavanje srednje temperature, detekcija grešaka senzora, upravljanje ventilatorom i slanje poruka ka PC-ju.

Projekat je realizovan korišćenjem real-time operativnog sistema FreeRTOS i implementiran je u programskom jeziku C u Visual Studio okruženju.

## Funkcionalnosti sistema

Sistem obuhvata sledeće funkcionalnosti:

- [x] Očitavanje i validacija podataka sa dva temperaturna senzora (simulirano preko AdvUniCom)
- [x] Klizni prozor usrednjavanja – poslednjih 5 validnih merenja po senzoru
- [x] Računanje srednje temperature motora
- [x] Detekcija neispravnog senzora (vrednost > 150 Ω)
- [x] Detekcija prevelike razlike između senzora (> 5 °C)
- [x] Histerezisno upravljanje ventilatorom (ON > 90 °C, OFF < 85 °C)
- [x] Prikaz temperature na multipleksiranom 7-segmentnom displeju (XX_XC)
- [x] LED signalizacija (alarm, kritična temperatura, ventilator)
- [x] Slanje statusnih i alarm poruka preko serijske veze (simulirani CAN kanal)
## Arhitektura sistema

Softver je realizovan kao skup paralelnih taskova u okviru FreeRTOS-a:

- **Task za prijem podataka** – prima podatke sa senzora putem serijske komunikacije
- **Task za obradu temperature** – obrađuje podatke, vrši usrednjavanje i detekciju grešaka
- **Task za dispej** – prikazuje temperaturu na 7-segmentnom displeju
- **Task za komunikaciju sa PC-jem** – šalje statusne poruke i upozorenja
- **Task za led bar** – signalizira stanje motora
 
Razmena podataka između taskova realizovana je pomoću **queue** mehanizma, dok je sinhronizacija ostvarena korišćenjem **semafora i mutexa**.

## Korišćeni alati i tehnologije

- Programski jezik: **C**
- Razvojno okruženje: **Visual Studio**
- RTOS: **FreeRTOS**
- Simulacija komunikacije: **AdvUniCom**
- Simulacija displeja: **7SegMux**
- Simulacija logičkih izlaza: **LEDBars**
- Komunikacioni protokol: **CAN**

## Način rada sistema

Senzori temperature šalju vrednosti otpornosti u opsegu od **0 do 150 oma**. Ove vrednosti se linearno preslikavaju u temperaturni opseg od **0°C do 100°C**.

Sistem:
1. prima vrednosti sa dva senzora
2. pamti poslednjih 5 očitavanja
3. izračunava srednju vrednost za svaki senzor
4. računa konačnu temperaturu kao srednju vrednost oba senzora

## Pokretanje projekta

### 1. Pokretanje simulatora
Pre pokretanja programa potrebno je pokrenuti simulatore iz komandne linije:

- **AdvUniCom** – tri instance:
  - kanal 0 – senzor 1
  - kanal 1 – senzor 2
  - kanal 2 – komunikacija sa PC-jem
- **7SegMux** – za prikaz temperature
- **LEDBars** – za prikaz alarma i ventilatora

### 2. Pokretanje aplikacije
1. Otvoriti projekat u Visual Studio
2. Izvršiti build projekta
3. Pokrenuti program (Run)

## Testiranje sistema

Otvoriti COM0 (kanal 0) u AdvUniCom terminalu.
Umesto ASCII karaktera, uneti vrednosti senzora u opsegu 0–150, završiti sa \0d kako bi simulator prepoznao kraj poruke.
Primer: 75\0d (vrednost senzora 1)
Otvoriti COM1 (kanal 1) u AdvUniCom terminalu.
Isto se unose vrednosti za senzor 2 u opsegu 0–150, završiti sa \0d.
Primer: 80\0d (vrednost senzora 2)
COM2 (kanal 2) se koristi za komunikaciju sa PC-jem:
Sistem šalje statusne poruke, upozorenja i informacije o greškama senzora.
Poruke su u ASCII formatu i završavaju se sa CR.

### Slanje vrednosti senzora
Na kanalima:
- **kanal 0** – senzor 1
- **kanal 1** – senzor 2

Vrednosti se šalju direktno u decimalnom formatu u opsegu 0 – 150.

Na primer:
75

predstavlja određenu vrednost otpornosti koja se prevodi u temperaturu.

## Test scenariji

| Scenarij                       | Ulaz – Senzor 1 | Ulaz – Senzor 2  | Očekivani rezultat                                                |
|--------------------------------|-----------------|------------------|-------------------------------------------------------------------|
| Normalan rad                   | 90–110 Ω        | 90–110 Ω         | Prikaz ~60–73 °C, ventilator isključen                            |
| Jedan senzor neispravan        | 160 Ω           | 100 Ω            | Poruka "SENZOR 1 NEISPRAVAN", LED alarm, koristi se samo senzor 2 |
| Prevelika razlika              | 50 Ω            | 120 Ω            | Alarm LED (blink), poruka na COM2                                 |
| Kritična temperatura           | 145 Ω           | 145 Ω            | > 95 °C → blink LED 2 + poruka "UPOZORENJE"                       |
| Ventilator uključen/isključen  | > 135 Ω         | > 135 Ω          | LED 1 = ON iznad 90 °C, OFF ispod 85 °C                           |

## MISRA pravila

Kod pisan u okviru ovog projekta prati MISRA C pravila gde god je to bilo moguće. U slučajevima gde pravila nije bilo moguće primeniti, u kodu su ostavljeni odgovarajući komentari sa objašnjenjem.

## Struktura projekta

Autoelektronika/
│
├── src/
├── include/
├── README.md
└── .gitignore

## Autori

- Helena Ivezić 
- Marko Radanović
  
Fakultet tehničkih nauka, Novi Sad  
Smer: E1 – Primenjena elektronika

## Napomena
Ovaj projekat je razvijen u edukativne svrhe.
